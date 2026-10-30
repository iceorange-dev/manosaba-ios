// gfx/nvn/core/pend.h: buffer writes encoded but not executed on the GPU yet (plan item 264 moved them here from the Metal backend). Checked without
// a GPU by gfx/tests/t_core_pend.c; with the backend by t_pend.m and t_ub.m
// Target ranges of buffer writes (copies / inline uniform updates) encoded but not yet executed on the GPU, in this submission and in earlier ones
// still running: reading these addresses on the CPU side at encode time yields old values. Entries stay until their submission completes (pend_begin):
// the acquire sync no longer holds the CPU until the previous frame has run, and Tears of the Kingdom updates ring-buffer uniform blocks inline one
// submission before the draws that read storage buffer pointers out of them (MacNativeGames' pend_begin, 16384 entries; MIT)
// g_pend_data: an inline update's bytes (its copy in the update ring, alive until its submission completes); NULL for a GPU copy, whose source is
// g_pend_src (0: not known); g_pend_fence: the fence of the entry's submission; g_pend_over_fence: a submission whose writes did not fit
#pragma once
#include <stdint.h>
#include "core.h"

#define PEND_MAX 16384
typedef struct { uint64_t lo[PEND_MAX], hi[PEND_MAX], src[PEND_MAX], fence[PEND_MAX], over_fence; const uint8_t *data[PEND_MAX]; int n, over; } pend_state;
extern pend_state g_pend;
static inline int pend_hits(uint64_t a, uint64_t n) { if (g_pend.over) return 1; for (int i = 0; i < g_pend.n; i++) if (a < g_pend.hi[i] && a + n > g_pend.lo[i]) return 1; return 0; }   // A pending write reaches [a, a + n)
void pend_add(uint64_t dst, uint64_t n, const uint8_t *data, uint64_t src, uint64_t fence);   // A write of the submission that completes at fence: an inline update's bytes (data), or a GPU copy from src (0: not known)
void pend_begin(uint64_t done);   // Submission start: the writes of submissions that have completed are in memory now
// Bytes [a, a + n) as the GPU will see them at this point of the replay: guest memory with the pending writes recorded before entry `upto` applied in
// order: an inline update's bytes, or what a GPU copy reads from its source as of that copy (MacNativeGames' pend_read, MIT). 0 when that is not known
// (a copy without a source, the table overflowed, copies nested too deep)
int pend_read_x(uint64_t a, void *out, uint64_t n, int upto, int depth);
// The pending writes of submissions after done laid over out (= [a, a + n) as memory held it once done had completed), oldest first: 1, or 0 when
// one there has no bytes of its own (a GPU copy) or the table overflowed. A completed write is not laid: memory has it, or a newer CPU write
int pend_lay(uint64_t a, uint8_t *out, uint64_t n, uint64_t done);
