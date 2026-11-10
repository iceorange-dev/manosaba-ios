// Pacer unit tests: given a clock sequence, the expected wait durations are hand-calculated. The period 16.666 ms is written as P = 16666667 ns.
#include "../nvn/nvn_pace.h"
#include <stdio.h>

// FIFO consumption at the display link (nvn_fifo_due, plan item 140) against taking the newest. A 60 Hz link from t = 1 s whose tick timestamps
// wander up to 0.2 ms either way; a game presenting at its period, 2 ms before the tick, but every 10th frame 0.7 period late, so the next one is
// queued behind it by the following tick; at most 3 frames queued (the game waits at its acquire, then presents right after the tick that freed
// one); ticks [off_a, off_b) never come (a silent link); 600 ticks
typedef struct { int produced, shown, dropped, in_order, min_gap, held; } fifo_sim;   // held: ticks a frame was queued and its period over, not shown
static fifo_sim fifo_run(int fifo, int itv, int off_a, int off_b) {
    const uint64_t P = 16666667, period = (uint64_t)itv * P, T0 = 1000000000ull;
    int qid[4], qn = 0, next = 0, last_k = -1, last_id = -1; uint64_t ready_after = 0; nvn_pacer pc = {0};
    fifo_sim s = {0, 0, 0, 1, 1 << 30, 0};
    for (int k = 0; k < 600; k++) {
        uint64_t T = T0 + (uint64_t)k * P + (uint64_t)(((int64_t)((k * 7919) % 41) - 20) * 10000);
        for (;;) {   // frames ready by this tick join the queue while it has room
            uint64_t r = T0 + (uint64_t)next * period - 2000000 + (next % 10 == 9 ? period * 7 / 10 : 0);
            if (r < ready_after) r = ready_after;
            if (r > T || qn >= 3) break;
            qid[qn++] = next++; s.produced++;
        }
        if (k >= off_a && k < off_b) continue;
        if (!qn) continue;
        int take;
        if (fifo) { if (!nvn_fifo_due(&pc, T, period)) { s.held += last_k >= 0 && k - last_k >= itv; continue; } take = 0; }
        else { take = qn - 1; s.dropped += qn - 1; }
        if (last_k >= 0) { int gap = k - last_k; if (gap < s.min_gap) s.min_gap = gap; }
        s.in_order &= qid[take] > last_id; last_id = qid[take]; last_k = k; s.shown++;
        int full = qn >= 3;
        if (fifo) { for (int i = 1; i < qn; i++) qid[i - 1] = qid[i]; qn--; } else qn = 0;
        if (full) ready_after = T + 1;   // the game waited for room: its next frame comes right after this tick
    }
    return s;
}

int main(void) {
    const uint64_t P = 16666667, MS = 1000000;
    struct { uint64_t now, period, want; const char *why; } c[] = {
        {1000 * MS, P, 0, "first frame: start the cadence, no wait"},
        {1005 * MS, P, 1000 * MS + P - 1005 * MS, "5 ms early: wait until 1000+P"},
        {1000 * MS + 2 * P - 2 * MS, P, 2 * MS, "third frame 2 ms early: wait 2 ms"},
        {1000 * MS + 3 * P + 3 * MS, P, 0, "3 ms late (less than one period): present immediately, present slot stays 1000+3P"},
        {1000 * MS + 4 * P - 1 * MS, P, 1 * MS, "next frame 1 ms early: wait until 1000+4P on the original phase"},
        {1000 * MS + 7 * P, P, 0, "two periods behind (stutter): restart the cadence from now, no burst of catch-up presents"},
        {1000 * MS + 7 * P + 4 * MS, P, P - 4 * MS, "next frame after the restart: wait until the new target"},
        {1000 * MS + 9 * P, 0, 0, "period 0 (DM_PRESENT_HZ=0): no wait"},
        {1000 * MS + 9 * P + 1 * MS, 2 * P, 2 * P - 1 * MS, "switch back to interval 2: present slot = previous frame time 9P + 2P"},
        {1000 * MS + 11 * P + 3 * MS, 2 * P, 2 * P - 3 * MS, "interval 2, next frame: present slot 13P"},
    };
    nvn_pacer p = {0}; int fail = 0, n = sizeof c / sizeof *c;
    for (int i = 0; i < n; i++) {
        uint64_t got = nvn_pace(&p, c[i].now, c[i].period);
        int ok = got == c[i].want;
        printf("%-52s expected wait %8.3f ms → got %8.3f ms %s\n", c[i].why, c[i].want / 1e6, got / 1e6, ok ? "✓" : "✗");
        fail += !ok;
    }
    // Uncapped pacing (nvn_uncapped_hz, plan item 53): when our own clock paces the game, and at what rate
    struct { int rt; const char *phz, *unc, *cap; int disp, want; const char *why; } u[] = {
        {1, NULL, NULL, NULL, 0, 120, "a real-time step, no window: uncapped at 120"},
        {1, NULL, NULL, NULL, 60, 120, "... on a 60 Hz display: 120 (the display shows the newest frame)"},
        {1, NULL, NULL, NULL, 100, 120, "... on a 100 Hz display: 120"},
        {1, NULL, NULL, NULL, 120, 0, "... on a 120 Hz display: the usual pacing (nothing to gain)"},
        {0, NULL, NULL, NULL, 60, 0, "a step per frame (30 fps product, manosaba): the usual pacing"},
        {1, "90", NULL, NULL, 60, 0, "DM_PRESENT_HZ asked for: the usual pacing at that rate"},
        {1, NULL, "0", NULL, 60, 0, "DM_UNCAPPED=0: off"},
        {1, NULL, "1", NULL, 60, 120, "DM_UNCAPPED=1: on"},
        {1, NULL, NULL, "display", 60, 60, "DM_UNCAPPED_HZ=display on 60 Hz: 60"},
        {1, NULL, NULL, "display", 0, 60, "DM_UNCAPPED_HZ=display without one: 60"},
        {1, NULL, NULL, "90", 60, 90, "DM_UNCAPPED_HZ=90"},
        {1, NULL, NULL, "10", 60, 30, "DM_UNCAPPED_HZ=10: 30 at least"},
        {1, NULL, NULL, "240", 60, 120, "DM_UNCAPPED_HZ=240: 120 at most (the step's 1/120 s floor)"},
    };
    for (int i = 0; i < (int)(sizeof u / sizeof *u); i++) {
        int got = nvn_uncapped_hz(u[i].rt, u[i].phz, u[i].unc, u[i].cap, u[i].disp), ok = got == u[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", u[i].why, u[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // ProMotion (nvn_promotion_hz, plan item 172): 120 while a 120 Hz screen runs at 120, for a game stepping by the measured time only; a game
    // stepping per frame (-DDM_TEST_PROMOTION_ANY_STEP) at 120 fails 2. While the player's target holds the rate below 60 (plan item 324) 120
    // whatever the screen runs at; the screen's rate then too (-DDM_TEST_PROMOTION_HELD_BY_SCREEN) fails 2
    struct { int rt; const char *phz, *pro; int disp, now, held, want; const char *why; } pm[] = {
        {1, NULL, NULL, 120, 120, 0, 120, "a real-time step on a 120 Hz screen running at 120: 120"},
        {1, NULL, NULL, 120, 60, 0, 60, "... the screen at 60 (iOS limits it): 60"},
        {0, NULL, NULL, 120, 120, 0, 0, "a step per frame (manosaba): left at 60"},
        {1, NULL, NULL, 60, 60, 0, 0, "a 60 Hz screen: left alone"},
        {1, "90", NULL, 120, 120, 0, 0, "DM_PRESENT_HZ asked for: left alone"},
        {1, NULL, "0", 120, 120, 0, 0, "DM_PROMOTION=0: left alone"},
        {1, NULL, NULL, 120, 30, 1, 120, "the target 30, the screen at the 30 we asked for: 120 (324: as where the hold began later)"},
        {1, NULL, NULL, 120, 60, 1, 120, "the target 30, the screen at 60: 120"},
        {1, NULL, NULL, 120, 120, 1, 120, "the target 30, the screen still at 120: 120"},
        {0, NULL, NULL, 120, 30, 1, 0, "the target 30, a step per frame: left at 60"},
        {1, NULL, NULL, 60, 30, 1, 0, "the target 30, a 60 Hz screen: left alone"},
        {1, NULL, "0", 120, 30, 1, 0, "the target 30, DM_PROMOTION=0: left alone"},
    };
    for (int i = 0; i < (int)(sizeof pm / sizeof *pm); i++) {
        int got = nvn_promotion_hz(pm[i].rt, pm[i].phz, pm[i].pro, pm[i].disp, pm[i].now, pm[i].held), ok = got == pm[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", pm[i].why, pm[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // Display-link frame latency (nvn_latency_setting / nvn_frame_latency, plan item 153): unset 1; a number wins at every rate; "auto" 2 at a
    // 30 Hz link only. "auto" taking 2 at every rate (-DDM_TEST_LATENCY_AUTO_ANY) fails 2
    struct { const char *env; int lhz, want; const char *why; } fl[] = {
        {NULL, 30, 1, "DM_PRESENT_LATENCY unset, 30 Hz link: 1 (as before)"},
        {"2", 60, 2, "DM_PRESENT_LATENCY=2, 60 Hz link: 2 (a setting given wins)"},
        {"1", 30, 1, "DM_PRESENT_LATENCY=1, 30 Hz link: 1"},
        {"auto", 30, 2, "auto, 30 Hz link (power-saving mode): 2"},
        {"auto", 60, 1, "auto, 60 Hz link: 1"},
        {"auto", 120, 1, "auto, 120 Hz link (mode 2): 1"},
        {"0", 30, 1, "DM_PRESENT_LATENCY=0 (not above 0): as unset, 1"},
        {"x", 30, 1, "DM_PRESENT_LATENCY=x: as unset, 1"},
    };
    for (int i = 0; i < (int)(sizeof fl / sizeof *fl); i++) {
        int got = nvn_frame_latency(nvn_latency_setting(fl[i].env), fl[i].lhz), ok = got == fl[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", fl[i].why, fl[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // Display-link ticks a frame takes (nvn_link_step, plan item 123); the old step (-DDM_TEST_LINK_STEP_FIXED, the interval whatever the link's
    // rate) fails the three power-saving cases at interval 2, 3 and 4
    struct { unsigned itv, hz, lhz, want; const char *why; } k[] = {
        {1, 60, 60, 1, "interval 1, 60 Hz link: 1 tick (60 fps)"},
        {2, 60, 60, 2, "interval 2, 60 Hz link: 2 ticks (30 fps)"},
        {2, 60, 120, 4, "interval 2, 120 Hz link (mode 2): 4 ticks (30 fps)"},
        {2, 60, 30, 1, "interval 2, power-saving 30 Hz link: 1 tick (30 fps, was 2: 15 fps)"},
        {1, 60, 30, 1, "interval 1, 30 Hz link: 1 tick (30 fps, the cap)"},
        {3, 60, 30, 2, "interval 3, 30 Hz link: 2 ticks (15 fps; never faster than the 20 asked)"},
        {4, 60, 30, 2, "interval 4, 30 Hz link: 2 ticks (15 fps, as asked; was 4: 7.5 fps)"},
        {2, 120, 120, 2, "interval 2 of a 120 Hz present rate on a 120 Hz link: 2 ticks (60 fps)"},
    };
    for (int i = 0; i < (int)(sizeof k / sizeof *k); i++) {
        unsigned got = nvn_link_step(k[i].itv, k[i].hz, k[i].lhz); int ok = got == k[i].want; n++;
        printf("%-72s expected %3u → got %3u %s\n", k[i].why, k[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // The power-saving link (nvn_link30_step, plan item 324): 30 Hz while a frame is a whole number of its ticks, the present rate's link while one
    // is not and for NVN_LINK30_BACK_NS after; with each the ticks a frame then takes and the rate that gives. 30 Hz whatever the interval
    // (-DDM_TEST_LINK30_ALWAYS) fails 13: the six rows off 30 Hz, their ticks, and the frame rate of interval 3 (15 fps for the 20 asked)
    {   nvn_link30 s = {0}; const uint64_t S = 1000000000ull;
        struct { unsigned itv, hz; uint64_t at; int want; unsigned ticks; const char *why; } l[] = {
            {2, 60, 1 * S, 1, 1, "interval 2 at 60 Hz: the 30 Hz link, 1 tick (30 fps)"},
            {1, 60, 2 * S, 1, 1, "interval 1 (the hold not applied yet): its 2 under the target, the 30 Hz link"},
            {3, 60, 3 * S, 0, 3, "interval 3 at 60 Hz (20 fps): 1.5 ticks of 30 Hz, the link at 60 Hz, 3 ticks"},
            {2, 60, 3 * S + S / 10, 0, 2, "interval 2 again 0.1 s later: still at 60 Hz, 2 ticks (30 fps)"},
            {3, 60, 4 * S, 0, 3, "interval 3 again: at 60 Hz"},
            {2, 60, 4 * S + NVN_LINK30_BACK_NS - 1, 0, 2, "interval 2 until just under 3 s after the last interval 3: at 60 Hz"},
            {2, 60, 4 * S + NVN_LINK30_BACK_NS, 1, 1, "3 s after it: the 30 Hz link again"},
            {4, 60, 9 * S, 1, 2, "interval 4 at 60 Hz (15 fps asked): 2 ticks of 30 Hz, fits"},
            {3, 120, 10 * S, 1, 1, "interval 3 at 120 Hz: the hold's 4 under the target, 1 tick of 30 Hz"},
            {4, 120, 11 * S, 1, 1, "interval 4 at 120 Hz: 1 tick"},
            {4, 90, 12 * S, 0, 4, "interval 4 at 90 Hz (22.5 fps): the link at 90 Hz, 4 ticks"},
            {3, 90, 12 * S + 4 * S, 1, 1, "interval 3 at 90 Hz 4 s later: 1 tick of 30 Hz"},
            {0, 100, 20 * S, 1, 1, "no frame waits for a tick (uncapped pacing) on a 100 Hz screen: the 30 Hz link"},
            {5, 144, 21 * S, 0, 5, "interval 5 at 144 Hz (28.8 fps): the link at 144 Hz, 5 ticks"},
        };
        for (int i = 0; i < (int)(sizeof l / sizeof *l); i++) {
            int got = nvn_link30_step(&s, l[i].itv, l[i].hz, l[i].at), ok = got == l[i].want; n++;
            unsigned h = (l[i].hz + 29) / 30, itv = l[i].itv < h ? h : l[i].itv, t = nvn_link_step(itv, l[i].hz, got ? 30 : l[i].hz);
            printf("%-72s expected %3d → got %3d %s\n", l[i].why, l[i].want, got, ok ? "✓" : "✗"); fail += !ok;
            if (l[i].itv) { ok = t == l[i].ticks; n++; printf("%-72s expected %3u → got %3u %s\n", "... the ticks a frame takes", l[i].ticks, t, ok ? "✓" : "✗"); fail += !ok; }
        }
        nvn_link30 z = {0}; int on = nvn_link30_step(&z, 3, 60, 5 * S); double fps = (on ? 30.0 : 60.0) / nvn_link_step(3, 60, on ? 30 : 60); int ok = fps == 20.0; n++;
        printf("%-72s expected %3d → got %3.0f %s\n", "interval 3 at 60 Hz under the target: the 20 fps the game asks", 20, fps, ok ? "✓" : "✗"); fail += !ok;
    }
    // The frame-rate hold (nvn_hold_interval, plan item 130: Tears of the Kingdom's pre-rendered movies at 30 fps); the game's interval whatever the
    // hold (-DDM_TEST_HOLD_IGNORED) fails 4
    struct { int itv, hz, hold, want; const char *why; } h[] = {
        {1, 60, 30, 2, "the 60 fps product in a movie: interval 2 (30 fps)"},
        {2, 60, 30, 2, "the 30 fps product: its own 2"},
        {3, 60, 30, 3, "the game's longer interval stays (3: 20 fps)"},
        {1, 90, 30, 3, "DM_PRESENT_HZ=90: 3"},
        {1, 120, 30, 4, "uncapped pacing at 120: 4"},
        {1, 100, 30, 4, "100 Hz: 4 (25 fps, never faster than the hold)"},
        {1, 60, 0, 1, "no hold: the game's interval"},
        {0, 60, 0, 1, "interval 0 counts as 1"},
        {1, 0, 30, 1, "present rate 0 (DM_PRESENT_HZ=0, profiling): no hold"},
    };
    for (int i = 0; i < (int)(sizeof h / sizeof *h); i++) {
        int got = nvn_hold_interval(h[i].itv, h[i].hz, h[i].hold), ok = got == h[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", h[i].why, h[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // The iOS frame rate policy (nvn_fps_policy, plan item 135); the setting alone (-DDM_TEST_POLICY_USER_ONLY) fails 3 (4 with 135a's rows: the
    // Low Power one); the switch "Hold 30 fps when hot" off (plan item 135a) keeps 60 when hot, not in Low Power Mode; held whatever the switch
    // (-DDM_TEST_POLICY_HOT_ALWAYS) fails 2
    struct { int user, lp, th, hold, want; const char *why; } fp[] = {
        {60, 0, 0, 1, 60, "60 fps set, nominal: 60"},
        {60, 0, 1, 1, 60, "60 fps set, thermal Fair: 60"},
        {60, 0, 2, 1, 30, "60 fps set, thermal Serious: 30"},
        {60, 0, 3, 1, 30, "60 fps set, thermal Critical: 30"},
        {60, 1, 0, 1, 30, "60 fps set, Low Power Mode: 30"},
        {30, 0, 0, 1, 30, "30 fps set: 30"},
        {30, 1, 3, 1, 30, "30 fps set, Low Power and Critical: 30 (never above the setting)"},
        {60, 0, 2, 0, 60, "hold when hot off, thermal Serious: 60 (135a)"},
        {60, 0, 3, 0, 60, "hold when hot off, thermal Critical: 60 (135a)"},
        {60, 1, 2, 0, 30, "hold when hot off, Low Power Mode: still 30"},
    };
    for (int i = 0; i < (int)(sizeof fp / sizeof *fp); i++) {
        int got = nvn_fps_policy(fp[i].user, fp[i].lp, fp[i].th, fp[i].hold), ok = got == fp[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", fp[i].why, fp[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // The pacing hold with the player's target (nvn_pace_hold, plan item 135b); the game's hold alone (-DDM_TEST_TARGET_NOT_HELD) fails 2
    struct { int hold, target, hz, itv, want; const char *why; } ph[] = {
        {0, 60, 60, 1, 1, "no hold, target 60, the game's interval 1: 1"},
        {0, 30, 60, 1, 2, "target 30 (thermal hold), the game's interval 1 at 60 Hz: 2 (30 fps, none drawn to be dropped)"},
        {0, 30, 120, 1, 4, "target 30 at 120 Hz: 4"},
        {30, 60, 60, 1, 2, "a movie's hold 30, target 60: 2"},
        {30, 30, 60, 3, 3, "hold and target 30, the game's own 3: 3 (a longer interval stays)"},
        {0, 30, 60, 2, 2, "target 30, the game's interval 2 already: 2"},
    };
    for (int i = 0; i < (int)(sizeof ph / sizeof *ph); i++) {
        int got = nvn_hold_interval(ph[i].itv, ph[i].hz, nvn_pace_hold(ph[i].hold, ph[i].target)), ok = got == ph[i].want; n++;
        printf("%-72s expected %3d → got %3d %s\n", ph[i].why, ph[i].want, got, ok ? "✓" : "✗"); fail += !ok;
    }
    // FIFO consumption (nvn_fifo_due, plan item 140, see fifo_run); one frame every tick whatever its period (-DDM_TEST_FIFO_NO_PERIOD) fails 2
    {   fifo_sim nw = fifo_run(0, 1, 0, 0), ff = fifo_run(1, 1, 0, 0), f2 = fifo_run(1, 2, 0, 0), fs = fifo_run(1, 2, 200, 230);
        struct { int ok; const char *why; } f[] = {
            {nw.dropped > 0, "interval 1 with late frames, the newest taken: frames dropped (the problem)"},
            {ff.dropped == 0 && ff.in_order && ff.shown >= ff.produced - 3, "... FIFO: none dropped, every frame shown in order"},
            {f2.min_gap >= 2 && f2.held == 0 && f2.in_order, "interval 2, FIFO: a frame 2 ticks at least, never held past them (early ticks due)"},
            {fs.min_gap >= 2 && fs.in_order, "... after a silent link (30 ticks, 3 frames queued): 2 ticks a frame, no burst"},
        };
        printf("  newest: %d produced, %d shown, %d dropped; FIFO: %d / %d / %d; interval 2: %d / %d, smallest gap %d ticks; after the silent link: smallest gap %d\n",
               nw.produced, nw.shown, nw.dropped, ff.produced, ff.shown, ff.dropped, f2.produced, f2.shown, f2.min_gap, fs.min_gap);
        for (int i = 0; i < 4; i++) { n++; printf("%-72s %s\n", f[i].why, f[i].ok ? "✓" : "✗"); fail += !f[i].ok; } }
    // Dropped and slow frames against each frame's hold (plan item 156); against 60 fps whatever the hold (-DDM_TEST_DROP_60HZ) fails 2
    {   int d30 = 0, d60 = 0, dchg = 0, ddis = 0, s30 = 0; double h60 = nvn_hold_ms(16666667), h30 = nvn_hold_ms(33333333);
        for (int i = 0; i < 300; i++) { d30 += nvn_dropped(33.4, nvn_expect_ms(h30, h30)); s30 += nvn_slow(33.4, h30); }
        for (int i = 0; i < 300; i++) d60 += nvn_dropped(i % 60 == 7 ? 33.3 : 16.7, nvn_expect_ms(h60, h60));
        dchg = nvn_dropped(33.3, nvn_expect_ms(h60, h30)) + nvn_dropped(16.7, nvn_expect_ms(h30, h60));
        ddis = nvn_dropped(-1, h30);
        struct { int ok; const char *why; } f[] = {
            {d30 == 0 && s30 == 0, "300 frames at 30 fps (33.4 ms, hold 33.3): none dropped, none slow"},
            {d60 == 5, "300 at 60 fps with 5 frames shown twice: 5 dropped"},
            {dchg == 0 && ddis == 1 && nvn_hold_ms(0) == 1000.0 / 60, "the interval changing 1 -> 2 and back: not a drop; a discarded frame is; unpaced counts as 60"},
        };
        printf("  dropped at 30 fps %d, at 60 fps with 5 doubled %d\n", d30, d60);
        for (int i = 0; i < 3; i++) { n++; printf("%-72s %s\n", f[i].why, f[i].ok ? "✓" : "✗"); fail += !f[i].ok; } }
    // Time-constraint declarations (nvn_rt_*, plan item 61d): the cap's period; a new period keeps the computation declared (-DDM_TEST_RT_COMP_RESET,
    // 2 ms again on every change, fails 2); 1.25 x the p90 of 120 frames
    {   nvn_rt_win w = {0}, w2 = {0}; uint64_t got = 0, got2 = 0;
        for (int i = 0; i < 120; i++) got = nvn_rt_sample(&w, i < 108 ? 8.0 : 30.0, 2.0, 16666666, 0);
        for (int i = 0; i < 120; i++) got2 = nvn_rt_sample(&w2, 8.0, 10.0, 16666666, 0);
        nvn_rt_win w3 = {0}, w4 = {0}; uint64_t got3 = 0, got4 = 0;   // DM_RT_PEAK (plan item 248): one frame of 12 ms in 120 of 8
        for (int i = 0; i < 120; i++) got3 = nvn_rt_sample(&w3, i == 60 ? 12.0 : 8.0, 2.0, 33333333, 1);
        for (int i = 0; i < 120; i++) got4 = nvn_rt_sample(&w4, i == 60 ? 12.0 : 8.0, 2.0, 33333333, 0);
        struct { int ok; const char *why; } f[] = {
            {nvn_rt_period_ns(60, 1) == 16666666, "the cap at 60 Hz: 16.67 ms (the game's own interval is not an input)"},
            {nvn_rt_period_ns(120, 4) == 33333333, "the app's hold of 4 at 120 Hz: 33.3 ms"},
            {nvn_rt_comp_on_period(0, 0, 16666666) == 2000000, "the first declaration: 2 ms"},
            {nvn_rt_comp_on_period(0, 10000000, 33333333) == 10000000, "a new period keeps the 10 ms declared"},
            {nvn_rt_comp_on_period(0, 20000000, 16666666) == 14999999, "... clamped to 0.9 of a shorter period"},
            {nvn_rt_comp_on_period(5000000, 10000000, 33333333) == 5000000, "DM_RT_COMP fixed: 5 ms"},
            {got == 10000000, "p90 of 120 frames (108 at 8 ms, 12 at 30 ms) x 1.25: 10 ms"},
            {got2 == 0, "within 15 % of the declared 10 ms: no new declaration"},
            {got3 == 13200000, "DM_RT_PEAK: one 12 ms frame among 8 ms ones: 1.1 x 12 = 13.2 ms over 1.25 x 8 [-DDM_TEST_RT_NO_PEAK: 10 ms]"},
            {got4 == 10000000, "... without it: 1.25 x the p90, 10 ms"},
        };
        for (int i = 0; i < (int)(sizeof f / sizeof *f); i++) { n++; printf("%-72s %s\n", f[i].why, f[i].ok ? "✓" : "✗"); fail += !f[i].ok; } }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
