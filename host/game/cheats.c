// Cheats (plan item 150b; after MacNativeGames PR #22's split, MIT, open, written anew): DM_CHEATS=<file>, an Atmosphère cheat file for the game's
// main (the player's own: none ship here; a file named after a build ID, "<16 hex digits>.txt", must name main's), DM_CHEATS_ON=all or the names of
// the cheats to run, comma-separated ("{master}" sections always run). Mac and Android (iOS: a red line; it has no mach_vm_read / mach_vm_write).
//   A cheat made only of stores (0) into main's code is applied once at load, before the code is sealed (the code is never made writable while
//   guest threads run it): refused whole when a store touches a word the rewriter changed (pchtxt.c's tables) or is not 4 or 8 bytes aligned.
//   Every other cheat runs about 30 times a second (as PR #22) on a host thread through cheat_vm.c, reading and writing guest memory with
//   dm_mem_read / dm_mem_write (plat.h: Mach's, or process_vm_readv / writev), so a stale pointer fails instead of crashing; a store into any module's code or patch area is refused.
//   The held buttons are what the game last read (hle_hid.c). A program that stops is reported once (it is run again next time).
#include "dm.h"
#include "cheat_vm.h"
#include <TargetConditionals.h>
#include <ctype.h>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
int dm_rewrite_touches(const char *dir, const char *module, uint64_t lo, uint64_t hi);   // pchtxt.c
int hle_hid_last(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);   // hle_hid.c
void dm_guest_bases(uint64_t out[4]);   // hle_svc.c

static Cheat *g_ch; static int g_nch; static unsigned char g_on[CHEAT_MAX], g_code[CHEAT_MAX], g_told[CHEAT_MAX];
static uint64_t g_statics[256];
static int cheat_wanted(const char *list, const char *name) {
    if (!list) return 0;
    if (!strcmp(list, "all")) return 1;
    size_t l = strlen(name);
    for (const char *p = list; *p;) { size_t k = strcspn(p, ","); if (k == l && !strncmp(p, name, l)) return 1; p += k; if (*p == ',') p++; }
    return 0;
}
// A cheat of stores into main's code only (0, region 0, no offset register): 1, with its stores checked; 0 not such a cheat; -1 refused
static int code_cheat(const Cheat *c, uint64_t text_end, const char *dir, char *why, size_t n) {
    int any = 0;
    for (unsigned i = 0; i < c->n;) {
        unsigned len = cheat_insn_len(c->w, c->n, i); uint32_t a = c->w[i];
        if (!len) return 0;
        if (a >> 28 != 0 || ((a >> 24) & 0xf) == 0 || ((a >> 20) & 0xf) != 0 || ((a >> 16) & 0xf) != 0) return any ? (snprintf(why, n, "stores into main's code in a program that does more"), -1) : 0;
        unsigned t = (a >> 24) & 0xf; uint64_t off = ((uint64_t)(a & 0xff) << 32) | c->w[i + 1];
        if (off + t > text_end) return any ? (snprintf(why, n, "stores both into main's code and elsewhere"), -1) : 0;
        if ((t != 4 && t != 8) || (off & 3)) { snprintf(why, n, "a store of %u bytes at main+%#llx in its code (4 or 8 bytes, aligned)", t, (unsigned long long)off); return -1; }
#ifndef DM_TEST_CHEATS_NO_REWRITE_CHECK
        if (dm_rewrite_touches(dir, "main", off, off + t)) { snprintf(why, n, "main+%#llx is a word the rewriter changed", (unsigned long long)off); return -1; }
#endif
        any = 1; i += len;
    }
    return any;
}

#if TARGET_OS_IPHONE
void dm_cheats_code(const char *dir, module_t *m) { (void)dir; (void)m; if (getenv("DM_CHEATS")) LOG("[red] DM_CHEATS: not on iOS (no guarded access to guest memory there): no cheats"); }
void dm_cheats_start(void) {}
#else
#include "plat.h"
static int in_code(uint64_t a, unsigned n) {
    for (int i = 0; i < g_nmod; i++) { uint64_t b = (uint64_t)(uintptr_t)g_mod[i].base;
        if ((a < b + g_mod[i].text_end && b < a + n) || (a < b + g_mod[i].size && b + g_mod[i].patch_off < a + n)) return 1; }
    return 0;
}
static int vm_rd(void *ctx, uint64_t a, void *o, unsigned n) { (void)ctx; return dm_mem_read(o, a, n); }
static int vm_wr(void *ctx, uint64_t a, const void *i, unsigned n) { (void)ctx; if (in_code(a, n)) return 0; return dm_mem_write(a, i, n); }
// Main's image (from the loader, before the code is sealed): the file read, the cheats chosen, the code cheats written
void dm_cheats_code(const char *dir, module_t *m) {
    const char *path = getenv("DM_CHEATS"); if (!path || !*path || strcmp(m->name, "main")) return;
    FILE *f = fopen(path, "r"); if (!f) { LOG("[red] DM_CHEATS %s: cannot open it", path); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *text = calloc(1, (size_t)sz + 1); if (fread(text, 1, (size_t)sz, f) != (size_t)sz) sz = 0; fclose(f);
    const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
    int hexname = 1; for (int i = 0; i < 16; i++) hexname &= isxdigit((unsigned char)base[i]) != 0;
    if (hexname && !strcasecmp(base + 16, ".txt") && strncasecmp(base, dm_main_build(), 16)) { LOG("[red] DM_CHEATS %s: for build %.16s, main is %s: no cheats", path, base, dm_main_build()); free(text); return; }
    g_ch = calloc(CHEAT_MAX, sizeof *g_ch); char err[200];
    g_nch = cheat_parse(text, g_ch, CHEAT_MAX, err, sizeof err); free(text);
    if (g_nch < 0) { LOG("[red] DM_CHEATS %s: %s: no cheats", path, err); g_nch = 0; return; }
    const char *on = getenv("DM_CHEATS_ON"); int non = 0, ncode = 0;
    for (int k = 0; k < g_nch; k++) {
        g_on[k] = g_ch[k].master || cheat_wanted(on, g_ch[k].name); if (!g_on[k] || !g_ch[k].n) continue;
        char why[160] = ""; int cc = code_cheat(&g_ch[k], m->text_end, dir, why, sizeof why);
        if (cc < 0) { LOG("[red] cheat \"%s\": %s: not applied", g_ch[k].name, why); g_on[k] = 0; continue; }
        if (cc > 0) {
            for (unsigned i = 0; i < g_ch[k].n; i += cheat_insn_len(g_ch[k].w, g_ch[k].n, i)) {
                uint32_t a = g_ch[k].w[i]; unsigned t = (a >> 24) & 0xf; uint64_t off = ((uint64_t)(a & 0xff) << 32) | g_ch[k].w[i + 1];
                uint64_t v = t == 8 ? ((uint64_t)g_ch[k].w[i + 2] << 32) | g_ch[k].w[i + 3] : g_ch[k].w[i + 2]; memcpy(m->base + off, &v, t);
            }
            g_code[k] = 1; ncode++; LOG("cheat \"%s\": written into main's code at load", g_ch[k].name);
        }
        non++;
    }
    LOG("DM_CHEATS %s: %d cheats, %d on (%d into main's code at load)", path, g_nch, non, ncode);
}
static void *cheat_thread(void *arg) {
    (void)arg; pthread_setname_np("dm cheats");
    for (;;) {
        usleep(33000);
        CheatEnv e = {.statics = g_statics, .read = vm_rd, .write = vm_wr}; dm_guest_bases(e.base);
        int32_t lx, ly, rx, ry; hle_hid_last(&e.keys, &lx, &ly, &rx, &ry);
        for (int k = 0; k < g_nch; k++) {
            if (!g_on[k] || g_code[k] || !g_ch[k].n) continue;
            char err[200]; if (!cheat_run(&g_ch[k], &e, err, sizeof err) && !g_told[k]) { g_told[k] = 1; LOG("cheat \"%s\" stopped: %s (run again each time)", g_ch[k].name, err); }
        }
    }
    return NULL;
}
void dm_cheats_start(void) {   // after the modules are loaded
    int run = 0; for (int k = 0; k < g_nch; k++) run |= g_on[k] && !g_code[k] && g_ch[k].n;
    if (!run) return;
    pthread_t t; pthread_create(&t, NULL, cheat_thread, NULL); pthread_detach(t);
    LOG("cheats: running about 30 times a second");
}
#endif
