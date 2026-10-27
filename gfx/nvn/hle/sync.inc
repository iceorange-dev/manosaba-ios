// gfx/nvn/hle/sync.inc, part of hle_nvn.c (included there in order, one translation unit): Sync objects and events
// ---------- Sync / events ----------
// nvnProgramSetShadersExt (NVN 55; TotK main 0x72ff70): each stage is 0x18 bytes = NVNshaderData {data, control} + one 8-byte extension field. The host
// identifies stages by the control section and does not use the extension: converted to plain NVNshaderData for SetShaders. From nxnative (seedds, MIT)
typedef struct { NVNbufferAddress data; const void *control; uint64_t ext; } shader_data_ext;
NVN_NOPROTO(nvnProgramSetShadersExt, NVNboolean, NVNprogram *p, int n, const shader_data_ext *sd) {
    if (n < 0 || n > 6) { LOG("[red] ProgramSetShadersExt: %d stages", n); return 0; }
    NVNshaderData plain[6];
    for (int i = 0; i < n; i++) {
        plain[i].data = sd[i].data; plain[i].control = sd[i].control;
        if (sd[i].ext) { static int once; if (!once++) LOG("[nvn] ProgramSetShadersExt: extension field %#llx not used by the host", (unsigned long long)sd[i].ext); }
    }
    return h_nvnProgramSetShaders(p, n, plain);
}
NVN(nvnSyncInitialize, NVNboolean, NVNsync *s, NVNdevice *d) { (void)d; *(sync_t *)s = (sync_t){MAGIC('S', 'Y', 'N', 'C'), 0, 0}; return 1; }
NVN(nvnSyncFinalize, void, NVNsync *s) { ((sync_t *)s)->magic = 0; }
// Fence = "all commands previously submitted to the queue have completed": record the fence value of the most recent submit
NVN(nvnQueueFenceSync, void, NVNqueue *q, NVNsync *s, NVNsyncCondition c, int f) {
    (void)q; (void)c; (void)f;
    if (xlat_on()) { uint64_t k = x_token(); sync_token_store((sync_t *)s, k); trace_end(TR_FENCE, trace_now(), k | SYNC_TOKEN, 0); return; }   // the submissions queued so far (plan item 61)
    uint64_t fe = mtl_last_fence(); sync_fence_store((sync_t *)s, fe); trace_end(TR_FENCE, trace_now(), fe, 0);
}
// A sync object that was never fenced: FAILED, as the SDK's NVNsyncWaitResult says (plan item 116; it was TIMEOUT_EXPIRED; dr3-mobile follows the
// SDK too). Counted and logged: neither game does it (the prologue, the cliff save, manosaba: 0). A fenced one is released only after the GPU has
// actually completed up to that fence value
// The sync of the last nvnWindowAcquireTexture: a wait on it is the game's thread between two frames (dm_game_frame_sync, plan item 167)
static const void *_Atomic g_acq_sync_last;
__attribute__((weak)) void dm_game_frame_sync(void) {}
static NVNsyncWaitResult sync_wait(const NVNsync *s, uint64_t timeout);
// CPU waits on syncs (plan item 177, measured before LASRecomp's dc1d28f: a game that waits for its newest submission right after presenting it
// never overlaps a frame's CPU work with the last one's GPU work): how many, how many on the newest submission still running, how long. The
// timing summary prints them a frame
static _Atomic uint64_t g_sw_n, g_sw_newest, g_sw_ns;
void nvn_syncwait_stats(uint64_t out[3]) { out[0] = atomic_load(&g_sw_n); out[1] = atomic_load(&g_sw_newest); out[2] = atomic_load(&g_sw_ns); }
NVN(nvnSyncWait, NVNsyncWaitResult, const NVNsync *s, uint64_t timeout) {
    uint64_t f0 = 0, t0 = now_ns(), tr = trace_now(); int newest = s && sync_fence_load((const sync_t *)s, &f0) && f0 && f0 == mtl_last_fence() && mtl_done_fence() < f0;
    if (s && (f0 & SYNC_TOKEN)) { uint64_t k = f0 & ~SYNC_TOKEN, fk; newest = k && k == x_token() && !(x_token_fence(k, 0, &fk) && mtl_done_fence() >= fk); }
#ifdef DM_TEST_SYNCWAIT_DONE_TOO   // negative control of t_nvn only: a wait on the newest fence counted even when it is done
    newest = s && f0 && f0 == mtl_last_fence();
#endif
    NVNsyncWaitResult r = sync_wait(s, timeout);
    if (r == NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED || r == NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED) mtl_wb_fence_done();   // what the GPU wrote is in memory now
    trace_end(TR_WAIT, tr, f0, (uint64_t)r);
    atomic_fetch_add(&g_sw_n, 1); if (newest) atomic_fetch_add(&g_sw_newest, 1); atomic_fetch_add(&g_sw_ns, now_ns() - t0);
    if (s && s == atomic_load_explicit(&g_acq_sync_last, memory_order_relaxed)) dm_game_frame_sync();
    return r;
}
static NVNsyncWaitResult sync_wait(const NVNsync *s, uint64_t timeout) {
    NVN_BLOCKING();
    uint64_t fence;
    if (!sync_fence_load((const sync_t *)s, &fence)) {
        static _Atomic uint64_t n; uint64_t k = atomic_fetch_add(&n, 1) + 1;
        if (k <= 3 || !(k & (k - 1))) LOG("[nvn] SyncWait on a sync never fenced (#%llu, timeout %llu ns, sync %p): FAILED", (unsigned long long)k, (unsigned long long)timeout, (const void *)s);
#ifndef DM_TEST_SYNC_UNFENCED_TIMEOUT   // negative control of t_nvn only: TIMEOUT_EXPIRED, as before
        return NVN_SYNC_WAIT_RESULT_FAILED;
#else
        return NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;
#endif
    }
    if (fence & SYNC_TOKEN) {   // a submission in the translation thread's queue when it was fenced (plan item 61): its translation, then its Metal fence
        uint64_t f, t0 = now_ns();
        if (x_token_fence(fence & ~SYNC_TOKEN, 0, &f) && mtl_wait(f, 0)) return NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED;
        if (!x_token_fence(fence & ~SYNC_TOKEN, timeout, &f)) return NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;
        uint64_t used = now_ns() - t0;
        return mtl_wait(f, timeout > used ? timeout - used : 0) ? NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED : NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;
    }
    if (mtl_wait(fence, 0)) return NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED;
    return mtl_wait(fence, timeout) ? NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED : NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;
}
NVN(nvnEventBuilderSetDefaults, void, NVNeventBuilder *b) { memset(b, 0, sizeof(ev_builder_t)); }
NVN(nvnEventBuilderSetStorage, void, NVNeventBuilder *b, const NVNmemoryPool *p, int64_t off) { *(ev_builder_t *)b = (ev_builder_t){(const pool_t *)p, off}; }
NVN(nvnEventInitialize, NVNboolean, NVNevent *e, const NVNeventBuilder *b) {
    const ev_builder_t *s = (const ev_builder_t *)b;
    if (!s->pool || !s->pool->mem) { LOG("[red] EventInitialize: no storage"); return 0; }
    *(ev_t *)e = (ev_t){MAGIC('E', 'V', 'N', 'T'), (volatile uint32_t *)(s->pool->mem + s->off)}; return 1;
}
NVN(nvnEventFinalize, void, NVNevent *e) { ((ev_t *)e)->magic = 0; }
NVN(nvnEventGetValue, uint32_t, const NVNevent *e) { return *((const ev_t *)e)->val; }
static void ev_signal(const ev_t *e, int mode, uint32_t v) { if (mode == NVN_EVENT_SIGNAL_MODE_ADD) *e->val += v; else *e->val = v; }
NVN(nvnEventSignal, void, NVNevent *e, NVNeventSignalMode m, uint32_t v) { ev_signal((const ev_t *)e, m, v); }

