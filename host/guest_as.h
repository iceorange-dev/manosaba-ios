// Where the guest address space goes (plan item 62a, after MacNativeGames games/totk/runtime/services.c reserve_in_gap, MIT, and T5eng's
// nxnative_totk, written anew): in the lowest free gap of the process's map from 4 GiB that holds it, 2 MiB aligned and ending at or below 2^39
// (the Switch's 39-bit address space: a guest pointer above it is not one the game expects); the size steps down from what is wanted by 256 MiB to
// the least the game can run with. On iOS the free space depends on the device and on what UIKit and Metal mapped first, so the host reserves at
// the top of main() (ios/Sources/main.m), and logs the free gaps when it gets less than it wanted.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "plat.h"

#define AS_LIMIT (1ull << 39)
#define AS_FROM (1ull << 32)
typedef struct { uint64_t lo, hi; } as_gap;

// The lowest gap (gaps sorted by lo) holding the largest size from want down to least in steps of step: 1 and *at / *size, else 0
static inline int as_pick(const as_gap *g, int n, uint64_t want, uint64_t least, uint64_t step, uint64_t align, uint64_t *at, uint64_t *size) {
    for (uint64_t s = want; s >= least && s > 0; s = s > step ? s - step : 0) {
        for (int i = 0; i < n; i++) {
            uint64_t lo = g[i].lo > AS_FROM ? g[i].lo : AS_FROM, hi = g[i].hi;
#ifndef DM_TEST_AS_NO_LIMIT   // negative control of host/tests t_guest_as only: the end not held to 2^39
            if (hi > AS_LIMIT) hi = AS_LIMIT;
#endif
            lo = (lo + align - 1) & ~(align - 1);
            if (hi > lo && hi - lo >= s) { *at = lo; *size = s; return 1; }
        }
        if (s == least) break;
    }
    return 0;
}
// The free gaps of this process's map from `from` (sorted), the last up to `end` (the map does not tell where the address space ends); count
static inline int as_gaps(as_gap *out, int max, uint64_t from, uint64_t end) {
    int n = 0; uint64_t prev = from;
    while (n < max) {
        uint64_t r = prev, rs = 0; int readable;
        int more = dm_mem_region(&r, &rs, &readable);
        uint64_t hi = more ? (uint64_t)r : end;
        if (hi > prev) out[n++] = (as_gap){prev, hi};
        if (!more || r + rs >= end) break;
        prev = r + rs;
    }
    return n;
}
// Reserve [at, at + size) where nothing is mapped (fails instead of replacing a mapping): the address or NULL
static inline void *as_reserve_at(uint64_t at, uint64_t size) { return dm_reserve_at(at, size); }
// The gaps of at least 256 MiB, one line each, for a reservation that got less than it wanted
static inline void as_log_gaps(FILE *f, const as_gap *g, int n) {
    for (int i = 0; i < n; i++) if (g[i].hi - g[i].lo >= (256ull << 20))
        fprintf(f, "[dm] guest address space: free %#llx..%#llx (%llu MB)%s\n", (unsigned long long)g[i].lo, (unsigned long long)g[i].hi,
                (unsigned long long)((g[i].hi - g[i].lo) >> 20), g[i].hi > AS_LIMIT ? ", past 2^39" : "");
}
