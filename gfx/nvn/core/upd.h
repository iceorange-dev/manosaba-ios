// gfx/nvn/core/upd.h: inline uniform updates kept in a log instead of ending the render pass, and the private copies of the ranges they reach (plan
// item 264 moved them here from the Metal backend; writing them to memory stays the backend's, mtl/uniform_rename.inc upd_flush). Checked without a
// GPU by gfx/tests/t_core_upd.c; with the backend by t_ubren.m and t_nvn
// Inline uniform updates without ending the render pass (renaming; MacNativeGames' upd_log / upd_shadow, MIT; written anew here):
// Tears of the Kingdom updates small uniform ranges between draws about 200 times a frame (nvnCommandBufferUpdateUniformBuffer). Each was a GPU copy,
// and a copy ends the render pass: a copy encoder in between and the pass reopened with every attachment loaded again (MacNativeGames counted ~80 copy
// encoders and ~23 split passes a frame). Now an update keeps its bytes in the update ring and is logged (upd_log). A draw binding a uniform range that
// logged updates overlap gets a private copy of that range (upd_shadow): its contents as of the submit call (the snapshot) with the logged updates
// applied in order, so draws before an update read the old bytes and draws after it the new ones. The logged updates reach memory, merged, at the end
// of the submission (mtl_sub_commit), before any copy or dispatch (blit(), dispatch) and before a draw that reads an updated range other than as a
// uniform buffer, or binds one without a snapshot (upd_guard_draw, before its render pass is opened). DM_NVN_UB_RENAME=0: a GPU copy per update
#pragma once
#include <stdint.h>
#include "core.h"

typedef struct { uint64_t lo, hi, seq; const uint8_t *data; } upd_ent;
// A log kept sorted by start, with the largest end up to each entry: overlaps are found by a binary search and a walk back over real overlaps (every
// uniform binding of a draw is looked up, and most overlap nothing). g_upd: updates not in memory yet (written by upd_flush); g_uh: every update of
// the submission, written or not (a private copy is the snapshot with all of them: the snapshot stays valid after a write, which only adds them)
typedef struct { upd_ent *e; uint64_t *maxhi; int n, cap; uint64_t lo, hi, ver; } upd_list;   // ver: changed by every add and clear (ul_seq's cache)
extern upd_list g_upd, g_uh;
extern int g_nupd;   // Updates not in memory yet (g_upd.n)
typedef struct {
    uint64_t updates, shadows, reuse, flushes, guard, noshot, unknown, incr;   // incr: private copies made from the range's previous copy of the submission and the updates after it (plan item 225)
    uint64_t g[5];        // What made upd_guard_draw write the updates: index, vertex, storage buffer, buffer texture, uniform range without a snapshot
    uint64_t pass;        // The render pass of the latest update logged while one was open
    uint64_t ul_calls, ul_walk, ul_applied, ul_moves;   // ul_apply: calls past the range check, log entries walked, entries applied, sort moves
    uint64_t copied, copy_max;   // Bytes copied into private copies (each the whole bound range), the largest range copied (plan item 271)
} upd_stats;
extern upd_stats g_ren;
int ub_rename_on(void);   // DM_NVN_UB_RENAME (read once)
void ul_add(upd_list *l, upd_ent v);
void ul_clear(upd_list *l);
uint64_t ul_seq(const upd_list *l, uint64_t lo, uint64_t hi);   // The newest entry overlapping [lo, hi), 0 if none
void ul_apply(const upd_list *l, uint64_t lo, uint8_t *out, uint64_t n, uint64_t after);   // The entries newer than after over out (= [lo, lo + n) before them), oldest first
const int *ul_by_seq(const upd_list *l, int i, int j);   // Entries i .. j - 1 of l, oldest first (indices)
void upd_add(uint64_t lo, uint64_t hi, const uint8_t *data);   // Log an update (its bytes kept by the caller until the submission completes)
void upd_sub_end(void);   // mtl_sub_commit, after upd_flush
int upd_any(void);        // The submission logged inline uniform updates (written to memory or not): a snapshot alone predates them
uint64_t upd_overlap_seq(uint64_t lo, uint64_t hi);   // Updates not in memory yet
void upd_apply(uint64_t lo, uint8_t *out, uint64_t n);  // Over a snapshot: every update of the submission
uint64_t upd_shadow_at(uint64_t lo, uint64_t size, void **buf, uint64_t sub);   // GPU address of [lo, lo + size) with submission sub's updates, 0 to read as before (none overlaps, or memory holds them)
void ren_report(uint64_t f);   // Every 300 frames
