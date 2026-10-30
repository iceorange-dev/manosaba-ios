// gfx/nvn/core/usnap.h: uniform snapshots, and the deferred binding records' handles read once a submission (plan item 264 moved them here from the
// Metal backend). Checked without a GPU by gfx/tests/t_core_usnap.c; with the backend by t_usnap.m, t_ussort.m, t_xlat.m and t_bindfreeze.m.
// Uniform snapshots (method from MacNativeGames' usnap, ly05010419/MacNativeGames nvn_mtl.m, MIT; written anew here):
// A submission is replayed inside nvnQueueSubmitCommands for 10-17 ms and its GPU work starts after that; on the console the call returns at once
// and the GPU reads the constants soon after. Tears of the Kingdom, having handed the frame over, rebuilds per-object constant pools on other threads
// (MacNativeGames found main+0x87a604, called from main+0x874f50): in the dive from the Great Sky Island up to 1041 bound uniform ranges of one
// submission changed while it was on the GPU (DM_NVN_GPUWATCH, byte compare; vertex stage slot 4, 256 bytes: the objects' transforms), and those
// frames showed rock walls in front of the camera, a flat ground or no terrain. So when a submission starts, every uniform range it binds is copied
// into its argument arena (ranges up to 256 bytes apart in one memory pool merged, at most 1 MiB each, each copy at the same offset modulo 256), and
// bindings and replay-time reads use the copy. A GPU write within the submission that reaches a copied range (inline update, buffer copy, texture to
// buffer copy, a writable storage buffer) drops that copy from then on: later readers see memory, where the GPU applies the writes in order. A range
// that a GPU write of an earlier, still running submission reaches is not copied (its result is not in memory yet), unless every such write is
// renamed uniform updates written (upd.h), whose bytes the pending-write table holds (pend.h): that range is copied and those bytes laid over it in
// order (plan item 301e3: in manosaba's debate the next submission's snapshot left out the ranges the last one's updates were written to, so each
// update there wrote the log to memory before its draw, ending the render pass 3-9 times a frame). DM_NVN_UBO_SNAPSHOT=0 reads live memory.
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../nvn_obj.h"
#include "core.h"

typedef struct { uint64_t lo, hi; uint8_t *cpu; uint64_t gpu; void *buf; int dropped, lay; } us_ent;   // buf: the arena block holding the copy; lay: pending writes laid over it
typedef struct { uint64_t lo, hi; } us_rng;
typedef struct { uint64_t lo, hi, fence; int known; } us_gw;   // GPU writes, with the fence of their submission; known: renamed updates written (their bytes in g_pend)
typedef struct {
    us_ent *ent; int n, cap;              // This submission's copies, sorted and disjoint
    us_rng *rng; int rn, rcap;            // The bound ranges the walk collected
    us_gw *gw; int gwn, gwcap;            // GPU writes of submissions that may not have completed
    int last;                             // The copy the last lookup found, tried first (plan item 225: consecutive draws bind the same blocks; ~13,000 lookups a frame)
    int on;                               // -1: not read yet (tests set it directly)
    int par; uint64_t par_n;              // DM_NVN_UBO_SNAPSHOT_PAR=0: one thread; par_n: snapshots copied in parts
    uint64_t subs, ranges, copies, bytes, drops, skip_gpu, ns, laid;   // laid: ranges copied with an earlier submission's pending writes laid over
    _Atomic uint64_t faults, jumps;       // jumps: sigsetjmp calls of the copies (t_usnap)
} us_state;
extern us_state g_us;

int us_on(void);   // DM_NVN_UBO_SNAPSHOT (read once)
// A deferred binding record (hle_nvn.c BindTexturesDeferred) reads its handles from where the game keeps them (nxnative), once a submission (plan item
// 61b, after bayonetta3-mobile nvn55_snapshot.h, MIT: frozen at submit): the first read stamps the record with the submission's generation
// (mtl_sub_begin), and the walks at the submission's start make that read (mtl_sub_index's, else mtl_sub_snapshot's, which walks for this alone
// when uniform snapshots are off), so the replay's walks (lookahead, deferral, the draw) all see the handles of the submit even if the game writes
// them meanwhile; the next submission reads them again. Before, every walk read them anew
extern int32_t g_bh_gen;   // records start at defer = 1: never a generation
extern int g_bh_seen;
void mtl_bh_deferred_seen(void);
static inline void bh_fresh(const p_bind_handles *b) {
#ifndef DM_TEST_BH_EACH_READ   // negative control of t_bindfreeze only: read at every walk, as before
    if (b->defer == g_bh_gen) return;
#endif
    if (b->defer && b->src && b->n > 0 && b->n <= 32) { memcpy((void *)b->h, (const void *)(uintptr_t)b->src, 8 * (size_t)b->n); ((p_bind_handles *)b)->defer = g_bh_gen; }
}
void us_collect(const uint8_t *p, int depth, int us);   // A walk of a command list: deferred handles read (bh_fresh); with us, the uniform ranges bound into g_us.rng
int us_rng_cmp(const void *x, const void *y);           // By start
// The bindings by start: an LSD radix sort, 11 bits a pass, all six digit histograms from one read, and a pass whose digit is the same for every range
// skipped (the addresses share their high bits). qsort with a comparison call was 0.14-0.43 ms of the 0.3-0.9 ms a snapshot took in Tears of the
// Kingdom (4,000-11,000 bindings a submission). The scratch is the caller's (the capture of plan item 61 sorts on the game's thread)
void us_rng_sort_x(us_rng *a, int n, us_rng **tmp, int *tcap, uint32_t cnt[6][2048]);
void us_rng_sort(us_rng *a, int n);
int us_merge(us_rng *u, int n);   // Sorted ranges merged in place, the result disjoint: up to 256 bytes apart in one memory pool, at most 1 MiB; returns how many
// GPU writes of submissions after fence keep, sorted by start (the rest forgotten); returns how many, *gw_max the longest
int us_gw_keep(uint64_t keep, uint64_t *gw_max);
int us_gw_reaches(uint64_t lo, uint64_t hi, int ng, uint64_t gw_max);   // Of those ng writes, none reaches [lo, hi) (0), only known ones (1), another (2)
void us_reserve(int n);                    // Room for n copies in g_us.ent
us_ent *us_add(uint64_t lo, uint64_t hi);   // The next copy, its place in the arena at the same offset modulo 256 as the original (the bytes not copied yet)
void us_copy_all(void);                     // Fill every copy in g_us.ent, under the fault guard; a copy whose source faults is dropped
void us_snapshot(const uint64_t *h, int n); // Submission start (mtl_sub_snapshot without a capture): collect, merge, copy
void us_cap_copy(int nr, const us_rng *r, uint64_t *off, uint8_t *data);   // The capture's copies of r[i] to data + off[i] under the fault guard; one that faults gets off UINT64_MAX
static inline const us_ent *us_find(uint64_t a, uint64_t n) {   // The live copy holding all of [a, a + n), or NULL
    if (!g_us.n || !a) return NULL;
    if (!n) n = 1;
    if (g_us.last < g_us.n) { const us_ent *c = &g_us.ent[g_us.last];   // the copies are disjoint: one holding the range is the only one
#ifndef DM_TEST_US_LAST_UNCHECKED   // negative control of t_usnap only: the last copy found taken whatever the range
        if (c->lo <= a && a + n <= c->hi) return c->dropped ? NULL : c;
#else
        return c->dropped ? NULL : c;
#endif
    }
    int lo = 0, hi = g_us.n;
    while (lo < hi) { int m = (lo + hi) / 2; if (g_us.ent[m].hi <= a) lo = m + 1; else hi = m; }
    if (lo < g_us.n && g_us.ent[lo].lo <= a && a + n <= g_us.ent[lo].hi && !g_us.ent[lo].dropped) { g_us.last = lo; return &g_us.ent[lo]; }
    return NULL;
}
// A GPU write of the submission that completes at fence reaches [a, a + n): the next submissions do not copy it until it completes, unless known (the
// write is renamed updates whose bytes are in g_pend: they are laid over the copy)
void us_gw_note(uint64_t a, uint64_t n, uint64_t fence, int known);
int us_drop_range(uint64_t a, uint64_t n, uint64_t fence); // ... and its copies are dropped from then on (later readers see memory); 1 when one was
void us_report(uint64_t f);   // Every 300 frames

// The backend provides:
int nvn_be_pool_span(uint64_t lo, uint64_t n);                          // 1 when [lo, lo + n) lies in one memory pool
void nvn_be_arena(size_t n, uint8_t **cpu, uint64_t *gpu, void **buf);   // n bytes of this submission's argument arena (kept until it completes): CPU and GPU address, the block
void nvn_be_parallel(int n, void (*fn)(void *ctx, size_t i), void *ctx); // fn(ctx, 0 .. n - 1) on the system's worker threads, returning when all are done
uint64_t nvn_be_done_fence(void);                                       // The highest completed submission fence
