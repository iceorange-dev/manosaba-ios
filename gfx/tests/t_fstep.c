// The frame step in whole vsyncs (gfx/nvn/nvn_fstep.h, plan item 133; scenarios after nxnative's t_fstep, MIT, numbers our own model's). A
// frame is shown k vsyncs, or up to the next whole vsync after its cost when it took longer (a missed vsync); the game steps by the step handed
// at the end of the previous frame. Steady 10 ms frames: 60 fps, game time = real time; steady 22 ms: 30 fps at once and for good; a 100 ms
// hitch: paid back within a few frames; a 0.5 s stall: 15 vsyncs paid back, the rest dropped; heavy then light: back to 60 fps after a trial;
// a movie hold (kmin 2): 30 fps during, 60 the frame after; costs jittering just under a vsync at 30 fps: no flapping. Real time lost never paid
// back (-DDM_TEST_FSTEP_NO_DEBT): 3 fail; no margin on "fits k - 1" (-DDM_TEST_FSTEP_NO_MARGIN): 1 fail.
// With opt (plan item 222) against a GPU whose clock follows the frame rate (as t_fptrial's: 0.45 to 1 of the top, heading for the clock that fills
// 3/4 of the interval, falling over 1 s, rising over 0.5 s): a light scene (11 ms of work at the top clock) held at 30 fps stays there without opt
// and reaches 60 for good with it; a heavy one (22 ms) stays at 30 with its trials failing at growing waits. Without the grace period
// (-DDM_TEST_FSTEP_NO_GRACE) the light scene's trials fail while the clock is low: 2 fail.
// Plan item 321: steady 22 ms frames step up on their 25th late frame (the window: more than 24 of 120 shown late), game time then within 2
// vsyncs of real time less what was dropped; bursts of three late frames every 30 (10 %) at 60 fps stay at 60; a scene at 30 fps whose cost
// (1.05 vsyncs) never fits 60 is probed back while the caller reports the frames slowed and stays at 60 when they then fit, not without the
// report; the GPU tracker reports slowed when the time at hold 2 is 1.5 times that at 60 and over 0.85 of a vsync, not at 1.2 times. Three in a
// row as before (-DDM_TEST_FSTEP_RUN3): 2 fail (the bursts, the 25th frame); no probe (-DDM_TEST_FSTEP_NO_PROBE): 1 fails. The window took the
// flapping the jitter case showed without the margin away, so a scene whose frames take 0.9 vsyncs at 30 fps and 1.15 at 60 (they fit 1 without
// the margin, then every frame is late) checks it: no trial in 2,000 frames (without the margin, -DDM_TEST_FSTEP_NO_MARGIN: trials, 1 fails).
#include "../nvn/nvn_fstep.h"
#include <stdio.h>
#include <string.h>
static int n, fail;
static void check(const char *what, int ok, const char *fmt, double a, double b) {
    n++; fail += !ok; printf("%-78s ", what); printf(fmt, a, b); printf(" %s\n", ok ? "✓" : "✗");
}
typedef struct { double game, real; int changes, k_end; } sim;
// cost(i) and kmin(i) per frame
static sim run(nvn_fstep *s, int frames, double (*cost)(int), int (*kmin)(int), int *ks) {
    sim r = {0}; int step = kmin(0), prevk = -1;
    memset(s, 0, sizeof *s); nvn_fstep_next(s, 0, 0, kmin(0), FSTEP_MAX);
    for (int i = 0; i < frames; i++) {
        int k = s->k; double c = cost(i);
        double shown = c > k ? ceil(c - 1e-9) : k;
        r.game += step; r.real += shown;
        step = nvn_fstep_next(s, shown, c, kmin(i + 1), FSTEP_MAX);
        if (ks) ks[i] = s->k;
        if (prevk >= 0 && s->k != prevk) r.changes++;
        prevk = s->k;
    }
    r.k_end = s->k; return r;
}
static int km1(int i) { (void)i; return 1; }
static double c10ms(int i) { (void)i; return 0.6; }
static double c22ms(int i) { (void)i; return 1.32; }
static double hitch(int i) { return i == 100 ? 5.5 : 0.6; }
static double stall(int i) { return i == 100 ? 30 : 0.6; }
static double heavy_light(int i) { return i < 200 ? 1.5 : 0.5; }
static int movie(int i) { return i < 100 ? 2 : 1; }
static double light(int i) { (void)i; return 0.5; }
static double jitter(int i) { return i < 10 ? 1.5 : (i % 43) >= 40 ? 1.2 : 0.92; }   // 30 fps, then 0.92 vsync with three 1.2 in a row every 43 frames
static double bursts(int i) { return i % 30 >= 27 ? 1.3 : 0.6; }   // 60 fps with three 22 ms frames every 30 (321)
// A scene of w vsyncs of GPU work at the top clock and c of CPU, started at k = 2 with the clock low; returns the share of time at k = 1 in the
// last half and k at the end
typedef struct { double at1_late_half; int k_end; uint64_t downs, failed; } dsim;
static dsim run_dvfs(nvn_fstep *s, int opt, double w, double c, double seconds) {
    memset(s, 0, sizeof *s); s->opt = opt; nvn_fstep_next(s, 0, 0, 1, FSTEP_MAX); s->k = 2;
    double clock = 0.45, t = 0, at1 = 0, half = seconds / 2; dsim r; memset(&r, 0, sizeof r);
    while (t < seconds) {
        int k = s->k; double gpu = w / clock, cost = gpu > c ? gpu : c;
        double shown = cost > k ? ceil(cost - 1e-9) : k;
        double want = w / (0.75 * k); if (want < 0.45) want = 0.45; if (want > 1) want = 1;
        if (want > clock) { clock += 0.55 * shown / 60 / 0.5; if (clock > want) clock = want; }
        else { clock -= 0.55 * shown / 60 / 1.0; if (clock < want) clock = want; }
        t += shown / 60; if (t > half && k == 1) at1 += shown / 60;
        nvn_fstep_next(s, shown, cost, 1, FSTEP_MAX);
    }
    r.at1_late_half = at1 / (seconds - half); r.k_end = s->k; r.downs = s->downs; r.failed = s->failed; return r;
}
int main(void) {
    nvn_fstep s; sim r; static int ks[2000];
    r = run(&s, 600, c10ms, km1, NULL);
    check("10 ms frames: k = 1 (60 fps) throughout", r.k_end == 1 && r.changes == 0, "k %.0f, changes %.0f", r.k_end, r.changes);
    check("... game time = real time", r.game == r.real, "game %.0f real %.0f vsyncs", r.game, r.real);
    r = run(&s, 600, c22ms, km1, ks);
    check("22 ms frames: k = 2 (30 fps) on the 25th late frame and for good (321's window)", ks[23] == 1 && ks[24] == 2 && r.k_end == 2 && r.changes == 1, "k at frame 24 %.0f, changes %.0f", ks[24], r.changes);
    check("... game time within 2 vsyncs of real time less the dropped", r.real - r.game - s.dropped <= 2 && r.game - r.real + s.dropped <= 1, "real - game %.0f, dropped %.0f", r.real - r.game, s.dropped);
    r = run(&s, 300, hitch, km1, NULL);
    check("a 100 ms hitch: paid back (game time within a vsync of real time)", r.real - r.game <= 1 && r.game - r.real <= 1 && s.paid >= 4, "real - game %.0f, paid %.0f", r.real - r.game, (double)s.paid);
    r = run(&s, 300, stall, km1, NULL);
    check("a 0.5 s stall: 15 vsyncs paid back, the rest dropped", s.dropped >= 14 && r.real - r.game - s.dropped <= 1 && r.real - r.game - s.dropped >= -1, "dropped %.0f, real - game %.0f", s.dropped, r.real - r.game);
    r = run(&s, 600, heavy_light, km1, NULL);
    check("heavy (25 ms) then light (8 ms): back to k = 1 after a trial", r.k_end == 1 && s.downs >= 1, "k %.0f, downs %.0f", r.k_end, (double)s.downs);
    r = run(&s, 200, light, movie, ks);
    check("a movie hold (kmin 2 for frames 0-99): 30 fps during, 60 from frame 100", ks[50] == 2 && ks[98] == 2 && ks[99] == 1, "k of frame 99 %.0f, of frame 100 %.0f", ks[98], ks[99]);   // ks[i]: frame i + 1
    r = run(&s, 2000, jitter, km1, NULL);
    check("costs jittering under a vsync at 30 fps (0.92, three 1.2 in every 43): no flapping", r.changes <= 2, "changes %.0f, failed trials %.0f", r.changes, (double)s.failed);
    dsim d = run_dvfs(&s, 0, 0.66, 0.5, 60);
    check("opt off, a light scene (11 ms at the top clock) held at 30 fps with the clock low: stays at 30", d.k_end == 2 && d.at1_late_half == 0, "k %.0f, share at 60 %.2f", d.k_end, d.at1_late_half);
    d = run_dvfs(&s, 1, 0.66, 0.5, 60);
    check("opt on, the same scene: 60 fps at the end", d.k_end == 1, "k %.0f%.0s", d.k_end, 0);
    check("... and through the second half of the minute", d.at1_late_half > 0.95, "share at 60 %.2f, failed trials %.0f", d.at1_late_half, (double)d.failed);
    d = run_dvfs(&s, 1, 1.32, 0.5, 300);
    check("opt on, a heavy scene (22 ms at the top clock): 30 fps at the end, under a tenth of the second half in trials", d.k_end == 2 && d.at1_late_half < 0.10, "k %.0f, share at 60 %.2f", d.k_end, d.at1_late_half);
    check("... the waits double: at most 14 trials in 5 minutes", d.downs >= 3 && d.downs <= 14, "%.0f trials%.0s", (double)d.downs, 0);
    { nvn_fstep p; memset(&p, 0, sizeof p); nvn_fstep_next(&p, 0, 0, 1, FSTEP_MAX); p.k = 2;
      for (int i = 0; i < 2000; i++) { int k = p.k; double c = k == 1 ? 1.15 : 0.9, shown = c > k ? ceil(c - 1e-9) : k; nvn_fstep_next(&p, shown, c, 1, FSTEP_MAX); }
      check("0.9 vsyncs at 30 fps, 1.15 at 60: no trial in 2,000 frames (the margin) [-DDM_TEST_FSTEP_NO_MARGIN: trials]", p.downs == 0 && p.k == 2, "trials %.0f, k %.0f", (double)p.downs, p.k); }
    r = run(&s, 1200, bursts, km1, NULL);
    check("bursts of three late frames every 30 at 60 fps (10 %): stays at 60 (321) [-DDM_TEST_FSTEP_RUN3: steps up]", r.k_end == 1 && s.ups == 0, "k %.0f, ups %.0f", r.k_end, (double)s.ups);
    { nvn_fstep p; memset(&p, 0, sizeof p); nvn_fstep_next(&p, 0, 0, 1, FSTEP_MAX); p.k = 2; int at1 = 0;
      for (int i = 0; i < 1200; i++) { int k = p.k; double c = k == 1 ? 0.7 : 1.05, shown = c > k ? ceil(c - 1e-9) : k; p.slow = 1; nvn_fstep_next(&p, shown, c, 1, FSTEP_MAX); at1 += i >= 600 && p.k == 1; }
      check("a scene at 30 fps (1.05 vsyncs, 0.7 at 60) probed back while slowed: 60 through the second half [-DDM_TEST_FSTEP_NO_PROBE: stays at 30]", at1 == 600 && p.probes >= 1, "frames at 60 %.0f of 600, probes %.0f", at1, (double)p.probes);
      memset(&p, 0, sizeof p); nvn_fstep_next(&p, 0, 0, 1, FSTEP_MAX); p.k = 2;
      for (int i = 0; i < 1200; i++) { int k = p.k; double c = k == 1 ? 0.7 : 1.05, shown = c > k ? ceil(c - 1e-9) : k; nvn_fstep_next(&p, shown, c, 1, FSTEP_MAX); }
      check("... without the report: stays at 30", p.k == 2 && p.probes == 0, "k %.0f, probes %.0f", p.k, (double)p.probes); }
    { nvn_fgpu g; memset(&g, 0, sizeof g); int sl = 0, sl2 = 0;
      for (int i = 0; i < 100; i++) nvn_fstep_gpu_slowed(&g, 0.6, 1, 1, 0);
      for (int i = 0; i < 40; i++) sl = nvn_fstep_gpu_slowed(&g, 0.9, 2, 1, 0);
      nvn_fgpu h; memset(&h, 0, sizeof h);
      for (int i = 0; i < 100; i++) nvn_fstep_gpu_slowed(&h, 0.75, 1, 1, 0);
      for (int i = 0; i < 40; i++) sl2 = nvn_fstep_gpu_slowed(&h, 0.9, 2, 1, 0);
      check("the GPU at hold 2 1.5 times its time at 60 and over 0.85 vsync: slowed; 1.2 times: not", sl == 1 && sl2 == 0, "%.0f, %.0f", sl, sl2); }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
