// gfx/nvn/hle/pacing.inc, part of hle_nvn.c (included there in order, one translation unit): Frame pacing and nvnQueuePresentTexture: dropped frames, the real-time thread's declaration, the present rate, frame-rate trials
// Dropped-frame definition: on-screen present interval over 1.5 × the frame's hold (nvn_dropped, plan item 156) or −1; without a window (no on-screen
// present time) fall back to the present call interval against the hold now
static void slow_emit(uint64_t f) {
    static uint64_t judged, slow;
    double pm = srow(f, "@presented_ms", 0), fr = srow(f, "@frame_ms", 0);
    int has = mtl_has_window(), bad = has ? (pm != 0 && nvn_dropped(pm, g_pres_exp_last > 0 ? g_pres_exp_last : g_hold_now)) : nvn_dropped(fr, g_hold_now);
    if (has ? pm != 0 : fr != 0) judged++;
    if (bad) {
        slow++;
        char b[4096]; size_t o = snprintf(b, sizeof b, "[slowframe]\t%llu", f);
        __typeof__(g_sr[0]) *r = &g_sr[f % SRN];
        for (int i = 0; i < r->n && o < sizeof b - 64; i++) o += snprintf(b + o, sizeof b - o, "\t%s=%.3f", r->k[i], r->v[i]);
        double m = 0; for (uint64_t p = f >= 3 ? f - 3 : 0; p < f; p++) { double x = srow(p, "@frame_ms", 0); if (x > m) m = x; }
        snprintf(b + o, sizeof b - o, "\t@prev3_max_frame_ms=%.3f", m);
        fprintf(stderr, "%s\n", b);
    }
    if (f % 300 == 0) fprintf(stderr, "[slowframe]\t#\tup to frame %llu\tjudged=%llu\tslow=%llu\tcriterion=%s\n", f, judged, slow, has ? "on-screen present interval" : "present call interval (no window)");
}
// Declaration for the time-constraint (real-time) thread: period = the frame rate cap's period (rt_cap_period; nvn_pace.h nvn_rt_*), constraint = period,
// computation = the CPU time this thread actually needs per frame (the game main thread: game logic + recording + submit, about 10 ms a frame when hot).
// Exceeding the computation does not demote the thread (XNU: it ends its deadline for that period); whether the performance controller sets clocks from
// the declaration is not documented. DM_RT_COMP=<ms> fixes the declaration; default auto = every 120 frames 1.25 x the p90 of this thread's per-frame CPU
// time, re-declared when it differs from the declared value by > 15% (capped at 0.9 × period), kept across period changes.
static void set_time_constraint_comp(uint64_t period_ns, uint64_t comp_ns) {
    if (comp_ns < 1000000ull) comp_ns = 1000000ull; if (comp_ns > period_ns * 9 / 10) comp_ns = period_ns * 9 / 10;
    int ok = dm_self_deadline(period_ns, comp_ns);   // (plat.h)
    if (ok) { g_rt_comp_ms = comp_ns / 1e6; g_rt_period_ms = period_ns / 1e6; }
    LOG("[nvn] present thread time-constraint policy (period %.3f ms, computation %.3f ms) %s", period_ns / 1e6, comp_ns / 1e6, ok ? "set" : "failed to set");
}
static int rt_comp_fixed_ms(void) { static int v = -2; if (v == -2) { const char *e = getenv("DM_RT_COMP"); v = e && *e && strcmp(e, "auto") ? atoi(e) : -1; } return v; }   // −1 = auto
static void set_time_constraint(uint64_t period_ns) { int f = rt_comp_fixed_ms(); uint64_t fx = f > 0 ? (uint64_t)f * 1000000ull : 0;
    set_time_constraint_comp(period_ns, rt_old() ? (fx ? fx : 2000000ull) : nvn_rt_comp_on_period(fx, (uint64_t)(g_rt_comp_ms * 1e6), period_ns)); }   // first time: the fixed value, or auto's starting value of 2 ms
// Called every frame at acquire: records this thread's CPU time since the last call; in auto mode re-declares based on p90
static void rt_comp_tick(uint64_t period_ns) {
    if (rt_comp_fixed_ms() > 0 || !period_ns || !g_rt_comp_ms) return;
    static uint64_t last; static nvn_rt_win w;
    uint64_t c = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID); if (!last) { last = c; return; }
    uint64_t v = nvn_rt_sample(&w, (double)(c - last) / 1e6, g_rt_comp_ms, period_ns, rt_peak()); last = c;
    if (v) set_time_constraint_comp(period_ns, v);
}
static void sleep_until_ns(uint64_t dl) {   // absolute time on the monotonic clock
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    uint64_t now = now_ns(); if (dl <= now) return;
    mach_wait_until(mach_absolute_time() + (dl - now) * tb.denom / tb.numer);
}
// Present is fixed at 60 Hz: the adaptive mechanism stepped down at frame 60 in 24/24 real-device logs and never came back up, so for this phase 60 fps is the hard floor.
// DM_PRESENT_HZ=30..120 sets the rate one present interval stands for (the display link ticks at it): only for a game whose step follows the measured
// frame time (Tears of the Kingdom with its real-time frame step, games/totk/frame_step.c); a game that steps per frame runs at rate / 60 speed.
// DM_PRESENT_HZ=0 (uncapped frame rate) serves only as an explicit performance profiling switch: some of the game's UI logic counts frames (with the same save, uncapped gives
// 4181 SaveIndicatorPanel entries and capped at 60 fps gives 0, which matches the ground truth), so uncapped is a semantically wrong way to run and must not be used as a criterion / cross-check / default. Unit tests set it directly with nvn_present_config
static int g_hz = -1, g_display_mode;
void nvn_present_config(int hz, int display_mode) { g_hz = hz; g_display_mode = display_mode; }
static void present_cfg(const win_t *o) {
    if (g_hz >= 0) return;
    // With a window the default is still display interval mode; without a window the default is game mode. Environment variables can override the fixed 60 Hz.
    int scr = mtl_display_hz();
    const char *e = getenv("DM_PRESENT_HZ"); g_hz = e && !fstep_on() ? atoi(e) : 60;   // (the vsync step counts 60 Hz vsyncs)
    if (g_hz > 0 && (g_hz < 30 || g_hz > 120)) { LOG("[nvn] [red] DM_PRESENT_HZ=%d outside 30..120: 60", g_hz); g_hz = 60; }
    if (g_hz > 0) { void mtl_link_set_rate(int hz); mtl_link_set_rate(g_hz); }
    if (!fstep_on()) { int uc = nvn_uncapped_hz(dm_game_realtime_step(), e, getenv("DM_UNCAPPED"), getenv("DM_UNCAPPED_HZ"), scr);
      if (uc) { g_uncapped = 1; g_hz = uc; void mtl_link_set_rate(int hz); mtl_link_set_rate(scr > 0 ? scr : 60);
          LOG("[nvn] uncapped pacing (DM_UNCAPPED=0 turns it off): the game does not wait for the display, our clock paces it at up to %d fps (DM_UNCAPPED_HZ); "
              "the display (%d Hz) shows the newest finished frame at each refresh", uc, scr); } }
    if (!g_hz) LOG("[nvn] [warning] DM_PRESENT_HZ=0: uncapped frame rate, frame pacing differs from a real device and UI logic may diverge (for performance profiling only; not valid for criteria / cross-checks)");
    // Cut (intentional deviation from the original game's behavior): with DM_PRESENT_INTERVAL=display the present interval set by the game is not followed and one frame is presented per display-link tick,
    // with a fixed hard floor of 60 fps; the logic stays at 25 ticks, and the client's frame-counted UI logic is unaffected -
    // with the same save and the same input, the UI message streams of the two modes match message by message. Proof (before character select): r9 game / display 60 ×3 / display 120 ×2, 6 groups in total, uicmp consistent
    // (the save indicator is exempted as a timing race), and the timing differences of key messages are within game mode's own noise band; proof after entering a run is still missing, pending action script coverage
    // A game whose step is per frame (Tears of the Kingdom: its controller picks interval 2 or 3 and every frame advances 1/30 or 1/20 s) runs twice
    // as fast in display mode: game.json present_interval "game" keeps the game's interval with a window too
    const char *m = getenv("DM_PRESENT_INTERVAL"); g_display_mode = m ? !strcmp(m, "display") : scr > 0 && !dm_game.present_game_interval;
    LOG("[nvn] present rate %d Hz, interval mode %s (game-set present interval %d; %s)", g_hz, g_display_mode ? "display (one frame per tick, cut)" : "game (original semantics)", o->interval,
        scr > 0 ? (dm_game.present_game_interval && !m ? "window, game.json present_interval game" : "window") : "no window");
    // Which queued frame the display link shows (plan item 140): the newest, or every frame in order for a game that steps once per frame (game.json
    // present_consume "fifo": manosaba). Uncapped pacing shows the newest by design. DM_PRESENT_CONSUME=newest|fifo overrides
    { const char *c = getenv("DM_PRESENT_CONSUME"); int fifo = c ? !strcmp(c, "fifo") : dm_game.present_fifo;
      if (g_uncapped) fifo = 0;
      void mtl_link_set_fifo(int on); mtl_link_set_fifo(fifo);
      LOG("[nvn] the display link shows %s (%s)", fifo ? "every frame in order, each for its present period" : "the newest frame", c ? "DM_PRESENT_CONSUME" : "game.json present_consume"); }
}
// The rate the screen runs at now (the iOS front end's display link, every tick; plan item 172): a game stepping by the measured time follows it
// between 60 and 120 on a ProMotion screen (nvn_promotion_hz). Any thread
static _Atomic int g_pro_last;
void nvn_display_rate_now(int now_hz) {
    static int disp = -1; if (disp < 0) disp = mtl_display_hz();
    if (g_hz < 0 || g_uncapped || fstep_on()) return;   // (before the first present the configuration is not made yet)
    int hz = nvn_promotion_hz(dm_game_realtime_step(), getenv("DM_PRESENT_HZ"), getenv("DM_PROMOTION"), disp, now_hz);
    if (!hz || hz == atomic_load(&g_pro_last)) return;
    atomic_store(&g_pro_last, hz); g_hz = hz; void mtl_link_set_rate(int hz); mtl_link_set_rate(hz);
    LOG("[nvn] ProMotion: the screen runs at %d Hz, presents at %d (DM_PROMOTION=0 keeps 60)", now_hz, hz);
}
static uint64_t present_period(const win_t *o) {
    present_cfg(o);
    int itv = win_interval(o), hz = g_hz;
    return hz > 0 ? (uint64_t)itv * 1000000000ull / (uint64_t)hz : 0;
}
static uint64_t rt_cap_period(const win_t *o) {   // the period the time-constraint threads declare: the cap's, without the game's own (late) interval (nvn_pace.h nvn_rt_period_ns)
    present_cfg(o);
    return nvn_rt_period_ns(g_hz, nvn_hold_interval(1, g_hz, nvn_pace_hold(atomic_load(&g_fps_hold), mtl_fps_target())));
}
int (*g_nvn_loadscreen_up)(void);   // optional game hook: is a loading screen up right now (waits for the pipeline warm-up there; marks long frames hidden behind it)
__attribute__((weak)) void dm_movie_tick(void) {}   // host hle_movie.c: hands paced movie frames over, one per game frame; none in the graphics unit tests
__attribute__((weak)) void dm_game_present(void) {}   // game hook (host/game.h): every present, on the game's thread; none in the graphics unit tests
__attribute__((weak)) void dm_sdk_test_present(uint64_t frame) { (void)frame; }   // host/diag/sdktest.c (DM_SDK_TEST); none in the graphics unit tests
__attribute__((weak)) int dm_game_realtime_step(void) { return 0; }   // game hook (host/game.h): the game's step follows the measured frame time
// Frame-rate trials (plan item 216, nvn_fptrial.h): with the player's target at 60 and nothing holding the rate, a game that picks its own interval
// from the GPU time it reads (Tears of the Kingdom's controller, rules.py fps60) and sits at interval 2 or 3 reads shorter GPU times for a while,
// so it tries 60 and the GPU's clock can come up. Only for a game whose step the host sets (its speed then holds at any interval), at a present rate
// of 60 Hz in game interval mode (not the host's frame step, not uncapped). DM_FPS_TRIAL=1 turns them on: on the iPhone 17 Pro at 60 Hz none of 22
// trials on Great Sky Island held (its sustained rate is under 60 there) and each failed one is seconds of uneven frames, so they stay off until a
// scene where they hold is measured
static nvn_fptrial g_fpt;
void mtl_ts_factor(double f);   // nvn_mtl.m: the factor on the report times of the next submissions
static int fpt_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_FPS_TRIAL"); v = e && !strcmp(e, "1") && dm_game_realtime_step();
    if (v) LOG("[fps trial] on: a game held at interval 2 or 3 for %.0f s with the target at 60 reads its GPU times x%.2f until it picks 60, then %.0f s more (DM_FPS_TRIAL=1)",
               FPT_WAIT0, FPT_FACTOR, FPT_GRACE_S); } return v; }
// DM_NVN_TS_DVFS=<x> (a diagnostic, for a Mac): report times x times further apart once frames have been paced at interval 2 or 3 (the game's own
// or a hold's) for 1 s, the real times 0.5 s after interval 1, as an iPhone's GPU clock follows the frame rate (what the trials are for)
static double dvfs_emu(int itv, double now) {
    static double x = -1, since, cur = 1; static int last = -1;
    if (x < 0) { const char *e = getenv("DM_NVN_TS_DVFS"); x = e ? atof(e) : 0; if (x > 1) LOG("[nvn] DM_NVN_TS_DVFS=%g: report times %g times apart after 1 s at interval 2 or 3 (a diagnostic)", x, x); else x = 0; }
    if (!x) return 1;
    if (itv != last) { last = itv; since = now; }
    if (itv >= 2 && now - since >= 1.0) cur = x; else if (itv < 2 && now - since >= 0.5) cur = 1;
    return cur;
}
static void fpt_present(const win_t *o) {
    double now = now_ns() / 1e9, f = 1;
    if (fpt_on()) {
        int want = g_hz == 60 && !g_display_mode && !g_uncapped && !fstep_on() && mtl_fps_target() >= 60 && !atomic_load(&g_fps_hold);
        nvn_fptrial was = g_fpt;
        f = nvn_fptrial_step(&g_fpt, now, o->interval, want);
        static unsigned logs;
        if (g_fpt.st != was.st && (logs++ < 64 || logs % 32 == 0)) {
            if (g_fpt.st == FPT_PUSH) LOG("[fps trial] %llu: interval %d for %.0f s with the target at 60: GPU times x%.2f until the game picks 60", (unsigned long long)g_fpt.trials, o->interval, now - was.since, FPT_FACTOR);
            else if (g_fpt.st == FPT_GRACE) LOG("[fps trial] interval 1 after %.1f s: x%.2f for %.0f s more while the GPU clock comes up", now - was.since, FPT_FACTOR, FPT_GRACE_S);
            else if (g_fpt.st == FPT_WATCH) LOG("[fps trial] the real GPU times again: held if the game stays at 60 for %.0f s", FPT_WATCH_S);
            else if (g_fpt.held != was.held) LOG("[fps trial] held: 60 fps on the real times (%llu held, %llu failed of %llu trials)", (unsigned long long)g_fpt.held, (unsigned long long)g_fpt.failed, (unsigned long long)g_fpt.trials);
            else if (g_fpt.failed != was.failed) LOG("[fps trial] failed (%s): next trial after %.0f s at interval 2 or 3 (%llu held, %llu failed of %llu trials)",
                g_fpt.unlowered != was.unlowered ? "the game did not pick interval 1" : was.st == FPT_GRACE ? "back up on the shorter times: its CPU time" : "back up on the real times", g_fpt.wait,
                (unsigned long long)g_fpt.held, (unsigned long long)g_fpt.failed, (unsigned long long)g_fpt.trials);
            else LOG("[fps trial] stopped: the target or a hold changed");
        }
    }
    mtl_ts_factor(f * dvfs_emu(win_interval(o), now));
}
NVN(nvnQueuePresentTexture, void, NVNqueue *q, NVNwindow *w, int idx) {
    uint64_t tr = trace_now();
    if (fstep_on()) {   // the step for the next frame from this one's time on screen and cost (nvn_fstep.h), before the game's hook stores it
        const double vs = 1e9 / 60; uint64_t now = now_ns();
        double shown = g_fs_prev_start && g_fs_start > g_fs_prev_start ? (g_fs_start - g_fs_prev_start) / vs : 0, cpu = g_fs_start && now > g_fs_start ? (now - g_fs_start) / vs : 0;
        double gpu = g_frame >= 8 ? mtl_gpu_ms(g_frame - 8) : -1, cost = gpu * 1e6 / vs > cpu ? gpu * 1e6 / vs : cpu;
        if (shown > 0) { g_fs_game += g_fs_step; g_fs_real += shown; }
        g_fs_step = nvn_fstep_next(&g_fs, shown, cost, nvn_hold_interval(1, 60, nvn_pace_hold(atomic_load(&g_fps_hold), mtl_fps_target())), FSTEP_MAX);
        g_fs_started = 0;
        if (g_fs.frames % 300 == 0) LOG("[frame step] vsync step at frame %llu: k %d, step %d; %llu late, %llu up, %llu down (%llu failed), %llu paid back, %.0f vsyncs dropped; game time / real time %.4f",
            (unsigned long long)g_fs.frames, g_fs.k, g_fs_step, (unsigned long long)g_fs.late, (unsigned long long)g_fs.ups, (unsigned long long)g_fs.downs, (unsigned long long)g_fs.failed,
            (unsigned long long)g_fs.paid, g_fs.dropped, g_fs_real > 0 ? g_fs_game / g_fs_real : 0);
    }
    dm_game_present(); dm_sdk_test_present(g_frame);
    dm_movie_tick();   // before the core is given away: it signals the game's movie events (guest code)
    NVN_BLOCKING();
    (void)q; win_t *o = (win_t *)w; o->presents++;
    static uint64_t prev_in, prev_out, prev_pass, prev_draw;
    // Loading screen on first entering a run: if the background pipeline warm-up has not finished, wait for it here (the loading screen stays a bit longer), so that after entering the run no pipelines are compiled on the fly on the submit thread. When warm-up finished long ago this passes with a single check
    { static int gate;
      if (!gate && g_nvn_loadscreen_up && g_nvn_loadscreen_up()) { gate = 1; int ms = mtl_warm_wait(15000); if (ms) LOG("[nvn] run-entry loading screen waited %d ms for pipeline warm-up", ms); } }
    present_cfg(o);
    fpt_present(o);   // frame-rate trials (plan item 216): the report times' factor for the submissions from here on
    { static long sf = -1; static double ss; if (sf < 0) { const char *e = getenv("DM_TEST_PRESENT_STALL"); sf = 0; if (e) { sf = atol(e); const char *c = strchr(e, ':'); ss = c ? atof(c + 1) : 8; } }
      if (sf > 0 && g_frame == (uint64_t)sf) { LOG("DM_TEST_PRESENT_STALL: present held for %.1f s at frame %llu", ss, (unsigned long long)g_frame); usleep((useconds_t)(ss * 1e6)); } }
    { static long ha = -1, hb; static int hf;   // DM_TEST_FPS_HOLD=<from>:<to>:<fps>: the frame-rate hold of plan item 130 between two frames, without a movie
      if (ha < 0) { const char *e = getenv("DM_TEST_FPS_HOLD"); ha = 0; if (e && sscanf(e, "%ld:%ld:%d", &ha, &hb, &hf) != 3) ha = 0; }
      if (ha > 0 && g_frame == (uint64_t)ha) nvn_fps_hold(hf);
      if (ha > 0 && g_frame == (uint64_t)hb) nvn_fps_hold(0); }   // a freeze on purpose (host/runtime/crash.c's frames-stopped watch)
    gfx_enter();
    uint64_t t0 = now_ns();
    if (xlat_on()) {   // a job after the frame's submissions (plan item 61): ticket[idx] holds the job, rfence[idx] the token of the frame's last submission
        if (idx >= 0 && idx < o->ntex && idx < 4) { xjob j = {.kind = XJ_PRESENT, .tex = CTX(o->tex[idx]), .period = present_period(o) / 1e9}; memcpy(j.crop, o->crop, sizeof j.crop);
            pthread_mutex_lock(&g_xord_mu); o->ticket[idx] = x_push(&j); o->rfence[idx] = x_token() | SYNC_TOKEN; pthread_mutex_unlock(&g_xord_mu); }
    } else {
    gfx_mt_enter("PresentTexture");
    if (idx >= 0 && idx < o->ntex && idx < 4) { o->ticket[idx] = mtl_present(CTX(o->tex[idx]), present_period(o) / 1e9, o->crop); o->rfence[idx] = mtl_last_fence(); }   // enqueue only, return immediately; the frame's work is committed (QueueSubmitCommands replays and commits at once)
    gfx_mt_leave();
    }
    gfx_leave();
    atomic_fetch_add(&g_nvn_ns, now_ns() - t0);
    trace_end(TR_PRESENT, tr, (uint64_t)idx, g_frame);
    uint64_t f = g_frame, ps, dr; mtl_stats(&ps, &dr);
    if (nvn_real[0] || nvn_real[1]) {   // DM_NVN_PROF: record phase = time of all implemented functions except submit; submit phase = QueueSubmitCommands
        static unsigned isub = ~0u; static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
        if (isub == ~0u) for (isub = 0; isub < NVN_NFUNC && strcmp(nvn_names[isub], "nvnQueueSubmitCommands"); isub++) {}
        uint64_t rec = 0, sub = 0;
        static uint64_t acc[NVN_NFUNC];
        for (unsigned i = 0; i < NVN_NFUNC; i++) { uint64_t t = atomic_exchange(&nvn_prof_ticks[i], 0); acc[i] += t; if (i == isub) sub = t; else rec += t; }
        if ((g_frame + 1) % TWIN == 0) {   // every 300 frames list the 5 most time-consuming functions
            char line[1024]; size_t o = 0;
            for (int k = 0; k < 5; k++) { unsigned best = 0; for (unsigned i = 1; i < NVN_NFUNC; i++) if (acc[i] > acc[best]) best = i;
                if (!acc[best]) break; o += snprintf(line + o, sizeof line - o, " %s %.2f ms;", nvn_names[best] + 3, acc[best] * tb.numer / tb.denom / 1e6); acc[best] = 0; }
            LOG("[nvn] translation layer top 5 by time (last 300 frames total):%s", line); memset(acc, 0, sizeof acc);
        }
        trow(g_frame, "@rec_ms", rec * tb.numer / tb.denom / 1e6); trow(g_frame, "@submit_ms", sub * tb.numer / tb.denom / 1e6);
        tpush(4, rec * tb.numer / tb.denom / 1e6); tpush(5, sub * tb.numer / tb.denom / 1e6);
    }
    if (prev_in) {
        double fr = (t0 - prev_in) / 1e6, wk = (t0 - prev_out) / 1e6, nv = atomic_exchange(&g_nvn_ns, 0) / 1e6;
        { static double prev_hold = 1000.0 / 60; g_tw0_hold[g_tn[0] % TWIN] = nvn_expect_ms(prev_hold, g_hold_now); prev_hold = g_hold_now; }
        tpush(0, fr); tpush(1, wk); tpush(2, nv);
        perfline_frame(&g_perf, t0);
        trow(f, "@frame_ms", fr); trow(f, "@work_ms", wk); trow(f, "@nvn_ms", nv);
        // Slow-frame attribution (taken only when DM_NVN_TSV is on): this frame's present-thread CPU time (−1 if the thread changed), CPU of all threads in the process, acquire wait accumulated per frame, present call time (same base as the GPU / on-screen present times: ms since process start)
        trow(f, "@wait_ms", g_acq_wait_ns / 1e6); trow(f, "@pq_wait_ms", g_pq_wait_ns / 1e6);
        if (frame_rec()) {
            static pthread_t pth; static uint64_t pcpu, pproc;
            uint64_t c = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID); pthread_t th = pthread_self();
            trow(f, "@cpu_ms", pcpu && pthread_equal(th, pth) ? (c - pcpu) / 1e6 : -1); pth = th; pcpu = c;
            struct rusage ru; getrusage(RUSAGE_SELF, &ru);
            uint64_t pc = (uint64_t)(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000000000ull + (uint64_t)(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) * 1000ull;
            if (pproc) trow(f, "@proc_cpu_ms", (pc - pproc) / 1e6); pproc = pc;
            trow(f, "@call_ms", (t0 - g_t0_ns) / 1e6);
        }
        trow(f, "@passes", (double)(ps - prev_pass)); trow(f, "@draws", (double)(dr - prev_draw));
        static uint64_t pl, pst, pf, pudc; uint64_t l, st, fu; mtl_bw(&l, &st, &fu);
        trow(f, "@rt_load_mb", (l - pl) / 1048576.0); trow(f, "@rt_store_mb", (st - pst) / 1048576.0); trow(f, "@pass_full_rw", (double)(fu - pf));
        uint64_t udc = mtl_udc_saved_bytes(); trow(f, "@unwritten_dc_mb", (udc - pudc) / 1048576.0);
        pl = l; pst = st; pf = fu; pudc = udc;
        static uint64_t pae, par; uint64_t ae_, ar_; mtl_args_stats(&ae_, &ar_);
        trow(f, "@args_enc", (double)(ae_ - pae)); trow(f, "@args_reuse", (double)(ar_ - par)); pae = ae_; par = ar_;
        static uint64_t puc, pus; uint64_t uc_, us_; mtl_use_stats(&uc_, &us_);
        static uint64_t pdo; uint64_t do_ = mtl_ds_omit(); trow(f, "@ds_omit", (double)(do_ - pdo)); pdo = do_;
        trow(f, "@use_call", (double)(uc_ - puc)); trow(f, "@use_skip", (double)(us_ - pus)); puc = uc_; pus = us_;
        static uint64_t pfa, pre_; uint64_t fa_, pr_, up_, re_; nvn_dirty_stats(&fa_, &pr_); mtl_pkg_stats(&up_, &re_);
        trow(f, "@faults", (double)(fa_ - pfa)); trow(f, "@reuploads", (double)(re_ - pre_)); pfa = fa_; pre_ = re_;
        static uint64_t pmc, pmp, pmn; uint64_t mc_, mpg_, mn_; nvn_dirty_mp_stats(&mc_, &mpg_, &mn_);
        trow(f, "@mprotect_calls", (double)(mc_ - pmc)); trow(f, "@mprotect_pages", (double)(mpg_ - pmp)); trow(f, "@mprotect_ms", (mn_ - pmn) / 1e6); pmc = mc_; pmp = mpg_; pmn = mn_;
        double up_ms = mtl_upload_ns_take() / 1e6; trow(f, "@upload_ms", up_ms);
        trow(f, "@upload_gpu_ms", mtl_upload_gpu_ns_take() / 1e6);
        // Accounting: number of synchronous creations on the submit thread this frame + a timestamp on the same clock as UIMSG (offline, frames are split into front end / loading / in-run by the loading screen intervals)
        static uint64_t plib, ppso; uint64_t lib_, pso_; mtl_create_stats(&lib_, &pso_);
        trow(f, "@lib_new", (double)(lib_ - plib)); trow(f, "@pso_new", (double)(pso_ - ppso));
        {   // long-frame log: this frame's items are first recorded into the ring; whether to print is decided 8 frames later, once the GPU time has arrived (see below)
            static uint64_t plns, ppns, pup, prup, pub; uint64_t lns, pns, up, rup, ub; mtl_create_ns(&lns, &pns); mtl_pkg_stats(&up, &rup); ub = mtl_upload_bytes();
            lf_rec *r = &g_lf[f % LFN]; r->f = f; r->fr = fr; r->nv = nv; r->ls = g_nvn_loadscreen_up ? g_nvn_loadscreen_up() : 0; r->t_s = (now_ns() - g_t0_ns) / 1e9; r->pso = (unsigned)(pso_ - ppso); r->lib = (unsigned)(lib_ - plib);
            r->make_ms = ((lns - plns) + (pns - ppns)) / 1e6; r->up = (unsigned)(up - pup); r->rup = (unsigned)(rup - prup); r->up_mb = (ub - pub) / 1048576.0; r->up_ms = up_ms;
            plns = lns; ppns = pns; pup = up; prup = rup; pub = ub;
        }
        plib = lib_; ppso = pso_;
        trow(f, "@t_s", (now_ns() - g_t0_ns) / 1e9);
        trow(f, "@drawable_ms", mtl_drawable_ns_take() / 1e6);
        trow(f, "@sub_wall_ms", atomic_exchange(&g_sub_wall_ns, 0) / 1e6); trow(f, "@sub_cpu_ms", atomic_exchange(&g_sub_cpu_ns, 0) / 1e6); trow(f, "@sub_instr", (double)atomic_exchange(&g_sub_instr, 0));
    } else atomic_store(&g_nvn_ns, 0);
    g_acq_wait_ns = g_pq_wait_ns = 0;
    if (f >= 8) { double g = mtl_gpu_ms(f - 8); if (g >= 0) { tpush(3, g); perfline_gpu(&g_perf, g); trow(f - 8, "@gpu_ms", g); trow(f - 8, "@gpu_sum_ms", mtl_gpu_sum_ms(f - 8)); g_hr[g_hn++ % HRN] = g; }   // GPU time is taken 8 frames late (already completed)
        { lf_rec *r = &g_lf[(f - 8) % LFN];   // long-frame log (on by default, DM_LONGFRAME=0 turns it off): a line is printed only when the whole frame > 20 ms or GPU > 16.7 ms, and it is clearly above recent conditions
          static int on = -1; if (on < 0) { const char *e = getenv("DM_LONGFRAME"); on = !(e && !strcmp(e, "0")); }
          // "Long" is relative to recent conditions: when running steadily at 30 fps (menus / windowless host) or under sustained overload, every frame is > 20 ms; those are not long frames and must not flood the log - a frame has to be 40% above the sliding mean of the last 16 frames to count
          static double ef = 16.7, eg = 8; int lf = r->fr > 20.0 && r->fr > 1.4 * ef, lg = g > 16.7 && g > 1.3 * eg;
          if (r->f == f - 8) { ef += ((r->fr < 3 * ef ? r->fr : 3 * ef) - ef) / 16; eg += ((g < 3 * eg ? g : 3 * eg) - eg) / 16; }
          if (on && r->f == f - 8 && f - 8 > 0 && (lf || lg)) { char b[512];
              snprintf(b, sizeof b, "frame %llu (%.2f s) whole frame %.1f ms, submit+present %.1f ms, GPU %.1f ms; new pipelines %u + shader libraries %u, %.1f ms total; texture uploads: first %u + re-uploads %u, %.1f MB / %.1f ms",
                       (unsigned long long)r->f, r->t_s, r->fr, r->nv, g, r->pso, r->lib, r->make_ms, r->up, r->rup, r->up_mb, r->up_ms);
              // Visibility: during the time a long frame "comes out late", what stays on screen is the previous frame; if the previous frame was a loading screen (or this frame itself is one), all the player sees is the loading screen staying a bit longer, with no motion interrupted
              const lf_rec *pv = &g_lf[(f - 9) % LFN]; int hidden = r->ls || (f >= 9 && pv->f == f - 9 && pv->ls);
              if (hidden) strlcat(b, "; behind loading screen, not visible", sizeof b);
              dm_longframe_note(b); } }
        uint64_t g0, g1; if (frame_rec() && mtl_gpu_span(f - 8, &g0, &g1)) { trow(f - 8, "@gpu_beg_ms", (g0 - g_t0_ns) / 1e6); trow(f - 8, "@gpu_end_ms", (g1 - g_t0_ns) / 1e6); } }
    prev_in = t0; prev_pass = ps; prev_draw = dr;
    dump_frame();
    if (g_frame % TWIN == 0) tsummary(g_frame);
    if (xlat_on()) { xjob j = {.kind = XJ_FRAME, .frame = g_frame, .period = (rt_old() ? present_period(o) : rt_cap_period(o)) / 1e9}; pthread_mutex_lock(&g_xord_mu); x_push(&j); pthread_mutex_unlock(&g_xord_mu); }   // in order with the replays (plan item 61)
    else mtl_set_frame(g_frame);
    // Real-device semantics: present only enqueues and returns immediately; this frame's display time is scheduled by the pacer, and the texture can be acquired again only at "display time + one period" (blocking in AcquireTexture)
    uint64_t period = present_period(o), now = dm_detclock_enabled() ? guest_ns() : now_ns();
    g_hold_now = nvn_hold_ms(period); g_hold_ring[o->presents % 16] = g_hold_now;   // this present's hold (plan item 156)
    if (test_work_ms() > 0) atomic_store(&g_test_work_from, now_ns());
    nvn_pacer pc = {o->pace};
    uint64_t slot = now + nvn_pace(&pc, now, period);
    o->pace = pc.target;
    if (idx >= 0 && idx < 4) o->avail[idx] = period ? slot + period : 0;
    static uint64_t prev_slot; if (prev_slot && period) trow(f, "@display_ms", (slot - prev_slot) / 1e6); prev_slot = slot;   // display interval between two adjacent frames on screen (this is the one to look at for dropped frames)
    // Actual presentation interval (windowed mode): taken 8 presents late (the presented callback has arrived by then), as the difference between the n-th and the (n−1)-th; −1 = discarded
    if (o->presents > 9) { uint64_t n = o->presents - 8; double a_ = mtl_presented_time(n), b_ = mtl_presented_time(n - 1);
        if (a_ < 0) trow(f - 8, "@presented_ms", -1); else if (a_ > 0 && b_ > 0) trow(f - 8, "@presented_ms", (a_ - b_) * 1e3);
        g_pres_exp[g_presn % TWIN] = g_pres_exp_last = nvn_expect_ms(g_hold_ring[(n - 1) % 16], g_hold_ring[n % 16]);   // judged against its own presents' holds
        g_pres_iv[g_presn++ % TWIN] = a_ < 0 ? -1 : (a_ > 0 && b_ > 0 ? (a_ - b_) * 1e3 : 0);
        perfline_screen(&g_perf, a_ > 0 ? (uint64_t)(a_ * 1e9) : 0);
        { double iv_ = a_ < 0 ? -1 : (a_ > 0 && b_ > 0 ? (a_ - b_) * 1e3 : 0); mtl_ub_present(iv_); }   // same-process alternating measurement (DM_NVN_UB) tallies on-screen present intervals per phase; when off it is just one atomic read
        dm_perf_present(a_ < 0 ? -1 : (a_ > 0 && b_ > 0 ? (a_ - b_) * 1e3 : 0));   // on-screen present intervals are also kept in a separate ring; tsummary reports dropped frames every 300 frames (M1 criterion)
        if (a_ > 0) trow(f - 8, "@pres_at_ms", a_ * 1e3 - g_t0_ns / 1e6); }   // on-screen present time (ms since process start; presentedTime shares a time base with mach_absolute_time)
    if (f >= 8 && slow_on()) slow_emit(f - 8);
    prev_out = now_ns();
}
