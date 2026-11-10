// Present pacer (pure function, shared by Mac / iOS; unit test tests/t_pace.c).
// Semantics as vsync: present slot = previous present slot + period. Early → wait until the present slot; late by less than one period → present immediately and keep the original phase;
// behind by one period or more (stutter) → restart the cadence from the current time, no burst of presents to catch up. Period 0 = no wait (the present slot follows the current time).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t target; } nvn_pacer;

// now: monotonic clock (ns); period: this frame's period (ns, 0 = no wait). Returns the ns to wait (0 = present immediately) and updates the internal target time.
static inline uint64_t nvn_pace(nvn_pacer *p, uint64_t now, uint64_t period) {
    if (!period || !p->target) { p->target = now; return 0; }
    uint64_t next = p->target + period;
    if (now <= next) { p->target = next; return next - now; }
    p->target = now - next < period ? next : now;
    return 0;
}

// Display-link ticks one frame takes (plan item 123): the game's present interval counts refreshes of the present rate hz, the link ticks at link_hz
// (30 in the power-saving mode, 120 in pacing mode 2), so a frame takes interval x link_hz / hz ticks, rounded up, at least one. The step used to
// be the interval (x 2 in mode 2) whatever the link's rate: Tears of the Kingdom at interval 2 with the 30 fps setting got two 30 Hz ticks a frame,
// 15 fps. Rounded up, interval 3 at 30 Hz gives 2 ticks (15 fps for the 20 the game asks; never faster than asked): the link does not tick at 30
// Hz for such a frame (nvn_link30_step below, plan item 324). Unit test tests/t_pace.c
static inline unsigned nvn_link_step(unsigned itv, unsigned hz, unsigned link_hz) {
    if (!itv) itv = 1;
    if (!hz) hz = 60;
#ifndef DM_TEST_LINK_STEP_FIXED   // negative control of t_pace only: the interval, doubled at 120 Hz, as before
    unsigned s = (itv * link_hz + hz - 1) / hz;
    return s ? s : 1;
#else
    return itv * (link_hz >= 120 && hz < 120 ? 2 : 1);
#endif
}

// The power-saving link (plan item 324): with the frame rate target at 30 the display link ticks at 30 Hz only while a frame is a whole number of
// its ticks. One that is not (Tears of the Kingdom's interval 3 at a present rate of 60 Hz, 20 fps) took 2 ticks by nvn_link_step's rounding: an
// iPhone Air started hot showed 15.0 fps for 150 s, and a game that steps once per frame ran at three quarters of its speed. The link then ticks at
// the present rate and the frame takes its interval in ticks, until every frame has fitted for NVN_LINK30_BACK_NS (the game's controller tries
// interval 2 for a few frames and goes back to 3: the link's rate does not follow those). itv as the pacing set it, the hold applied here (a frame
// under the target is never shorter than the hold's interval); 0: no frame waits for a tick (uncapped pacing, our own clock), 30 Hz whatever the
// rates. 1: the link at 30 Hz. Unit test tests/t_pace.c
typedef struct { uint64_t off_until; } nvn_link30;
#define NVN_LINK30_BACK_NS 3000000000ull
static inline int nvn_link30_step(nvn_link30 *s, unsigned itv, unsigned hz, uint64_t now) {
    if (!itv) return 1;
    if (!hz) hz = 60;
    unsigned h = (hz + 29) / 30; if (itv < h) itv = h;   // (nvn_hold_interval at 30 fps, defined below)
#ifndef DM_TEST_LINK30_ALWAYS   // negative control of t_pace and t_linkfifo only: 30 Hz whatever the interval, as before
    if (itv * 30 % hz) { s->off_until = now + NVN_LINK30_BACK_NS; return 0; }
    if (now < s->off_until) return 0;
#else
    (void)s; (void)now;
#endif
    return 1;
}

// Dropped and slow frames judged against each frame's own hold (plan item 156, after nxnative e7a84c7, MIT, written anew): a frame is meant to stay
// on screen its present period × interval (hold_ms, 1/60 s when unpaced); the on-screen interval between presents n - 1 and n shows frame n - 1,
// judged against the larger of their two holds (a change of interval between them is not a drop). Dropped: discarded (-1) or on screen over 1.5 ×
// that; slow (the frame interval line): over 1.1 × its hold. Against a fixed 16.7 / 25 ms every frame of a 30 fps game counted (299 of 300).
// Unit test tests/t_pace.c
static inline double nvn_hold_ms(uint64_t period_ns) { return period_ns ? period_ns / 1e6 : 1000.0 / 60; }
// When a present reached the screen, for the statistics above (plan item 71b): by the ticket the present thread gave it, not by the game's count of
// presents. A present made while presents are discarded (a covered window, the app in the background) gets no ticket, so the count ran ahead of the
// tickets from then on and no later present was ever found: "on screen –" for the rest of the run. known: PresentTexture's ticket is known (on
// the translation thread its job has run); ticket 0: discarded before it was queued; shown: the present thread's time for that ticket (seconds;
// 0 none yet, -1 dropped). Returns seconds, 0 for no reading, -1 for dropped or discarded
static inline double nvn_pres_time(int known, uint64_t ticket, double shown) {
    if (!known) return 0;
#ifndef DM_TEST_PRES_DISCARD_NO_READING   // negative control of t_pace only: a discarded present is no reading (it was not counted as dropped)
    if (!ticket) return -1;
#else
    if (!ticket) return 0;
#endif
    return shown;
}
static inline double nvn_expect_ms(double hold_prev, double hold_cur) { return hold_prev > hold_cur ? hold_prev : hold_cur; }
static inline int nvn_dropped(double iv_ms, double expect_ms) {
#ifdef DM_TEST_DROP_60HZ   // negative control of t_pace only: against 60 fps whatever the hold, as before
    expect_ms = 1000.0 / 60;
#endif
    return iv_ms < 0 || iv_ms > 1.5 * expect_ms;
}
static inline int nvn_slow(double iv_ms, double hold_ms) { return iv_ms > 1.1 * hold_ms; }

// FIFO consumption at the display link (plan item 140; acnh-mobile c2af194 consumes FIFO at the game's period, MIT, written anew): the link shows
// the oldest queued frame instead of the newest, for a game that steps its logic once per frame (game.json present_consume "fifo": manosaba); the
// newest drops the frames queued before it, each a step of the game never shown (manosaba on an iPhone: 14 by tick 1,549). A frame stays at least
// its present period from the tick that showed it, as the console's swap interval counts vsyncs from the previous flip (acnh-mobile keeps the
// phase instead, which shows the frame after a late one for a single tick at interval 2). now = the tick's target time, p->target = the last
// shown. 1: the head frame is due; 0: the frame on screen stays this tick. A tick up to 2 ms early is due (timestamps wander; ticks are 8.3 ms
// apart at 120 Hz). Unit test tests/t_pace.c
static inline int nvn_fifo_due(nvn_pacer *p, uint64_t now, uint64_t period) {
#ifndef DM_TEST_FIFO_NO_PERIOD   // negative control of t_pace only: one frame every tick whatever its period
    if (period && p->target && now + 2000000 < p->target + period) return 0;
#endif
    p->target = now;
    return 1;
}

// A frame-rate hold (plan item 130): while the host holds the game at hold_fps (hle_oe.c: Tears of the Kingdom's pre-rendered movies at 30),
// a frame takes at least the present interval that gives that rate at the present rate hz, rounded up (never faster than the hold): 2 at 60 Hz,
// 3 at 90, 4 at 120. A longer interval of the game's own stays. hold_fps 0 = no hold. Unit test tests/t_pace.c
static inline int nvn_hold_interval(int itv, int hz, int hold_fps) {
    if (itv < 1) itv = 1;
#ifndef DM_TEST_HOLD_IGNORED   // negative control of t_pace only: the game's interval whatever the hold
    if (hold_fps > 0 && hz > 0) { int h = (hz + hold_fps - 1) / hold_fps; if (h > itv) itv = h; }
#endif
    return itv;
}

// The hold a frame is paced with: the lower of the game's hold (130: movies) and the player's frame rate target below 60 (DM_FPS=30, the iOS setting
// or 135's thermal hold), 0 for none. The target alone used to set the display link's rate only: a game choosing interval 1 itself (Tears of the
// Kingdom's 60 fps build in its map screen) kept presenting 60 frames a second at a 30 Hz link, half of them drawn and dropped (plan item 135b)
static inline int nvn_pace_hold(int hold_fps, int target_fps) {
#ifndef DM_TEST_TARGET_NOT_HELD   // negative control of t_pace only: the game's hold alone, as before
    if (target_fps > 0 && target_fps < 60 && (!hold_fps || target_fps < hold_fps)) return target_fps;
#else
    (void)target_fps;
#endif
    return hold_fps;
}
// The iOS frame rate the device can afford (plan item 135, after super-mario-odyssey-ios's frame pacer, MIT): the player's setting, held at 30 in
// Low Power Mode or from thermal state Serious up (NSProcessInfoThermalStateSerious = 2, Critical = 3), where iOS throttles the device anyway and a
// game paced for 60 misses frames unevenly. hold_hot 0 (the app's "Hold 30 fps when hot" off, plan item 135a) keeps the setting at any thermal
// state: Tears of the Kingdom's 60 fps build picks 60, 30 or 20 itself. Unit test tests/t_pace.c
static inline int nvn_fps_policy(int user_fps, int low_power, int thermal, int hold_hot) {
#ifdef DM_TEST_POLICY_USER_ONLY   // negative control of t_pace only: the setting alone, as before
    (void)low_power; (void)thermal; (void)hold_hot; return user_fps;
#endif
#ifdef DM_TEST_POLICY_HOT_ALWAYS   // negative control of t_pace only: held when hot whatever the switch, as before 135a
    hold_hot = 1;
#endif
    return (low_power || (hold_hot && thermal >= 2)) && user_fps > 30 ? 30 : user_fps;
}

// Uncapped pacing (plan item 53, after MacNativeGames' TOTK_UNCAPPED, ly05010419/MacNativeGames, MIT): for a game whose step follows the measured
// frame time (the host's dm_game_realtime_step), the game does not wait for the display. Our own clock paces it at up to `cap` Hz (its present
// interval × 1 / cap a frame) and the display link shows the newest finished frame at each refresh, so a scene slower than 60 fps shows at its own
// rate instead of the next step down (30). Off with DM_UNCAPPED=0, with DM_PRESENT_HZ (a fixed rate asked for) and on a display of 120 Hz or more
// (nothing to gain there). The cap: DM_UNCAPPED_HZ=30..120 or "display" (the display's rate, 60 without one); 120 at most, as the frame step counts a
// frame as at least 1/120 s. Returns the clock rate in Hz, 0 for the usual pacing.
// ProMotion (plan item 172, after Touchable's SS_DISPLAY_HZ=120, MIT): on a screen of 120 Hz or more a game whose step follows the measured frame time
// presents at 120 while the screen runs at 120 (the front end asks it for 80-120 Hz and reports the rate it runs at: iOS lowers it in Low Power Mode),
// at 60 otherwise; a game that steps per frame (manosaba) stays at 60, as it would run twice as fast. DM_PROMOTION=0 or DM_PRESENT_HZ (a fixed rate)
// leave the rate alone. Returns the present rate for the screen's current rate now_hz, 0 to leave it as it is.
// held (plan item 324): the player's frame rate target is below 60 (DM_FPS=30, the iOS setting, Low Power Mode or 135's thermal hold). The screen
// then runs at 30 by our own request, which says nothing of the rate it could run at, and the front end used to report nothing: the present rate
// stayed what it was, 120 where the hold began while the game ran and 60 where the game started under it. At 60 the game's own interval 3 is 20
// fps and paces the frames (15 fps on the 30 Hz link); at 120 it is 40 fps, under the hold's 4, and frames of 35 ms show at 27 fps. 120 while
// held, as where the hold begins later: a frame takes the hold's interval either way
static inline int nvn_promotion_hz(int realtime_step, const char *present_hz, const char *promotion, int display_hz, int now_hz, int held) {
    if (present_hz || (promotion && !strcmp(promotion, "0")) || display_hz < 120) return 0;
#ifndef DM_TEST_PROMOTION_ANY_STEP   // negative control of t_pace only: a game stepping per frame at 120 too
    if (!realtime_step) return 0;
#endif
#ifndef DM_TEST_PROMOTION_HELD_BY_SCREEN   // negative control of t_pace only: the screen's rate while held too
    if (held) return 120;
#else
    (void)held;
#endif
    return now_hz >= 110 ? 120 : 60;
}
static inline int nvn_uncapped_hz(int realtime_step, const char *present_hz, const char *uncapped, const char *cap, int display_hz) {
#ifdef DM_TEST_UNCAPPED_ANY_STEP   // negative control of t_pace only: a game stepping per frame paced uncapped too (it would run fast)
    (void)realtime_step;
    if (present_hz || (uncapped && !strcmp(uncapped, "0")) || display_hz >= 120) return 0;
#else
    if (!realtime_step || present_hz || (uncapped && !strcmp(uncapped, "0")) || display_hz >= 120) return 0;
#endif
    int v = !cap || !*cap ? 120 : !strcmp(cap, "display") ? (display_hz > 0 ? display_hz : 60) : atoi(cap);
    return v < 30 ? 30 : v > 120 ? 120 : v;
}
// Display-link frame latency (plan item 153, after lfkdsk/switch-appletv-port-skill references/05-perf.md §5.3, MIT: with the link at 30 Hz,
// preferredFrameLatency 2 halved the present-to-present standard deviation, 0.603 → 0.301 ms over 1,530 frames, and frames above 40 ms went 4 → 1,
// for 33 ms more latency; at 60 Hz they keep 1, with no headroom to trade; a setting given always wins). DM_PRESENT_LATENCY=<n> fixes it; "auto"
// takes 2 while the link ticks at 30 Hz (the power-saving mode, or a present rate of 30) and 1 otherwise; unset, 1 as before (to be measured on
// a device: a device check). nvn_latency_setting: n, 0 for "auto", -1 unset or not a number above 0
static inline int nvn_latency_setting(const char *e) {
    if (!e || !*e) return -1;
    if (!strcmp(e, "auto")) return 0;
    int v = atoi(e); return v > 0 ? v : -1;
}
static inline int nvn_frame_latency(int setting, int link_hz) {
    if (setting > 0) return setting;
    if (setting < 0) return 1;
#ifndef DM_TEST_LATENCY_AUTO_ANY   // negative control of t_pace only: "auto" takes 2 at every rate
    return link_hz <= 30 ? 2 : 1;
#else
    (void)link_hz; return 2;
#endif
}
// The time-constraint declaration of a per-frame thread (the acquiring thread and the translation thread; plan item 61d). Its period is the frame
// rate cap's (the display rate times the app's own hold, nvn_hold_interval(1, hz, hold)), not one the game raised for lateness: with the game's
// interval 2 declared, Tears of the Kingdom on an iPhone went from 3.4 to 1.5 GHz and from 65 % to 5 % of its CPU time on performance cores at
// thermal state 0 and stayed at 23-30 fps (seedds/nxnative 5efe1d8, MIT; ours on the iPhone 17 Pro: 0-15 % on performance cores, 0.9-1.3 GHz). A new
// period keeps the computation declared so far, which went back to 2 ms before (nxnative 04ea8f8, MacNativeGames c5d8671, MIT); the computation
// follows 1.25 x the p90 of the thread's own CPU time a frame over 120 frames, between 1 ms and 0.9 of the period (nvn_rt_sample). Using more than
// the computation does not demote the thread (XNU sched_prim.c / priority.c: it ends the thread's deadline for that period; the fail-safe demotion
// needs about 100 quanta without blocking). DM_RT_DECL=old declares as before (the held period, 2 ms again on each change, half the period for the
// translation thread)
static inline uint64_t nvn_rt_period_ns(int hz, int hold_interval) {
    return hz > 0 ? (uint64_t)(hold_interval > 0 ? hold_interval : 1) * 1000000000ull / (uint64_t)hz : 0;
}
static inline uint64_t nvn_rt_comp_clamp(uint64_t comp_ns, uint64_t period_ns) {
    if (comp_ns > period_ns * 9 / 10) comp_ns = period_ns * 9 / 10;
    return comp_ns < 1000000ull ? 1000000ull : comp_ns;
}
// The computation to declare with a new period: the fixed one (DM_RT_COMP), else the one declared so far (0: none yet, 2 ms to start)
static inline uint64_t nvn_rt_comp_on_period(uint64_t fixed_ns, uint64_t declared_ns, uint64_t period_ns) {
#ifdef DM_TEST_RT_COMP_RESET   // negative control of t_pace only: every new period starts again from 2 ms (before 2026-10-22)
    declared_ns = 0;
#endif
    return nvn_rt_comp_clamp(fixed_ns ? fixed_ns : declared_ns ? declared_ns : 2000000ull, period_ns);
}
typedef struct { double win[120]; int n; } nvn_rt_win;
static inline int nvn_rt_dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
// One frame's CPU time of the thread (ms); every 120 frames the computation to declare (ns) when 1.25 x their p90, clamped, differs from the
// declared one by more than 15 %, else 0. peak (DM_RT_PEAK=1, plan item 248, after nxnative 65d4a4d, MIT): at least 1.1 x the window's longest
// frame too (nxnative: frames past the declaration stalled 30-60 ms about once a second in Dark Souls Remastered, p99 63-127 → 21 ms)
static inline uint64_t nvn_rt_sample(nvn_rt_win *w, double cpu_ms, double declared_ms, uint64_t period_ns, int peak) {
    w->win[w->n++] = cpu_ms;
    if (w->n < 120) return 0;
    double v[120]; memcpy(v, w->win, sizeof v); qsort(v, 120, sizeof *v, nvn_rt_dcmp); w->n = 0;
    double want = v[107] * 1.25;
#ifndef DM_TEST_RT_NO_PEAK   // negative control of t_pace only: the p90 alone, as before
    if (peak && v[119] * 1.1 > want) want = v[119] * 1.1;
#else
    (void)peak;
#endif
    uint64_t c = nvn_rt_comp_clamp((uint64_t)(want * 1e6), period_ns); double cm = c / 1e6;
    return cm > declared_ms * 1.15 || cm < declared_ms * 0.85 ? c : 0;
}
