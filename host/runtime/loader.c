// Multi-module loading (playing the role of Horizon's rtld): mapping, claiming thread slots, gate registration, cross-module relocation, take-over by name, segment permissions.
// The code area (original text and patch area) is set to read-only+execute as soon as it is copied in, and from then on the loader only writes data: on iOS these two blocks must be embedded as is as signed code segments.
#include "rodata_seal.h"
#include "dm.h"
#include <TargetConditionals.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif
#include "game.h"
#include "hle.h"

module_t g_mod[MAX_MOD];
int g_nmod;
dm_slot g_key18, g_keytls, g_keytlr, g_keygate;
dm_slot g_keyparam[4];   // rewriter PARAMS slots (key page entries KPARAM0..3), dm.h
enum { KEY_K18, KEY_KTLS, KEY_KTLR, KEY_KGATE, KEY_KPARAM0, KEY_N = KEY_KPARAM0 + 4 };

uint64_t resolve_import(module_t *m, const char *name, int weak);
void gate_register(uint64_t ret_addr, const char *kind, int arg, uint64_t orig);

static dm_slot key_value(unsigned i) {
    switch (i) {
    case KEY_K18: return g_key18; case KEY_KTLS: return g_keytls; case KEY_KTLR: return g_keytlr;
    case KEY_KGATE: return g_keygate; default: return g_keyparam[i - KEY_KPARAM0];
    }
}
static void create_keys(void) {
    if (dm_slot_create(&g_key18) || dm_slot_create(&g_keytls) || dm_slot_create(&g_keytlr) || dm_slot_create(&g_keygate)) die("cannot create the host keys");
    // Parameter slot keys are always created, used or not: a key page entry of 0 would make the guest read TSD slot 0
    for (int i = 0; i < 4; i++) if (dm_slot_create(&g_keyparam[i])) die("cannot create the parameter slot keys");
    LOG("keys K18=%lu KTLS=%lu KTLR=%lu KGATE=%lu params %lu %lu %lu %lu",
        g_key18, g_keytls, g_keytlr, g_keygate, g_keyparam[0], g_keyparam[1], g_keyparam[2], g_keyparam[3]);
}

// K slot check: slot = the slot block's base + 8 * slot, as the rewritten code reads it (slots.h)
static void tsd_check(void) {
    dm_slot_set(g_key18, (void *)0x1234);
    uint64_t base = dm_slot_base();
    uint64_t off = *(uint64_t *)(g_mod[0].base + g_mod[0].key_page_off + g_mod[0].key_idx[KEY_K18] * 8);
    if (((uint64_t *)base)[off / 8] != 0x1234) die("unexpected thread slot layout");
    dm_slot_set(g_key18, 0);
}

static void build_symhash(module_t *m);
static void parse_dynamic(module_t *m) {
    uint32_t mod0 = *(uint32_t *)(m->base + 4);
    if (memcmp(m->base + mod0, "MOD0", 4)) die("%s: no MOD0", m->name);
    m->dyn = (const Elf64_Dyn *)(m->base + mod0 + *(int32_t *)(m->base + mod0 + 4));
    for (const Elf64_Dyn *d = m->dyn; d->d_tag; d++) switch (d->d_tag) {
        case DT_SYMTAB: m->symtab = (const Elf64_Sym *)(m->base + d->d_un.d_ptr); break;
        case DT_STRTAB: m->strtab = (const char *)(m->base + d->d_un.d_ptr); break;
        case DT_INIT: m->init = d->d_un.d_ptr; break;
        case DT_INIT_ARRAY: m->init_array = d->d_un.d_ptr; break;
        case DT_INIT_ARRAYSZ: m->init_arraysz = d->d_un.d_val; break;
    }
    m->nsym = (unsigned)(((const uint8_t *)m->strtab - (const uint8_t *)m->symtab) / sizeof(Elf64_Sym));
    build_symhash(m);
}

static void *module_sym_linear(module_t *m, const char *name) {
    for (unsigned i = 1; i < m->nsym; i++) {
        const Elf64_Sym *s = &m->symtab[i];
        if (s->st_shndx && !strcmp(m->strtab + s->st_name, name)) return m->base + s->st_value;
    }
    return NULL;
}
// Symbol name hash table: inserted in ascending index order, not overwritten if the same name is already there ⇒ lookup results are identical, entry by entry, to a linear scan (the first defined symbol with that name).
// Of the 91–99 ms of host loading, relocation + forced takeover took 77–86 ms, mainly because every by-name relocation scanned the symbol tables of two modules linearly
static uint32_t sym_hash(const char *s) { uint32_t h = 2166136261u; while (*s) h = (h ^ (uint8_t)*s++) * 16777619u; return h; }
static void build_symhash(module_t *m) {
    unsigned cap = 16; while (cap < m->nsym * 2) cap <<= 1;
    m->symh = calloc(cap, sizeof *m->symh); m->symh_mask = cap - 1;
    for (unsigned i = 1; i < m->nsym; i++) {
        const Elf64_Sym *s = &m->symtab[i]; if (!s->st_shndx) continue;
        const char *nm = m->strtab + s->st_name;
        for (unsigned j = sym_hash(nm) & m->symh_mask;; j = (j + 1) & m->symh_mask) {
            if (!m->symh[j]) { m->symh[j] = i; break; }
            if (!strcmp(m->strtab + m->symtab[m->symh[j]].st_name, nm)) break;   // The same name already exists with a smaller index: keep that one
        }
    }
}
static unsigned long g_symchk_n, g_symchk_bad;
void *module_sym(module_t *m, const char *name) {
    unsigned long nq; (void)nq;
    if (!m->symh) return module_sym_linear(m, name);
    void *r = NULL;
    for (unsigned j = sym_hash(name) & m->symh_mask; m->symh[j]; j = (j + 1) & m->symh_mask) {
        const Elf64_Sym *s = &m->symtab[m->symh[j]];
        if (!strcmp(m->strtab + s->st_name, name)) { r = m->base + s->st_value; break; }
    }
    static int chk = -1; if (chk < 0) chk = getenv("DM_SYMCHECK") != NULL;
    if (chk) { void *l = module_sym_linear(m, name); g_symchk_n++; nq = g_symchk_n;
               if (l != r) { g_symchk_bad++; LOG("[red] DM_SYMCHECK: %s in %s, hash %p ≠ linear %p", name, m->name, r, l); } }
    return r;
}
static const Elf64_Sym *module_sym_entry(module_t *m, const char *name) {   // module_sym's symbol itself (the first defined one of the name)
    if (!m->symh) { for (unsigned i = 1; i < m->nsym; i++) { const Elf64_Sym *s = &m->symtab[i]; if (s->st_shndx && !strcmp(m->strtab + s->st_name, name)) return s; } return NULL; }
    for (unsigned j = sym_hash(name) & m->symh_mask; m->symh[j]; j = (j + 1) & m->symh_mask) { const Elf64_Sym *s = &m->symtab[m->symh[j]]; if (!strcmp(m->strtab + s->st_name, name)) return s; }
    return NULL;
}
void *guest_sym(const char *name) {
    for (int i = 0; i < g_nmod; i++) { void *p = module_sym(&g_mod[i], name); if (p) return p; }
    return NULL;
}
void *dm_host_page(const char *module) {
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, module)) return g_mod[i].host_page_off ? g_mod[i].base + g_mod[i].host_page_off : NULL;
    return NULL;
}
module_t *module_of(const void *p) {
    for (int i = 0; i < g_nmod; i++)
        if ((const uint8_t *)p >= g_mod[i].base && (const uint8_t *)p < g_mod[i].base + g_mod[i].size) return &g_mod[i];
    return NULL;
}

// Cross-module symbol resolution: first look for a definition in the loaded modules, and only if not found hand over to HLE / stubs (relocations
// go through bind_sym below; host/tests t_loader_weak calls this)
__attribute__((unused)) static uint64_t resolve_sym(module_t *m, const char *name, int weak) {
    for (int i = 0; i < g_nmod; i++) { void *p = module_sym(&g_mod[i], name); if (p) return (uint64_t)p; }
    return resolve_import(m, name, weak);
}
// Horizon's rtld (plan item 64h; its rules as the ISC-licensed oss-rtld reimplementation has them): a reference of hidden or protected visibility
// binds in its own module; one of default visibility to the first module, in the order main, subsdk0-9, sdk (rtld walks the address space upward,
// where the loader put them in that order), that defines the name with a non-local binding, even when its own module defines it too (UE4's main
// overrides malloc for every module). Ours bound a symbol its module defines there and looked the others up in the profile's module order (main sdk
// subsdk0). Both are worked out for every symbol relocation and the relocation line counts where the two differ. rtld's is the default since
// both games ran the same with it (their sdk binds 801 / 782 relocations otherwise, all typeinfo, vtables and the like, to main's or subsdk0's
// copies, as the console does); DM_RTLD=0 binds by ours
static int rtld_rank(const module_t *m, int i) {
#ifdef DM_TEST_RTLD_LOAD_ORDER   // negative control of host/tests t_rtld only: the profile's order
    (void)m; return i;
#endif
    return !strcmp(m->name, "main") ? 0 : !strncmp(m->name, "subsdk", 6) ? 1 + atoi(m->name + 6) : !strcmp(m->name, "sdk") ? 100 : 200 + i;
}
static int g_rtld = -1; static unsigned long g_rtld_diff;
static uint64_t bind_sym(module_t *m, const Elf64_Sym *s, const char *nm) {
    if (g_rtld < 0) { const char *e = getenv("DM_RTLD"); g_rtld = !(e && !strcmp(e, "0"));
#ifdef DM_TEST_RTLD_DEFAULT_OURS   // negative control of t_rtld only: ours unless DM_RTLD=1, as before
                      g_rtld = e && !strcmp(e, "1");
#endif
                      if (!g_rtld) LOG("DM_RTLD=0: symbols bound as before item 64h (a module's own definition first, then the profile's order)"); }
    uint64_t local = s->st_shndx ? (uint64_t)m->base + s->st_value : 0, cur = local, rt = 0;
    for (int i = 0; i < g_nmod && !cur; i++) { void *p = module_sym(&g_mod[i], nm); if (p) cur = (uint64_t)p; }
    if (ELF64_ST_VISIBILITY(s->st_other) != STV_DEFAULT) rt = local;
    else for (int i = 0, best = 1 << 30; i < g_nmod; i++) {
        const Elf64_Sym *d = module_sym_entry(&g_mod[i], nm); int r;
        if (d && ELF64_ST_BIND(d->st_info) != STB_LOCAL && d->st_shndx != SHN_COMMON && (r = rtld_rank(&g_mod[i], i)) < best) { best = r; rt = (uint64_t)g_mod[i].base + d->st_value; }
    }
    if (cur != rt && g_rtld_diff++ < 4) {
        module_t *a = module_of((void *)(uintptr_t)rt), *b = module_of((void *)(uintptr_t)cur);
        LOG("%s: %s binds to %s+%#llx by rtld's rules, %s+%#llx by ours (plan item 64h; %s)", m->name, nm, a ? a->name : "nothing", a ? (unsigned long long)(rt - (uint64_t)(uintptr_t)a->base) : 0ull,
            b ? b->name : "nothing", b ? (unsigned long long)(cur - (uint64_t)(uintptr_t)b->base) : 0ull, g_rtld > 0 ? "rtld's taken, DM_RTLD=0 takes ours" : "ours taken (DM_RTLD=0)");
    }
    uint64_t v = g_rtld > 0 ? rt : cur;
    return v ? v : resolve_import(m, nm, ELF64_ST_BIND(s->st_info) == STB_WEAK);
}

// Forced take-over table (hle.h HLE_FORCE, the __DATA,swforce section): a hit overrides the definition inside the module
static const hle_force_ent *g_force; static unsigned g_nforce; static unsigned *g_force_hits;
static void force_init(void) {
    unsigned long sz; g_force = (const hle_force_ent *)DM_TABLE_GET(swforce, &sz);
    g_nforce = g_force ? (unsigned)(sz / sizeof *g_force) : 0;
    g_force_hits = calloc(g_nforce + 1, sizeof *g_force_hits);
    for (unsigned i = 0; i < g_nforce; i++)
        for (unsigned j = i + 1; j < g_nforce; j++)
            if (!strcmp(g_force[i].name, g_force[j].name)) die("forced takeover listed twice: %s", g_force[i].name);
}
// What the game's references to name are bound to (host/diag/sdktest.c): the host function when it is taken over (HLE_FORCE), else the module's own
// definition. guest_sym gives the definition even when the game's calls go to the host
void *dm_sym_bound(const char *name) {
    for (unsigned i = 0; i < g_nforce; i++) if (!strcmp(g_force[i].name, name)) return g_force[i].fn;
    return guest_sym(name);
}
static void *force_lookup(const char *name) {
    for (unsigned i = 0; i < g_nforce; i++)
        if (!strcmp(g_force[i].name, name)) {
            if (!g_force_hits[i]++ && g_flag.imports) LOG("force %s  (%s)", name, g_force[i].why);
            void *hlecount_wrap(const char *, const char *, void *); return hlecount_wrap("FORCE", name, dm_hwrap(g_force[i].fn));   // On the host stack; a counting stub in front when DM_HLECOUNT is set
        }
    return NULL;
}
// The product must be built from the game's own main: main.cuts.tsv starts with "# BUILD_ID <hex>" (patch_nso.py), compared with the profile's
// main_build_id. An app bundle without the table (iOS ships only the module images) skips the check.
static char g_main_build[64];
static void read_main_build(const char *dir) {
    char path[1024]; snprintf(path, sizeof path, "%s/main.cuts.tsv", dir);
    FILE *f = fopen(path, "r"); if (!f) return;
    if (fscanf(f, "# BUILD_ID %63s", g_main_build) != 1) die("%s: the first line is not # BUILD_ID", path);
    fclose(f);
    if (strcasecmp(g_main_build, dm_game.main_build_id))
        die("the product's main is build %s, but the game profile (%s) is for build %s", g_main_build, dm_game.id, dm_game.main_build_id);
}
const char *dm_main_build(void) { return g_main_build[0] ? g_main_build : dm_game.main_build_id; }

static void force_check(void) {
    int bad = 0;
    for (unsigned i = 0; i < g_nforce; i++)
        if (!g_force_hits[i] && !g_force[i].optional) { if (g_flag.imports) LOG("forced takeover not referenced: %s", g_force[i].name); bad++; }
    if (bad) LOG("force: %d of %u entries not referenced by this game (DM_IMPORTS=1 lists them)", bad, g_nforce);
    else LOG("force: all %u entries referenced", g_nforce);
}

// Read the rewriter table header (segment boundaries, slot numbers); any row beyond the header means an old-format build output (code modified at load time), which is rejected
static void read_layout(module_t *m, const char *dir) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s.fixups.tsv", dir, m->name);
    FILE *f = fopen(path, "r");
    if (!f) die("open %s", path);
    static const char *kn[8] = {"K18", "KTLS", "KTLR", "KGATE", "KPARAM0", "KPARAM1", "KPARAM2", "KPARAM3"};
    char line[256], key[32]; unsigned long v; unsigned seen = 0; int android = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "# %31s %lx", key, &v) != 2) die("%s: product from an old rewriter; rebuild it with the current one (PRODUCT)", path);
        if (!strcmp(key, "PATCH_OFF")) m->patch_off = v;
        else if (!strcmp(key, "KEY_PAGE_OFF")) m->key_page_off = v;
        else if (!strcmp(key, "KEY_PAGE_SIZE")) m->key_page_size = v;
        else if (!strcmp(key, "HOST_PAGE_OFF")) m->host_page_off = v;
        else if (!strcmp(key, "TEXT_END")) m->text_end = v;
        else if (!strcmp(key, "RO_OFF")) m->ro_off = v;
        else if (!strcmp(key, "DATA_OFF")) m->data_off = v;
        else if (!strcmp(key, "REWRITER")) m->rewriter = v;
        else if (!strcmp(key, "TARGET_ANDROID")) android = v == 1;
        else for (int k = 0; k < 8; k++) {
            size_t n = strlen(kn[k]);
            if (!strncmp(key, "KEY_", 4) && !strncmp(key + 4, kn[k], n) && !strcmp(key + 4 + n, "_IDX")) {
                m->key_idx[k] = (unsigned)strtoul(strrchr(line, '\t') + 1, 0, 10); seen |= 1u << k;
            }
        }
    }
    fclose(f);
#ifdef DM_REWRITER_STAMP
    // A product from another rewriter or other rules than this host was built with (host/Makefile). Not refused: a product may be kept on purpose (Tears
    // of the Kingdom's default product predates its 60 fps rules), but a stale one is the first thing to rule out when something looks wrong
    if (m->rewriter != (uint64_t)DM_REWRITER_STAMP)
        LOG("%s: %s (this host expects %#llx): rebuild it with tools/build_product.sh unless it is kept on purpose", m->name,
            m->rewriter ? "the product was made by another rewriter or other rules" : "the product has no rewriter stamp (made before 2026-10-19)", (unsigned long long)DM_REWRITER_STAMP);
#endif
    if (!m->patch_off || m->key_page_off % 0x4000 || m->key_page_size != 0x4000 || seen != 0xff)
        die("%s: product from an old rewriter; rebuild it with the current one (PRODUCT)", path);
    // The rewriter's target (patch_nso.py DM_TARGET, plan item 301b): where the blocks find the thread slots and keep what they borrow
#ifdef __APPLE__
    if (android) die("%s: a product for Android (DM_TARGET=android); this host needs one for Apple", path);
#else
    if (!android) die("%s: a product for Apple; this host needs one for Android (DM_TARGET=android tools/build_product.sh)", path);
#endif
    if (m->host_page_off && (m->host_page_off % 0x4000 || m->host_page_off < m->key_page_off + m->key_page_size || m->host_page_off + 0x4000 > m->patch_off))
        die("%s: host page %#llx outside the writable part before the patch area", path, (unsigned long long)m->host_page_off);
}

static void install_key_page(module_t *m) {
    if (m->key_page_off + m->key_page_size > m->size) die("%s: key page out of bounds", m->name);
    uint64_t *page = (uint64_t *)(m->base + m->key_page_off);
    memset(page, 0, m->key_page_size);
    for (unsigned i = 0; i < KEY_N; i++) page[m->key_idx[i]] = (uint64_t)key_value(i) * 8;
    for (int k = 0; k < 8; k++) if (!page[m->key_idx[k]]) die("%s: key page entry %d is 0 (host key not created); the guest would read TSD slot 0", m->name, k);
    if (mprotect(m->base + m->key_page_off, m->key_page_size, PROT_READ)) die("%s: key page mprotect failed", m->name);
}

static void register_gates(module_t *m, const char *dir) {
    char path[1024], line[256]; snprintf(path, sizeof path, "%s/%s.gates.tsv", dir, m->name);
    FILE *f = fopen(path, "r");
    int ng = 0;
    if (f) {
        unsigned long ret, orig; char kind[16]; int arg;
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "%lx %15s %d %lx", &ret, kind, &arg, &orig) == 4) { gate_register((uint64_t)m->base + ret, kind, arg, orig); ng++; }
        fclose(f);
    }
    LOG("%s: gates %d patch_off %#llx keys %lu/%lu/%lu/%lu", m->name, ng, m->patch_off, g_key18, g_keytls, g_keytlr, g_keygate);
}

// DT_RELR (packed RELATIVE relocations, gABI; newer SDK toolchains emit them): an even word is the offset of one slot and
// starts a run; an odd word is a bitmap whose bit i (i = 1..63) marks the slot (i − 1) words after the run position, which then moves on 63 words.
// Each slot holds its link-time address (implicit addend): the loaded value is base + slot.
// The whole table is checked before any slot is touched (plan item 144; totk-mobile's relr.h checks the same, its vectors in tests/t_relr.c): a
// bitmap before any address, a slot not 8-byte aligned, outside the module or past the end of the address space, or a table outside the module
// makes it refuse every slot and return the bad entry's index + 1 (it wrote wherever the words said). 0 = applied
enum { DT_RELRSZ_ = 35, DT_RELR_ = 36 };
static uint64_t relr_walk(const module_t *m, const uint64_t *e, uint64_t n, void (*fn)(const module_t *, uint64_t off, void *), void *ctx) {
    uint64_t at = 0; int anchored = 0;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t w = e[i];
        if (!(w & 1)) {
#ifndef DM_TEST_RELR_UNCHECKED   // negative control of t_relr only: no checks, as before
            if (w & 7 || w > m->size - 8) return i + 1;
#endif
            if (fn) fn(m, w, ctx);
            at = w + 8; anchored = 1; continue;
        }
#ifndef DM_TEST_RELR_UNCHECKED
        if (!anchored) return i + 1;
        for (uint64_t v = w >> 1, b = 0; v; v >>= 1, b++) if (v & 1 && (at + 8 * b < at || at + 8 * b > m->size - 8)) return i + 1;
#else
        (void)anchored;
#endif
        if (fn) for (uint64_t v = w >> 1, b = 0; v; v >>= 1, b++) if (v & 1) fn(m, at + 8 * b, ctx);
        if (at > UINT64_MAX - 63 * 8) return i + 1;
        at += 63 * 8;
    }
    return 0;
}
static uint64_t relr_each(const module_t *m, void (*fn)(const module_t *, uint64_t off, void *), void *ctx) {
    uint64_t relr = 0, relrsz = 0;
    for (const Elf64_Dyn *d = m->dyn; d->d_tag; d++) {
        if (d->d_tag == DT_RELR_) relr = d->d_un.d_ptr;
        if (d->d_tag == DT_RELRSZ_) relrsz = d->d_un.d_val;
    }
    if (!relr) return 0;
    if (m->size < 8 || relr > m->size || relrsz > m->size - relr || relr & 7) return 1;
    const uint64_t *e = (const uint64_t *)(m->base + relr);
    uint64_t bad = relr_walk(m, e, relrsz / 8, NULL, NULL);   // all checked first: a bad table touches nothing
    return bad ? bad : relr_walk(m, e, relrsz / 8, fn, ctx);
}
static void relr_apply(const module_t *m, uint64_t off, void *ctx) { *(uint64_t *)(m->base + off) += (uint64_t)m->base; ++*(int *)ctx; }

static void relocate(module_t *m) {
    uint64_t rela = 0, relasz = 0, jmprel = 0, pltsz = 0;
    for (const Elf64_Dyn *d = m->dyn; d->d_tag; d++) switch (d->d_tag) {
        case DT_RELA: rela = d->d_un.d_ptr; break;
        case DT_RELASZ: relasz = d->d_un.d_val; break;
        case DT_JMPREL: jmprel = d->d_un.d_ptr; break;
        case DT_PLTRELSZ: pltsz = d->d_un.d_val; break;
    }
    int cnt[3] = {0}; unsigned long diff0 = g_rtld_diff;
    const Elf64_Rela *tabs[2] = {(const Elf64_Rela *)(m->base + rela), (const Elf64_Rela *)(m->base + jmprel)};
    uint64_t sizes[2] = {relasz, pltsz};
    for (int t = 0; t < 2; t++)
        for (uint64_t i = 0; i < sizes[t] / sizeof(Elf64_Rela); i++) {
            const Elf64_Rela *r = &tabs[t][i];
            uint64_t *where = (uint64_t *)(m->base + r->r_offset);
            unsigned type = ELF64_R_TYPE(r->r_info), si = ELF64_R_SYM(r->r_info);
            if (type == R_AARCH64_RELATIVE) { *where = (uint64_t)m->base + r->r_addend; cnt[0]++; continue; }
            const Elf64_Sym *s = &m->symtab[si];
            uint64_t v = (uint64_t)force_lookup(m->strtab + s->st_name);
            if (!v) v = bind_sym(m, s, m->strtab + s->st_name);
            if (type == R_AARCH64_JUMP_SLOT) { *where = v; cnt[2]++; }
            else { *where = v + r->r_addend; cnt[1]++; }       // ABS64 / GLOB_DAT
        }
    int nrelr = 0; uint64_t bad = relr_each(m, relr_apply, &nrelr);
    if (bad) die("[red] %s: DT_RELR entry %llu is not a valid relocation (a bitmap before any address, or a slot unaligned or outside the module): the module is not loaded", m->name, (unsigned long long)bad - 1);
    LOG("%s: reloc relative=%d relr=%d abs/glob=%d jump=%d, %lu bound otherwise by rtld's rules", m->name, cnt[0], nrelr, cnt[1], cnt[2], g_rtld_diff - diff0);
}

// DM_LOADHASH=1 (diagnostics): after loading, output an address-independent sha256 per module, to prove that the load results of "early start / no early start" are byte-identical.
// Module base addresses and host ASLR differ every run, and only the values of the relocation slots change with them: copy the image and, using the same relocation table, replace every slot with an address-independent token --
// RELATIVE → (0, addend); symbol target falls in some module → (1 + module number, offset within the module); otherwise (host forced take-over / host symbol) → (name FNV, addend); all other bytes as is.
#include <CommonCrypto/CommonDigest.h>
struct relr_tok { uint8_t *cp; uint64_t *tail; unsigned n, cap; };
static void relr_token(const module_t *m, uint64_t off, void *ctx) {
    struct relr_tok *rt = ctx;
    if (rt->n == rt->cap) { rt->cap = rt->cap ? rt->cap * 2 : 4096; rt->tail = realloc(rt->tail, 8ull * rt->cap); }
    rt->tail[rt->n++] = *(const uint64_t *)(m->base + off) - (uint64_t)m->base;
    memset(rt->cp + off, 0, 8);
}
void loader_hash_log(void) {
    if (!getenv("DM_LOADHASH")) return;
    for (int k = 0; k < g_nmod; k++) {
        module_t *m = &g_mod[k];
        uint8_t *cp = malloc(m->size); memcpy(cp, m->base, m->size);
        uint64_t rela = 0, relasz = 0, jmprel = 0, pltsz = 0;
        for (const Elf64_Dyn *d = m->dyn; d->d_tag; d++) switch (d->d_tag) {
            case DT_RELA: rela = d->d_un.d_ptr; break; case DT_RELASZ: relasz = d->d_un.d_val; break;
            case DT_JMPREL: jmprel = d->d_un.d_ptr; break; case DT_PLTRELSZ: pltsz = d->d_un.d_val; break;
        }
        const Elf64_Rela *tabs[2] = {(const Elf64_Rela *)(m->base + rela), (const Elf64_Rela *)(m->base + jmprel)};
        uint64_t sizes[2] = {relasz, pltsz}; unsigned nslot = 0;
        for (int t = 0; t < 2; t++)
            for (uint64_t i = 0; i < sizes[t] / sizeof(Elf64_Rela); i++) {
                const Elf64_Rela *r = &tabs[t][i]; uint64_t tok[1] = {0};
                if (ELF64_R_TYPE(r->r_info) != R_AARCH64_RELATIVE) {
                    uint64_t v = *(const uint64_t *)(m->base + r->r_offset); int hit = 0;
                    for (int j = 0; j < g_nmod; j++) if (v >= (uint64_t)g_mod[j].base && v < (uint64_t)g_mod[j].base + g_mod[j].size) { tok[0] = 1 + j; hit = 1; break; }
                    if (!hit) { const char *nm = m->strtab + m->symtab[ELF64_R_SYM(r->r_info)].st_name; uint64_t h = 0xcbf29ce484222325ull;
                                for (; *nm; nm++) h = (h ^ (uint8_t)*nm) * 0x100000001b3ull; tok[0] = h | 1ull << 63; }
                }
                memcpy(cp + r->r_offset, &tok[0], 8);                // The 8 slot bytes are replaced by the token head; the token tail is appended below in slot order
                nslot++;
            }
        struct relr_tok rt = {cp, NULL, 0, 0};
        if (relr_each(m, relr_token, &rt)) die("[red] %s: invalid DT_RELR table (see the load)", m->name);
        nslot += rt.n;            // RELR slots: token 0 like RELATIVE, tail = module-relative value
        CC_SHA256_CTX c; CC_SHA256_Init(&c); CC_SHA256_Update(&c, cp, (CC_LONG)m->size);
        CC_SHA256_Update(&c, rt.tail, (CC_LONG)(8 * rt.n)); free(rt.tail);
        for (int t = 0; t < 2; t++)                                // Token tails (addend / offset) are appended in slot order, and together with the slot head uniquely determine the slot value
            for (uint64_t i = 0; i < sizes[t] / sizeof(Elf64_Rela); i++) {
                const Elf64_Rela *r = &tabs[t][i]; uint64_t tail = (uint64_t)r->r_addend;
                if (ELF64_R_TYPE(r->r_info) != R_AARCH64_RELATIVE) {
                    uint64_t v = *(const uint64_t *)(m->base + r->r_offset);
                    for (int j = 0; j < g_nmod; j++) if (v >= (uint64_t)g_mod[j].base && v < (uint64_t)g_mod[j].base + g_mod[j].size) { tail = v - (uint64_t)g_mod[j].base; break; }
                }
                CC_SHA256_Update(&c, &tail, 8);
            }
        uint8_t d[32]; CC_SHA256_Final(d, &c); free(cp);
        char hx[65]; for (int i = 0; i < 32; i++) sprintf(hx + 2 * i, "%02x", d[i]);
        LOG("DM_LOADHASH %s size %#zx, %u relocated slots, sha256 %s", m->name, m->size, nslot, hx);
    }
}

// Code options asked for (options.c): applied to each module's image before its code is sealed
static char g_options_wanted[256];
int dm_options_embedded(const char *dir, const char *module, const uint8_t *img, size_t sz, const char *wanted);   // options.c (iOS)
void dm_options_wanted(const char *dir, char *out, size_t n); int dm_options_apply(const char *dir, const char *module, uint8_t *img, size_t sz, const char *wanted); int dm_options_check(const char *wanted);
#if TARGET_OS_IPHONE
// iOS does not allow code loaded at run time to execute, and Apple allows an executable only one segment with execute permission (ITMS-90999): the image is compiled into this executable by ios/tools/gen_embed.py --
// the two code blocks (original text, patch area) are sections in __TEXT, and W (the rest of ro + data + bss) is a separate RW segment. The three blocks are not adjacent in the file; here one contiguous address range is reserved,
// and they are vm_remap'ed back at their original relative positions: code pages with copy=FALSE (sharing the same signed pages with __TEXT, r-x), W with copy=TRUE (copy-on-write, paged only when used).
// The run-time layout is byte-identical to the original image. Remapping signed pages to a new address and then executing them on iOS is exactly what libffi's trampoline table (FFI_EXEC_TRAMPOLINE_TABLE) does
#include <mach/mach.h>
static void remap_at(module_t *m, vm_address_t to, const void *from, unsigned long size, boolean_t copy, vm_prot_t need) {
    vm_address_t a = to; vm_prot_t cur = 0, max = 0;
    kern_return_t kr = vm_remap(mach_task_self(), &a, size, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(), (vm_address_t)from, copy, &cur, &max, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS || a != to) die("%s: vm_remap %p → %#lx (%#lx bytes) failed: %d", m->name, from, (unsigned long)to, size, kr);
    if ((cur & need) != need) die("%s: permissions %#x after vm_remap, %#x missing", m->name, cur, need);
}
#define IMAGE_MH (&_mh_execute_header)
static void map_image(module_t *m, const char *dir) {
    char sx0[17], sx1[17], dm[17]; snprintf(sx0, sizeof sx0, "__dm%sx0", m->name); snprintf(sx1, sizeof sx1, "__dm%sx1", m->name); snprintf(dm, sizeof dm, "__DM%sW", m->name);
    unsigned long z0, zw, z1;
    const uint8_t *x0 = getsectiondata(IMAGE_MH, "__TEXT", sx0, &z0), *w = getsectiondata(IMAGE_MH, dm, "__img", &zw), *x1 = getsectiondata(IMAGE_MH, "__TEXT", sx1, &z1);
    if (!x0 || !w || !x1) die("embedded sections %s / %s / %s missing", sx0, dm, sx1);
    if (z0 + zw != m->patch_off || (z0 & 0x3FFF) || (zw & 0x3FFF) || (z1 & 0x3FFF) || ((uintptr_t)x0 & 0x3FFF) || ((uintptr_t)w & 0x3FFF) || ((uintptr_t)x1 & 0x3FFF))
        die("%s: unexpected embedded section layout (x0 %#lx w %#lx x1 %#lx, PATCH_OFF %#llx)", m->name, z0, zw, z1, (unsigned long long)m->patch_off);
    m->size = m->patch_off + z1;
    vm_address_t base = 0;
    if (vm_allocate(mach_task_self(), &base, m->size, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) die("%s: reserving %#zx bytes of address space failed", m->name, m->size);
    remap_at(m, base, x0, z0, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    remap_at(m, base + z0, w, zw, TRUE, VM_PROT_READ | VM_PROT_WRITE);
    remap_at(m, base + m->patch_off, x1, z1, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    m->base = (uint8_t *)base; install_key_page(m);
    dm_options_embedded(dir, m->name, m->base, m->patch_off, g_options_wanted);   // the options are built into the image (ios/tools/gen_embed.py, plan item 136)
    { static int once; if (getenv("DM_PCHTXT") && !once++) LOG("[red] DM_PCHTXT: not on iOS (the app's code is signed; pchtxt.c is the Mac's): the game as built"); }
    { void dm_cheats_code(const char *, module_t *); dm_cheats_code(dir, m); }   // (iOS: a red line when DM_CHEATS is set)
}
#else
static uint8_t *slurp(const char *path, size_t *n) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) die("open %s", path);
    struct stat st; fstat(fd, &st);
    uint8_t *p = malloc(st.st_size);
    if (read(fd, p, st.st_size) != st.st_size) die("read %s", path);
    close(fd); *n = st.st_size; return p;
}

// Seal the code area immediately after copying into anonymous pages (16 KB pages): [0, text_end) and the patch area are read-only+execute, and from then on any write to the code area crashes on the spot;
// what lies between (ro and data on the same page can only go together) is writable
static void map_image(module_t *m, const char *dir) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s.patched.bin", dir, m->name);
    size_t sz; uint8_t *raw = slurp(path, &sz);
    m->size = (sz + 0x3FFF) & ~0x3FFFull;
    m->base = mmap(NULL, m->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m->base == MAP_FAILED) die("%s mmap", m->name);
    memcpy(m->base, raw, sz); free(raw); install_key_page(m);
    if (*g_options_wanted) dm_options_apply(dir, m->name, m->base, sz, g_options_wanted);   // before the code is sealed (options.c)
    { const char *pf = getenv("DM_PCHTXT"); int dm_pchtxt_apply(const char *, const char *, uint8_t *, size_t, const char *);
      if (pf && *pf) dm_pchtxt_apply(dir, m->name, m->base, m->patch_off, pf); }   // community ExeFS patches, after the options (pchtxt.c, plan item 150a)
    { void dm_cheats_code(const char *, module_t *); dm_cheats_code(dir, m); }   // cheats into main's code, before it is sealed (cheats.c, plan item 150b)
    uint64_t rx = (m->text_end + 0x3FFF) & ~0x3FFFull;
    if (mprotect(m->base, rx, PROT_READ | PROT_EXEC)) die("%s mprotect text", m->name);
    uint64_t po = m->patch_off & ~0x3FFFull;
    if (mprotect(m->base + po, m->size - po, PROT_READ | PROT_EXEC)) die("%s mprotect patch", m->name);
}
#endif

void load_modules(const char *dir, const char **names, int n) {
    // Map everything and parse the symbol tables first, then relocate all at once (cross-module symbols must all be in place first)
    // DM_BOOTPROF: breakdown of host loading (milliseconds)
    uint64_t t0 = now_ns(), tm = 0, tp = 0, tg = 0, tr = 0, t;
    for (int i = 0; i < n; i++) {
        module_t *m = &g_mod[i];
        strlcpy(m->name, names[i], sizeof m->name);
        read_layout(m, dir);
    }
    read_main_build(dir);
    dm_options_wanted(dir, g_options_wanted, sizeof g_options_wanted);   // DM_OPTIONS or the product's options.default (options.c)
    create_keys();
    for (int i = 0; i < n; i++) {
        module_t *m = &g_mod[i];
        t = now_ns(); map_image(m, dir); tm += now_ns() - t;   // On return the code area is already read-only+execute; from here on the loader only writes data
        if (!i) tsd_check();
        t = now_ns(); parse_dynamic(m); tp += now_ns() - t;
        t = now_ns(); register_gates(m, dir); tg += now_ns() - t;
        LOG("%s: base %p size %#zx", names[i], m->base, m->size);
        g_nmod++;
    }
    if (*g_options_wanted) { dm_options_check(g_options_wanted); LOG("options asked for: %s", g_options_wanted); }
    { void dm_pchtxt_report(const char *); const char *pf = getenv("DM_PCHTXT"); if (pf && *pf) dm_pchtxt_report(pf); }
    force_init();
    t = now_ns(); for (int i = 0; i < g_nmod; i++) relocate(&g_mod[i]); tr = now_ns() - t;
    force_check();
    { void dm_cheats_start(void); dm_cheats_start(); }   // the cheats that run on a host thread (cheats.c, plan item 150b)
    // rodata read-only now that relocation is done (rodata_seal.h); DM_RODATA_RW=1 leaves it writable
    if (!getenv("DM_RODATA_RW")) for (int i = 0; i < g_nmod; i++) { module_t *m = &g_mod[i]; uint64_t lo, hi;
        if (dm_rodata_seal(m->base, m->text_end, m->ro_off, m->data_off, m->key_page_off, &lo, &hi)) die("%s: rodata mprotect failed", m->name);
        if (g_flag.bootprof) LOG("%s: rodata read-only +%#llx..+%#llx", m->name, (unsigned long long)lo, (unsigned long long)hi); }
    { void hlecount_seal(void); hlecount_seal(); }   // DM_HLECOUNT: relocation done, seal the counting stub pages as read-only executable
    if (getenv("DM_SYMCHECK")) LOG("DM_SYMCHECK: %lu symbol lookups checked at load, %lu hash / linear mismatches", g_symchk_n, g_symchk_bad);
    if (g_flag.bootprof) LOG("host loading: map (read + copy + seal) %.1f ms, dynamic sections %.1f ms, gates %.1f ms, relocation + forced takeover %.1f ms, total %.1f ms",
                             tm / 1e6, tp / 1e6, tg / 1e6, tr / 1e6, (now_ns() - t0) / 1e6);
}

// Plays the role of rtld's InitModules (rtld+0x830): call each module's DT_INIT in forward order of the load list.
// The timing is decided by nn::init::Start: after nninitStartup and _init_libc2, before nnMain (the 4th argument callback of Start).
void run_module_inits(void) {
    for (int i = 0; i < g_nmod; i++) {
        module_t *m = &g_mod[i];
        if (!m->init) continue;
        LOG("%s: DT_INIT +%#llx (init_array %llu entries)", m->name, m->init, m->init_arraysz / 8);
        ((void (*)(void))(m->base + m->init))();
    }
}
