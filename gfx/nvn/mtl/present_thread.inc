// gfx/nvn/mtl/present_thread.inc, part of nvn_mtl.m (included there in order, one translation unit): The present thread and the display link, statistics getters
// ---------- Present thread ----------
// NVN semantics: QueuePresentTexture only queues the present and returns immediately (the game often calls it while holding a lock; iOS real device: synchronously waiting for nextDrawable inside it
// takes about 30 ms, the lock is held about 90% of the time, and the loading thread starves). Taking the drawable, blit and present are all done on this thread in enqueue order.
// Ticket number = which enqueue it is (starting from 1); g_pq_done = the ticket number that has been processed (blit committed to the GPU queue).
// Swapchain texture return: before AcquireTexture gets a texture it waits for the ticket of that texture's previous present to be processed (mtl_present_wait), after which the commands the game draws into it
// are necessarily ordered after that blit on the GPU queue.
#define PQ 8
typedef struct { tex_t t; double period; uint64_t frame; int crop[4]; double t_in; } pq_item;   // t_in: the time the game handed this frame into the present queue (CACurrentMediaTime)
static pq_item g_pq[PQ];
static uint64_t g_pq_in;
static int present_now(const tex_t *, double, uint64_t, uint64_t, const int[4], id<CAMetalDrawable>, double);

@interface NvMetalDisplayLinkDelegate : NSObject <CAMetalDisplayLinkDelegate>
@end

// Fallback for the display link going silent while in the foreground (two smoke-test hangs before merge: the display link stopped calling back while the app was in the foreground, nobody takes the queued frames, and the game waits forever for its previous present to be processed before "acquiring the next swapchain texture"):
// when mtl_present_wait has waited longer than STALL_NS and the display link has not called back for that long, drop the waiting frame and earlier queued frames as when entering the background (accounted as dropped frames) and let the game keep running; when the display link comes back things return to normal automatically.
// One stall logs one hint line, and one line on recovery (neither is a failure); only when the same stall lasts longer than STALL_RED_NS is it a failure. All under g_pq_mu
#define STALL_NS 100000000ull
#define STALL_RED_NS 2000000000ull
static uint64_t g_stall_t0, g_stall_drop, g_stall_n; static int g_stall_red, g_stall_logged;
// The display link watchdog only recognizes the "active" state: when the App loses active status (pulling down Control Center / notifications, incoming call, system popup) and in the background the system does not call back anyway, and the picture is covered too -- the frame-drop fallback stays as before,
// but it is not counted as stuck and no failure is reported; the stall is measured from the later of "the most recent callback / the most recent activation" (A16 logs: 8–17 seconds inactive and 17 seconds in the background were both reported as "stuck in foreground")
// Rescue (from upstream d2r-mobile): once the display link has been silent for LINK_RESCUE_NS while active, stop waiting for it and let the present
// thread acquire drawables itself (mode 0, no display-link callback), so the picture keeps moving; after g_rescue_back_ns switch back to the display
// link once more, doubling that interval (cap 60 s) each time it stalls again. Seen on iPhones after pressing a volume button: the display link stopped
// while active and never came back, and the fallback alone dropped every frame for 80 s (a frozen "black screen"). Changed under g_pq_mu; the mode
// switch itself happens in link_sync on the game's present thread
#define LINK_RESCUE_NS 500000000ull
static int g_rescue_mode; static uint64_t g_rescue_back_at, g_rescue_back_ns = 3000000000ull, g_rescue_n;
__attribute__((weak)) int dm_focus_now(void) { return 1; }   // The host's hle_oe.c provides the strong definition; when graphics unit tests do not link the host it is treated as always active
__attribute__((weak)) uint64_t dm_focus_in_since(void) { return 0; }
// The queued frame a display-link tick shows (g_pq_mu held; now = the tick's target time, ns); 0: none, the frame on screen stays. By default the
// newest: the older ones are counted dropped and marked done. With FIFO (plan item 140, game.json present_consume "fifo", DM_PRESENT_CONSUME) the
// oldest, once its period since the last one shown is over (nvn_pace.h nvn_fifo_due): every frame of a game that steps once per frame is shown
static _Atomic int g_link_fifo; static nvn_pacer g_link_fp;
void mtl_link_set_fifo(int on) { atomic_store(&g_link_fifo, on != 0); }
static uint64_t link_take(uint64_t now, pq_item *it) {
    if (!g_dm_present_link || g_pq_read == g_pq_in) return 0;
    if (atomic_load(&g_link_fifo)) {
        if (!nvn_fifo_due(&g_link_fp, now, (uint64_t)(g_pq[g_pq_read % PQ].period * 1e9))) return 0;
        *it = g_pq[g_pq_read % PQ];
        return ++g_pq_read;
    }
    uint64_t first = g_pq_read + 1, ticket = g_pq_in;
    *it = g_pq[(g_pq_in - 1) % PQ];
    g_pq_read = g_pq_in;
    if (ticket > first) { g_link_dropped += ticket - first; for (uint64_t k = first; k < ticket; k++) g_pres[k % PRING] = (pres_ent){k, -1}; }   // Old frames merged and dropped are recorded as "not presented", so the present statistics honestly count them as dropped frames
    if (ticket > 1 && g_pq_done < ticket - 1) g_pq_done = ticket - 1;
    return ticket;
}
@implementation NvMetalDisplayLinkDelegate
- (void)metalDisplayLink:(CAMetalDisplayLink *)link needsUpdate:(CAMetalDisplayLinkUpdate *)update {
    (void)link;
    pthread_mutex_lock(&g_pq_mu);
    if (g_stall_t0) { uint64_t since = dm_focus_in_since(), t = now_ns();
        MLOG("display link recovered: this stall lasted %.0f ms (%.0f ms of it while active), fallback dropped %llu frames (stall #%llu)", (t - g_stall_t0) / 1e6, (t - (g_stall_t0 > since ? g_stall_t0 : since)) / 1e6, (unsigned long long)g_stall_drop, (unsigned long long)g_stall_n); g_stall_t0 = 0; }
    pthread_mutex_unlock(&g_pq_mu);
    pthread_mutex_lock(&g_link_mu);
    g_link_tick_ns = now_ns();
    g_link_tick_n++;
    pthread_cond_broadcast(&g_link_cv);
    pthread_mutex_unlock(&g_link_mu);
    pthread_mutex_lock(&g_pq_mu);
    double ts = update.targetPresentationTimestamp; pq_item it;
    uint64_t ticket = link_take((uint64_t)((ts > 0 ? ts : CACurrentMediaTime()) * 1e9), &it);
    if (!ticket) { pthread_mutex_unlock(&g_pq_mu); return; }
    g_link_it_tin = it.t_in;
    pthread_cond_broadcast(&g_pq_cv);
    pthread_mutex_unlock(&g_pq_mu);
    int submitted;
    @autoreleasepool { submitted = present_now(&it.t, it.period, it.frame, ticket, it.crop, update.drawable, update.targetPresentationTimestamp); }
    if (!submitted) {
        pthread_mutex_lock(&g_pq_mu);
        if (ticket > g_pq_done) g_pq_done = ticket;
        pthread_cond_broadcast(&g_pq_cv);
        pthread_mutex_unlock(&g_pq_mu);
    }
}
@end

static CAMetalDisplayLink *g_link; static CFRunLoopRef g_link_rl; static int g_link_started;
static int g_link_owns_layer, g_classic_busy;   // Under g_pq_mu: nextDrawable throws while the layer has the display link object attached, so the present thread that takes its own drawable and the display link object are mutually exclusive
static int link_on(void) { return g_dm_present_link && g_link != nil; }
// Mode 1 ticks at the present rate (hle_nvn.c present_cfg: 60, or DM_PRESENT_HZ for a game whose step follows the measured frame time: 90, 120)
static _Atomic int g_link_hz = 60;
void mtl_link_set_rate(int hz) { atomic_store(&g_link_hz, hz < 30 ? 30 : hz > 120 ? 120 : hz); }
static CAFrameRateRange link_range(int mode) {
    float hz = (float)atomic_load(&g_link_hz);
    return mode == 3 ? CAFrameRateRangeMake(30, 30, 30) : mode == 2 ? CAFrameRateRangeMake(60, 120, 120) : CAFrameRateRangeMake(hz, hz, hz);
}
static int g_link_mode = 1;   // Current mode of the display link object: 1 / 2 same as g_dm_present_link, 3 = power-saving mode 30 Hz
static _Atomic int g_link_itv;   // the present interval a frame waiting for ticks is paced with (mtl_link_set_interval; game interval mode: Tears of the Kingdom 2 or 3; display mode: 1); 0: none has waited (uncapped pacing)
static nvn_link30 g_link30;   // the power-saving link only while a frame is a whole number of its ticks (nvn_pace.h nvn_link30_step, plan item 324)
static int link_hz_of(int mode) { return mode == 3 ? 30 : mode == 2 ? 120 : atomic_load(&g_link_hz); }
static int g_link_latency;   // the link's preferredFrameLatency: set where it is made, and on the present thread with its rate (link_latency_sync)
static void link_latency_sync(CAMetalDisplayLink *link, int mode) {
    int l = nvn_frame_latency(g_latency_set, link_hz_of(mode)); if (l == g_link_latency) return;
    MLOG("display link frame latency %d → %d (%d Hz link, frame %llu)", g_link_latency, l, link_hz_of(mode), (unsigned long long)atomic_load(&g_cur_frame));
    g_link_latency = l; link.preferredFrameLatency = l;
}
static void *link_main(void *arg) {   // Display link thread: started when switching to display-link mode, exits when switching back to 0
    (void)arg;
    pthread_setname_np("nvn-present-link");
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    CAMetalLayer *layer = (__bridge CAMetalLayer *)front_metal_layer();
    pthread_mutex_lock(&g_pq_mu); g_link_owns_layer = 1; while (g_classic_busy) pthread_cond_wait(&g_pq_cv, &g_pq_mu); pthread_mutex_unlock(&g_pq_mu);   // Wait for the present thread to finish handing over the frame it holds
    NvMetalDisplayLinkDelegate *delegate = [NvMetalDisplayLinkDelegate new];
    CAMetalDisplayLink *link = [[CAMetalDisplayLink alloc] initWithMetalLayer:layer];
    link.preferredFrameRateRange = link_range(g_link_mode);
    link.preferredFrameLatency = g_link_latency = nvn_frame_latency(g_latency_set, link_hz_of(g_link_mode));
    link.delegate = delegate;
    [link addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
    g_link_rl = CFRunLoopGetCurrent(); g_link = link;
    CFRunLoopRun();   // When switching back to mode 0 the display link object is invalidated and this thread exits (nextDrawable cannot get a drawable while the layer still has the display link object attached, so pausing alone is not enough)
    g_link_started = 0;
    return NULL;
}
static void link_sync(void) {   // Called every frame by the game's present thread: aligns the display link object's state to g_dm_present_link and the power-saving mode (can change at run time)
    static int applied, applied_hz = 60;
    if (g_rescue_mode && now_ns() >= g_rescue_back_at) {   // rescue period over: try the display link again (stall timing restarts, so the old stall does not trigger another rescue at once)
        pthread_mutex_lock(&g_pq_mu); g_dm_present_link = g_rescue_mode; g_rescue_mode = 0; g_stall_t0 = 0; g_link_tick_ns = now_ns(); pthread_mutex_unlock(&g_pq_mu);
        MLOG("display link: rescue period over, back to pacing mode %d (frame %llu)", g_dm_present_link, (unsigned long long)atomic_load(&g_cur_frame)); }
    int want = g_dm_present_link;
    if (want < 0 || want > 2) { g_dm_present_link = applied == 3 ? 1 : applied; return; }
    int hz = atomic_load(&g_link_hz);
    if (want && mtl_fps_target() == 30) {   // Power-saving mode: display link at 30 Hz, while a frame is a whole number of its ticks (plan item 324)
        static int fits = 1; int itv = atomic_load(&g_link_itv), f = nvn_link30_step(&g_link30, (unsigned)itv, (unsigned)hz, now_ns());
        if (f != fits) { fits = f;
            if (f) MLOG("display link: every frame a whole number of 30 Hz ticks for %.0f s, the power-saving link again (frame %llu)", NVN_LINK30_BACK_NS / 1e9, (unsigned long long)atomic_load(&g_cur_frame));
            else MLOG("display link: a frame of interval %d at %d Hz is not a whole number of 30 Hz ticks, the link at %d Hz while it is (frame %llu)", itv, hz, link_hz_of(want), (unsigned long long)atomic_load(&g_cur_frame)); }
        if (f) want = 3;
    }
    if (want == applied && (want != 1 || hz == applied_hz || !g_link)) return;
    if (want == applied) { g_link.preferredFrameRateRange = link_range(want); link_latency_sync(g_link, want); MLOG("display link rate %d → %d Hz (frame %llu)", applied_hz, hz, (unsigned long long)atomic_load(&g_cur_frame)); applied_hz = hz; return; }
    applied_hz = hz;
    g_link_mode = want;
    if (!g_link_started) {
        if (!want) { applied = 0; return; }
        if (!front_metal_layer()) return;
        g_link_started = 1; pthread_t th; if (pthread_create(&th, NULL, link_main, NULL)) MDIE("failed to start the display link thread"); pthread_detach(th);
        return;   // The display link object is created on a new thread; align again next frame
    }
    CAMetalDisplayLink *link = g_link; if (!link) return;
    if (want) { link.preferredFrameRateRange = link_range(want); link_latency_sync(link, want); }
    else { CFRunLoopRef rl = g_link_rl; g_link = nil; CFRunLoopPerformBlock(rl, kCFRunLoopDefaultMode, ^{ [link invalidate]; pthread_mutex_lock(&g_pq_mu); g_link_owns_layer = 0; pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu); CFRunLoopStop(rl); }); CFRunLoopWakeUp(rl); }
    MLOG("present pacing mode %d → %d (frame %llu)", applied, want, (unsigned long long)atomic_load(&g_cur_frame));
    applied = want;
    pthread_mutex_lock(&g_pq_mu); pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu);
}
// Before going to the background (mtl_drain): when the display link drives presents, frames queued but not yet presented can only be taken by the display-link callback, and in the background the system no longer calls back --
// waiting as-is for "the present queue to empty" would wait forever, the enter-background callback would not return, and the system would kill the process (the process disappears within 22 seconds after switching to another App).
// These frames will not be seen anyway: discard them directly (accounted as dropped frames) and only wait for the one present already submitted to the GPU to finish.
static void present_drop_for_suspend(void) {
    pthread_mutex_lock(&g_pq_mu);
    if (g_link_owns_layer) {
        uint64_t n = g_pq_in - g_pq_read;
        for (uint64_t k = g_pq_read + 1; k <= g_pq_in; k++) g_pres[k % PRING] = (pres_ent){k, -1};
        g_link_dropped += n; g_pq_read = g_pq_in;
        while (g_pq_inflight) pthread_cond_wait(&g_pq_cv, &g_pq_mu);
        if (g_pq_done < g_pq_in) g_pq_done = g_pq_in;
        pthread_cond_broadcast(&g_pq_cv);
        if (n) MLOG("entering background: dropping %llu queued frames not yet on screen (the display link does not call back in the background)", (unsigned long long)n);
    }
    pthread_mutex_unlock(&g_pq_mu);
}
static void *present_main(void *arg) {
    (void)arg;
    pthread_setname_np("nvn-present");
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    qos_class_t qc = QOS_CLASS_UNSPECIFIED; int rp = 0; pthread_get_qos_class_np(pthread_self(), &qc, &rp); atomic_store(&g_present_qos, (uint32_t)qc);
    for (;;) {
        pthread_mutex_lock(&g_pq_mu);
        while (g_pq_read == g_pq_in || g_link_owns_layer) pthread_cond_wait(&g_pq_cv, &g_pq_mu);   // In display-link mode the display-link callback takes them; an empty queue is judged by "taken"; "done" has to wait for the feedback of the present submit and lags behind taken
        pq_item it = g_pq[g_pq_read % PQ];
        uint64_t ticket = ++g_pq_read; g_classic_busy = 1;
        pthread_mutex_unlock(&g_pq_mu);
        int submitted;
        @autoreleasepool { submitted = present_now(&it.t, it.period, it.frame, ticket, it.crop, nil, 0); }
        pthread_mutex_lock(&g_pq_mu); g_classic_busy = 0; pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu);
        if (!submitted) {
            pthread_mutex_lock(&g_pq_mu);
            if (ticket > g_pq_done) g_pq_done = ticket;
            pthread_cond_broadcast(&g_pq_cv);
            pthread_mutex_unlock(&g_pq_mu);
        }
    }
    return NULL;
}
uint32_t mtl_present_qos(void) { return atomic_load(&g_present_qos); }
static void present_start(void) { pthread_t th; if (pthread_create(&th, NULL, present_main, NULL)) MDIE("failed to start the present thread"); pthread_detach(th); }
static _Atomic int g_present_discard;
void mtl_link_pause(int on);
// The short period when the front end goes to the background and waits for the game to save: presents are dropped directly (the display link does not call back in the background, enqueueing would block the game thread; nobody sees the picture anyway).
// When turned on, also clear the frames already queued: the system first stops the display-link callback and only afterwards notifies about going to the background, and in that gap the game has already filled the swapchain and is stuck at "before acquiring the next texture, wait for its previous present to be processed"
// (hle_nvn.c nvnWindowAcquireTexture -> mtl_present_wait). Without clearing, the game cannot advance a single tick during those 2.5 seconds of waiting for the save, and the save is not written (the same pattern three times in player logs: 3 frames queued, 0 submits)
void mtl_present_discard(int on) { atomic_store(&g_present_discard, on != 0); if (on) present_drop_for_suspend(); }
uint64_t mtl_present(const tex_t *t, double period, const int crop[4]) { @autoreleasepool {
    if (atomic_load(&g_present_discard)) return 0;   // Ticket number 0 = no pending present; the swapchain texture acquire does not wait
    if (t) { xv_check(t->serial, 0, 0, "present"); if (g_xv.npresent < 128) g_xv.present[g_xv.npresent++] = t->serial; }
    static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, present_start);
    link_sync();
    {   // Test switch DM_TEST_LINK_STALL=start seconds,duration seconds: counting from the first present, pause the display link at that point (imitating the display link not calling back while in the foreground) and release it when the duration is up. For verifying the fallback; not set in the product
        static double t0, at = -1, dur; static int st; double now = CACurrentMediaTime();
        if (at < 0) { at = 0; const char *e = getenv("DM_TEST_LINK_STALL"); if (e && sscanf(e, "%lf,%lf", &at, &dur) == 2 && at > 0 && dur > 0) { t0 = now; st = 1; } else at = 0; }
        if (st == 1 && link_on() && now - t0 >= at) { st = 2; mtl_link_pause(1); MLOG("DM_TEST_LINK_STALL: pausing the display link for %.1f s (frame %llu)", dur, (unsigned long long)atomic_load(&g_cur_frame)); }
        else if (st == 2 && now - t0 >= at + dur) { st = 3; mtl_link_pause(0); MLOG("DM_TEST_LINK_STALL: resuming the display link (frame %llu)", (unsigned long long)atomic_load(&g_cur_frame)); }
    }
    pthread_mutex_lock(&g_pq_mu);
    while (g_pq_in - g_pq_done >= PQ) pthread_cond_wait(&g_pq_cv, &g_pq_mu);   // Wait only when the queue is full; normally AcquireTexture blocks first
    pq_item *it = &g_pq[g_pq_in % PQ]; *it = (pq_item){*t, period, atomic_load(&g_cur_frame), {0}, CACurrentMediaTime()};
    if (crop) memcpy(it->crop, crop, sizeof it->crop);   // The current frame's state is enqueued with the ticket number, rather than reading the mutable window late on the present thread
    uint64_t tk = ++g_pq_in;
    pthread_cond_broadcast(&g_pq_cv);
    pthread_mutex_unlock(&g_pq_mu);
    return tk;
} }
// Do not wait during discard: the frame enqueued only after the flag was set (the present thread had just passed the flag check) has nobody to take it, and waiting for it would stall again; it is left for the following mtl_drain to clear
// Do not wait indefinitely in display-link mode: see the note at STALL_NS. Only frames "not yet taken by the display link" are dropped; the one already on its way to the screen is waited for until handed to the GPU (same dropping as when entering the background)
void mtl_present_wait(uint64_t tk) { @autoreleasepool {
    pthread_mutex_lock(&g_pq_mu);
    while (g_pq_done < tk && !atomic_load(&g_present_discard)) {
        if (!g_link_owns_layer) { pthread_cond_wait(&g_pq_cv, &g_pq_mu); continue; }
        if (g_pq_read >= tk) { pthread_cond_wait(&g_pq_cv, &g_pq_mu); continue; }   // This one has already been taken by the display link and is being presented: wait for it to be handed to the GPU (the GPU completion callback will wake us)
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += (long)STALL_NS; if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        // Already determined to be in a stall (when the display link comes back the callback first clears g_stall_t0, then records the display-link tick time): no longer wait 100 ms in vain every frame, drop directly, and the game proceeds by its own display-link wait timeout (about two ticks)
        if (!g_stall_t0 && (pthread_cond_timedwait(&g_pq_cv, &g_pq_mu, &ts) != ETIMEDOUT || g_pq_done >= tk)) continue;
        uint64_t now = now_ns(), last = __atomic_load_n(&g_link_tick_ns, __ATOMIC_RELAXED);
        if (g_pq_read >= tk) continue;
        if (now - last < STALL_NS) { pthread_cond_timedwait(&g_pq_cv, &g_pq_mu, &ts); continue; }   // The display link is still running, it is just not this one's turn yet: keep waiting (bounded, no busy spinning)
        uint64_t n = tk - g_pq_read;
        for (uint64_t k = g_pq_read + 1; k <= tk; k++) g_pres[k % PRING] = (pres_ent){k, -1};
        g_link_dropped += n; g_pq_read = tk;
        while (g_pq_inflight) pthread_cond_wait(&g_pq_cv, &g_pq_mu);
        if (g_pq_done < tk) g_pq_done = tk;
        pthread_cond_broadcast(&g_pq_cv);
        if (!g_stall_t0) { g_stall_t0 = last ? last : now; g_stall_drop = 0; g_stall_red = 0; g_stall_logged = 0; g_stall_n++; }
        g_stall_drop += n;
        int active = dm_focus_now() == 1; uint64_t since = dm_focus_in_since(), from = g_stall_t0 > since ? g_stall_t0 : since;
        if (active && !g_stall_logged && now - from >= STALL_NS) { g_stall_logged = 1;
            MLOG("display link has not called back for %.0f ms (while active): dropping %llu queued frames not yet on screen, the game continues (stall #%llu; back to normal automatically when the link returns)", (now - from) / 1e6, (unsigned long long)n, (unsigned long long)g_stall_n); }
        if (active && !g_rescue_mode && g_dm_present_link && now - from >= LINK_RESCUE_NS) {
            g_rescue_mode = g_dm_present_link; g_dm_present_link = 0; g_rescue_n++; g_rescue_back_at = now + g_rescue_back_ns;
            MLOG("display link silent for %.0f ms while active: presenting with our own drawables (rescue #%llu), back to the display link in %.0f s", (now - from) / 1e6, (unsigned long long)g_rescue_n, g_rescue_back_ns / 1e9);
            if (g_rescue_back_ns < 60000000000ull) g_rescue_back_ns *= 2;
        }
        if (active && !g_stall_red && now - from >= STALL_RED_NS) { g_stall_red = 1; MLOG("[red] display link has not called back for %.1f s in a row while active, presenting relies entirely on fallback frame drops, the picture is frozen", (now - from) / 1e9); }
    }
    pthread_mutex_unlock(&g_pq_mu);
} }
// Wait until all queued presents have been processed (mtl_drain, the tests): goes through mtl_present_wait, so there is still a fallback when the display link stops (real device: the game called QueueFinish once while the display link was stopped,
// and this used to wait here without a timeout; nobody took the 3 queued frames and the game stalled; those two runs on Mac did not hit this call)
double mtl_link_tick_age_ms(void) { uint64_t t = __atomic_load_n(&g_link_tick_ns, __ATOMIC_RELAXED); return t ? (now_ns() - t) / 1e6 : -1; }   // front ends log it next to volume / route changes
void mtl_present_flush(void) { pthread_mutex_lock(&g_pq_mu); uint64_t h = g_pq_in; pthread_mutex_unlock(&g_pq_mu); mtl_present_wait(h); }
void mtl_stats(uint64_t *passes, uint64_t *draws) { *passes = g_stat_pass; *draws = g_stat_draw; }
void mtl_args_stats(uint64_t *enc, uint64_t *reuse) { *enc = g_stat_args_enc; *reuse = g_stat_args_reuse; }
void mtl_use_stats(uint64_t *call, uint64_t *skip) { *call = 0; *skip = 0; }
uint64_t mtl_ds_omit(void) { return g_stat_ds_omit; }
void mtl_bw(uint64_t *load, uint64_t *store, uint64_t *full) { *load = g_bw_load; *store = g_bw_store; *full = g_pass_full_rw; }
uint64_t mtl_udc_saved_bytes(void) { return g_udc_saved_bytes; }
int mtl_has_window(void) { return front_metal_layer() != NULL; }
int mtl_link_active(void) { return link_on(); }
void mtl_link_pause(int on) { CAMetalDisplayLink *l = g_link; if (l) l.paused = on != 0; }   // For testing (DM_TEST_BG): imitates the system stopping the display-link callback in the background
// Display-link ticks one frame takes at the game's present interval (g_link_itv, above)
void mtl_link_set_interval(int n) { atomic_store(&g_link_itv, n < 1 ? 1 : n > 4 ? 4 : n); }
int mtl_link_wait_tick(uint64_t timeout_ns) { @autoreleasepool {
    if (!mtl_link_active()) return 0;
    // Wait for the display link only when running faster than the display: if a display-link tick has already passed since the previous frame started work (this frame is already behind), start immediately instead of waiting in vain for the next display-link tick --
    // otherwise when the frame time is slightly more than one display-link period it would "miss the tick -> wait another whole tick", locking the frame rate straight to half (measured on Mac under windowed load: runs of dropped frames)
    static uint64_t started_tick;   // Display-link tick number when the previous frame started work (only the game's present thread calls this function)
    // How many display-link ticks one frame takes, from the rate the link runs at (nvn_pace.h nvn_link_step): the present interval in game interval mode
    // (1 in display mode) of the present rate, on a 60 Hz link that many ticks, on the 120 Hz one twice as many, on the power-saving 30 Hz one half
    const int lhz = g_link_mode == 3 ? 30 : g_link_mode == 2 ? 120 : atomic_load(&g_link_hz);
    const uint64_t step = nvn_link_step((unsigned)atomic_load(&g_link_itv), (unsigned)atomic_load(&g_link_hz), (unsigned)lhz);
    pthread_mutex_lock(&g_link_mu);
    if (g_link_tick_n >= started_tick + step) { started_tick = g_link_tick_n; pthread_mutex_unlock(&g_link_mu); return 1; }
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ns / 1000000000ull; ts.tv_nsec += timeout_ns % 1000000000ull;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    while (g_link_tick_n < started_tick + step) if (pthread_cond_timedwait(&g_link_cv, &g_link_mu, &ts) == ETIMEDOUT) { pthread_mutex_unlock(&g_link_mu); return 0; }
    started_tick = g_link_tick_n;
    pthread_mutex_unlock(&g_link_mu); return 1;
} }



// Called by the watchdog (no progress): writes the state of all blocking wait points on the graphics side to the log -- who is waiting for whom is visible at a glance (after switching to the Metal 4 submit model there was once a case of "ran to 900 frames then stopped, no failure line")
void mtl_wait_state_dump(void) {
    MLOG("wait point state: main fence committed %llu / completed %llu; main allocator fences [%llu %llu %llu]; upload pending %d, allocators busy [%d %d %d], seq %u, unflushed batch %d; present queue in %llu / done %llu; main command buffer encoding %d, submit thread set %d",
         (unsigned long long)g_fence, (unsigned long long)g_done, (unsigned long long)g_alloc_fence[0], (unsigned long long)g_alloc_fence[1], (unsigned long long)g_alloc_fence[2],
         atomic_load(&g_up_pending), atomic_load(&g_up_alloc_busy[0]), atomic_load(&g_up_alloc_busy[1]), atomic_load(&g_up_alloc_busy[2]), g_up_seq, g_up_cb != nil,
         (unsigned long long)g_pq_in, (unsigned long long)g_pq_done, g_cb != nil, atomic_load(&g_sub_thread_set));
}
void mtl_alloc_stats(uint64_t *waits, uint64_t *grown, int *n) { *waits = g_alloc_waits; *grown = g_alloc_grown; *n = g_alloc_n; }   // Main submit allocator pool: how many times it waited, how many times it grew, how many exist now
#pragma clang attribute pop
