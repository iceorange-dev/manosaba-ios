// gfx/nvn/core/frame.c: measurements by frame, see frame.h
#include "frame.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

bp_state g_bp;
uint64_t g_up_total_ns;
__attribute__((constructor)) static void bp_t0(void) { g_bp.t0 = bp_now(); }
int bootprof(void) { static int v = -1; if (v < 0) v = getenv("DM_BOOTPROF") != NULL; return v; }
// ---------- Per-frame GPU time aggregation ----------
#define GRING 1024
int g_gpu_span_host;
static struct { _Atomic uint64_t frame, ns, t0, t1; _Atomic int pending; } g_gpu[GRING];   // t0 / t1: the frame's earliest GPUStart / latest GPUEnd (mach absolute time, ns)
// The frame's GPU time is the union of its command buffers' spans: Apple GPUs start a buffer while the one before it runs, so the plain sum (ns) counted
// the overlap twice (LASRecomp PR #197: summed 13-17 ms against a union of 6.5-7 ms). The spans of the last GIVR frames are kept (the time is read 8
// frames late); a full list is merged, and if no two spans overlap the two closest are joined (their gap is counted, the only overestimate)
enum { GIV = 128 };
static struct { uint64_t frame; uint32_t n; uint64_t iv[GIV][2]; core_lock lk; } g_giv[GIVR];
static int giv_cmp(const void *a, const void *b) { const uint64_t *x = a, *y = b; return x[0] < y[0] ? -1 : x[0] > y[0]; }
static uint32_t giv_merge(uint64_t (*iv)[2], uint32_t n) {   // sort by start and join overlapping spans in place; returns the new count
    if (n < 2) return n;
    qsort(iv, n, sizeof *iv, giv_cmp);
    uint32_t m = 0;
    for (uint32_t i = 1; i < n; i++) { if (iv[i][0] <= iv[m][1]) { if (iv[i][1] > iv[m][1]) iv[m][1] = iv[i][1]; } else { m++; iv[m][0] = iv[i][0]; iv[m][1] = iv[i][1]; } }
    return m + 1;
}
static uint64_t giv_union_ns(uint64_t (*iv)[2], uint32_t *n) { *n = giv_merge(iv, *n); uint64_t s = 0; for (uint32_t i = 0; i < *n; i++) s += iv[i][1] - iv[i][0]; return s; }
static void giv_add(uint64_t f, uint64_t s0, uint64_t s1) {
    unsigned k = f % GIVR; core_lock_take(&g_giv[k].lk);
    if (g_giv[k].frame != f) { g_giv[k].frame = f; g_giv[k].n = 0; }
    if (g_giv[k].n == GIV) g_giv[k].n = giv_merge(g_giv[k].iv, GIV);
    if (g_giv[k].n == GIV) {   // still full: join the two closest neighbours
        uint32_t bi = 0; uint64_t bg = UINT64_MAX;
        for (uint32_t i = 0; i + 1 < GIV; i++) { uint64_t gap = g_giv[k].iv[i + 1][0] - g_giv[k].iv[i][1]; if (gap < bg) { bg = gap; bi = i; } }
        g_giv[k].iv[bi][1] = g_giv[k].iv[bi + 1][1]; memmove(&g_giv[k].iv[bi + 1], &g_giv[k].iv[bi + 2], (GIV - bi - 2) * sizeof g_giv[k].iv[0]); g_giv[k].n--;
    }
    g_giv[k].iv[g_giv[k].n][0] = s0; g_giv[k].iv[g_giv[k].n][1] = s1; g_giv[k].n++;
    core_lock_give(&g_giv[k].lk);
}
void gpu_tag_begin(uint64_t f) {
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f) { atomic_store(&g_gpu[k].frame, f); atomic_store(&g_gpu[k].ns, 0); atomic_store(&g_gpu[k].pending, 0); atomic_store(&g_gpu[k].t0, UINT64_MAX); atomic_store(&g_gpu[k].t1, 0); }
    atomic_fetch_add(&g_gpu[k].pending, 1);
}
void gpu_tag_done(uint64_t f, double g0, double g1) {
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f) return;
    double d = g1 - g0;
    if (d > 0) {
        atomic_fetch_add(&g_gpu[k].ns, (uint64_t)(d * 1e9));
        uint64_t s0 = (uint64_t)(g0 * 1e9), s1 = (uint64_t)(g1 * 1e9), v;
        giv_add(f, s0, s1);
        for (v = atomic_load(&g_gpu[k].t0); s0 < v && !atomic_compare_exchange_weak(&g_gpu[k].t0, &v, s0);) {}
        for (v = atomic_load(&g_gpu[k].t1); s1 > v && !atomic_compare_exchange_weak(&g_gpu[k].t1, &v, s1);) {}
    }
    atomic_fetch_sub(&g_gpu[k].pending, 1);
}

double mtl_gpu_ms(uint64_t f) {   // the union of the frame's command-buffer spans; -1 while one is still running or the frame is gone
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f || atomic_load(&g_gpu[k].pending)) return -1;
    if (!atomic_load(&g_gpu[k].ns)) return 0;
#ifdef DM_TEST_GIV_SUM   // negative control of t_core_frame only: the plain sum, as before 2026-10-19
    return atomic_load(&g_gpu[k].ns) / 1e6;
#endif
    unsigned j = f % GIVR; core_lock_take(&g_giv[j].lk);
    double ms = g_giv[j].frame == f ? giv_union_ns(g_giv[j].iv, &g_giv[j].n) / 1e6 : -1;
    core_lock_give(&g_giv[j].lk);
    return ms;
}
double mtl_gpu_sum_ms(uint64_t f) {   // the plain sum of the spans (what mtl_gpu_ms reported before 2026-10-19), for comparison with older numbers
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f || atomic_load(&g_gpu[k].pending)) return -1;
    return atomic_load(&g_gpu[k].ns) / 1e6;
}
int mtl_gpu_span(uint64_t f, uint64_t *t0, uint64_t *t1) {
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f || atomic_load(&g_gpu[k].pending)) return 0;
    *t0 = atomic_load(&g_gpu[k].t0); *t1 = atomic_load(&g_gpu[k].t1);
    return *t1 >= *t0 && *t1;
}
ub_state g_ub = {.n = -1, .N = 180, .cur = -1};
void mtl_ub_present(double iv) {
    if (!atomic_load(&g_ub.p_on) || iv == 0) return;
    if (iv < 0) { atomic_fetch_add(&g_ub.p_c[3], 1); return; }
    atomic_fetch_add(&g_ub.p_c[iv <= 20.8 ? 0 : iv <= 29.2 ? 1 : 2], 1); atomic_fetch_add(&g_ub.p_sum_us, (uint64_t)(iv * 1000));
}
// Registered runtime switches: name → address. Main-program symbols of an iOS release build are not in the dynamic symbol table and dlsym cannot find them (in the first real-device round all four categories were "global variable not found" and the whole round was wasted),
// so a switch to be measured on a real device must have a line added here; on Mac / in unit tests unregistered ones are still looked up by symbol name.
static int *ub_var(const char *nm) {
    extern int g_dm_present_link;
    static const struct { const char *n; int *p; } t[] = { {"g_dm_present_link", &g_dm_present_link} };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(nm, t[i].n)) return t[i].p;
    return dlsym(RTLD_DEFAULT, nm);
}
void ub_frame(uint64_t f) {
    if (g_ub.n < 0) { g_ub.n = 0; const char *e = getenv("DM_NVN_UB"), *n = getenv("DM_NVN_UB_N"); if (n && atoi(n) >= 60) g_ub.N = atoi(n);
        for (const char *q = e; q && *q && g_ub.n < 16; ) { const char *eq = strchr(q, '='), *end = strchr(q, ';'); if (!end) end = q + strlen(q); if (!eq || eq > end) break;
            int c = g_ub.n++; size_t l = (size_t)(eq - q); if (l > 15) l = 15; memcpy(g_ub.name[c], q, l);
            for (const char *k = eq + 1; k < end && g_ub.nk[c] < 16; ) { size_t kl = 0; while (k + kl < end && k[kl] != ',') kl++; size_t cl = kl > 47 ? 47 : kl; memcpy(g_ub.keys[c][g_ub.nk[c]++], k, cl); k += kl; if (k < end) k++; }
            q = *end ? end + 1 : end; }
        if (g_ub.n) MLOG("DM_NVN_UB: %d categories, %d frames per phase (alternating measurement in one process; phases that drop draws render incorrectly)", g_ub.n, g_ub.N); }
    if (!g_ub.n) return;
    uint64_t ph = f / (uint64_t)g_ub.N, r = f % (uint64_t)g_ub.N;
    if (r == 45) { for (int i = 0; i < 4; i++) atomic_store(&g_ub.p_c[i], 0); atomic_store(&g_ub.p_sum_us, 0); atomic_store(&g_ub.p_on, 1); }
    if (r != 0) return;
    if (f && atomic_load(&g_ub.p_on)) { atomic_store(&g_ub.p_on, 0); uint64_t pp = ph - 1; int pc = (pp & 1) ? (int)((pp / 2) % (uint64_t)g_ub.n) : -1;
        int c0 = atomic_load(&g_ub.p_c[0]), c1 = atomic_load(&g_ub.p_c[1]), c2 = atomic_load(&g_ub.p_c[2]), c3 = atomic_load(&g_ub.p_c[3]), n_ = c0 + c1 + c2;
        MLOG("UB present %llu %s frames %d avg %.3f ms 2-tick %d 3-tick %d 4+tick %d dropped %d", (unsigned long long)pp, pc < 0 ? "base" : g_ub.name[pc], n_, n_ ? atomic_load(&g_ub.p_sum_us) / 1000.0 / n_ : 0.0, c0, c1, c2, c3); }
    int nc = (ph & 1) ? (int)((ph / 2) % (uint64_t)g_ub.n) : -1;
    static int *sp[16]; static int sv[16], ns;   // Variables set by the previous phase: restore first
    for (int i = 0; i < ns; i++) *sp[i] = sv[i]; ns = 0;
    for (int i = 0; nc >= 0 && i < g_ub.nk[nc] && ns < 16; i++) { const char *k = g_ub.keys[nc][i]; if (strncmp(k, "@set:", 5)) continue;
        char nm[48]; const char *eq = strchr(k, '='); if (!eq) continue; size_t l = (size_t)(eq - (k + 5)); if (l >= sizeof nm) continue; memcpy(nm, k + 5, l); nm[l] = 0;
        int *a_ = ub_var(nm); static int bad, ok;
        if (!a_) { if (bad++ < 8) MLOG("[red] DM_NVN_UB: global variable not found: %s (on a real device it must first be registered in the ub_var table)", nm); continue; }
        sp[ns] = a_; sv[ns] = *a_; ns++; *a_ = atoi(eq + 1); if (ok++ < 8) MLOG("DM_NVN_UB: %s = %d (was %d)", nm, atoi(eq + 1), sv[ns - 1]); }
    atomic_store(&g_ub.cur, nc);
}
int ub_skip(const char *key) {
    int uc = atomic_load(&g_ub.cur); if (uc < 0) return 0;
    for (int i = 0; i < g_ub.nk[uc]; i++) { const char *k = g_ub.keys[uc][i]; if (k[0] == '@') continue; if (k[0] == '*' || !strncmp(key, k, strlen(k))) return 1; }
    return 0;
}
