// gfx/nvn/mtl/chunks.inc, part of nvn_mtl.m (included there in order, one translation unit): Mid-submission commits (MacNativeGames' TOTK_CHUNK_DRAWS)
// ---------- Mid-submission commits (MacNativeGames, MIT: TOTK_CHUNK_DRAWS; written anew here) ----------
// A game that puts a whole frame into one nvnQueueSubmitCommands had the replay of the frame and its GPU work strictly in sequence. Where an encoder is
// about to open and none is open, the work encoded so far is committed as a command buffer of its own once enough draws went into it, and the
// submission goes on in a fresh one (same allocator), so the GPU starts on the first passes while the replay encodes the rest. Every encoder opens
// with a queue barrier, which orders it after all earlier work on the queue, across command buffers too; this submission's uploads are flushed
// first. The fence, the counter / query write-backs, the arena and the deferred releases stay with the last command buffer (mtl_sub_commit).
// MacNativeGames' uncapped world-scene frame means: 150/500 draws 12.50 ms, 60/75 11.66 ms. Not while the TBDR rewrites that merge or defer passes are
// on (defer, fuse, hoist: their passes span the submission; MacNativeGames turns the rewrites off for Tears of the Kingdom); the others decide within a
// pass or at its end, and a commit comes only where no encoder is open (plan item 228: DM_NVN_TBDR=1 DM_NVN_TBDR_OFF=defer,fuse,hoist keeps the
// commits). DM_NVN_CHUNK_DRAWS=<n> (default 75, 0 = off), DM_NVN_CHUNK_FIRST=<n> (default 60)
// A commit is there so that the GPU does not sit idle while the replay encodes; with the game's work still on the GPU (g_inflight: earlier parts, the
// last submission's end; the keep-alive's spin kernels are not counted) it gains nothing and puts one more command buffer in the queue (plan item
// 315b; S2Arm, Apache-2.0, written anew: "the GPU waited 5-40 ms with the next one already committed"). So a commit that is due waits for the first
// encoder boundary where the GPU has none of the game's work left (S2Arm starts the draw count again instead); the parts' clocks (nvn_ts.h) then
// cover the merged parts. DM_NVN_CHUNK_BUSY=0: every due commit is made, the GPU busy or not
static int g_chunk_first = -1, g_chunk_size, g_sub_chunks, g_chunk_busy = -1; static uint64_t g_chunk_mark, g_chunk_commits, g_chunk_held;
static int tbdr_merging(void) { return tbdr_part(TBP_DEFER) || tbdr_part(TBP_FUSE) || tbdr_part(TBP_HOIST); }
static void chunk_cfg(void) {
    if (g_chunk_first >= 0) return;
    const char *e = getenv("DM_NVN_CHUNK_DRAWS"), *f = getenv("DM_NVN_CHUNK_FIRST");
    g_chunk_size = e ? atoi(e) : 75; g_chunk_first = f ? atoi(f) : 60;
    if (g_chunk_size <= 0 || g_chunk_first <= 0) g_chunk_size = g_chunk_first = 0;
    const char *b = getenv("DM_NVN_CHUNK_BUSY"); g_chunk_busy = !b || atoi(b) != 0;
    if (g_chunk_size && !tbdr_merging()) MLOG("mid-submission commits: after %d draws, then every %d draws%s (DM_NVN_CHUNK_DRAWS=0 turns them off)%s", g_chunk_first, g_chunk_size,
                                              g_chunk_busy ? ", each at the first encoder boundary where the GPU has none of the game's work left (DM_NVN_CHUNK_BUSY=0: at once)" : ", the GPU busy or not (DM_NVN_CHUNK_BUSY=0)",
                                              tbdr_on() ? ", with the TBDR rewrites that do not merge or defer passes" : "");
}
static void mtl_sub_split(void) {
    uint64_t tr = trace_now(); unsigned tr_part = ++g_tr_parts;
    up_flush(); res_commit(); rs_sync();
    uint64_t gpu_f = atomic_load(&g_cur_frame), fence_next = g_fence + 1; gpu_tag_begin(gpu_f);
    atomic_fetch_add(&g_inflight, 1);
    // this part's place for the counter timestamps (nvn_ts.h): its work ends here, its feedback gives its GPU start / end
    if (!g_sclk && (g_sclk = calloc(1, sizeof *g_sclk))) atomic_store(&g_sclk->refs, 1);
    sub_clk *sc = g_sclk && g_sclk->n < SUBCLK_N ? g_sclk : NULL; int part = sc ? sc->n++ : 0;
    if (sc) { sc->w[part + 1] = g_sub_work; atomic_fetch_add(&sc->refs, 1); }
#define PART_CLOCK(st_, en_) do { if (sc) { uint64_t hn, gn; ts_now(&hn, &gn); atomic_store(&sc->s[part], ts_guest_ns(st_, hn, gn)); atomic_store(&sc->e[part], ts_guest_ns(en_, hn, gn)); sclk_put(sc); } } while (0)
#if DM_NVN_M3
    [g_cb addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        gpu_tag_done(gpu_f, cb.GPUStartTime, cb.GPUEndTime); PART_CLOCK(cb.GPUStartTime, cb.GPUEndTime); trace_at(TR_GPU, cb.GPUStartTime, cb.GPUEndTime, fence_next, tr_part);
        if (cb.error) { atomic_store(&g_last_commit_error, 1); static int n; if (n++ < 8) MLOG("[red] command buffer error in a part of submission %llu: %s", (unsigned long long)fence_next, cb.error.description.UTF8String); }
        if (atomic_fetch_sub(&g_inflight, 1) == 1 && g_ka_idle) dispatch_semaphore_signal(g_ka_idle); }];
    [g_cb commit]; cb_committed(); trace_end(TR_COMMIT, tr, fence_next, tr_part);
    g_cb = [g_q commandBuffer];
#else
    MTL4CommitOptions *o = [MTL4CommitOptions new];
    [o addFeedbackHandler:^(id<MTL4CommitFeedback> f) {
        gpu_tag_done(gpu_f, f.GPUStartTime, f.GPUEndTime); PART_CLOCK(f.GPUStartTime, f.GPUEndTime); trace_at(TR_GPU, f.GPUStartTime, f.GPUEndTime, fence_next, tr_part);
        if (f.error) { atomic_store(&g_last_commit_error, 1); static int n; if (n++ < 8) MLOG("[red] command buffer error in a part of submission %llu: %s", (unsigned long long)fence_next, f.error.description.UTF8String); }
        if (atomic_fetch_sub(&g_inflight, 1) == 1 && g_ka_idle) dispatch_semaphore_signal(g_ka_idle); }];
    [g_cb endCommandBuffer];
    CBUF buffers[] = {g_cb}; [g_q4 commit:buffers count:1 options:o]; cb_committed(); trace_end(TR_COMMIT, tr, fence_next, tr_part);
    g_cb = [g_dev newCommandBuffer]; [g_cb beginCommandBufferWithAllocator:g_alloc4[g_alloc_cur]];
#endif
#undef PART_CLOCK
    g_sub_chunks++; g_chunk_commits++; g_chunk_mark = g_stat_draw;
}
static void chunk_maybe_split(void) {   // where a new encoder is about to open and none is open
    chunk_cfg();
#ifndef DM_TEST_CHUNK_ANY_TBDR   // negative control of t_chunk only: no commits while any TBDR rewrite is on, as before
    if (!g_chunk_size || !g_cb || g_enc || tbdr_merging()) return;
#else
    if (!g_chunk_size || !g_cb || g_enc || tbdr_on()) return;
#endif
    if (g_stat_draw - g_chunk_mark < (uint64_t)(g_sub_chunks ? g_chunk_size : g_chunk_first)) return;
#ifndef DM_TEST_CHUNK_BUSY_IGNORED   // negative control of t_chunk --busy only: committed with the GPU busy, as before
    if (g_chunk_busy && atomic_load_explicit(&g_inflight, memory_order_relaxed) > 0) { g_chunk_held++; return; }   // the count goes on: the next boundary looks again
#endif
    if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    mtl_sub_split();
}
uint64_t mtl_chunk_commits(void) { return g_chunk_commits; }
void mtl_chunk_stats(uint64_t out[2]) { out[0] = g_chunk_commits; out[1] = g_chunk_held; }   // timing line (hle/timing.inc): commits made, boundaries that held a due commit
static void chunk_reset(void) { g_sub_chunks = 0; g_chunk_mark = g_stat_draw; }   // mtl_sub_begin
static void fz_end(void);   // Wrap-up of the readable-tile pass merge (defined before draw)
static void defer_fallback(void);
