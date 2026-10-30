// gfx/nvn/core/pend.c: pending buffer writes, see pend.h
#include "pend.h"
#include <string.h>

pend_state g_pend;
void pend_add(uint64_t dst, uint64_t n, const uint8_t *data, uint64_t src, uint64_t fence) {
    if (g_pend.n < PEND_MAX) { g_pend.lo[g_pend.n] = dst; g_pend.hi[g_pend.n] = dst + n; g_pend.data[g_pend.n] = data; g_pend.src[g_pend.n] = src; g_pend.fence[g_pend.n] = fence; g_pend.n++; }
    else { g_pend.over = 1; g_pend.over_fence = fence; }
}
void pend_begin(uint64_t done) {   // submission start: the writes of submissions that have completed are in memory now
    int n = 0;
    for (int i = 0; i < g_pend.n; i++) if (g_pend.fence[i] > done) {
        g_pend.lo[n] = g_pend.lo[i]; g_pend.hi[n] = g_pend.hi[i]; g_pend.data[n] = g_pend.data[i]; g_pend.src[n] = g_pend.src[i]; g_pend.fence[n] = g_pend.fence[i]; n++; }
    g_pend.n = n; g_pend.over = g_pend.over_fence > done;
}
int pend_read_x(uint64_t a, void *out, uint64_t n, int upto, int depth) {
    memcpy(out, (const void *)(uintptr_t)a, n);
    if (g_pend.over) return 0;
    for (int i = 0; i < upto; i++) {
        if (a >= g_pend.hi[i] || a + n <= g_pend.lo[i]) continue;
        uint64_t lo = a > g_pend.lo[i] ? a : g_pend.lo[i], hi = a + n < g_pend.hi[i] ? a + n : g_pend.hi[i];
        if (g_pend.data[i]) memcpy((uint8_t *)out + (lo - a), g_pend.data[i] + (lo - g_pend.lo[i]), hi - lo);
#ifndef DM_TEST_PEND_COPY_NOW   // negative control of t_core_pend only: a copy reads its source as it is now (every pending write applied)
        else if (!g_pend.src[i] || depth >= 4 || !pend_read_x(g_pend.src[i] + (lo - g_pend.lo[i]), (uint8_t *)out + (lo - a), hi - lo, i, depth + 1)) return 0;
#else
        else if (!g_pend.src[i] || depth >= 4 || !pend_read_x(g_pend.src[i] + (lo - g_pend.lo[i]), (uint8_t *)out + (lo - a), hi - lo, g_pend.n, depth + 1)) return 0;
#endif
    }
    return 1;
}
int pend_lay(uint64_t a, uint8_t *out, uint64_t n, uint64_t done) {
    if (g_pend.over) return 0;
    for (int i = 0; i < g_pend.n; i++) {
#ifndef DM_TEST_PEND_LAY_DONE   // negative control of t_core_usnap only: completed writes laid too (over a newer CPU write)
        if (g_pend.fence[i] <= done) continue;
#endif
        if (a >= g_pend.hi[i] || a + n <= g_pend.lo[i]) continue;
        if (!g_pend.data[i]) return 0;
        uint64_t lo = a > g_pend.lo[i] ? a : g_pend.lo[i], hi = a + n < g_pend.hi[i] ? a + n : g_pend.hi[i];
        memcpy(out + (lo - a), g_pend.data[i] + (lo - g_pend.lo[i]), hi - lo);
    }
    return 1;
}
