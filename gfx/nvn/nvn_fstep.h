// A frame step in whole vsyncs decided by the host (plan item 133; the design and its numbers are nxnative's nvn_fstep.h, seedds, MIT, written
// anew here). For a game whose step the host sets (Tears of the Kingdom with the realtime option: games/totk/frame_step.c) the host keeps each
// frame on screen k vsyncs (1/60 s) and hands the game k as its step, so the step is the time the frame is shown and motion is even; real time
// the game falls behind (a late frame, a hitch) is a debt paid back one vsync a frame (the step k + 1 while the frame still shows k), so game
// time follows real time. k follows the frames' cost: three late frames in a row step up to the cost; 30 frames in a row whose cost fits
// 0.85 × (k − 1) try k − 1 for 30 frames, and a trial that fails doubles the wait before the next (1 s up to 8 s), so a device on the edge does
// not flap. kmin: 1 at 60 fps, 2 while a movie is held at 30 (item 130); kmax: 3, the largest interval the game's own logic knows.
// With opt (plan item 222, after 216): an iPhone lowers its GPU clock while frames are held at 2 vsyncs, so the cost measured there never fits
// k − 1 though the frames would take far less at 60 (Great Sky Island: 20.65 ms of GPU at 30 fps, 11.09 ms at 60). After FSTEP_OPT0 vsyncs at
// k > kmin a trial of k − 1 starts whatever the cost, and its first FSTEP_GRACE frames do not count as late while the clock comes up; a failed
// one doubles that wait (up to FSTEP_OPT_MAX), one that holds sets it back.
// Plan item 321 (S2Arm's measurements of Tears of the Kingdom on an iPhone, written anew): steady at kmin (no trial running) the step up waits for
// more than FSTEP_WIN_LATE of the last FSTEP_WIN frames shown late (three in a row stepped up on scattered spikes, 0.6-9.5 % late with 3-5 in a
// row, after which the GPU clock fell at the longer hold and kmin never fitted again; a sustained overload still steps up on its 25th late frame,
// about 0.4 s); the window counts frames shown late only (a cost over the hold whose frame went out on time is no hitch the player sees); trials
// keep the three in a row. A probe is a trial back to kmin the cost does not justify, started after the hold-off while the caller reports the
// frames slowed for reasons other than the scene (slow: the CPU parked on the efficiency cores, or the GPU clock lowered at the longer hold,
// nvn_fstep_gpu_slowed): its first FSTEP_GRACE frames do not count, and the run at kmin it starts must last FSTEP_HOLD_MAX before the hold-off
// returns to 1 s (a shorter one fails the probe: a CPU that cannot hold kmin is not probed every second). The cost itself is the frame's critical
// path where one is known (nvn_fstep_cost, nvn_fpath, at the end of this file).
// Unit test gfx/tests/t_fstep.c. Pure: no clock, no I/O.
#pragma once
#include <math.h>
#include <stdint.h>

enum { FSTEP_MAX = 3, FSTEP_RUN = 30 };
#define FSTEP_LATE 0.1        // late: shown or cost over k by more than a tenth of a vsync (1.7 ms)
#define FSTEP_FIT 0.85        // fits k − 1: cost at most this share of it
#define FSTEP_DEBT_MAX 15.0   // the debt kept at most (0.25 s): the rest of a long stall (a load) is dropped, not replayed
#define FSTEP_HOLD0 60.0      // vsyncs after a step up before a trial (1 s), doubled after each failed trial up to FSTEP_HOLD_MAX
#define FSTEP_HOLD_MAX 480.0
#define FSTEP_OPT0 300.0      // with opt: vsyncs at k > kmin before a trial whatever the cost (5 s), doubled after each failed one
#define FSTEP_OPT_MAX 1920.0  // up to 32 s
enum { FSTEP_GRACE = 60 };    // with opt: frames of a trial that do not count as late (1 s at 60 fps)
enum { FSTEP_WIN = 120, FSTEP_WIN_LATE = 24, FSTEP_PGRACE = 30 };   // plan item 321: the window at kmin and its late frames allowed; a probe's grace frames

typedef struct {
    int k, step, kmin;          // vsyncs a frame stays on screen; the step for the next frame; the last call's kmin
    double debt;                // real time the game is behind, in vsyncs (at most one vsync ahead is remembered)
    int late_run, fit_run, trial;
    double hold, since;         // the wait before a trial; vsyncs since the last step up
    int opt, grace, otrial;     // opt: trials whatever the cost (the caller sets it); frames of the trial still in its grace period; such a trial runs
    double opt_wait;            // with opt: the wait before such a trial
    int slow;                   // set by the caller before each call: the frames slowed for reasons other than the scene (plan item 321: probes)
    uint8_t win[FSTEP_WIN]; int wi, wn, wlate;   // steady at kmin: the last frames' lateness shown (a ring), how many filled, how many late
    int probe;                  // 1: the trial running is a probe; 2: a probe held and its run at kmin has not been judged yet
    int no321;                  // set by the caller: plan item 321's rules off (the window at kmin, the probes), for an A/B (DM_FSTEP_RULES=0)
    uint64_t frames, late, paid, ups, downs, failed, probes; double dropped;   // for the log
} nvn_fstep;

// One frame ended: shown = real time since the previous frame's start, cost = its work (the larger of CPU and GPU), both in vsyncs. Returns the
// step for the next frame; s->k is how long that frame stays on screen
static inline int nvn_fstep_next(nvn_fstep *s, double shown, double cost, int kmin, int kmax) {
    if (kmin < 1) kmin = 1;
    if (kmax > FSTEP_MAX) kmax = FSTEP_MAX;
    if (kmax < kmin) kmax = kmin;
    if (s->hold <= 0) s->hold = FSTEP_HOLD0;
    if (s->opt_wait <= 0) s->opt_wait = FSTEP_OPT0;
    s->frames++;
    if (!s->k) { s->k = s->step = s->kmin = kmin; return s->step; }   // the first frame: nothing measured yet
    int held = s->k;
#ifndef DM_TEST_FSTEP_NO_DEBT   // negative control of t_fstep only: real time lost is never paid back
    s->debt += shown - s->step;
#endif
    if (s->debt > FSTEP_DEBT_MAX) { s->dropped += s->debt - FSTEP_DEBT_MAX; s->debt = FSTEP_DEBT_MAX; }
    if (s->debt < -1) s->debt = -1;
    s->since += shown;
    if (kmin < s->kmin || s->k < kmin) s->k = kmin;   // a hold ended (the lower kmin takes effect at once) or began
    if (s->k > kmax) s->k = kmax;
    s->kmin = kmin;
    int slate = shown > held + FSTEP_LATE, late = slate || cost > held + FSTEP_LATE;   // slate: shown late (what the player sees)
#ifndef DM_TEST_FSTEP_NO_GRACE   // negative control of t_fstep only: a trial's first frames count as late as any
    if (s->grace > 0) { s->grace--; late = 0; }   // the GPU clock still coming up (opt)
#endif
    if (late) { s->late++; s->late_run++; s->fit_run = 0; } else s->late_run = 0;
    // Steady at kmin (plan item 321): the window of the last FSTEP_WIN frames shown late decides; elsewhere (a trial or a probe running, a longer hold) three in a row
    int steady = !s->no321 && held == kmin && s->k == kmin && !s->trial;
#ifdef DM_TEST_FSTEP_RUN3   // negative control of t_fstep only: three late frames in a row step up from kmin as before
    steady = 0;
#endif
    if (!steady) s->wi = s->wn = s->wlate = 0;
    else { if (s->wn == FSTEP_WIN) s->wlate -= s->win[s->wi]; else s->wn++; s->win[s->wi] = (uint8_t)slate; s->wlate += slate; s->wi = (s->wi + 1) % FSTEP_WIN; }
    if ((steady ? s->wlate > FSTEP_WIN_LATE : s->late_run >= 3) && s->k < kmax) {   // step up to the cost, at least one vsync
        int want = (int)ceil(cost - 0.05);
        if (want < s->k + 1) want = s->k + 1;
        if (want > kmax) want = kmax;
        if (s->trial || (s->probe == 2 && s->since < FSTEP_HOLD_MAX)) { s->failed++; s->trial = 0; s->hold = s->hold * 2 < FSTEP_HOLD_MAX ? s->hold * 2 : FSTEP_HOLD_MAX;
            if (s->otrial) s->opt_wait = s->opt_wait * 2 < FSTEP_OPT_MAX ? s->opt_wait * 2 : FSTEP_OPT_MAX; }
        else if (s->probe == 2) s->hold = FSTEP_HOLD0;   // the probe's run at kmin lasted (321)
        s->k = want; s->ups++; s->late_run = s->fit_run = 0; s->since = 0; s->otrial = s->grace = 0; s->probe = 0; s->wi = s->wn = s->wlate = 0;
    } else if (s->trial && !--s->trial) {   // the trial held (a probe's run at kmin is judged at the next step up: 321)
        if (s->probe) { s->probe = 2; s->since = 0; } else s->hold = FSTEP_HOLD0;
        if (s->otrial) s->opt_wait = FSTEP_OPT0; s->otrial = 0; }
#ifndef DM_TEST_FSTEP_NO_MARGIN   // negative control of t_fstep only: any cost below k − 1 counts as fitting
    double fit = FSTEP_FIT;
#else
    double fit = 1.0;
#endif
    if (!late) s->fit_run = s->k > kmin && cost <= fit * (s->k - 1) ? s->fit_run + 1 : 0;
    if (s->fit_run >= FSTEP_RUN && s->since >= s->hold && !s->trial) { s->k--; s->downs++; s->trial = FSTEP_RUN; s->fit_run = 0; }
    else if (s->opt && !s->trial && s->k > kmin && s->since >= s->opt_wait) {   // a trial whatever the cost, with a grace period (opt)
        s->k--; s->downs++; s->trial = FSTEP_RUN + FSTEP_GRACE; s->grace = FSTEP_GRACE; s->otrial = 1; s->fit_run = 0; }
#ifndef DM_TEST_FSTEP_NO_PROBE   // negative control of t_fstep only: no probe however slowed the frames are
    else if (s->slow && !s->no321 && !s->trial && s->k > kmin && s->since >= s->hold) {   // a probe (321): straight to kmin, so the performance controller sees the whole deadline
        s->k = kmin; s->downs++; s->probes++; s->trial = FSTEP_RUN + FSTEP_PGRACE; s->grace = FSTEP_PGRACE; s->probe = 1; s->fit_run = s->late_run = 0; }
#endif
    s->step = s->k + (s->debt >= 1 && s->k < FSTEP_MAX);
    s->paid += s->step > s->k;
    return s->step;
}

// The GPU slowed at the longer hold (plan item 321; S2Arm: an iPhone lowers its GPU clock to the hold, Breath of the Wild's 14.4 ms a frame became
// about 25 at hold 2 with the same draws): its time at a hold above kmin is at least FSTEP_GPU_SLOW times its time at kmin and alone does not fit
// k − 1. ref: the GPU time at kmin (a running mean; not during a probe's trial, whose frames run while the clock rises); cur: at the current hold
#define FSTEP_GPU_SLOW 1.3
enum { FSTEP_GPU_N = 30 };    // frames at the longer hold before the comparison counts
typedef struct { double ref, cur; int n, k; } nvn_fgpu;
static inline int nvn_fgpu_slow(const nvn_fgpu *g) { return g->n >= FSTEP_GPU_N && g->ref > 0 && g->cur >= FSTEP_GPU_SLOW * g->ref && g->cur > FSTEP_FIT * (g->k - 1); }
// Once a frame with its GPU time (vsyncs; 0 or less: none finished), the hold it ran at and the probe state (nvn_fstep.probe)
static inline int nvn_fstep_gpu_slowed(nvn_fgpu *g, double gpu, int k, int kmin, int probe) {
    if (gpu <= 0) return nvn_fgpu_slow(g);
    if (k <= kmin) { if (probe != 1) g->ref = g->ref > 0 ? g->ref + (gpu - g->ref) / 32 : gpu; g->n = 0; g->k = k; return 0; }
    if (k != g->k) { g->k = k; g->n = 0; }
    g->cur = g->n ? g->cur + (gpu - g->cur) / 8 : gpu; g->n++;
    return nvn_fgpu_slow(g);
}

// The cost handed to nvn_fstep_next, in vsyncs (plan item 321; S2Arm's measurement of Tears of the Kingdom on an iPhone, written anew). cpu: the
// time from the frame's start (its first acquire) to its present call; gpu: its GPU time; path: its critical path, from where its submits began
// to the end of its GPU work (0: not known). The submitting thread's waits on the GPU are in cpu and not in the path: S2Arm at a hold of 3 read
// 35 ms of CPU where the path was 27 ms, which fits a hold of 2, so a failed probe stepped up to 20 fps and 30 never fitted again. With a path
// the cost is the path, and never under the GPU's time. Tears of the Kingdom here submits before it acquires its window texture, so its cpu is
// 0.03 vsyncs on a Mac and the cost was the GPU's time alone (0.46-0.55 on Great Sky Island, the path 0.61-0.70). Neither measure holds the game's own
// work between its frames: a scene bound by that looks as if it fitted k − 1 (plan item 327)
static inline double nvn_fstep_cost(double cpu, double gpu, double path) {
#ifndef DM_TEST_FSTEP_COST_CPU   // negative control of t_fstep only: max(CPU, GPU), as before
    if (path > 0) return path > gpu ? path : gpu;
#endif
    return cpu > gpu ? cpu : gpu;
}
// Where each of the last FSTEP_PATH_N frames' submits began (ns), kept until the frame's GPU work has ended
enum { FSTEP_PATH_N = 16 };
typedef struct { uint64_t f[FSTEP_PATH_N], t[FSTEP_PATH_N]; } nvn_fpath;   // f: the frame's number + 1 (0: empty); t: its first submit
static inline void nvn_fpath_submit(nvn_fpath *p, uint64_t frame, uint64_t now) {   // at every submit: the frame's first one is where they began
    unsigned k = (unsigned)(frame % FSTEP_PATH_N);
#ifndef DM_TEST_FPATH_LAST_SUBMIT   // negative control of t_fstep only: every submit moves the start
    if (p->f[k] == frame + 1) return;
#endif
    p->f[k] = frame + 1; p->t[k] = now;
}
// The critical path of a frame whose GPU work ended at gpu_end (ns, on the clock of nvn_fpath_submit), in vsyncs of vs ns; 0: the frame is not
// kept (more than FSTEP_PATH_N frames ago, or it made no submit) or the end is not after its submits' start
static inline double nvn_fpath_vsyncs(const nvn_fpath *p, uint64_t frame, uint64_t gpu_end, double vs) {
    unsigned k = (unsigned)(frame % FSTEP_PATH_N);
    return p->f[k] == frame + 1 && gpu_end > p->t[k] ? (double)(gpu_end - p->t[k]) / vs : 0;
}
