// gfx/nvn/mtl/commit.inc, part of nvn_mtl.m (included there in order, one translation unit): The GPU memory budget, submission commits, fences, waits and drains
// GPU memory budget (MacNativeGames' gpu_budget_check, MIT; written anew here): the four macOS kernel panics it saw all came with the driver out of
// wired memory. Before every commit the GPU allocation is held against DM_NVN_GPU_BUDGET_MB (default 40 % of physical memory on the Mac, off on iOS,
// where the system ends the app first; 0 turns it off): past it the residency census is written and the process stops instead of feeding the driver
static uint64_t gpu_budget_bytes(void) {
    static uint64_t b; static int init;
    if (!init) { init = 1; const char *e = getenv("DM_NVN_GPU_BUDGET_MB");
        if (e) b = strtoull(e, NULL, 10) << 20;
        else if (!TARGET_OS_IPHONE) { uint64_t phys = 0; size_t n = sizeof phys; if (!sysctlbyname("hw.memsize", &phys, &n, NULL, 0)) b = phys / 5 * 2; }
        if (b) MLOG("GPU memory budget %llu MB (DM_NVN_GPU_BUDGET_MB, 0 turns it off)", (unsigned long long)(b >> 20)); }
    return b;
}
static int gpu_budget_exceeded(uint64_t *cur, uint64_t *budget) { *budget = gpu_budget_bytes(); *cur = (uint64_t)g_dev.currentAllocatedSize; return *budget && *cur > *budget; }
static void res_census(unsigned long long frame, unsigned long long rss_mb);
static void up_retire(void);   // upload.inc
static void wb_stage_encode(void);   // writeback.inc
// Over the budget, part of the allocation can be objects already retired and staging that submissions still on the GPU hold, both released when
// those complete (plan item 279; MacNativeGames 4de7b4e, MIT, written anew: Tears of the Kingdom stopped there while the world loaded, and on a
// dive): the running work is let complete and what it held released, at most 2 s, ending early once nothing has completed for 20 ms with nothing
// running; only memory still allocated after that stops the process. The submission being built is not committed yet, so its own fence is not
// waited for. 1: back under the budget
static int gpu_budget_settle(uint64_t budget, uint64_t *cur, double *waited_ms) {
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW), quiet = t0, done = mtl_done_fence();
    *cur = (uint64_t)g_dev.currentAllocatedSize;
#ifndef DM_TEST_BUDGET_NO_WAIT   // negative control of t_poolcheck only: stop at once, as before
    for (;;) {
        @autoreleasepool { defer_drain(); up_retire(); }
        *cur = (uint64_t)g_dev.currentAllocatedSize; if (*cur <= budget) break;
        uint64_t t = clock_gettime_nsec_np(CLOCK_UPTIME_RAW), d = mtl_done_fence();
        if (d != done || atomic_load(&g_inflight)) { done = d; quiet = t; }
        else if (t - quiet > 20000000ull) break;
        if (t - t0 > 2000000000ull) break;
        usleep(500);
    }
#endif
    *waited_ms = (double)(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0) / 1e6;
    return *cur <= budget;
}
static void gpu_budget_check(void) {
    uint64_t cur, budget; if (!gpu_budget_exceeded(&cur, &budget)) return;
    uint64_t first = cur; double ms;
    if (gpu_budget_settle(budget, &cur, &ms)) {
        static unsigned spikes; spikes++;
        if (spikes <= 8 || !(spikes % 100)) MLOG("GPU allocated %llu MB, above the budget of %llu MB (%u times): %llu MB once the running work completed and released what it held (%.1f ms)",
            (unsigned long long)(first >> 20), (unsigned long long)(budget >> 20), spikes, (unsigned long long)(cur >> 20), ms);
        return;
    }
    res_census((unsigned long long)atomic_load(&g_cur_frame), 0);
    MDIE("GPU allocated %llu MB, above the budget of %llu MB (%llu MB before the running work completed and released what it held, %.1f ms): stopping before the driver runs out of wired memory (DM_NVN_GPU_BUDGET_MB)",
         (unsigned long long)(cur >> 20), (unsigned long long)(budget >> 20), (unsigned long long)(first >> 20), ms);
}
uint64_t mtl_sub_commit(void) {
    uint64_t tr = trace_now(); unsigned tr_part = ++g_tr_parts;
    gpu_budget_check();
    defer_fallback();   // Deferred merged pass: at the end of the submission there are still P draws not replayed (the normal path never gets here)
    upd_flush(); upd_sub_end();   // renamed uniform updates reach memory at the end of their submission
    rp_flush();
    wb_stage_encode();   // 278's targets rendered in this submission, copied for the write-back after a fence wait (writeback.inc, 286)
    if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    up_flush();
    uint64_t fence = ++g_fence;
    keepalive_start(); atomic_store(&g_last_sub_ns, clock_gettime_nsec_np(CLOCK_UPTIME_RAW)); atomic_fetch_add(&g_inflight, 1);
    if (g_vis_ring_i >= 0) g_vis_ring[g_vis_ring_i].fence = fence;
    // CPU-visible effects after completion: copy the op table into the callback
    int n = g_npost; post_op *ops = n ? malloc(n * sizeof *ops) : NULL;
    if (n) memcpy(ops, g_post, n * sizeof *ops);
    gw_commit();
    NSArray<dispatch_block_t> *commit_blocks = [g_commit_blocks copy];
#if !DM_NVN_M3
    MTL4CommitOptions *o = [MTL4CommitOptions new];
#endif
    [FB_ON(o, g_cb) FB_SEL:^(FB_ARG f) { (void)f; for (dispatch_block_t block in commit_blocks) block(); }];
    pg_commit(FB_ON(o, g_cb));
    uint64_t gpu_f = atomic_load(&g_cur_frame); gpu_tag_begin(gpu_f);
    NSString *cctx = @(cctx_snap());
    uint32_t work = g_sub_work; sub_clk *sclk = g_sclk; g_sclk = NULL;   // the parts committed before this one (mid-submission commits), or none
    double tsf = atomic_load(&g_ts_factor) / 1000.0;   // the frame-rate trial's factor when this submission was committed (plan item 216)
    [FB_ON(o, g_cb) FB_SEL:^(FB_ARG cb) {
        gpu_tag_done(gpu_f, cb.GPUStartTime, cb.GPUEndTime); trace_at(TR_GPU, cb.GPUStartTime, cb.GPUEndTime, fence, tr_part);
        // each report's place among the parts' GPU start / end on the guest clock (nvn_ts.h ts_place): parts committed earlier, then this one
        int parts = 1, inside = n && ts_inside(); uint32_t pw[SUBCLK_N + 2] = {0}; uint64_t ps[SUBCLK_N + 1], pe[SUBCLK_N + 1], late = 0;
        if (inside) { uint64_t hn, gn; ts_now(&hn, &gn); late = gpu_late_ns();
            if (sclk) for (int k = 0; k < sclk->n; k++) { pw[k + 1] = sclk->w[k + 1]; ps[k] = atomic_load(&sclk->s[k]); pe[k] = atomic_load(&sclk->e[k]); parts++; }
            pw[parts] = work; ps[parts - 1] = ts_guest_ns(cb.GPUStartTime, hn, gn); pe[parts - 1] = ts_guest_ns(cb.GPUEndTime, hn, gn); }
        sclk_put(sclk);
        double tsk = ts_scale() * tsf;
        #define REPORT_TICKS(o_) ({ uint64_t ns_ = inside ? ts_place(pw, ps, pe, parts, (o_).pos, (o_).type == NVN_COUNTER_TYPE_TIMESTAMP_TOP) : 0; \
            if (ns_ && tsk != 1 && ps[0] && ns_ > ps[0]) ns_ = ps[0] + (uint64_t)((ns_ - ps[0]) * tsk); ns_ ? (ns_ + late) * 12 / 625 : gpu_ticks(); })
        if (cb.error) { atomic_store(&g_last_commit_error, 1); static int n, nv; int victim = [cb.error.description containsString:@"InnocentVictim"]; if (victim ? nv++ < 1 : n++ < 8) MLOG("[red] command buffer error details fence=%llu: %s | GPU %.3f→%.3f", (unsigned long long)fence, cb.error.description.UTF8String, cb.GPUStartTime, cb.GPUEndTime); if (!victim) MLOG("[red] context of the failing submit fence=%llu: %s", (unsigned long long)fence, cctx.UTF8String); MLOG("[red] command buffer execution error: %s", cb.error.localizedDescription.UTF8String); }
        for (int i = 0; i < n; i++) {
            if (ops[i].kind == 1) { if (ops[i].mode == 1) *ops[i].val += ops[i].v; else *ops[i].val = ops[i].v; }
            else if (ops[i].kind == 3) {   // Samples passed: the sum of all slots since the reset
                vis_list *l = ops[i].vis; uint64_t sum = l->base;
                for (int k = 0; k < l->n; k++) { sum += ((const uint64_t *)((__bridge id<MTLBuffer>)l->s[k].buf).contents)[l->s[k].slot]; CFRelease(l->s[k].buf); }
                free(l);
                { static long long force = -2; if (force == -2) { const char *e = getenv("DM_NVN_OCC_FORCE"); force = e ? atoll(e) : -1;   // diagnostic: every samples-passed result this value
                      if (force >= 0) MLOG("[red] DM_NVN_OCC_FORCE=%lld: every samples-passed result reported as that (a diagnostic)", force); }
                  if (force >= 0) sum = (uint64_t)force; }
                uint64_t rec[2] = {sum, REPORT_TICKS(ops[i])}; memcpy((void *)ops[i].addr, rec, sizeof rec);
            }
            else { uint64_t t = REPORT_TICKS(ops[i]); memcpy((uint8_t *)(uintptr_t)ops[i].addr + 8, &t, 8); }   // the timestamp half only: the counter half was written in command order at replay (see OP_REPORT_COUNTER)
        }
        #undef REPORT_TICKS
        free(ops);
        pthread_mutex_lock(&g_mu); if (g_test_done_hold) { if (fence > g_test_done_held) g_test_done_held = fence; } else if (fence > g_done) g_done = fence; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_mu);
        if (atomic_fetch_sub(&g_inflight, 1) == 1 && g_ka_idle) dispatch_semaphore_signal(g_ka_idle);
        defer_drain();
    }];
    void arena_commit(uint64_t fence); arena_commit(fence);
    res_commit(); rs_sub_end();   // the encode stage has ended its last pass before the command buffer ends
#if DM_NVN_M3
    [g_cb encodeSignalEvent:g_ev value:fence];
    [g_cb commit];
#else
    [g_cb endCommandBuffer];
    CBUF buffers[] = {g_cb};
    [g_q4 commit:buffers count:1 options:o];
    [g_q4 signalEvent:g_ev value:fence];
#endif
    cb_committed(); trace_end(TR_COMMIT, tr, fence, tr_part);
    g_us.n = 0;   // the uniform copies belong to this submission
    g_cap_cur = NULL;   // and its capture (the translation thread frees it)
    g_alloc_fence[g_alloc_cur] = fence;
    g_cb = nil;
    g_commit_blocks = nil;
    objc_autoreleasePoolPop(g_sub_pool); g_sub_pool = NULL;
    return fence;
}

uint64_t mtl_done_fence(void) { pthread_mutex_lock(&g_mu); uint64_t d = g_done; pthread_mutex_unlock(&g_mu); return d; }   // highest completed submission fence
uint64_t mtl_last_fence(void) { return g_fence; }
int mtl_last_commit_error(void) { return atomic_load(&g_last_commit_error); }

// Test only (t_nvn, plan item 142): while held, completed submissions do not move the done fence, as if the GPU were still on them; letting go
// moves it to the newest completed. Under g_mu
void mtl_test_done_hold(int on) {
    pthread_mutex_lock(&g_mu); g_test_done_hold = on;
    if (!on && g_test_done_held > g_done) { g_done = g_test_done_held; pthread_cond_broadcast(&g_cv); }
    pthread_mutex_unlock(&g_mu);
}
// Test only (t_rqueue, plan item 61a): while held, the done fence moved up to f (no further than the newest completed); returns the newest completed
uint64_t mtl_test_done_to(uint64_t f) {
    pthread_mutex_lock(&g_mu); uint64_t to = f < g_test_done_held ? f : g_test_done_held, held = g_test_done_held;
    if (g_test_done_hold && to > g_done) { g_done = to; pthread_cond_broadcast(&g_cv); }
    pthread_mutex_unlock(&g_mu); return held;
}
int mtl_wait(uint64_t fence, uint64_t timeout_ns) { @autoreleasepool {
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl);
    uint64_t ns = (uint64_t)dl.tv_nsec + (timeout_ns > 3600000000000ull ? 3600000000000ull : timeout_ns);
    dl.tv_sec += ns / 1000000000ull; dl.tv_nsec = ns % 1000000000ull;
    pthread_mutex_lock(&g_mu);
    int r = 0;
    while (g_done < fence) if ((r = pthread_cond_timedwait(&g_cv, &g_mu, &dl))) break;
    int ok = g_done >= fence;
    pthread_mutex_unlock(&g_mu);
    return ok;
} }

static void mtl_finish_gpu(void) {
    if (g_fence) mtl_wait(g_fence, UINT64_MAX);
    up_wait_zero(&g_up_pending);
}
void mtl_present_flush(void);
// nvnQueueFinish waits for the GPU work submitted so far, not for queued presents to be shown: with a window the display link takes one present per
// tick, so each call cost a display frame or two (seedds/nxnative 3b72aea, MIT: Fire Emblem: Three Houses calls it after every resource upload, its
// first load took 78 s on an iPhone). A swapchain texture is still handed back only after its previous present (AcquireTexture); mtl_drain flushes
void mtl_finish(void) { @autoreleasepool { mtl_finish_gpu(); } }
static void present_drop_for_suspend(void);
void mtl_drain(void) { @autoreleasepool { present_drop_for_suspend(); mtl_present_flush(); mtl_init(); id<MTLCommandBuffer> cb = q_cb(); [cb commit]; [cb waitUntilCompleted]; mtl_finish_gpu(); } }

// ================= Render replay =================
#include "../../../host/front.h"
// A storage binding's buffer: one whose range runs past the end of its pool binds from the pool it starts in (plan item 233, after nxport 79b736d,
// MIT). Pools are Metal buffers of their own here, so what lies past the end is not the next pool as in the console's address space; the shader
// reaches what it uses from the start. Before, such a binding, and one in no pool, fell silently to the dummy buffer: the GPU's writes were lost
static uint64_t g_sbuf_past, g_sbuf_lost;
void mtl_sbuf_stats(uint64_t *past, uint64_t *lost) { *past = g_sbuf_past; *lost = g_sbuf_lost; }
// The bytes a storage binding covers, for the uniform snapshots a shader writing it drops: its size, or with size 0 the rest of the pool its address
// lies in (Breath of the Wild binds its particle outputs so; seedds/nxnative f293a70, MIT; plan item 296: it dropped one byte); 1 outside the pools
static uint64_t sbuf_extent(uint64_t addr, uint64_t len) {
    if (len) return len;
#ifndef DM_TEST_SBUF_SIZE0_ONE   // negative control of t_argbind only: one byte, as before
    void *mb; uint64_t mo; if (mtl_addr(addr, 1, &mb, &mo)) return ((__bridge id<MTLBuffer>)mb).length - mo;
#endif
    return 1;
}
static int sbuf_gpu_addr(uint64_t addr, uint64_t len, void **buf, uint64_t *off, uint64_t *gpu) {
    if (mtl_gpu_addr(addr, len ? len : 1, buf, off, gpu)) return 1;
#ifndef DM_TEST_SBUF_WHOLE_RANGE   // negative control of t_sbufpast only: the whole range or the dummy, as before
    if (len > 1 && mtl_gpu_addr(addr, 1, buf, off, gpu)) {
        if (!g_sbuf_past++) MLOG("[red] storage buffer %#llx + %llu runs past the end of its pool: bound from the pool it starts in (first case)", (unsigned long long)addr, (unsigned long long)len);
        return 1;
    }
#endif
    if (!g_sbuf_lost++) MLOG("[red] storage buffer %#llx + %llu is in no pool: the dummy buffer, its writes lost (first case)", (unsigned long long)addr, (unsigned long long)len);
    return 0;
}

