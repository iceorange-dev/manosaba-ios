// gfx/nvn/hle/window.inc, part of hle_nvn.c (included there in order, one translation unit): Windows: building them, their crop and present interval, acquiring their images
// ---------- Window / present ----------
#define WB(b) ((win_builder_t *)(b))
NVN(nvnWindowBuilderSetDefaults, void, NVNwindowBuilder *b) { NVNdevice *dev = WB(b)->dev; memset(b, 0, sizeof(win_builder_t)); WB(b)->dev = dev; WB(b)->interval = 1; }
NVN(nvnWindowBuilderSetDevice, void, NVNwindowBuilder *b, NVNdevice *d) { WB(b)->dev = d; }
NVN(nvnWindowBuilderSetNativeWindow, void, NVNwindowBuilder *b, NVNnativeWindow w) { WB(b)->native = w; }
NVN(nvnWindowBuilderSetTextures, void, NVNwindowBuilder *b, int n, NVNtexture *const *t) {
    if (n < 0 || n > 4) { LOG("[red] WindowBuilderSetTextures: count %d", n); n = n < 0 ? 0 : 4; }
    WB(b)->ntex = n; for (int i = 0; i < n; i++) WB(b)->tex[i] = t[i];
}
NVN(nvnWindowBuilderSetPresentInterval, void, NVNwindowBuilder *b, int v) { WB(b)->interval = v; }
// No prototype in the header. Signature from the call site main+0x9c9218 (2.0.4b): x0 = builder, w1 = count (ldr w1), return value unused
NVN_NOPROTO(nvnWindowBuilderSetNumActiveTextures, void, NVNwindowBuilder *b, int n) { WB(b)->nactive = n; }
NVN(nvnWindowInitialize, NVNboolean, NVNwindow *w, const NVNwindowBuilder *b) {
    const win_builder_t *s = (const win_builder_t *)b; win_t *o = (win_t *)w;
    memset(o, 0, sizeof *o); o->magic = MAGIC('W', 'I', 'N', 'D'); o->native = s->native; o->ntex = s->ntex; o->interval = s->interval;
    o->nactive = s->nactive > 0 && s->nactive <= s->ntex ? s->nactive : 0;
    if (s->nactive && !o->nactive) LOG("[red] WindowInitialize: active texture count %d exceeds the %d-texture swap chain, using all", s->nactive, s->ntex);
    memcpy(o->tex, s->tex, sizeof o->tex);
    for (int i = 0; i < o->ntex; i++) if (o->tex[i]) mtl_xv_window_tex(CTX(o->tex[i])->serial);
    LOG("[nvn] WindowInitialize swap chain %d textures, interval %d, first %dx%d format %#x", o->ntex, o->interval,
        o->ntex ? (int)CTX(o->tex[0])->d.width : 0, o->ntex ? (int)CTX(o->tex[0])->d.height : 0, o->ntex ? CTX(o->tex[0])->d.format : 0);
    return 1;
}
NVN(nvnWindowGetPresentInterval, int, const NVNwindow *w) { return ((const win_t *)w)->interval; }
__attribute__((weak)) void dm_backtrace(uint64_t fp) { (void)fp; }   // host crash.c (guest frame chain); none in the graphics unit tests
// The caller is logged with the change: Tears of the Kingdom's frame-rate controller picks 2 (30 fps) or 3 (20 fps) itself
NVN(nvnWindowSetPresentInterval, void, NVNwindow *w, int v) {
    if (((win_t *)w)->interval != v) { LOG("[nvn] game set present interval %d → %d", ((win_t *)w)->interval, v); dm_backtrace((uint64_t)__builtin_frame_address(0)); }
    ((win_t *)w)->interval = v;
}
NVN(nvnWindowSetCrop, void, NVNwindow *w, int x, int y, int ww, int hh) {
    win_t *o = (win_t *)w; int v[4] = {x, y, ww, hh};
    if (memcmp(o->crop, v, sizeof v)) LOG("[nvn] WindowCrop %d,%d %dx%d", x, y, ww, hh);
    memcpy(o->crop, v, sizeof v);
    dm_touch_screen(ww, hh);
}
NVN(nvnWindowGetCrop, void, const NVNwindow *w, NVNrectangle *r) { const win_t *o = (const win_t *)w; *r = (NVNrectangle){o->crop[0], o->crop[1], o->crop[2], o->crop[3]}; }   // (plan item 64a)
static void trow(uint64_t f, const char *name, double v);
static void set_time_constraint(uint64_t period_ns);
static void rt_comp_tick(uint64_t period_ns);
static double g_rt_comp_ms;   // Currently declared real-time computation amount (ms; 0 = not declared)
static double g_rt_period_ms;
static void sleep_until_ns(uint64_t dl);
static uint64_t present_period(const win_t *o), rt_cap_period(const win_t *o);
__attribute__((weak)) void dm_main_yield_stats(uint64_t *n, uint64_t *ns) { *n = 0; *ns = 0; }   // The host's hle_thread.c provides the ground truth; 0 in graphics unit tests
__attribute__((weak)) uint32_t dm_guest_work_qos(unsigned *count) { if (count) *count = 0; return 0; }   // (QOS_CLASS_UNSPECIFIED)
__attribute__((weak)) int dm_guest_heap_stat(unsigned long long *t, unsigned long long *f, unsigned long long *m) { (void)t; (void)f; (void)m; return 0; }   // a game's main heap readout (D2R's hook had it; none now)
extern uint32_t mtl_present_qos(void);
static uint64_t g_frame;
// The GPU work of the frame last drawn into a swapchain image, at its acquire (plan item 142, after blade2-mobile 43e760e hle_nvn.c, MIT): on the
// console an image comes back only after the display let go of it, long after that frame's GPU work, and a game that recycles per-frame memory by
// frame count relies on that (blade2: UE4's rings, black frames without the wait). Our present only enqueues. Counted: acquires, and those that
// return before that work is done; DM_NVN_ACQ_GPU=wait waits for it there. Unit test gfx/tests/t_nvn.c 6g
static uint64_t g_acq_n, g_acq_early, g_acq_gpu_ns;
static int g_acq_gpu = -1;   // 1: wait (DM_NVN_ACQ_GPU=wait); -1 until read (t_nvn sets it)
static int acq_gpu_wait(void) { if (g_acq_gpu < 0) { const char *e = getenv("DM_NVN_ACQ_GPU"); g_acq_gpu = e && !strcmp(e, "wait"); if (g_acq_gpu) LOG("[nvn] DM_NVN_ACQ_GPU=wait: a swapchain acquire waits for the GPU work of the frame last drawn into its image"); } return g_acq_gpu; }
static uint64_t g_acq_wait_ns, g_pq_wait_ns;   // Accumulated display-link wait / wait for the present queue to return inside acquire, recorded per frame at present (there can be several acquires within one frame)
uint64_t dm_nvn_frame_count(void) { return g_frame; }
extern uint64_t g_t0_ns;   // Host process start point (main.c; same base as the UIMSG timestamps)
static int g_display_mode;   // present_cfg (tentative definition: the variable is defined with the present rate further down)
static int g_uncapped;       // present_cfg: our own clock paces the game (nvn_uncapped_hz), not the display link
static int g_hz;             // present_cfg: the present rate (tentative definition, as g_display_mode)
// The frame-rate hold (plan item 130): host/hle/hle_oe.c holds the game at a rate while it extends the user inactivity time (Tears of the Kingdom's
// pre-rendered movies at 30 fps); every pacing interval goes through nvn_hold_interval (gfx/nvn/nvn_pace.h). 0 = no hold
static _Atomic int g_fps_hold;
void nvn_fps_hold(int fps) {
    if (fps < 0) fps = 0;
    int old = atomic_exchange(&g_fps_hold, fps);
    if (old != fps) { if (fps) LOG("[nvn] frame rate held at %d fps (the game extends the user inactivity time)", fps); else LOG("[nvn] frame rate hold released"); }
}
// The frame step in whole vsyncs (plan item 133, nvn_fstep.h): DM_FRAME_STEP=vsync for a game whose step the host sets (dm_game_realtime_step):
// each frame stays on screen k vsyncs of 60 Hz, the game gets k (k + 1 while it pays back lost time) through nvn_fstep_step(), the game's own
// present interval is not used for pacing and uncapped pacing is off. Off by default: the realtime step follows the measured frame time (frame_step.c)
static nvn_fstep g_fs; static int g_fs_step; static uint64_t g_fs_start, g_fs_prev_start; static int g_fs_started; static double g_fs_game, g_fs_real;
int dm_game_realtime_step(void);   // (its weak default is below)
static int fstep_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_FRAME_STEP"); v = e && !strcmp(e, "vsync") && dm_game_realtime_step();
    if (e && !strcmp(e, "vsync") && !v) LOG("[nvn] [red] DM_FRAME_STEP=vsync needs a game whose step the host sets (Tears of the Kingdom's option realtime)");
    const char *o = getenv("DM_FSTEP_OPT"); g_fs.opt = !(o && !strcmp(o, "0"));   // trials whatever the cost, with a grace period (plan item 222)
    if (v) LOG("[nvn] frame step in whole vsyncs: each frame shown k vsyncs of 60 Hz, the game steps k (DM_FRAME_STEP=vsync)%s",
               g_fs.opt ? "; a trial of k - 1 every 5 s or more whatever the cost, its first second not counted late (DM_FSTEP_OPT=0 turns that off)" : ""); } return v; }
int nvn_fstep_step(void) { return fstep_on() ? g_fs_step : 0; }   // the step for the frame the game is about to make, in vsyncs (0 = not in this mode)
int mtl_fps_target(void);   // nvn_mtl.m: the player's frame rate target (DM_FPS, the iOS setting, 135's thermal hold)
static int win_interval(const win_t *o) {
    if (fstep_on()) return g_fs.k > 0 ? g_fs.k : 1;
    return nvn_hold_interval(g_display_mode ? 1 : o->interval, g_hz, nvn_pace_hold(atomic_load(&g_fps_hold), mtl_fps_target())); }   // (plan item 135b)
NVN(nvnWindowAcquireTexture, NVNwindowAcquireTextureResult, NVNwindow *w, NVNsync *s, int *idx) {
    NVN_BLOCKING();
    uint64_t tr = trace_now();
    win_t *o = (win_t *)w;
    int nuse = o->nactive ? o->nactive : o->ntex;
    *idx = nuse ? o->next++ % nuse : 0;
    // Vsync blocks here (real-device NVN: when all textures in the swapchain are waiting for display, wait for the next one to be released), not in present -- the game often calls present while holding a lock
    uint64_t at = *idx < 4 ? o->avail[*idx] : 0, now = dm_detclock_enabled() ? guest_ns() : now_ns(), wait = at > now ? at - now : 0;
    if (!dm_detclock_enabled()) {   // Register the acquire thread as a time-constraint thread (no longer registered only when it "arrived early": it must also declare itself when it is constantly behind in the hot state)
        static uint64_t rt_period; uint64_t period = present_period(o), rtp = rt_old() ? period : rt_cap_period(o);
        if (rtp && rt_period != rtp) { rt_period = rtp; set_time_constraint(rtp); }
        rt_comp_tick(rtp);
        if (mtl_link_active() && !g_uncapped) { void mtl_link_set_interval(int n); mtl_link_set_interval(win_interval(o));   // game interval mode: as many ticks as the interval (it waited one: 60 fps at interval 2, the game ran twice as fast)
            if (!mtl_link_wait_tick(period ? 2 * period : 33333333ull) && wait) sleep_until_ns(at); }   // DM_PRESENT_LINK: pacing fully follows the display link (wait for the next display-link tick when running fast, go immediately when behind); our own 60 Hz clock is only the fallback when no display-link tick arrives
        else if (wait) sleep_until_ns(at);
    } else {   // DM_DETCLOCK_PACE=1 (used by the screenshot gate): while the guest clock advances per frame, each frame's wall clock is no shorter than one step -- frame count and in-game time are deterministic, and the background loading thread has at least one step of wall clock available per frame;
               // When the machine is fully loaded frames only get slower and the background only gets more wall clock, so the picture captured at "the Nth frame after the message" is independent of load
        static int pace = -1; static uint64_t step, last;
        if (pace < 0) { const char *e = getenv("DM_DETCLOCK_PACE"), *d = getenv("DM_DETCLOCK"); step = d ? strtoull(d, 0, 0) : 0; pace = e && atoi(e) && step; }
        if (pace) { if (last && now_ns() < last + step) sleep_until_ns(last + step); last = now_ns(); }
    }
    g_acq_wait_ns += wait; trow(g_frame, "@late_ms", wait ? (now_ns() - at) / 1e6 : 0);
    // The previous present of this texture has not yet been processed by the present thread (display cannot keep up, a whole swapchain round behind): wait for it to be returned, then hand it to the game to draw
    uint64_t tk = *idx < 4 ? o->ticket[*idx] : 0;
    if (tk && xlat_on()) tk = x_present_ticket(tk);   // the image's last present job has run: its present-thread ticket (plan item 61)
    if (tk) { uint64_t w0 = now_ns(); mtl_present_wait(tk); g_pq_wait_ns += now_ns() - w0; }
    { uint64_t rf = *idx < 4 ? o->rfence[*idx] : 0; g_acq_n++;   // the frame last drawn into this image (see g_acq_early)
#ifndef DM_TEST_ACQ_GPU_NO_WAIT   // negative control of t_nvn only: never waits
      if (rf & SYNC_TOKEN) { uint64_t f;   // a token: its fence once translated (the wait waits for that too); not translated yet counts as early
          if (x_token_fence(rf & ~SYNC_TOKEN, acq_gpu_wait() ? 5000000000ull : 0, &f)) rf = f; else { g_acq_early++; rf = 0; } }
      if (rf && mtl_done_fence() < rf) { g_acq_early++; if (acq_gpu_wait()) { uint64_t w0 = now_ns(); mtl_wait(rf, 5000000000ull); g_acq_gpu_ns += now_ns() - w0; } }
#endif
    }
    if (s) atomic_store_explicit(&g_acq_sync_last, (const void *)s, memory_order_relaxed);
    // The acquire sync means "the display path no longer reads this texture": mtl_present_wait above waited for its last present to be committed, and
    // every later use of the texture is ordered after that present on the one Metal queue. Fencing it to the newest submission instead held the game's
    // CPU here until the whole previous frame had run, so the replay of a frame and the GPU work of the one before never overlapped (MacNativeGames,
    // MIT: TOTK_ACQ_SYNC). DM_NVN_ACQ_SYNC=newest restores that (A/B)
    if (s) { static int newest = -1; if (newest < 0) { const char *e = getenv("DM_NVN_ACQ_SYNC"); newest = e && !strcmp(e, "newest"); if (newest) LOG("[nvn] DM_NVN_ACQ_SYNC=newest: the acquire sync waits for the newest submission"); }
             if (newest && xlat_on()) sync_token_store((sync_t *)s, x_token()); else sync_fence_store((sync_t *)s, newest ? mtl_last_fence() : 0); }
    if (!g_fs_started) { g_fs_started = 1; g_fs_prev_start = g_fs_start; g_fs_start = now_ns(); }   // a frame starts when its first acquire returns (nvn_fstep.h)
    trace_end(TR_ACQUIRE, tr, (uint64_t)*idx, wait);
    return NVN_WINDOW_ACQUIRE_TEXTURE_RESULT_SUCCESS;
}
// The queue's acquire (NVN 55; plan item 64a): the window's, without a sync (both results: 0 success, 1 native error)
NVN(nvnQueueAcquireTexture, NVNqueueAcquireTextureResult, NVNqueue *q, NVNwindow *w, int *idx) { (void)q; return (NVNqueueAcquireTextureResult)h_nvnWindowAcquireTexture(w, NULL, idx); }

// Frame boundary: on each present, print / write to disk the call count of each NVN function for this frame
static uint64_t g_prev[NVN_NFUNC];
static FILE *g_tsv; static int g_tsv_full;   // g_tsv_full: the size cap was reached, nothing more is written (the file stays open: other threads write rows)
static void dump_frame(void) {
    static int inited, nframes = 3, calls = 1;
    if (!inited) {
        inited = 1;
        const char *e = getenv("DM_NVN_FRAMES"); if (e) nframes = atoi(e);
        const char *c = getenv("DM_NVN_TSV_CALLS"); if (c) calls = atoi(c);   // 0 = write only the @ timing rows (pulling files from a real device has a size limit, and the call-count rows take about 2/3)
        const char *t = getenv("DM_NVN_TSV"); if (t && !(g_tsv = fopen(t, "w"))) LOG("[red] cannot open %s", t);
        if (g_tsv) fprintf(g_tsv, "# kind: nvn-per-frame  when name is an NVN function name, value = call count; names starting with @ are timing probes (ms / count)\nframe\tname\tvalue\n");
    }
    uint64_t total = 0; unsigned nfn = 0;
    char line[8192]; size_t o = 0; line[0] = 0;
    for (unsigned i = 0; i < NVN_NFUNC; i++) {
        uint64_t c = atomic_load(&nvn_slot[i].cnt), d = c - g_prev[i];
        g_prev[i] = c;
        if (!d) continue;
        total += d; nfn++;
        if (g_tsv && !g_tsv_full && calls) fprintf(g_tsv, "%llu\t%s\t%llu\n", g_frame, nvn_names[i], d);
        if (g_frame <= (uint64_t)nframes && o < sizeof line - 96) o += snprintf(line + o, sizeof line - o, " %s=%llu", nvn_names[i] + 3, d);
    }
    if (g_tsv && !g_tsv_full) {   // a size cap (plan item 145, bayonetta3-mobile's copy of blade2-mobile, MIT): DM_NVN_TSV_MAX_MB, 32 by default (1-4096)
        static uint64_t cap; if (!cap) { const char *e = getenv("DM_NVN_TSV_MAX_MB"); long mb = e ? strtol(e, NULL, 10) : 32; if (mb < 1 || mb > 4096) mb = 32; cap = (uint64_t)mb << 20; }
#ifndef DM_TEST_TSV_NO_CAP   // negative control of t_nvn only: no cap
        if ((uint64_t)ftello(g_tsv) >= cap) { fprintf(g_tsv, "# size cap of %llu MB reached at frame %llu: nothing more is written (DM_NVN_TSV_MAX_MB)\n", (unsigned long long)(cap >> 20), (unsigned long long)g_frame);
            g_tsv_full = 1; LOG("[nvn] DM_NVN_TSV reached its size cap of %llu MB at frame %llu: nothing more is written (DM_NVN_TSV_MAX_MB)", (unsigned long long)(cap >> 20), (unsigned long long)g_frame); }
#endif
        fflush(g_tsv); }
    if (g_frame <= (uint64_t)nframes) LOG("[nvn] %s %llu: %u functions, %llu calls |%s", g_frame ? "frame" : "init", g_frame, nfn, total, line);
    else if (g_frame % 300 == 0) LOG("[nvn] frame %llu: %u functions, %llu calls", g_frame, nfn, total);
    g_frame++; cs_dump(); trace_frame(g_frame);   // DM_NVN_TRACE (nvn_trace.h): its window starts and ends at frame starts
    // Every 300 frames read resident memory and hand it to the graphics layer to print the memory readout line (mtl_mem_stat, low frequency, capped). Memory watchdog: when resident memory exceeds the cap, log a failure and exit on its own. On by default only on the Mac host (24 GB: runaway host memory
    // drags the whole machine into a kernel panic, which happened twice with other programs on the same machine), or set for tests with DM_MEM_CAP_GB=<integer>; the player's iOS app has no cap (exiting before the system kills it is pointless and only adds another kind of crash)
    // Read every 60 frames (plan item 178, lordran-native 24740d4, MIT: a short spike, a movie's buffers, is caught); the line is still printed only on
    // a change of 64 MB or more, or every 3000 frames (mtl_mem_stat)
    if (g_frame % 60 == 0) { uint64_t fp = dm_footprint();   // (plat.h)
        if (fp) { unsigned long long mb = (unsigned long long)(fp >> 20); unsigned long long ht = 0, hf = 0, hm = 0; int hok = dm_guest_heap_stat(&ht, &hf, &hm);
            void mtl_mem_stat(unsigned long long, unsigned long long, int, unsigned long long, unsigned long long, unsigned long long); mtl_mem_stat(g_frame, mb, hok, ht, hf, hm);
            static long cap = -1; if (cap < 0) { const char *e = getenv("DM_MEM_CAP_GB"); cap = e ? atol(e) : DM_MEM_CAP_DEFAULT_GB; }
            if (cap > 0 && mb > (unsigned long long)cap * 1024) { LOG("[red] memory watchdog: resident %llu MB exceeds the %ld GB cap (frame %llu), exiting", mb, cap, (unsigned long long)g_frame); _exit(86); } } }
    dm_guest_frame_tick();   // Deterministic guest clock (DM_DETCLOCK, host hle_os.c): each present advances a fixed step; a no-op when not enabled
}
