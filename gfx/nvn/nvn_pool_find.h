// Address → memory pool lookup (unit test tests/t_pool_find.c), after nxnative d6cd8a8 (seedds, MIT): the last hit first, then a binary search
// over pool indices sorted by start address, and the old full scan only when both miss (pools that overlap). Breath of the Wild keeps about
// 1,760 pools alive and looks one up for every buffer binding of every draw; nxnative measured its submit time going from 21 to 7 ms per frame.
// The pools are an array of entries that begin with uintptr_t lo, hi (stride = entry size). Adding or removing a pool must call
// pool_find_reset (the index is sorted again when next used). Not thread-safe: the caller holds the pool table's lock.
// The binary search reads a sorted copy of the start addresses (slo), not the pool entries through the index (two dependent loads per step), and before
// it a direct-mapped cache by address (hc: 16 KB granules → pool index + 1) answers the addresses of the few pools a draw binds in turn, which the
// single last hit kept missing (MacNativeGames keeps its pool index sorted in place for the same reason, nvn_pool_index.h, MIT). Every answer is
// checked to hold the range, so a stale cache entry only costs the search.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { POOL_FIND_HC = 1024 };
typedef struct { int *idx; uintptr_t *slo; int cap, sorted, last; uint64_t rescued, cached; int hc[POOL_FIND_HC]; } pool_find_t;   // rescued: lookups only the full scan answered (overlapping pools); cached: answered by hc

static inline uintptr_t pool_find_lo_(const void *pools, size_t stride, int i) { return ((const uintptr_t *)((const char *)pools + stride * (size_t)i))[0]; }
static inline uintptr_t pool_find_hi_(const void *pools, size_t stride, int i) { return ((const uintptr_t *)((const char *)pools + stride * (size_t)i))[1]; }
typedef struct { uintptr_t lo; int i; } pool_find_pair_;   // sorted with plain qsort (bionic has no qsort_r of Apple's form)
static inline int pool_find_pcmp_(const void *a, const void *b) {
    uintptr_t x = ((const pool_find_pair_ *)a)->lo, y = ((const pool_find_pair_ *)b)->lo; return x < y ? -1 : x > y;
}
static inline void pool_find_reset(pool_find_t *f) { f->sorted = 0; f->last = -1; memset(f->hc, 0, sizeof f->hc); }
static inline unsigned pool_find_hslot_(uint64_t addr) { return (unsigned)((addr >> 14) * 0x9E3779B1u) >> 22; }   // 10 bits: POOL_FIND_HC
static inline int pool_find_contains_(const void *pools, size_t stride, int i, uint64_t addr, uint64_t len) {
#ifndef DM_TEST_POOL_WRAP   // negative control of t_pool_find only: addr + len may wrap, as before
    if (len > UINT64_MAX - addr) return 0;   // a range past the end of the address space is in no pool (plan item 128; totk-mobile's case: it wrapped to a small end)
#endif
    return addr >= pool_find_lo_(pools, stride, i) && addr + len <= pool_find_hi_(pools, stride, i);
}
// The index of a pool holding [addr, addr + len), or -1
static inline int pool_find(pool_find_t *f, const void *pools, size_t stride, int n, uint64_t addr, uint64_t len) {
    if (f->last >= 0 && f->last < n && pool_find_contains_(pools, stride, f->last, addr, len)) return f->last;
    unsigned hs = pool_find_hslot_(addr); int hcv = f->hc[hs] - 1;
    if (hcv >= 0 && hcv < n && pool_find_contains_(pools, stride, hcv, addr, len)) { f->cached++; return f->last = hcv; }
    if (!f->sorted) {
        if (f->cap < n) {
            int *p = realloc(f->idx, (size_t)n * 2 * sizeof *p); if (!p) goto scan; f->idx = p;
            uintptr_t *q = realloc(f->slo, (size_t)n * 2 * sizeof *q); if (!q) goto scan; f->slo = q; f->cap = n * 2;
        }
        pool_find_pair_ *pp = malloc((size_t)(n ? n : 1) * sizeof *pp); if (!pp) goto scan;
        for (int i = 0; i < n; i++) pp[i] = (pool_find_pair_){pool_find_lo_(pools, stride, i), i};
        qsort(pp, (size_t)n, sizeof *pp, pool_find_pcmp_);
        for (int i = 0; i < n; i++) { f->idx[i] = pp[i].i; f->slo[i] = pp[i].lo; }
        free(pp);
        f->sorted = 1;
    }
    {   // the last pool whose start ≤ addr
        int lo = 0, hi = n - 1, hit = -1;
        while (lo <= hi) { int m = lo + (hi - lo) / 2; if (f->slo[m] <= addr) { hit = m; lo = m + 1; } else hi = m - 1; }
        if (hit >= 0 && pool_find_contains_(pools, stride, f->idx[hit], addr, len)) { f->hc[hs] = f->idx[hit] + 1; return f->last = f->idx[hit]; }
    }
scan:
    for (int i = 0; i < n; i++) if (pool_find_contains_(pools, stride, i, addr, len)) { f->rescued++; return f->last = i; }
    return -1;
}
