// gfx/nvn/mtl/encoders.inc, part of nvn_mtl.m (included there in order, one translation unit): The encoder layer: Metal 4 / Metal 3 encoder wrappers, blits, inline updates, events, counters, timestamps
// Shared with the parts after this one: the zero-source skip's sets, the command buffer, the commit blocks, the in-flight slots
typedef struct { uint64_t ser; uint16_t level, slice; } zskip_zero;
static zskip_zero g_zskip_zero[4096]; static int g_zskip_n, g_zskip_count;
static zskip_zero g_zskip_cand[8]; static uint8_t g_zskip_cand_ok[8];
static void zskip_clear(void);
id g_cb;
BENC g_blit;
NSMutableArray<dispatch_block_t> *g_commit_blocks;
static uint64_t g_alloc_fence[ALLOC_MAX]; static uint64_t g_alloc_waits, g_alloc_grown;
int mtl_wait(uint64_t fence, uint64_t timeout_ns);
static void up_flush(void);
// The upload batch belongs only to the submit thread (the thread that called mtl_sub_begin): the batch's encoder cannot be used across threads. Every entry point asserts current thread == submit thread,
// and on mismatch prints the stack and aborts on the spot - so that 'another thread touched the batch' is exposed deterministically on Mac too, not by timing (real iOS device stuck at the main menu: the present thread flushed the submit thread's batch via q_cb).
static pthread_t g_sub_thread; static _Atomic int g_sub_thread_set;
static int on_sub_thread(void) { return atomic_load(&g_sub_thread_set) && pthread_equal(pthread_self(), g_sub_thread); }
static void up_owner_check(const char *where) {
    if (!atomic_load(&g_sub_thread_set) || on_sub_thread()) return;
    char name[64] = ""; pthread_getname_np(pthread_self(), name, sizeof name);
    MLOG("[red] upload batches belong to the submit thread only: %s called on thread '%s'", where, name);
    void *bt[32]; int n = backtrace(bt, 32); backtrace_symbols_fd(bt, n, 2);
    abort();
}
// Every command buffer created outside the replay / upload batch goes through this. On the submit thread: flush this batch's uploads first, so that commands that then read or write these textures are ordered after the uploads;
// other threads (the present thread etc.) creating a command buffer do not touch the batch: they do not read or write textures uploaded in this batch; commands that need these textures all come after this replay submission (the batch is flushed before it)
// Old-queue command buffers (readback / diagnostics / present; to be deleted once M4-4 and M4-5 are migrated away): uploads are already on the Metal 4 queue, and there is no ordering guarantee between the two queues -
// wait for the upload to complete after flushing it, and only then can the old queue read the upload result (mtl_read_texels waited first and flushed afterwards, the readback beat the upload, 4 failures on t_nvn upload content)
id<MTLCommandBuffer> q_cb(void) { if (on_sub_thread()) { up_flush(); up_wait_zero(&g_up_pending); } return [g_q commandBuffer]; }
static post_op *g_post; static int g_npost, g_postcap;
// g_sub_work, the work replayed in this submission so far: gfx/nvn/core/exec.h
// The parts of a submission committed before its end (mid-submission commits): their work boundaries and GPU start / end on the guest clock, filled
// by each part's feedback, read by the last part's (nvn_ts.h ts_place). Allocated at the first part; the submission and every part hold a reference
#define SUBCLK_N 128
typedef struct { _Atomic int refs; int n; uint32_t w[SUBCLK_N + 1]; _Atomic uint64_t s[SUBCLK_N], e[SUBCLK_N]; } sub_clk;
static sub_clk *g_sclk;
static void sclk_put(sub_clk *c) { if (c && atomic_fetch_sub(&c->refs, 1) == 1) free(c); }
uint64_t guest_ns(void);
static void ts_now(uint64_t *host_ns, uint64_t *guest) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); *host_ns = mach_absolute_time() * tb.numer / tb.denom; *guest = guest_ns(); }
// One autorelease pool per submission: submissions run on a guest thread (long-lived, no run loop, never drains the implicit pool), so objects autoreleased in the Metal replay
// (command buffers, encoders, pass descriptors, etc.) are never released unless put in a pool. Measured on the character select screen of a real iOS device: the host heap grows by about 47 objects per frame, 0.22 MiB/s.
void *objc_autoreleasePoolPush(void); void objc_autoreleasePoolPop(void *);
static void *g_sub_pool;

static uint64_t g_sub_serial;   // Submission index (used by the parallel precompile's 'predict only once per submission')
static void fin_drain(void);
static void chunk_reset(void);   // mid-submission commits (defined with the render pass state)
// DM_NVN_PASSGPU's pg_sub_begin / pg_commit: mtl/passgpu.m (declared in mtl/mtl.h)
static void pkg_census(void);   // DM_NVN_COUNTS: uploaded textures by last use
static unsigned g_tr_parts;   // parts of this submission committed so far (nvn_trace.h commit / gpu)
void mtl_sub_begin(void) {
    g_tr_parts = 0;
    g_bh_gen = g_bh_gen >= 0x7ffffffe ? 2 : g_bh_gen + 1;   // a new submission: deferred handles read again (bh_fresh; mtl_sub_index's walk may read first)
    mtl_init(); g_sub_serial++;
    if (tbdr_on() == 0 && g_ruler_pending) { g_ruler_truth = 0; g_ruler_pending = 0; MLOG("DM_NVN_RULER: switched to the measured mode"); }
    // A pool of their own: they run before the submission's pool below, on the game's submit thread, which has none and never drains one. The
    // retired objects they release went into the thread's implicit pool and lived for good (nxnative 0fc0ce4: Metroid Dread's GPU memory grew by
    // gigabytes; t_retire)
    @autoreleasepool { defer_drain(); defer_reset(); fin_drain(); warm_claim(); }
    wb_census_tick(); wb_cpu_pass(); wb_idle_pass();
    if (g_cb) MDIE("submit re-entered");
    g_sub_thread = pthread_self(); atomic_store(&g_sub_thread_set, 1);
    atomic_store(&g_last_commit_error, 0);
    g_sub_pool = objc_autoreleasePoolPush();
#if DM_NVN_M3
    g_cb = [g_q commandBuffer];
#else
    g_cb = [g_dev newCommandBuffer];
#endif
    // Do not wait for the GPU mid-frame (hot real device: with a fixed rotation of 3, when the GPU cannot keep up every frame blocks here, and the submit thread runs at low frequency after waking, instruction count −18% but CPU time +49%):
    // pick an allocator the GPU has finished with; if all are in use create a new one; only at the cap wait for the oldest one. How far ahead it runs is bounded by the wait where the game acquires the swapchain texture, same as the old interface.
    int alloc_i = -1; uint64_t done = g_done;
    for (int i = 0; i < g_alloc_n; i++) if (g_alloc_fence[i] <= done) { alloc_i = i; break; }
#if DM_NVN_M3
    if (alloc_i < 0 && g_alloc_n < ALLOC_MAX) { alloc_i = g_alloc_n++; g_alloc_grown++; }
#else
    if (alloc_i < 0 && g_alloc_n < ALLOC_MAX) { NSError *ae = nil; MTL4CommandAllocatorDescriptor *ad = [MTL4CommandAllocatorDescriptor new];
        g_alloc4[g_alloc_n] = [g_dev newCommandAllocatorWithDescriptor:ad error:&ae]; if (!g_alloc4[g_alloc_n]) MDIE("failed to create new MTL4CommandAllocator: %s", ae.localizedDescription.UTF8String);
        alloc_i = g_alloc_n++; g_alloc_grown++; }
#endif
    if (alloc_i < 0) { alloc_i = 0; for (int i = 1; i < g_alloc_n; i++) if (g_alloc_fence[i] < g_alloc_fence[alloc_i]) alloc_i = i; g_alloc_waits++; mtl_wait(g_alloc_fence[alloc_i], UINT64_MAX); }
    g_alloc_cur = alloc_i;
#if !DM_NVN_M3
    [g_alloc4[alloc_i] reset];
    [g_cb beginCommandBufferWithAllocator:g_alloc4[alloc_i]];
#endif
    g_commit_blocks = [NSMutableArray array];
    pg_sub_begin(g_fence + 1);
    id<MTLBuffer> vis_prev = g_vis;   // the previous submission's: an overflow buffer is retired below
    g_cnt_sub_ts = 0;
    if (g_cnt_on > 0 && g_sub_serial % 300 == 0) pkg_census();
    g_npost = 0; g_sub_work = 0; g_sclk = NULL; g_vis = nil; g_vis_n = 0; g_vis_cur = -1; pend_begin(mtl_done_fence());
    zskip_clear(); g_zskip_count = 0; chunk_reset();
    int pick = -1;
    for (int i = 0; i < g_vis_ring_n; i++) if (g_vis_ring[i].fence <= g_done) { pick = i; break; }
    if (pick < 0 && g_vis_ring_n < 4) pick = g_vis_ring_n++;
    if (pick < 0) { uint64_t oldest = UINT64_MAX; for (int i = 0; i < 4; i++) if (g_vis_ring[i].fence < oldest) { oldest = g_vis_ring[i].fence; pick = i; } if (oldest > g_done) mtl_wait(oldest, UINT64_MAX); }
    g_vis_ring_i = pick; g_vis_ring[pick].fence = UINT64_MAX;
    if (!g_vis_ring[pick].buf) { g_vis_ring[pick].buf = [g_dev newBufferWithLength:VIS_SLOTS * 8 options:MTLResourceStorageModeShared]; res_add(g_vis_ring[pick].buf); }
    if (vis_prev && !vis_in_ring(vis_prev)) defer_obj_release(vis_prev);   // the overflow buffer left by the previous submission (see the place with g_vis_n >= VIS_SLOTS): retire it, otherwise it stays in the residency set forever (it was read after g_vis was cleared, and never retired)
    g_vis = g_vis_ring[pick].buf;
    vis_recycle(g_vis);
    void arena_begin(void); arena_begin();
}

// Metal 4 conservative barriers (first version): a consumer barrier at the start of an encoder (all prior work → this encoder) and a producer barrier at the end (this encoder and earlier → later encoders).
// Only the one at the start is not enough: t_view_levels' 'pass 1 → pass 2 → copy' (then t_rt_range) read back black, and after adding the end barrier it is 19/19. Old-queue encoders (upload / diagnostics / present) do not go through these barriers.
#if DM_NVN_M3
static inline void m3_rbuf(id<MTLRenderCommandEncoder> e, int vertex, NSUInteger i, uint64_t gpu) {
    NSUInteger off; id<MTLBuffer> b = m3_buf(gpu, &off); if (gpu && !b) { { static int n_; if (!n_++) MLOG("[red] " "legacy API bind: GPU address %#llx is not in any registered buffer (slot %lu)", (unsigned long long)gpu, (unsigned long)i); } return; }
    if (vertex) [e setVertexBuffer:b offset:off atIndex:i]; else [e setFragmentBuffer:b offset:off atIndex:i];
}
#else
#endif
// Render state already set on the current encoder (MacNativeGames' es_* filter, MIT; written anew here): the replay sets viewport, scissor, cull,
// winding, fill, depth bias, depth clip, stencil reference and blend color for every draw, nearly always unchanged (about 30,000 encoder calls a
// frame in Tears of the Kingdom). The cache belongs to one encoder: reset where a render encoder opens and where it ends (t_esfilter)
// vb: the guest address of each vertex stream bound on the encoder (its buffer index 16 + stream), valid while the pool generation is vb_gen;
// vb_zero: index 30 holds the zero buffer (stream 14 shares that index)
static struct { __unsafe_unretained id en; MTLViewport vp; MTLScissorRect sc; int cull, wind, fill, clip; float bias[3], bc[4], db[2]; uint32_t sref; uint8_t has_vp, has_sc, has_bias, has_bc, has_sref, has_db;
                uint64_t vb[16], vb_gen; uint8_t vb_zero; } g_es;
static uint64_t g_es_sets, g_es_skips, g_vb_sets, g_vb_skips;
static inline void es_reset(id e) { memset(&g_es, 0, sizeof g_es); g_es.cull = g_es.wind = g_es.fill = g_es.clip = -1; g_es.en = e; }
static inline int es_mine(id e) { if (g_es.en != e) es_reset(e); return 1; }
void mtl_es_stats(uint64_t *sets, uint64_t *skips) { *sets = g_es_sets; *skips = g_es_skips; }
void mtl_vb_stats(uint64_t *sets, uint64_t *skips) { *sets = g_vb_sets; *skips = g_vb_skips; }
#if DM_NVN_M3
static inline RENC enc_r_begin(RPD *d) { RENC e = [g_cb renderCommandEncoderWithDescriptor:d]; es_reset(e); return e; }
static inline void enc_r_end(id e) { m4_end(e); es_reset(nil); }
#define RS(call) do {} while (0)   // no encode stage on the older API
static inline void rs_sync(void) {}
static inline void rs_sub_end(void) {}
#else
#include "../nvn_render_stream.h"
#define RS(call) do { if (rs_mine(e)) { call; return; } } while (0)   // a pass the encode stage owns: the call becomes a packet
static inline RENC enc_r_begin(RPD *d) { if (rs_stream_pass()) { id s = rs_pass_begin(d, g_cb); es_reset(s); return s; }
    RENC e = [g_cb renderCommandEncoderWithDescriptor:d]; es_reset(e); [e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; [e setArgumentTable:g_vt atStages:MTLRenderStageVertex]; [e setArgumentTable:g_ft atStages:MTLRenderStageFragment]; return e; }
static inline void enc_r_end(id e) { if (rs_mine(e)) rs_pass_end(); else m4_end(e); es_reset(nil); }
#endif
static inline void enc_r_pso(RENC e, id<MTLRenderPipelineState> p) { RS(rs_obj(RS_PSO, p)); [e setRenderPipelineState:p]; }
static inline void enc_r_dss(RENC e, id<MTLDepthStencilState> s) { RS(rs_obj(RS_DSS, s)); [e setDepthStencilState:s]; }
static inline void enc_r_store(RENC e, int which, MTLStoreAction a, NSUInteger i) {   // which: 0 colour attachment i, 1 depth, 2 stencil (deferred store actions)
    RS(rs_op(RS_STORE, (uint64_t)which, (uint64_t)a, i));
    if (which == 1) [e setDepthStoreAction:a]; else if (which == 2) [e setStencilStoreAction:a]; else [e setColorStoreAction:a atIndex:i]; }
#define ES_SAME(cond) do { es_mine(e); if (cond) { g_es_skips++; return; } g_es_sets++; } while (0)
static inline void enc_r_viewport(RENC e, MTLViewport v) { ES_SAME(g_es.has_vp && !memcmp(&g_es.vp, &v, sizeof v)); g_es.vp = v; g_es.has_vp = 1; RS(rs_bytes(RS_VIEWPORT, &v, sizeof v)); [e setViewport:v]; }
static inline void enc_r_scissor(RENC e, MTLScissorRect r) { ES_SAME(g_es.has_sc && !memcmp(&g_es.sc, &r, sizeof r)); g_es.sc = r; g_es.has_sc = 1; RS(rs_bytes(RS_SCISSOR, &r, sizeof r)); [e setScissorRect:r]; }
static inline void enc_r_cull(RENC e, MTLCullMode m) { ES_SAME(g_es.cull == (int)m); g_es.cull = (int)m; RS(rs_op(RS_CULL, m, 0, 0)); [e setCullMode:m]; }
static inline void enc_r_winding(RENC e, MTLWinding w) { ES_SAME(g_es.wind == (int)w); g_es.wind = (int)w; RS(rs_op(RS_WINDING, w, 0, 0)); [e setFrontFacingWinding:w]; }
static inline void enc_r_fill(RENC e, MTLTriangleFillMode m) { ES_SAME(g_es.fill == (int)m); g_es.fill = (int)m; RS(rs_op(RS_FILL, m, 0, 0)); [e setTriangleFillMode:m]; }
static inline void enc_r_depth_bias(RENC e, float u, float s, float c) { float b[3] = {u, s, c}; ES_SAME(g_es.has_bias && !memcmp(g_es.bias, b, sizeof b)); memcpy(g_es.bias, b, sizeof b); g_es.has_bias = 1; RS(rs_bytes(RS_BIAS, b, sizeof b)); [e setDepthBias:u slopeScale:s clamp:c]; }
// Depth bounds (plan item 64c): fragments whose stored depth is outside [lo, hi] discarded; (0, 1) turns the test off (Metal's header)
static inline void enc_r_depth_bounds(RENC e, float lo, float hi) {
    float b[2] = {lo, hi}; ES_SAME(g_es.has_db ? !memcmp(g_es.db, b, sizeof b) : lo == 0 && hi == 1); memcpy(g_es.db, b, sizeof b); g_es.has_db = 1; RS(rs_bytes(RS_DBOUNDS, b, sizeof b));
#ifdef DM_TEST_DBOUNDS_NOT_SET   // negative control of gfx/tests t_dbounds only: not given to Metal, as before
    return;
#endif
#if DM_NVN_M3
    if (@available(macOS 26.0, iOS 26.0, *)) [e setDepthTestMinBound:lo maxBound:hi];
    else if (!(lo == 0 && hi == 1)) { static int once; if (!once++) MLOG("[red] depth bounds need macOS / iOS 26: not applied (first case)"); }
#else
    [e setDepthTestMinBound:lo maxBound:hi];
#endif
}
static inline void enc_r_depth_clip(RENC e, BOOL v) { MTLDepthClipMode m = v ? MTLDepthClipModeClip : MTLDepthClipModeClamp; ES_SAME(g_es.clip == (int)m); g_es.clip = (int)m; RS(rs_op(RS_CLIP, m, 0, 0)); [e setDepthClipMode:m]; }
static inline void enc_r_stencil_ref(RENC e, uint32_t f, uint32_t b) { (void)b; ES_SAME(g_es.has_sref && g_es.sref == f); g_es.sref = f; g_es.has_sref = 1; RS(rs_op(RS_STENCIL, f, 0, 0)); [e setStencilReferenceValue:f]; }
static inline void enc_r_blend_color(RENC e, float r, float g, float b, float a) { float c[4] = {r, g, b, a}; ES_SAME(g_es.has_bc && !memcmp(g_es.bc, c, sizeof c)); memcpy(g_es.bc, c, sizeof c); g_es.has_bc = 1; RS(rs_bytes(RS_BLEND, c, sizeof c)); [e setBlendColorRed:r green:g blue:b alpha:a]; }
static inline void enc_r_visibility(RENC e, MTLVisibilityResultMode m, NSUInteger o) { RS(rs_op(RS_VISIBILITY, m, o, 0)); [e setVisibilityResultMode:m offset:o]; }
static inline void enc_r_layer(RENC e, uint32_t off) { RS(rs_op(RS_LAYER, off, 0, 0)); MTLVertexAmplificationViewMapping m = {0, off}; [e setVertexAmplificationCount:1 viewMappings:&m]; }   // the draws go to layer slice + off
#if DM_NVN_M3
static inline void enc_r_vbuf(RENC e, NSUInteger i, uint64_t gpu) { m3_rbuf(e, 1, i, gpu); }
static inline void enc_r_args(RENC e, MTLRenderStages s, uint64_t gpu, NSUInteger i) { m3_rbuf(e, s == MTLRenderStageVertex, i, gpu); }
#else
// The argument tables: once the encode stage exists it applies every address (in order with its draws, which read the tables when encoded)
static inline void enc_r_vbuf(RENC e, NSUInteger i, uint64_t gpu) { (void)e; if (g_rs_created) { rs_op(RS_ADDRESS, 0, i, gpu); return; } [g_vt setAddress:gpu atIndex:i]; }
static inline void enc_r_args(RENC e, MTLRenderStages s, uint64_t gpu, NSUInteger i) { (void)e; if (g_rs_created) { rs_op(RS_ADDRESS, s != MTLRenderStageVertex, i, gpu); return; } [(s == MTLRenderStageVertex ? g_vt : g_ft) setAddress:gpu atIndex:i]; }
#endif
static inline void enc_r_draw_instanced(RENC e, MTLPrimitiveType p, NSUInteger v, NSUInteger n, NSUInteger i, NSUInteger b) {
    RS(rs_put((rs_packet){.op = RS_INSTANCED, .u = {p, v, n, i, b}}, 1)); [e drawPrimitives:p vertexStart:v vertexCount:n instanceCount:i baseInstance:b]; }
#if DM_NVN_M3
static inline void enc_r_draw_indexed(RENC e, MTLPrimitiveType p, NSUInteger n, MTLIndexType t, uint64_t gpu, NSUInteger len, NSUInteger i, NSInteger bv, NSUInteger bi) { (void)len; if (!n) return; NSUInteger off; id<MTLBuffer> b = m3_buf(gpu, &off); if (!b) { { static int n_; if (!n_++) MLOG("[red] " "legacy API draw: index buffer address %#llx is not in any registered buffer", (unsigned long long)gpu); } return; } [e drawIndexedPrimitives:p indexCount:n indexType:t indexBuffer:b indexBufferOffset:off instanceCount:i baseVertex:bv baseInstance:bi]; }
#else
static inline void enc_r_draw_indexed(RENC e, MTLPrimitiveType p, NSUInteger n, MTLIndexType t, uint64_t gpu, NSUInteger len, NSUInteger i, NSInteger bv, NSUInteger bi) {
    RS(rs_put((rs_packet){.op = RS_INDEXED, .u = {p, n, t, gpu, len, i, (uint64_t)bv, bi}}, 1)); [e drawIndexedPrimitives:p indexCount:n indexType:t indexBuffer:gpu indexBufferLength:len instanceCount:i baseVertex:bv baseInstance:bi]; }
#endif
#if DM_NVN_M3
static inline CENC enc_c_begin(MTLComputePassDescriptor *d) { return d ? [g_cb computeCommandEncoderWithDescriptor:d] : [g_cb computeCommandEncoder]; }
#else
static inline CENC enc_c_begin(MTLComputePassDescriptor *d) { (void)d; rs_sync(); CENC e = [g_cb computeCommandEncoder]; [e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; [e setArgumentTable:g_ct]; return e; }
#endif
static inline id<MTLComputeCommandEncoder> enc_c_begin_concurrent(id<MTLCommandBuffer> cb) { return [cb computeCommandEncoderWithDispatchType:MTLDispatchTypeConcurrent]; }
static inline void enc_c_end(id e) { m4_end(e); }
static inline void enc_c_pso(id e, id<MTLComputePipelineState> p) { [e setComputePipelineState:p]; }
#if DM_NVN_M3
static inline void enc_c_args(id e, uint64_t gpu, NSUInteger i) { NSUInteger off; id<MTLBuffer> b = m3_buf(gpu, &off); if (gpu && !b) { { static int n_; if (!n_++) MLOG("[red] " "legacy API compute bind: GPU address %#llx is not in any registered buffer (slot %lu)", (unsigned long long)gpu, (unsigned long)i); } return; } [(id<MTLComputeCommandEncoder>)e setBuffer:b offset:off atIndex:i]; }
#else
static inline void enc_c_args(id e, uint64_t gpu, NSUInteger i) { (void)e; [g_ct setAddress:gpu atIndex:i]; }
#endif
static inline void enc_c_dispatch(id e, MTLSize n, MTLSize t) { [e dispatchThreadgroups:n threadsPerThreadgroup:t]; }
#if DM_NVN_M3
static inline id enc_b_begin(id cb) { return enc_b_open(cb); }
#else
static inline id enc_b_begin(id cb) { if (cb == g_cb) rs_sync(); return enc_b_open(cb); }
#endif
static void blit_note(void);   // Per-pass dump: a copy / buffer update ended the render pass (defined after PD)
static void upd_flush(void);
static void chunk_maybe_split(void);   // mid-submission commits (defined with the render pass state)
#if DM_NVN_M3
BENC blit(void) { if (g_nupd && !g_upd_flushing) upd_flush(); blit_note(); rp_end(); if (!g_blit) { chunk_maybe_split(); g_blit = enc_b_begin(g_cb); } return g_blit; }
#else
// A reused blit encoder also needs a barrier between two copies: Metal 4 does not keep the commands of one encoder in recording order, and the
// queue barrier at encoder creation covers only earlier encoders (Apple: "Synchronizing stages within a pass"). Without it a write-then-read or two
// writes to one range may overlap (seen upstream in t_nvn 5c, once in three runs). The blit encoder is a compute encoder on Metal 4: both stages
BENC blit(void) { if (g_nupd && !g_upd_flushing) upd_flush(); blit_note(); rp_end();   // logged uniform updates reach memory before any copy or dispatch
    if (!g_blit) { chunk_maybe_split(); g_blit = enc_b_begin(g_cb); [g_blit barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; }
    else [g_blit barrierAfterEncoderStages:MTLStageBlit | MTLStageDispatch beforeEncoderStages:MTLStageBlit | MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    return g_blit; }
#endif

void nvn_be_copy_b2b(uint64_t src, uint64_t dst, uint64_t n) {
    pend_note(dst, n, NULL, src);
    us_drop(dst, n);   // later readers see the copied bytes in memory
    void *sb, *db; uint64_t so, dof;
    if (!mtl_addr(src, n, &sb, &so) || !mtl_addr(dst, n, &db, &dof))
        MDIE("buffer copy address not in any memory pool src=%#llx dst=%#llx n=%#llx", (unsigned long long)src, (unsigned long long)dst, (unsigned long long)n);
    enc_b_copy_buffer(blit(), (__bridge id<MTLBuffer>)sb, so, (__bridge id<MTLBuffer>)db, dof, n);
}

// Inline uniform update (UpdateUniformBuffer): the data is copied into the host's own shared ring when the command is replayed, and from there in
// order to the destination. It used to be copied from command memory only when the GPU ran the copy: a game that recycles that command memory
// first hands over the bytes of its next commands instead (nxnative saw it in SMBW, seedds, MIT; Tears of the Kingdom reuses command memory as well)
static id<MTLBuffer> g_ubs_buf; static size_t g_ubs_off, g_ubs_size;
static void ring_put(const void *data, uint64_t n, __unsafe_unretained id<MTLBuffer> *rb, size_t *ro);
static const uint8_t *update_to(const void *data, uint64_t n, id<MTLBuffer> db, uint64_t dof) {   // returns the bytes' copy in the ring
    __unsafe_unretained id<MTLBuffer> rb; size_t ro; ring_put(data, n, &rb, &ro);
    enc_b_copy_buffer(blit(), rb, ro, db, dof, n);
    return (const uint8_t *)rb.contents + ro;
}
static void ring_put(const void *data, uint64_t n, __unsafe_unretained id<MTLBuffer> *rb, size_t *ro) {
    size_t need = (n + 15u) & ~(size_t)15u;
    if (!g_ubs_buf || g_ubs_off + need > g_ubs_size) {
        if (g_ubs_buf) defer_obj_release(g_ubs_buf);   // retired: copies already encoded still read it, released (and made non-resident) after this submission completes
        g_ubs_size = MAX((size_t)1 << 20, need);
        g_ubs_buf = [g_dev newBufferWithLength:g_ubs_size options:MTLResourceStorageModeShared];
        if (!g_ubs_buf) MDIE("uniform update ring allocation failed %#zx", g_ubs_size);
        res_add(g_ubs_buf); g_ubs_off = 0;
    }
    memcpy((uint8_t *)g_ubs_buf.contents + g_ubs_off, data, n);
    *rb = g_ubs_buf; *ro = g_ubs_off;
    g_ubs_off += need;
}
static void upd_log(uint64_t lo, uint64_t hi, const uint8_t *data);
void nvn_be_update_ub(const void *data, uint64_t dst, uint64_t n) {
    if (!n) return;
    void *db; uint64_t dof;
    if (!mtl_addr(dst, n, &db, &dof)) MDIE("uniform update destination not in any memory pool dst=%#llx n=%#llx", (unsigned long long)dst, (unsigned long long)n);
    if (ub_rename_on()) {   // renamed: logged, the render pass stays open (upd_log)
        __unsafe_unretained id<MTLBuffer> rb; size_t ro; ring_put(data, n, &rb, &ro);
        const uint8_t *bytes = (const uint8_t *)rb.contents + ro;
        pend_note(dst, n, bytes, 0); upd_log(dst, dst + n, bytes);
        return;
    }
    us_drop(dst, n);   // the update reaches memory on the GPU timeline; later readers read it there
    pend_note(dst, n, update_to(data, n, (__bridge id<MTLBuffer>)db, dof), 0);   // its bytes as copied into the ring (command memory may be reused once the submission is in)
}
static void post(post_op o) {
    if (g_npost == g_postcap) { g_postcap = g_postcap ? g_postcap * 2 : 64; g_post = realloc(g_post, g_postcap * sizeof *g_post); }
    g_post[g_npost++] = o;
}
void nvn_be_signal_event(volatile uint32_t *val, uint32_t mode, uint32_t v) { post((post_op){1, val, mode, v, 0, 0, NULL}); }
void mtl_sub_counter(uint64_t addr, uint32_t type) { post((post_op){2, NULL, 0, 0, addr, type, NULL, g_sub_work}); }
void mtl_counter_stats(uint64_t rep[32], uint64_t rst[32]) { memcpy(rep, g_cnt_report, sizeof g_cnt_report); memcpy(rst, g_cnt_reset, sizeof g_cnt_reset); }
void mtl_vis_state(int *open, uint64_t *folded) { *open = g_nvs; *folded = g_vis_folded; }   // samples-passed slots since the last reset, slots folded so far (power line)

// Counter timestamps: 19.2 MHz ticks of the guest clock, the one behind the guest's system tick (svc GetSystemTick / cntpct). On the console the GPU
// timer and the CPU's system counter share one time base, and games subtract one from the other: Tears of the Kingdom's frame-rate controller takes
// nvnDeviceGetTimestampInNanoseconds of the frame's last report, converts it with nn::os::ConvertToTick and subtracts the frame's start tick
// (main 0x2b3cff8). CLOCK_MONOTONIC counts the Mac's sleep too, mach_absolute_time does not: after 25 hours of sleep the "GPU time" came out as
// 25 hours, and the controller held the game at present interval 3 (20 fps) with CPU and GPU idle most of the frame
uint64_t guest_ns(void);
static uint64_t gpu_late_ns(void) {   // DM_NVN_TS_LATE_US=<n>: reports n µs later (the game sees a slower GPU: what does it do then)
    static int64_t late = -1; if (late < 0) { const char *e = getenv("DM_NVN_TS_LATE_US"); late = e ? atoll(e) * 1000 : 0; if (late) MLOG("DM_NVN_TS_LATE_US: counter timestamps %lld µs late", (long long)(late / 1000)); }
    return (uint64_t)late; }
static uint64_t gpu_ticks(void) { return (guest_ns() + gpu_late_ns()) * 12 / 625; }
// DM_NVN_TS_SCALE=<x>: report times spread x times further from their submission's GPU start (the game sees a GPU x times slower: does its dynamic
// resolution react), 1 = off
static double ts_scale(void) { static double v = -1; if (v < 0) { const char *e = getenv("DM_NVN_TS_SCALE"); v = e ? atof(e) : 1; if (v <= 0) v = 1; if (v != 1) MLOG("DM_NVN_TS_SCALE=%g: counter report times %g times further apart", v, v); } return v; }
// Frame-rate trials (plan item 216, nvn_fptrial.h; hle_nvn.c sets it at every present): a factor on the report times of the submissions committed
// from then on, as DM_NVN_TS_SCALE's; 1 = the real times
static _Atomic uint32_t g_ts_factor = 1000;   // thousandths
void mtl_ts_factor(double f) { atomic_store(&g_ts_factor, f > 0.001 && f < 1000 ? (uint32_t)(f * 1000 + 0.5) : 1000); }
// DM_NVN_GPU_TS=completion: every report of a submission at its completion, as before plan item 131 (A/B)
static int ts_inside(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_NVN_GPU_TS"); v = !(e && !strcmp(e, "completion")); if (!v) MLOG("DM_NVN_GPU_TS=completion: counter reports at their submission's completion"); } return v; }

