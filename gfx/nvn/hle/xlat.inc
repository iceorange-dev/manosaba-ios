// gfx/nvn/hle/xlat.inc, part of hle_nvn.c (included there in order, one translation unit): The translation thread (plan item 61, DM_NVN_XLAT=1)
// ---------- The translation thread (plan item 61, DM_NVN_XLAT=1; D2R Mobile b94670c and MacNativeGames' TOTK_ASYNC_REPLAY, MIT, written anew) ----------
// Submissions and presents go in order to one thread that replays them into Metal. The game's thread takes what the console reads at the submit
// call (mtl_sub_capture: the deferred handles, the uniform bytes, the FenceSync records' tokens) and returns, so the game's next frame overlaps this
// one's translation (61c: at 120 Hz 16 % of Tears of the Kingdom's frames waited on it). A fence the game takes (QueueFenceSync, the acquire's) is a
// token, the number of submissions queued so far; a wait on it waits until the thread has replayed that submission, then for the Metal fence it ended
// with, so a passed fence still means "translated and done on the GPU", and command memory or objects the game reuses after it have been read.
// A present is a job after the frame's submissions, and so is the frame number; the acquire of an image waits for the job of its last present, then
// for its ticket as before. What reads or ends Metal state from the game's side waits until the queue is empty (x_drain): Finish, texel reads,
// finalizing textures, programs, pools and the queue, a texture initialized again, shaders set again, a pool's invalidate, suspension. The thread runs
// at USER_INTERACTIVE and declares a time constraint of half the present period (MacNativeGames measured its replay 6.6 → 13 ms without one on an
// iPhone, preempted by the game's workers; DM_NVN_XLAT_RT=0 leaves it out, 61d). DM_NVN_XLAT=0 (the default for now): the calling thread, as before
enum { XJ_SUBMIT = 1, XJ_PRESENT, XJ_FRAME };
typedef struct { int kind, n; uint64_t *h; mtl_cap *cap; uint64_t sub, frame; const tex_t *tex; double period; int crop[4]; } xjob;
#define XQ 32
#define XF 4096
static xjob g_xq[XQ];
static uint64_t g_xq_in, g_xq_done, g_xsub_in, g_xsub_done, g_xsub_fence[XF];   // jobs queued / run; submissions queued / run, the Metal fence each ended with
static struct { uint64_t job, tk; } g_xtk[XF];                                   // a present job's present-thread ticket
static pthread_mutex_t g_xq_mu = PTHREAD_MUTEX_INITIALIZER, g_xord_mu = PTHREAD_MUTEX_INITIALIZER;   // g_xord_mu: a submission's number, capture and queueing in one order
static pthread_cond_t g_xq_cv = PTHREAD_COND_INITIALIZER;
static int g_xlat = -1, g_xleft = -1;   // g_xleft: jobs the thread may still take before it waits (-1: any; tests: nvn_xlat_hold / nvn_xlat_step)
static _Atomic uint64_t g_x_drains, g_x_drain_ns;
static int xlat_on(void) {
    if (g_xlat < 0) { const char *e = getenv("DM_NVN_XLAT"); g_xlat = e && atoi(e) > 0; if (g_xlat) LOG("[nvn] DM_NVN_XLAT=1: submissions and presents replayed in order on a translation thread"); }
    return g_xlat;
}
static int rt_peak(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_RT_PEAK"); v = e && !strcmp(e, "1"); if (v) LOG("[nvn] DM_RT_PEAK=1: real-time computations declared to cover 1.1 x each 120-frame window's longest frame too"); } return v; }   // (plan item 248)
static int rt_old(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_RT_DECL"); v = e && !strcmp(e, "old"); if (v) LOG("[nvn] DM_RT_DECL=old: time constraints declared as before 2026-10-22"); } return v; }   // (nvn_pace.h nvn_rt_*)
static void xlat_rt(uint64_t period_ns) {   // the translation thread's time constraint (61d's switch), once a frame: the cap's period, 1.25 x the p90 of its CPU a frame (nvn_pace.h)
    static int on = -1; if (on < 0) { const char *e = getenv("DM_NVN_XLAT_RT"); on = !(e && !strcmp(e, "0")); }
    if (!on || !period_ns || dm_detclock_enabled()) return;
    static uint64_t set, comp, last; static nvn_rt_win w;
    uint64_t c = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID), want = comp;
    if (rt_old()) want = period_ns / 2;
    else { uint64_t v = last ? nvn_rt_sample(&w, (double)(c - last) / 1e6, comp / 1e6, period_ns, rt_peak()) : 0; if (v) want = v; else if (set != period_ns) want = nvn_rt_comp_on_period(0, comp, period_ns); }
    last = c;
    if (set == period_ns && want == comp) return;
    int ok = dm_self_deadline(period_ns, want);   // (plat.h)
    set = period_ns; comp = want; LOG("[nvn] translation thread time constraint (period %.3f ms, computation %.3f ms) %s", period_ns / 1e6, want / 1e6, ok ? "set" : "not set");
}
static void x_run(xjob *j, uint64_t job) {
    if (j->kind == XJ_SUBMIT) {
        uint64_t tr = trace_now(), f = sub_exec(j->n, j->h, j->cap); mtl_cap_free(j->cap); free(j->h);
        trace_end(TR_SUBMIT, tr, (uint64_t)j->n, f);   // the replay itself, with its Metal fence (the game thread's span is the queueing, with the token)
        pthread_mutex_lock(&g_xq_mu); g_xsub_fence[j->sub % XF] = f; g_xsub_done = j->sub; pthread_mutex_unlock(&g_xq_mu);
    } else if (j->kind == XJ_PRESENT) {
        gfx_mt_enter("PresentTexture"); uint64_t tk = mtl_present(j->tex, j->period, j->crop); gfx_mt_leave();
        pthread_mutex_lock(&g_xq_mu); g_xtk[job % XF].job = job; g_xtk[job % XF].tk = tk; pthread_mutex_unlock(&g_xq_mu);
    } else { mtl_set_frame(j->frame); xlat_rt((uint64_t)(j->period * 1e9)); }
}
static void *x_thread(void *unused) {
    (void)unused; pthread_setname_np("nvn-translate");
    for (;;) {
        pthread_mutex_lock(&g_xq_mu);
        while (g_xq_done == g_xq_in || !g_xleft) pthread_cond_wait(&g_xq_cv, &g_xq_mu);
        uint64_t job = g_xq_done + 1; xjob j = g_xq[job % XQ]; if (g_xleft > 0) g_xleft--;
        pthread_mutex_unlock(&g_xq_mu);
        x_run(&j, job);
        pthread_mutex_lock(&g_xq_mu); g_xq_done = job; pthread_cond_broadcast(&g_xq_cv); pthread_mutex_unlock(&g_xq_mu);
    }
    return NULL;
}
static void x_start(void) {
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 8u << 20); dm_attr_interactive(&a);
    pthread_t t; if (pthread_create(&t, &a, x_thread, NULL)) die("NVN translation thread");
    pthread_attr_destroy(&a); pthread_detach(t);
}
static uint64_t x_push(xjob *j) {   // the job's number (from 1); a submission's number in j->sub is the caller's (under g_xord_mu)
    static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, x_start);
    pthread_mutex_lock(&g_xq_mu);
    while (g_xq_in - g_xq_done >= XQ) pthread_cond_wait(&g_xq_cv, &g_xq_mu);
    uint64_t job = ++g_xq_in; g_xq[job % XQ] = *j;
    if (j->kind == XJ_SUBMIT) g_xsub_in = j->sub;
    pthread_cond_broadcast(&g_xq_cv); pthread_mutex_unlock(&g_xq_mu);
    return job;
}
static void x_wait_job(uint64_t job) {
    pthread_mutex_lock(&g_xq_mu); int done = g_xq_done >= job; pthread_mutex_unlock(&g_xq_mu);
    if (done) return;
    NVN_BLOCKING();   // a guest thread gives its virtual core back while it waits
    pthread_mutex_lock(&g_xq_mu); while (g_xq_done < job) pthread_cond_wait(&g_xq_cv, &g_xq_mu); pthread_mutex_unlock(&g_xq_mu);
}
static void x_drain(void) {   // everything queued so far has run (nothing to do without the thread, or when it is idle)
    if (!xlat_on()) return;
#ifdef DM_TEST_XLAT_NO_DRAIN   // negative control of t_xlat only: nothing waits for the queue
    return;
#endif
    pthread_mutex_lock(&g_xq_mu); uint64_t want = g_xq_in, have = g_xq_done; pthread_mutex_unlock(&g_xq_mu);
    if (have >= want) return;
    uint64_t t0 = now_ns(); x_wait_job(want);
    atomic_fetch_add(&g_x_drains, 1); atomic_fetch_add(&g_x_drain_ns, now_ns() - t0);
}
static uint64_t x_token(void) { pthread_mutex_lock(&g_xq_mu); uint64_t k = g_xsub_in; pthread_mutex_unlock(&g_xq_mu); return k; }
// Submission k translated within timeout_ns: its Metal fence in *fence (a later fence when its entry was overwritten: conservative); 1 = translated
static int x_token_fence(uint64_t k, uint64_t timeout_ns, uint64_t *fence) {
    pthread_mutex_lock(&g_xq_mu);
    int ok = g_xsub_done >= k;
    if (ok) *fence = !k ? 0 : g_xsub_done - k < XF ? g_xsub_fence[k % XF] : mtl_last_fence();
    pthread_mutex_unlock(&g_xq_mu);
    if (ok || !timeout_ns) return ok;
    NVN_BLOCKING();   // the core given back before the lock is taken and taken back after it is released (at return): never waited for while holding it
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl);
    uint64_t ns = (uint64_t)dl.tv_nsec + (timeout_ns > 3600000000000ull ? 3600000000000ull : timeout_ns);
    dl.tv_sec += (time_t)(ns / 1000000000ull); dl.tv_nsec = (long)(ns % 1000000000ull);
    pthread_mutex_lock(&g_xq_mu);
    while (g_xsub_done < k) if (pthread_cond_timedwait(&g_xq_cv, &g_xq_mu, &dl)) break;
    ok = g_xsub_done >= k;
    if (ok) *fence = g_xsub_done - k < XF ? g_xsub_fence[k % XF] : mtl_last_fence();
    pthread_mutex_unlock(&g_xq_mu);
    return ok;
}
static uint64_t x_present_ticket(uint64_t job) {   // the present-thread ticket a present job got (0: none to wait for)
    x_wait_job(job);
    pthread_mutex_lock(&g_xq_mu); uint64_t tk = g_xtk[job % XF].job == job ? g_xtk[job % XF].tk : 0; pthread_mutex_unlock(&g_xq_mu);
    return tk;
}
// Tests (t_xlat): the thread holds its next job while on, or takes n more and holds; the jobs not run yet
void nvn_xlat_hold(int on) { pthread_mutex_lock(&g_xq_mu); g_xleft = on ? 0 : -1; pthread_cond_broadcast(&g_xq_cv); pthread_mutex_unlock(&g_xq_mu); }
void nvn_xlat_step(int n) { pthread_mutex_lock(&g_xq_mu); g_xleft = n; pthread_cond_broadcast(&g_xq_cv); pthread_mutex_unlock(&g_xq_mu); }
uint64_t nvn_xlat_pending(void) { pthread_mutex_lock(&g_xq_mu); uint64_t n = g_xq_in - g_xq_done; pthread_mutex_unlock(&g_xq_mu); return n; }
NVN(nvnQueueSubmitCommands, void, NVNqueue *q, int n, const NVNcommandHandle *h) {
    if (test_work_ms() > 0) { uint64_t from = atomic_exchange(&g_test_work_from, 0); if (from) sleep_until_ns(from + (uint64_t)test_work_ms() * 1000000ull); }
    NVN_BLOCKING();
    uint64_t tr = trace_now();
    gfx_enter();
    ((queue_t *)q)->submits++;
    if (xlat_on()) {
        xjob j = {.kind = XJ_SUBMIT, .n = n, .h = malloc(sizeof(uint64_t) * (size_t)(n > 0 ? n : 1))};
        if (!j.h) die("NVN translation queue");
        memcpy(j.h, h, sizeof(uint64_t) * (size_t)(n > 0 ? n : 0));
        pthread_mutex_lock(&g_xord_mu);
        j.sub = x_token() + 1; j.cap = mtl_sub_capture(j.h, n, j.sub); x_push(&j);
        pthread_mutex_unlock(&g_xord_mu);
        gfx_leave();
        trace_end(TR_SUBMIT, tr, (uint64_t)n, j.sub | SYNC_TOKEN);
        return;
    }
    sub_exec(n, (const uint64_t *)h, NULL);
    gfx_leave();
    trace_end(TR_SUBMIT, tr, (uint64_t)n, mtl_last_fence());
}

