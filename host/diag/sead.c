// sead heaps and intrusive lists (plan item 48, after MacNativeGames' TOTK_CRASH_HEAPFIND / TOTK_CRASH_HEAPWALK / TOTK_LISTCHECK,
// ly05010419/MacNativeGames, MIT). Games on Nintendo's sead library (Tears of the Kingdom) allocate from sead heaps; an ExpHeap's use list links
// every block it handed out, and a block overwritten past its end shows up there as a broken link. Layout, as MacNativeGames reads it and as
// Tears of the Kingdom 1.4.3 has it (names such as RootHeap, GraphicsSystem, Resource, Actor read back, 324 use lists walk whole):
//   heap   +0x00 vtable, +0x20 name (char *), +0x28 start (the heap's own address: a heap object starts its range), +0x30 size,
//          +0xc0 free block count (32 bits), +0xc8 use list sentinel {prev, next} (ExpHeap)
//   block  {prev, next} at its start (the use list's node), +0x10 attribute, +0x18 size, the data at +0x20
// A child heap lives in a block of its parent, its object at the block's data. Other heap classes have no use list at +0xc8 (in 1.4.3 two
// vtables have one, three do not; 6,000 small heaps are of one without), and the game has no C++ type names: heaps are told apart by vtable, and a
// use list counts as broken only in a heap whose class has whole ones elsewhere. Memory is read through dm_mem_read (an unmapped address is an
// error, not a second fault), so these run at a crash too.
//   DM_CRASH_HEAP=<reg>[,<reg>...]   on a crash, the heaps holding each register's address (x0-x30, sp) and the block it is in (crash.c)
//   DM_LIST_WATCH=[heap+]<addr>[,...] watch intrusive lists (the address of a sentinel {prev, next}); the first node whose next is null (seen twice
//                                    50 µs apart) is logged in red with every guest thread's state, once
// The command channel (cmdchan.c) has "heaps" (every heap, by class) and "heap <addr>" (as DM_CRASH_HEAP). Checked by host/tests/t_sead.c.
#include "dm.h"
#include "plat.h"
#include <pthread.h>
#include <unistd.h>
void svc_guest_layout(uint8_t **alias, size_t *alias_n, uint8_t **stack, size_t *stack_n, uint8_t **heap, size_t *heap_n);   // hle_svc.c
void svc_thread_dump(void);                                                                                                  // hle_thread.c
enum { H_NAME = 0x20, H_START = 0x28, H_SIZE = 0x30, H_NFREE = 0xc0, H_USE = 0xc8, H_HDR = 0xd8, B_DATA = 0x20 };
#define SEAD_PG 0x1000ull
#define SEAD_MAXSIZE (1ull << 32)
#define SEAD_MAXBLK 200000
#define SEAD_MAXH 32768

static int rd(uint64_t a, void *out, size_t n) { return a && dm_mem_read(out, a, n); }
static void rd_name(uint64_t p, char *out, size_t n) {   // a printable C string, or "?"
    char t[64]; size_t k = 0;
    if (p && rd(p, t, sizeof t)) while (k < sizeof t - 1 && k < n - 1 && t[k] >= 0x20 && t[k] < 0x7f) { out[k] = t[k]; k++; }
    if (!k) out[k++] = '?';
    out[k] = 0;
}
static void addr_name(uint64_t a, char *out, size_t n) {   // module+offset, or the address
    module_t *m = module_of((const void *)(uintptr_t)a);
    if (m) snprintf(out, n, "%s+%#llx", m->name, (unsigned long long)(a - (uint64_t)(uintptr_t)m->base)); else snprintf(out, n, "%#llx", (unsigned long long)a);
}
// a heap object at h: its start is h and its size is sane
static int sead_is_heap(uint64_t h, uint64_t *size) {
    uint64_t w[2]; if (!rd(h + H_START, w, sizeof w) || w[0] != h || w[1] < H_HDR || w[1] >= SEAD_MAXSIZE) return 0;
    if (size) *size = w[1];
    return 1;
}
typedef struct { uint64_t blocks, used, bad_at, bad_node; const char *bad; } sead_walk_t;
// Walks h's use list: blocks, bytes in them, and the first broken link (a node outside the heap, a null or unreadable next, a prev that does
// not point back, more than SEAD_MAXBLK blocks: a loop). each(block, size, attr, ctx) for every block, if given
static sead_walk_t sead_walk(uint64_t h, void (*each)(uint64_t, uint64_t, uint64_t, void *), void *ctx) {
    sead_walk_t r = {0}; uint64_t size = 0, sent = h + H_USE, prev = sent, p;
    if (!sead_is_heap(h, &size)) { r.bad = "not a heap"; return r; }
    if (!rd(sent + 8, &p, 8)) { r.bad = "use list unreadable"; return r; }
    while (p != sent) {
        uint64_t b[4];
        if (r.blocks >= SEAD_MAXBLK) { r.bad = "more blocks than the walk allows (a loop?)"; break; }
        if (!p) { r.bad = "null next"; break; }
        if (p < h || p >= h + size) { r.bad = "a node outside the heap"; break; }
        if (!rd(p, b, sizeof b)) { r.bad = "an unreadable node"; break; }
        if (b[0] != prev) { r.bad = "prev does not point back"; break; }
        r.blocks++; r.used += b[3]; if (each) each(p, b[3], b[2], ctx);
        prev = p; p = b[1];
    }
    if (r.bad) { r.bad_at = r.blocks; r.bad_node = p; }
    return r;
}

// ---------- every heap ----------
typedef struct { uint64_t h, size, vt; int list; } sead_heap;   // list: its use list walks whole
static sead_heap g_sh[SEAD_MAXH]; static int g_nsh;
static pthread_mutex_t g_sh_mu = PTHREAD_MUTEX_INITIALIZER;
static void add_heap(uint64_t h, uint64_t size) {
    if (g_nsh == SEAD_MAXH) return;
    sead_heap *s = &g_sh[g_nsh++]; s->h = h; s->size = size; s->vt = 0; rd(h, &s->vt, 8); s->list = !sead_walk(h, NULL, NULL).bad;
}
static void kid(uint64_t b, uint64_t size, uint64_t attr, void *ctx) {   // a block whose data is a heap object: a child heap
    (void)size; (void)attr; (void)ctx; uint64_t ks;
    if (sead_is_heap(b + B_DATA, &ks)) add_heap(b + B_DATA, ks);
}
// Every heap in [lo, hi), added to the table: page starts that are heaps, and their children (to depth 8); returns how many were added
int sead_scan(uint64_t lo, uint64_t hi) {
    int first = g_nsh;
    for (uint64_t pg = (lo + SEAD_PG - 1) & ~(SEAD_PG - 1); pg + H_HDR <= hi && g_nsh < SEAD_MAXH; pg += SEAD_PG) {
        uint64_t size; if (!sead_is_heap(pg, &size)) continue;
        add_heap(pg, size);
        pg += (size - 1) & ~(SEAD_PG - 1);   // past the heap: what is inside it is found through its blocks
    }
    for (int i = first, depth = 0, level_end = g_nsh; i < g_nsh && depth < 8; i++) {   // (a block belongs to one heap: each child is found once)
        if (g_sh[i].list) sead_walk(g_sh[i].h, kid, NULL);
        if (i + 1 == level_end) { depth++; level_end = g_nsh; }
    }
    return g_nsh - first;
}
static void scan_guest(void) {
    uint8_t *a, *s, *h; size_t an, sn, hn; svc_guest_layout(&a, &an, &s, &sn, &h, &hn);
    g_nsh = 0;
    if (h) sead_scan((uint64_t)h, (uint64_t)h + hn);
    if (a) sead_scan((uint64_t)a, (uint64_t)a + an);
}
static int class_lists(uint64_t vt) {   // a heap of this class has a whole use list
    for (int i = 0; i < g_nsh; i++) if (g_sh[i].vt == vt && g_sh[i].list) return 1;
    return 0;
}
// The innermost heap holding addr (the smallest of those holding it), -1 if none
int sead_innermost(uint64_t addr) {
    int best = -1;
    for (int i = 0; i < g_nsh; i++) {
#ifndef DM_TEST_SEAD_BROKEN   // negative control of t_sead only: a heap taken whether or not it reaches addr
        if (addr < g_sh[i].h || addr >= g_sh[i].h + g_sh[i].size) continue;
#else
        if (addr < g_sh[i].h) continue;
#endif
        if (best < 0 || g_sh[i].size < g_sh[best].size) best = i;
    }
    return best;
}
static void heap_line(const sead_heap *s, const char *indent) {
    uint64_t np = 0, free_n = 0; char name[64], vt[48]; rd(s->h + H_NAME, &np, 8); rd_name(np, name, sizeof name); addr_name(s->vt, vt, sizeof vt);
    if (s->list) { sead_walk_t r = sead_walk(s->h, NULL, NULL); rd(s->h + H_NFREE, &free_n, 8);
        LOG("[sead] %sheap %s (class %s) at %#llx, %#llx bytes: %llu blocks in use (%#llx bytes), %u free blocks", indent, name, vt, (unsigned long long)s->h,
            (unsigned long long)s->size, (unsigned long long)r.blocks, (unsigned long long)r.used, (unsigned)(free_n & 0xffffffff)); }
    else if (class_lists(s->vt)) { sead_walk_t r = sead_walk(s->h, NULL, NULL);
        LOG("[sead] %s[red] heap %s (class %s) at %#llx, %#llx bytes: use list broken (%s) after block %llu, at node %#llx", indent, name, vt, (unsigned long long)s->h,
            (unsigned long long)s->size, r.bad, (unsigned long long)r.bad_at, (unsigned long long)r.bad_node); }
    else LOG("[sead] %sheap %s (class %s) at %#llx, %#llx bytes (a class without a use list)", indent, name, vt, (unsigned long long)s->h, (unsigned long long)s->size);
}
// The command channel's "heaps": every heap in the guest's heap and alias regions, then the classes
void sead_heaps_print(void) {
    pthread_mutex_lock(&g_sh_mu);
    uint64_t t0 = now_ns(); scan_guest(); uint64_t t1 = now_ns();
    int bad = 0; for (int i = 0; i < g_nsh; i++) { heap_line(&g_sh[i], ""); bad += !g_sh[i].list && class_lists(g_sh[i].vt); }
    uint64_t vts[32]; int nvt = 0;
    for (int i = 0; i < g_nsh; i++) { int k = 0; while (k < nvt && vts[k] != g_sh[i].vt) k++; if (k == nvt && nvt < 32) vts[nvt++] = g_sh[i].vt; }
    for (int k = 0; k < nvt; k++) { int n = 0, whole = 0; char vt[48]; addr_name(vts[k], vt, sizeof vt);
        for (int i = 0; i < g_nsh; i++) if (g_sh[i].vt == vts[k]) { n++; whole += g_sh[i].list; }
        LOG("[sead] class %s: %d heaps, %d with a whole use list", vt, n, whole); }
    LOG("[sead] %d heaps%s (found in %.0f ms), %d with a broken use list", g_nsh, g_nsh == SEAD_MAXH ? " or more" : "", (t1 - t0) / 1e6, bad);
    pthread_mutex_unlock(&g_sh_mu);
}
// "heap <addr>" and DM_CRASH_HEAP: the heaps holding addr, outermost first, and the block of the innermost one it is in
typedef struct { uint64_t addr, b, size, attr; } in_blk;
static void find_blk(uint64_t b, uint64_t size, uint64_t attr, void *ctx) {
    in_blk *f = ctx; if (!f->b && f->addr >= b && f->addr < b + B_DATA + size) { f->b = b; f->size = size; f->attr = attr; }
}
void sead_heap_of(uint64_t addr) {
    pthread_mutex_lock(&g_sh_mu);
    scan_guest(); int in = sead_innermost(addr);
    if (in < 0) { LOG("[sead] %#llx: in none of the %d heaps", (unsigned long long)addr, g_nsh); pthread_mutex_unlock(&g_sh_mu); return; }
    int chain[16], nc = 0;   // the heaps holding addr, largest first (each holds the next)
    for (int pass = 0; pass < 16; pass++) { int pick = -1;
        for (int i = 0; i < g_nsh; i++) if (addr >= g_sh[i].h && addr < g_sh[i].h + g_sh[i].size && (!nc || g_sh[i].size < g_sh[chain[nc - 1]].size) && (pick < 0 || g_sh[i].size > g_sh[pick].size)) pick = i;
        if (pick < 0) break; chain[nc++] = pick; }
    LOG("[sead] %#llx is in %d heap%s:", (unsigned long long)addr, nc, nc == 1 ? "" : "s");
    for (int i = 0; i < nc; i++) heap_line(&g_sh[chain[i]], "  ");
    const sead_heap *s = &g_sh[in]; in_blk f = {addr, 0, 0, 0};
    if (s->list) sead_walk(s->h, find_blk, &f);
    if (f.b) { uint64_t d0 = 0; rd(f.b + B_DATA, &d0, 8);
        int64_t o = (int64_t)(addr - f.b - B_DATA);
        LOG("[sead]   in its block +%#llx (size %#llx, attr %#llx, data[0] %#llx), at data %c %#llx", (unsigned long long)(f.b - s->h), (unsigned long long)f.size,
            (unsigned long long)f.attr, (unsigned long long)d0, o < 0 ? '-' : '+', (unsigned long long)(o < 0 ? -o : o)); }
    else if (s->list) LOG("[sead]   in none of its blocks in use (free memory or the heap's own header)");
    pthread_mutex_unlock(&g_sh_mu);
}

// ---------- DM_LIST_WATCH ----------
#define LW_MAX 8
static uint64_t g_lw[LW_MAX]; static int g_nlw; static const char *g_lw_spec;
// A list's first node whose next is null, seen twice 50 µs apart (a list being changed is not caught), or 0
uint64_t sead_list_check(uint64_t sent, uint64_t *prev_out) {
    uint64_t p; if (!rd(sent + 8, &p, 8)) return 0;
    for (int k = 0; p && p != sent && k < SEAD_MAXBLK; k++) {
        uint64_t l[2]; if (!rd(p, l, sizeof l)) return 0;
        if (!l[1]) { usleep(50); if (rd(p, l, sizeof l) && !l[1]) { if (prev_out) *prev_out = l[0]; return p; } return 0; }
        p = l[1];
    }
    return 0;
}
static int lw_parse(void) {   // 0 while the guest heap a heap+ address needs is not reserved yet
    char buf[512]; strlcpy(buf, g_lw_spec, sizeof buf); g_nlw = 0;
    for (char *save, *t = strtok_r(buf, ",", &save); t && g_nlw < LW_MAX; t = strtok_r(NULL, ",", &save)) {
        uint64_t base = 0;
        if (!strncmp(t, "heap+", 5)) { uint8_t *a, *s, *h; size_t an, sn, hn; svc_guest_layout(&a, &an, &s, &sn, &h, &hn); if (!h) return 0; base = (uint64_t)h; t += 5; }
        g_lw[g_nlw++] = base + strtoull(t, NULL, 0);
    }
    return 1;
}
static void *lw_thread(void *arg) {
    (void)arg;
    while (!lw_parse()) usleep(100000);
    for (int i = 0; i < g_nlw; i++) LOG("list watch: %#llx", (unsigned long long)g_lw[i]);
    for (;;) {
        for (int i = 0; i < g_nlw; i++) {
            uint64_t prev = 0, node = sead_list_check(g_lw[i], &prev);
            if (node) { LOG("[red] list watch: the list at %#llx has a node %#llx with a null next (prev %#llx); the guest threads now:", (unsigned long long)g_lw[i],
                            (unsigned long long)node, (unsigned long long)prev); svc_thread_dump(); return NULL; }
        }
        usleep(1000);
    }
}
void sead_list_watch_start(void) {
    if (!(g_lw_spec = getenv("DM_LIST_WATCH")) || !*g_lw_spec) return;
    pthread_t t; if (!pthread_create(&t, NULL, lw_thread, NULL)) pthread_detach(t);
}
