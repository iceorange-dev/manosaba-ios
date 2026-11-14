// gfx/nvn/core/frame.h: measurements by frame (plan item 264 moved them here from the Metal backend): boot instrumentation, each frame's GPU
// time as the union of its command buffers' spans, and the same-process alternating measurement (DM_NVN_UB). Checked without a GPU by
// gfx/tests/t_core_frame.c; with the backend by t_gputime.m and t_ub.m
#pragma once
#include <stdint.h>
#include <time.h>
#include "core.h"

// ---------- Boot instrumentation (DM_BOOTPROF=1): metallib loading, pipeline creation (both happen synchronously during replay on the submit thread), texture upload, first-frame present ----------
static inline uint64_t bp_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
typedef struct { uint64_t t0, lib_n, lib_ns, pso_n, pso_ns, up_bytes, first_present; } bp_state;   // t0: time of process start (when this module is loaded)
extern bp_state g_bp;
extern uint64_t g_up_total_ns;   // Cumulative upload time (same source as the per-frame @upload_ms)
int bootprof(void);   // DM_BOOTPROF (read once)

// GPU timing registration: gpu_tag_begin(frame) before commit, gpu_tag_done(frame, GPU start, GPU end) on completion (seconds on the CACurrentMediaTime base).
// Old-queue command buffers go through gpu_tag_f; the Metal 4 main submission backfills in the feedback of mtl_sub_commit (when switching to Metal 4 all the call sites were lost and the GPU column of the "timing" line became empty)
#define GIVR 32   // The frames whose spans are kept (the time is read 8 frames late)
void gpu_tag_begin(uint64_t f);
void gpu_tag_done(uint64_t f, double g0, double g1);
double mtl_gpu_ms(uint64_t f);       // The union of the frame's command-buffer spans; -1 while one is still running or the frame is gone
double mtl_gpu_sum_ms(uint64_t f);   // The plain sum of the spans (what mtl_gpu_ms reported before 2026-10-19), for comparison with older numbers
int mtl_gpu_span(uint64_t f, uint64_t *t0, uint64_t *t1);   // The frame's earliest GPU start and latest GPU end (0 while running or gone)
extern int g_gpu_span_host;          // The spans are on the host's clock (now_ns), so a GPU end can be set against a CPU time: Metal's are (mtl_init sets it); Vulkan's are on the GPU's own clock

// ---------- Same-process alternating measurement (DM_NVN_UB, off by default: without the environment variable the code below only adds one 'is it enabled' check) ----------
// On a hot real device the system keeps adjusting power / frequency, and segment comparison (segments A and B each run for a minute) cannot resolve differences of <10%; within the same run, toggle by phase in turn and take differences of adjacent phase pairs, so the drift cancels.
// DM_NVN_UB="name=item,item;name=item", one phase every DM_NVN_UB_N frames (default 180, minimum 60): even phases = baseline (nothing changed), odd phases take one category in turn. Each item of a category:
//   @set:<global variable name>=<value>   on entering the phase set this int global variable to the value, on leaving restore it (address found by symbol name; switches to be used on a real device must have a line added to the registration table below)
//   <fragment shader key prefix> or *   the phase drops all draws of these fragment shaders (the picture will be wrong; it only measures 'how much this kind of draw is worth'; a nearly idle category needs a round of its own)
// Readings: each phase discards its first 45 frames (transition + present lag); for the remaining frames, the mean of the actual present interval (same source as perf.txt) and the frame counts in the three buckets 16.7 / 25 / ≥33 ms, one log line "UB present".
typedef struct {
    int n, N; _Atomic int cur;            // Categories (-1: not read yet), frames per phase, the current phase's category (-1: baseline)
    char name[16][16], keys[16][16][48]; int nk[16];
    _Atomic int p_on, p_c[4]; _Atomic uint64_t p_sum_us;   // Present readings of the phase: on, counts by band (16.7 / 25 / >= 33 ms, dropped), interval sum
} ub_state;
extern ub_state g_ub;
void mtl_ub_present(double iv);   // Called once per frame by the present thread: actual present interval in ms (-1 = dropped, 0 = no reading)
void ub_frame(uint64_t f);        // At each frame (mtl_set_frame)
int ub_skip(const char *key);     // Whether this phase should drop draws of this fragment shader
