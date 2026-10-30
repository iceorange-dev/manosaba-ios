// gfx/nvn/core/usnap.c: uniform snapshots, see usnap.h
#include "usnap.h"
#include "pend.h"
#include "../../../host/dm.h"
#include <pthread.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

us_state g_us = {.on = -1, .par = -1};
int32_t g_bh_gen = 2;
int g_bh_seen;
void mtl_bh_deferred_seen(void) { g_bh_seen = 1; }
int us_on(void) {
    if (g_us.on < 0) { const char *e = getenv("DM_NVN_UBO_SNAPSHOT"); g_us.on = !(e && !strcmp(e, "0")); MLOG("uniform snapshots at submission start: %s (DM_NVN_UBO_SNAPSHOT)", g_us.on ? "on" : "off, live memory"); }
    return g_us.on;
}
// Ranges already collected in this snapshot (open addressing, four probes): the same uniform block bound for draw after draw is collected (and
// sorted) once. A range finding no free place goes through again, which the merge drops
static us_rng g_us_seen[4096];
static int us_seen(uint64_t lo, uint64_t hi) {
    uint64_t h = ((lo >> 4) ^ (hi << 7)) * 0x9E3779B97F4A7C15ull;
    for (unsigned k = 0, i = (unsigned)(h >> 52); k < 4; k++, i = (i + 1) & 4095) {
        us_rng *e = &g_us_seen[i];
        if (e->lo == lo && e->hi == hi) return 1;
        if (!e->lo) { e->lo = lo; e->hi = hi; return 0; }
    }
    return 0;
}
void us_collect(const uint8_t *p, int depth, int us) {
    if (depth > 32) return;
    for (uint32_t guard = 0; guard < 4000000 && p; guard++) {
        op_hdr h; memcpy(&h, p, sizeof h);
        if (h.size < 8 || (h.size & 7) || h.op == OP_END) return;
        if (h.op == OP_JUMP) { const uint8_t *t; memcpy(&t, p + 8, 8); p = t; continue; }
        if (h.op == OP_CALL) { uint64_t t; memcpy(&t, p + 8, 8); us_collect((const uint8_t *)(uintptr_t)t, depth + 1, us); }
        else if (h.op == OP_BIND_TEX || h.op == OP_BIND_SAMP || h.op == OP_BIND_IMG) bh_fresh((const p_bind_handles *)(p + 8));
        else if (us && h.op == OP_BIND_UBO) { const p_bind_buf *b = (const p_bind_buf *)(p + 8);
            if (b->addr && b->size && b->size <= (1u << 20) && !us_seen(b->addr, b->addr + b->size)) {
                if (g_us.rn == g_us.rcap) { g_us.rcap = g_us.rcap ? g_us.rcap * 2 : 8192; g_us.rng = realloc(g_us.rng, (size_t)g_us.rcap * sizeof *g_us.rng); }
                g_us.rng[g_us.rn++] = (us_rng){b->addr, b->addr + b->size}; } }
        p += h.size;
    }
}
int us_rng_cmp(const void *x, const void *y) { const us_rng *a = x, *b = y; return (a->lo > b->lo) - (a->lo < b->lo); }
static us_rng *g_us_tmp; static int g_us_tcap;
void us_rng_sort_x(us_rng *a, int n, us_rng **tmp, int *tcap, uint32_t cnt[6][2048]) {
    if (n < 64) { qsort(a, (size_t)n, sizeof *a, us_rng_cmp); return; }
    if (*tcap < n) { *tcap = n * 2; *tmp = realloc(*tmp, (size_t)*tcap * sizeof **tmp); if (!*tmp) MDIE("uniform snapshot sort buffer"); }
    memset(cnt, 0, 6 * 2048 * sizeof(uint32_t));
    for (int i = 0; i < n; i++) { uint64_t k = a[i].lo; for (int d = 0; d < 6; d++) cnt[d][(k >> (11 * d)) & 2047]++; }
    us_rng *src = a, *dst = *tmp;
    for (int d = 0; d < 6; d++) {
        uint32_t *c = cnt[d]; if (c[(a[0].lo >> (11 * d)) & 2047] == (uint32_t)n) continue;   // one digit value for all: nothing to move
        for (uint32_t v = 0, acc = 0; v < 2048; v++) { uint32_t x = c[v]; c[v] = acc; acc += x; }
        for (int i = 0; i < n; i++) dst[c[(src[i].lo >> (11 * d)) & 2047]++] = src[i];
        us_rng *t = src; src = dst; dst = t;
    }
    if (src != a) memcpy(a, src, (size_t)n * sizeof *a);
}
void us_rng_sort(us_rng *a, int n) { static uint32_t cnt[6][2048]; us_rng_sort_x(a, n, &g_us_tmp, &g_us_tcap, cnt); }
int us_merge(us_rng *u, int n) {   // the copies stay disjoint: a range overlapping a finished copy keeps only its part beyond it
    int k = 0;
    for (int i = 1; i < n; i++) {
        uint64_t hi = u[i].hi > u[k].hi ? u[i].hi : u[k].hi;
        if (hi <= u[k].hi) continue;
        if (u[i].lo <= u[k].hi + 256 && hi - u[k].lo <= (1u << 20) && nvn_be_pool_span(u[k].lo, hi - u[k].lo)) u[k].hi = hi;
        else { us_rng r = u[i]; if (r.lo < u[k].hi) r.lo = u[k].hi; u[++k] = r; }
    }
    return k + 1;
}
static int us_gw_cmp(const void *x, const void *y) { const us_gw *a = x, *b = y; return (a->lo > b->lo) - (a->lo < b->lo); }
int us_gw_keep(uint64_t keep, uint64_t *gw_max) {
    int ng = 0; *gw_max = 0;
    for (int i = 0; i < g_us.gwn; i++) if (g_us.gw[i].fence > keep) { g_us.gw[ng++] = g_us.gw[i]; if (g_us.gw[i].hi - g_us.gw[i].lo > *gw_max) *gw_max = g_us.gw[i].hi - g_us.gw[i].lo; }
    g_us.gwn = ng;
    if (ng > 1) qsort(g_us.gw, (size_t)ng, sizeof *g_us.gw, us_gw_cmp);
    return ng;
}
int us_gw_reaches(uint64_t lo, uint64_t hi, int ng, uint64_t gw_max) {
    uint64_t from = lo > gw_max ? lo - gw_max : 0; int a = 0, b = ng, r = 0;
    while (a < b) { int m = (a + b) / 2; if (g_us.gw[m].lo < from) a = m + 1; else b = m; }
    for (int j = a; j < ng && g_us.gw[j].lo < hi; j++) if (g_us.gw[j].hi > lo) {
#ifndef DM_TEST_US_GW_ALL_KNOWN   // negative control of t_core_usnap only: every write taken as renamed updates written
        if (!g_us.gw[j].known) return 2;
#endif
        r = 1;
    }
    return r;
}
void us_reserve(int n) { if (g_us.cap < n) { g_us.cap = n + 256; g_us.ent = realloc(g_us.ent, (size_t)g_us.cap * sizeof *g_us.ent); } }
us_ent *us_add(uint64_t lo, uint64_t hi) {
    uint64_t pad = lo & 255, sz = hi - lo; uint8_t *cpu; uint64_t gpu; void *buf;
    nvn_be_arena((size_t)(sz + pad), &cpu, &gpu, &buf);   // same offset modulo 256 as the original
    us_ent *c = &g_us.ent[g_us.n++];
    c->lo = lo; c->hi = hi; c->cpu = cpu + pad; c->gpu = gpu + pad; c->buf = buf; c->dropped = c->lay = 0;
    g_us.bytes += sz;
    return c;
}
// The copies themselves, done before the replay starts as before: above US_PAR_MIN bytes split by bytes over US_PAR parts on the system's worker threads
// (one on this thread), each part whole entries
enum { US_PAR = 4, US_PAR_MIN = 256 << 10 };
// A source the game made inaccessible after its pool was registered used to end the process (SIGSEGV / SIGBUS, also on the worker threads that
// copy in parts): each copy runs under a per-thread fault guard and a faulting one is dropped, so its draws read memory as without snapshots
// (MacNativeGames guards usnap_copy_one the same way after a helper thread died in about one session of twelve; MIT, written anew here)
static __thread struct { sigjmp_buf jb; uint64_t lo, hi; int armed; } t_usg;
static int us_fault(void *addr, int is_write, void *uc) {   // fault hook: only a fault inside the range this thread is copying
    (void)is_write; (void)uc; uint64_t a = (uint64_t)(uintptr_t)addr;
    if (!t_usg.armed || a < t_usg.lo || a >= t_usg.hi) return 0;
    t_usg.armed = 0; siglongjmp(t_usg.jb, 1);
}
static void us_hook_once(void) { dm_fault_hook_register(us_fault); }
static pthread_once_t g_us_hook = PTHREAD_ONCE_INIT;
// One sigsetjmp for the run of copies, not one a copy (plan item 225): saving the signal mask is two system calls on Darwin (sigprocmask and
// sigaltstack), and the copies of a submission are hundreds; 38 % of the game thread's submit call on the M5. A fault drops its copy and the loop goes
// on with the next behind a new sigsetjmp
static void us_copy_range(int a, int b) {
    pthread_once(&g_us_hook, us_hook_once);
    volatile int i = a;
    for (;;) {
        atomic_fetch_add_explicit(&g_us.jumps, 1, memory_order_relaxed);
        if (!sigsetjmp(t_usg.jb, 1)) {
            for (; i < b; i++) {
#ifdef DM_TEST_US_GUARD_EACH   // negative control of t_usnap only: a sigsetjmp for every copy, as before
                atomic_fetch_add_explicit(&g_us.jumps, 1, memory_order_relaxed);
                if (sigsetjmp(t_usg.jb, 1)) { g_us.ent[i].dropped = 1; atomic_fetch_add(&g_us.faults, 1); continue; }
#endif
                t_usg.lo = g_us.ent[i].lo; t_usg.hi = g_us.ent[i].hi; t_usg.armed = 1;
                memcpy(g_us.ent[i].cpu, (const void *)(uintptr_t)g_us.ent[i].lo, (size_t)(g_us.ent[i].hi - g_us.ent[i].lo)); t_usg.armed = 0;
            }
            return;
        }
        g_us.ent[i].dropped = 1; atomic_fetch_add(&g_us.faults, 1); i++;   // (the fault hook disarmed the guard)
    }
}
static void us_copy_part(void *ctx, size_t part) { const int *cp = ctx; us_copy_range(cp[part], cp[part + 1]); }
void us_copy_all(void) {
    if (g_us.par < 0) { const char *e = getenv("DM_NVN_UBO_SNAPSHOT_PAR"); g_us.par = !(e && !strcmp(e, "0")); }
    uint64_t total = 0; for (int i = 0; i < g_us.n; i++) total += g_us.ent[i].hi - g_us.ent[i].lo;
    if (!g_us.par || total < US_PAR_MIN || g_us.n < US_PAR) { us_copy_range(0, g_us.n); return; }
    int cut[US_PAR + 1] = {0}; uint64_t acc = 0; int k = 1;
    for (int i = 0; i < g_us.n && k < US_PAR; i++) { acc += g_us.ent[i].hi - g_us.ent[i].lo; if (acc >= total * (uint64_t)k / US_PAR) cut[k++] = i + 1; }
    while (k <= US_PAR) cut[k++] = g_us.n;
    g_us.par_n++;   // nvn_be_parallel returns when every part is done
    nvn_be_parallel(US_PAR, us_copy_part, cut);
}
void us_cap_copy(int nr, const us_rng *r, uint64_t *off, uint8_t *data) {
    pthread_once(&g_us_hook, us_hook_once);
    volatile int i = 0;   // one guard for the run (us_copy_range)
    for (;;) {
        atomic_fetch_add_explicit(&g_us.jumps, 1, memory_order_relaxed);
        if (!sigsetjmp(t_usg.jb, 1)) {
            for (; i < nr; i++) {
                if (off[i] == UINT64_MAX) continue;
                t_usg.lo = r[i].lo; t_usg.hi = r[i].hi; t_usg.armed = 1;
                memcpy(data + off[i], (const void *)(uintptr_t)r[i].lo, (size_t)(r[i].hi - r[i].lo)); t_usg.armed = 0;
            }
            break;
        }
        off[i] = UINT64_MAX; atomic_fetch_add(&g_us.faults, 1); i++;   // not readable: read live at the replay
    }
}
void us_snapshot(const uint64_t *h, int n) {
    if (!us_on() && g_bh_seen) for (int i = 0; i < n; i++) us_collect((const uint8_t *)(uintptr_t)h[i], 0, 0);
    if (!us_on() || !n) return;
    uint64_t t0 = core_now_ns();
    g_us.subs++;
    uint64_t gw_max; int ng = us_gw_keep(nvn_be_done_fence(), &gw_max);   // GPU writes of submissions not completed yet, sorted by start
    g_us.rn = 0; memset(g_us_seen, 0, sizeof g_us_seen);
    for (int i = 0; i < n; i++) us_collect((const uint8_t *)(uintptr_t)h[i], 0, 1);
    if (!g_us.rn) return;
    g_us.ranges += (uint64_t)g_us.rn;
    us_rng_sort(g_us.rng, g_us.rn);
    us_rng *u = g_us.rng; int nr = us_merge(u, g_us.rn);
    us_reserve(nr);
    for (int i = 0; i < nr; i++) {
        if (!nvn_be_pool_span(u[i].lo, u[i].hi - u[i].lo)) continue;   // not in one pool: read live memory
        int gw = ng ? us_gw_reaches(u[i].lo, u[i].hi, ng, gw_max) : 0;
        if (gw == 2 || (gw == 1 && g_pend.over)) { g_us.skip_gpu++; continue; }   // a GPU write still running reaches it, its bytes not known: read on the GPU timeline
        us_add(u[i].lo, u[i].hi)->lay = gw == 1;
    }
    // Read after the arena allocations above and before the bytes: the writes completed by then are in memory, and the bytes of those not completed
    // are still in their submissions' arenas (a block is reused only once its submission completed)
    uint64_t done = nvn_be_done_fence();
    uint64_t uf0 = atomic_load(&g_us.faults);
    us_copy_all();
    for (int i = 0; i < g_us.n; i++) if (g_us.ent[i].lay && !g_us.ent[i].dropped) {   // renamed updates of a running submission over memory, in order
#ifndef DM_TEST_US_NO_LAY   // negative control of t_core_usnap only: memory as it is, the updates not laid over
        if (!pend_lay(g_us.ent[i].lo, g_us.ent[i].cpu, g_us.ent[i].hi - g_us.ent[i].lo, done)) { g_us.ent[i].dropped = 1; continue; }
#else
        (void)done;
#endif
        g_us.laid++;
    }
    if (atomic_load(&g_us.faults) != uf0) { static int once; if (once++ < 8) MLOG("[red] uniform snapshot: %llu source range(s) not readable (unmapped after the pool was registered): those copies dropped, their draws read memory", (unsigned long long)(atomic_load(&g_us.faults) - uf0)); }
    g_us.copies += (uint64_t)g_us.n;
    g_us.ns += core_now_ns() - t0;
}
void us_gw_note(uint64_t a, uint64_t n, uint64_t fence, int known) {
    int dup = 0;   // the same range written again and again (a storage buffer bound for many draws): noted once, known only if every time
    for (int j = g_us.gwn - 1; j >= 0 && j >= g_us.gwn - 8; j--) if (g_us.gw[j].lo == a && g_us.gw[j].hi == a + n && g_us.gw[j].fence == fence) { g_us.gw[j].known &= known; dup = 1; break; }
    if (!dup) { if (g_us.gwn == g_us.gwcap) { g_us.gwcap = g_us.gwcap ? g_us.gwcap * 2 : 4096; g_us.gw = realloc(g_us.gw, (size_t)g_us.gwcap * sizeof *g_us.gw); }
                g_us.gw[g_us.gwn++] = (us_gw){a, a + n, fence, known}; }
}
int us_drop_range(uint64_t a, uint64_t n, uint64_t fence) {
    if (!n || !us_on()) return 0;
    us_gw_note(a, n, fence, 0);
    if (!g_us.n) return 0;
    int lo = 0, hi = g_us.n;
    while (lo < hi) { int m = (lo + hi) / 2; if (g_us.ent[m].hi <= a) lo = m + 1; else hi = m; }
    int any = 0;
    for (int i = lo; i < g_us.n && g_us.ent[i].lo < a + n; i++) if (!g_us.ent[i].dropped) { g_us.ent[i].dropped = 1; g_us.drops++; any = 1; }
    return any;
}
void us_report(uint64_t f) {
    static uint64_t p[8]; uint64_t c[8] = {g_us.subs, g_us.ranges, g_us.copies, g_us.bytes, g_us.drops, g_us.skip_gpu, g_us.ns, g_us.laid};
    if (f % 300 || c[0] == p[0]) return;
    double ns = (double)(c[0] - p[0]);
    MLOG("uniform snapshots up to frame %llu: %llu submissions, per submission %.0f bindings in %.0f copies, %.1f KB, %.3f ms; %llu copies dropped by a GPU write of their submission, %llu ranges not copied (a GPU write of an earlier submission still running), %llu copied with an earlier submission's renamed updates laid over",
         (unsigned long long)f, (unsigned long long)(c[0] - p[0]), (c[1] - p[1]) / ns, (c[2] - p[2]) / ns, (c[3] - p[3]) / ns / 1024, (c[6] - p[6]) / ns / 1e6,
         (unsigned long long)(c[4] - p[4]), (unsigned long long)(c[5] - p[5]), (unsigned long long)(c[7] - p[7]));
    memcpy(p, c, sizeof p);
}
