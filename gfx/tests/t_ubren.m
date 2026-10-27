// Inline uniform updates without ending the render pass (nvn_mtl.m upd_log / upd_shadow / upd_flush; MacNativeGames, MIT). One submission draws the
// left half reading a uniform colour, updates it inline, draws the right half: the left half keeps the old colour, the right half has the new one, in
// one render pass, and memory holds the update once the submission is done. Also: a storage buffer over the updated range writes the updates first,
// a depth-tested draw after an update keeps the depth attachment (ds_scan goes on past it), a replay-time read of the range sees the update.
// Negative control: DM_NVN_UB_RENAME=0 fails the "one render pass" checks (each update ended the pass); with --expect-split it checks the old path.
// 2c, 5b, 6 (plan item 62k): once the updates are written (a copy or a dispatch in the submission), a draw, a replay-time read and a compute uniform
// pointer still see them over the snapshot; -DDM_TEST_UB_FLUSHED_SNAPSHOT (private copies only while updates are unwritten, as before) fails them.
// 7-9 (plan item 225): a copy made from the range's previous copy and the update after it; the next submission's copy made from its own snapshot,
// not from the copy an earlier submission left in the index (-DDM_TEST_UBSH_ANY_SUB fails 2); 200 updates of one range written at the end sorted in
// about 200 steps (-DDM_TEST_UL_NO_REVERSE, the walk's order sorted as it came: 19,900 moves, fails 1). The newest-update answers are cached until
// the log changes (-DDM_TEST_UL_SEQ_STALE, an answer kept after an update: 6 fail). The bytes copied are counted for both kinds of copy (plan item
// 271; -DDM_TEST_UB_COPIED_FULL_ONLY, the copy from the previous one not counted: fails 1).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-92s expected %-10llx → got %-10llx %s\n", name, (unsigned long long)w, (unsigned long long)g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static chain chain_new(void) { chain c={0}; if (posix_memalign((void **)&c.p,getpagesize(),65536)) abort(); memset(c.p,0,65536); mtl_pool_add(c.p,65536); return c; }
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static tex_t tex(unsigned w, int fmt) { static uint64_t s=9600; tex_t t={.serial=s++}; t.d.width=t.d.height=w; t.d.depth=t.d.samples=t.d.levels=1; t.d.target=NVN_TEXTURE_TARGET_2D; t.d.format=fmt; return t; }
static void ds(chain *c, int dt, int df) { ds_st d = {0}; d.dt = dt; d.df = df; d.sf_f = d.sf_b = NVN_STENCIL_FUNC_ALWAYS; p_bind_state xs = {.kind = 5}; memcpy(xs.bytes, &d, sizeof d); emit(c, OP_BIND_STATE, &xs, sizeof xs); }
static void scissor(chain *c, int x, int w) { p_scissor sc = {x, 0, w, 8}; emit(c, OP_SCISSOR, &sc, sizeof sc); }
static void update(chain *c, uint64_t dst, const float v[4]) { uint8_t up[16 + 16]; uint64_t len = 16; memcpy(up, &dst, 8); memcpy(up + 8, &len, 8); memcpy(up + 16, v, 16); emit(c, OP_UPDATE_UB, up, sizeof up); }
static uint8_t *g_mem; static prog_t g_pg, g_pg2, g_pg3, g_pgc; static uint64_t g_ubo;
static const float RED[4] = {1, 0, 0, 1}, GREEN[4] = {0, 1, 0, 1}, BLUE[4] = {0, 0, 1, 1};
// One submission on an 8x8 RGBA8 target (and a depth target if depth), returns the pixels at (1, 4) and (6, 4) and the render passes it opened
static void run(chain *c, const tex_t *t, uint32_t *left, uint32_t *right, uint64_t *passes) {
    emit(c, OP_END, NULL, 0);
    uint64_t p0 = g_stat_pass;
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c->p; mtl_sub_index(&hh, 1); mtl_sub_snapshot(&hh, 1); nvn_exec(c->p);
    *passes = g_stat_pass - p0;
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(b);
    [blit() copyFromTexture:tex_mtl(t) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    mtl_wait(mtl_sub_commit(), 5000000000ull);
    memcpy(left, (uint8_t *)b.contents + 4 * 32 + 1 * 4, 4); memcpy(right, (uint8_t *)b.contents + 4 * 32 + 6 * 4, 4);
}
static void begin_z(chain *c, const tex_t *t, const tex_t *d, int clear_d) {
    p_set_rt r = {.n = 1}; r.color[0] = t; r.depth = d; emit(c, OP_SET_RT, &r, sizeof r);
    p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = 8; emit(c, OP_VIEWPORTS, &v, sizeof v); scissor(c, 0, 8);
    p_clear_color x = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(c, OP_CLEAR_COLOR, &x, sizeof x);
    if (d && clear_d) { p_clear_ds z = {0.0f, 1, 0, 0}; emit(c, OP_CLEAR_DS, &z, sizeof z); }
    ds(c, 0, NVN_DEPTH_FUNC_ALWAYS);
    p_bind_program bp = {&g_pg, 3}; emit(c, OP_BIND_PROGRAM, &bp, sizeof bp);
    p_bind_buf ub = {NVN_SHADER_STAGE_FRAGMENT, 3, g_ubo, 256}; emit(c, OP_BIND_UBO, &ub, sizeof ub);
}
static void begin(chain *c, const tex_t *t, const tex_t *d) { begin_z(c, t, d, 1); }
static void draw3(chain *c) { p_draw dw = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3, 0, 1}; emit(c, OP_DRAW, &dw, sizeof dw); }
int main(int argc, char **argv) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    if (posix_memalign((void **)&g_mem, getpagesize(), 65536)) abort(); memset(g_mem, 0, 65536); mtl_pool_add(g_mem, 65536);
    g_ubo = (uint64_t)(uintptr_t)(g_mem + 0x1000);
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint id [[vertex_id]]) { float2 p[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)}; return float4(p[id], 0.5, 1); }\n"
        "vertex float4 v2(uint id [[vertex_id]]) { float2 p[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)}; return float4(p[id], 0.5, 1.0); }\n"
        "struct A { constant float4 *fp_c3 [[id(0)]]; };\n"
        "fragment float4 f(constant A &a [[buffer(0)]]) { return a.fp_c3[0]; }\n"
        "struct A3 { device float4 *fp_s0 [[id(0)]]; };\n"
        "fragment float4 f3(constant A3 &a [[buffer(0)]]) { return a.fp_s0[0]; }\n"
        "struct AC { device float4 *cp_s0 [[id(0)]]; constant float4 *cp_u0 [[id(1)]]; };\n"
        "kernel void kc(constant AC &a [[buffer(0)]]) { a.cp_s0[0] = a.cp_u0[0]; }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    NSDictionary *j = @{@"route": @"B", @"stage": @"fs", @"c2": @{@"tex": @[@(mtl_c2_tex_base()), @128], @"samp": @[@(mtl_c2_samp_base()), @128]},
        @"bindings": @[@{@"msl_name": @"fp_c3", @"kind": @"cbuf", @"reg": @3, @"arg_id": @0}], @"drv_reads": @{}};
    nvn_refl rf; char err[256] = ""; if (nvn_refl_from_json(j, &rf, err, sizeof err)) { printf("reflection: %s\n", err); return 1; }
    g_pg.serial = 8900; NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"];
    p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:@"f"]; p->sh[1]->rf = rf; p->sh[1]->ae = [p->sh[1]->fn newArgumentEncoderWithBufferIndex:0];
    serial_put(g_pg.serial, p);
    // a second program (same functions, another serial and vertex layout key) so that case 0 needs two new pipelines
    g_pg2.serial = 8901; NvProg *p2 = [NvProg new]; p2->sh[0] = [NvShader new]; p2->sh[0]->fn = [l newFunctionWithName:@"v2"];
    p2->sh[1] = [NvShader new]; p2->sh[1]->stage = 1; p2->sh[1]->fn = p->sh[1]->fn; p2->sh[1]->rf = rf; p2->sh[1]->ae = [p2->sh[1]->fn newArgumentEncoderWithBufferIndex:0];
    serial_put(g_pg2.serial, p2);
    // a program whose fragment shader reads the colour from storage buffer slot 0 (case 3)
    NSDictionary *j3 = @{@"route": @"B", @"stage": @"fs", @"c2": @{@"tex": @[@(mtl_c2_tex_base()), @128], @"samp": @[@(mtl_c2_samp_base()), @128]},
        @"bindings": @[@{@"msl_name": @"fp_s0", @"kind": @"sbuf", @"reg": @0, @"arg_id": @0, @"rw": @NO}], @"drv_reads": @{}};
    nvn_refl rf3; if (nvn_refl_from_json(j3, &rf3, err, sizeof err)) { printf("reflection: %s\n", err); return 1; }
    g_pg3.serial = 8902; NvProg *p3 = [NvProg new]; p3->sh[0] = [NvShader new]; p3->sh[0]->fn = p->sh[0]->fn;
    p3->sh[1] = [NvShader new]; p3->sh[1]->stage = 1; p3->sh[1]->fn = [l newFunctionWithName:@"f3"]; p3->sh[1]->rf = rf3; p3->sh[1]->ae = [p3->sh[1]->fn newArgumentEncoderWithBufferIndex:0];
    serial_put(g_pg3.serial, p3);
    // a compute program copying uniform slot 0 (a device pointer, as the compute stage reads uniforms) to storage slot 0 (case 6)
    NSDictionary *jc = @{@"route": @"B", @"stage": @"cs", @"c2": @{@"tex": @[@(mtl_c2_tex_base()), @128], @"samp": @[@(mtl_c2_samp_base()), @128]},
        @"bindings": @[@{@"msl_name": @"cp_s0", @"kind": @"sbuf", @"reg": @0, @"arg_id": @0, @"rw": @YES}, @{@"msl_name": @"cp_u0", @"kind": @"ubo_ptr", @"reg": @0, @"arg_id": @1}],
        @"drv_reads": @{}, @"threadgroup_size": @[@1, @1, @1]};
    nvn_refl rfc; if (nvn_refl_from_json(jc, &rfc, err, sizeof err)) { printf("reflection: %s\n", err); return 1; }
    g_pgc.serial = 8903; NvProg *pc = [NvProg new]; pc->sh[5] = [NvShader new]; pc->sh[5]->stage = 5; pc->sh[5]->fn = [l newFunctionWithName:@"kc"];
    pc->sh[5]->rf = rfc; pc->sh[5]->ae = [pc->sh[5]->fn newArgumentEncoderWithBufferIndex:0]; serial_put(g_pgc.serial, pc);
    const uint32_t R_ = 0xff0000ff, G_ = 0xff00ff00, B_ = 0xffff0000;
    int rename = ub_rename_on(), passes = argc > 1 && !strcmp(argv[1], "--expect-split") ? 2 : 1;   // --expect-split: DM_NVN_UB_RENAME=0 run, each update ends the pass
    // 0. The first draw of a new pipeline keeps its scissor: the parallel precompile walks the rest of the submission through state_op, which sets the
    //    scissor too, and put back only the draw state (that draw took the scissor of the last draw walked over: here the right half)
    { tex_t t = tex(8, NVN_FORMAT_RGBA8); chain c = chain_new(); memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      begin(&c, &t, NULL); scissor(&c, 0, 4); draw3(&c); scissor(&c, 4, 4); emit(&c, OP_BIND_PROGRAM, &(p_bind_program){&g_pg2, 3}, sizeof(p_bind_program)); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("new pipelines: the first draw keeps its scissor (left red)", R_, lp);
      EXPECT("... and the second draws the right half (red)", R_, rp); }
    // 1. draw left (red), update to green, draw right
    { tex_t t = tex(8, NVN_FORMAT_RGBA8); chain c = chain_new(); memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      begin(&c, &t, NULL); scissor(&c, 0, 4); draw3(&c); update(&c, g_ubo, GREEN); scissor(&c, 4, 4); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("draw before the update reads the old colour (red)", R_, lp);
      EXPECT("draw after the update reads the new colour (green)", G_, rp);
      EXPECT("one render pass for both draws [DM_NVN_UB_RENAME=0 fails; with --expect-split: two]", passes, np);
      float m[4]; memcpy(m, (const void *)(uintptr_t)g_ubo, 16);
      EXPECT("memory holds the update after the submission", 1, !memcmp(m, GREEN, 16)); }
    // 2. two updates of the same range between draws: the newest wins; a third draw reads it too
    { tex_t t = tex(8, NVN_FORMAT_RGBA8); chain c = chain_new(); memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      begin(&c, &t, NULL); scissor(&c, 0, 4); draw3(&c); update(&c, g_ubo, GREEN); update(&c, g_ubo, BLUE); scissor(&c, 4, 4); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("two updates: the left draw still red", R_, lp);
      EXPECT("two updates: the right draw reads the newest (blue)", B_, rp); }
    // 2b. updates written to memory in the middle (a buffer copy elsewhere ends the pass and writes them), then the range updated again: the snapshot is
    //     still there, so the next draw gets a private copy with both updates and nothing is written before it
    { tex_t t = tex(8, NVN_FORMAT_RGBA8); chain c = chain_new(); memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      uint64_t g0 = g_ren.guard, f0 = g_ren.flushes;
      begin(&c, &t, NULL); scissor(&c, 0, 4); draw3(&c); update(&c, g_ubo, GREEN);
      uint64_t cp[3] = {(uint64_t)(uintptr_t)(g_mem + 0x3000), (uint64_t)(uintptr_t)(g_mem + 0x3100), 64}; emit(&c, OP_COPY_B2B, cp, sizeof cp);
      float half[4] = {0, 0, 1, 1}; uint8_t up[16 + 8]; uint64_t dst = g_ubo + 8, len = 8; memcpy(up, &dst, 8); memcpy(up + 8, &len, 8); memcpy(up + 16, half + 2, 8); emit(&c, OP_UPDATE_UB, up, sizeof up);   // b, a = 1, 1
      scissor(&c, 4, 4); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("written in the middle, updated again: left red", R_, lp);
      EXPECT("... right: green's r, g with the second update's b, a (0, 1, 1, 1)", 0xffffff00, rp);
      if (rename) { EXPECT("... nothing written before that draw (the snapshot stayed)", 0, g_ren.guard - g0);
                    EXPECT("... written twice: by the copy and at the end", 2, g_ren.flushes - f0); } }
    // 2c. updates written to memory in the middle (a buffer copy elsewhere) and no update after: the next draw binds the range again (its stage was
    //     marked for encoding by the update) and must get the snapshot with the update over it, not the snapshot alone (red). Tears of the Kingdom's
    //     cave meshes read their vertex decoding constants this way and were drawn where another mesh is (plan item 62k)
    { tex_t t = tex(8, NVN_FORMAT_RGBA8); chain c = chain_new(); memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      uint64_t f0 = g_ren.flushes;
      begin(&c, &t, NULL); scissor(&c, 0, 4); draw3(&c); update(&c, g_ubo, GREEN);
      uint64_t cp[3] = {(uint64_t)(uintptr_t)(g_mem + 0x3000), (uint64_t)(uintptr_t)(g_mem + 0x3100), 64}; emit(&c, OP_COPY_B2B, cp, sizeof cp);
      scissor(&c, 4, 4); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("written in the middle, no update after: left red", R_, lp);
      EXPECT("... right reads the update (green) [-DDM_TEST_UB_FLUSHED_SNAPSHOT: the snapshot, red]", G_, rp);
      if (rename) EXPECT("... written by the copy, before that draw", 1, g_ren.flushes - f0 >= 1); }
    // 3. a storage buffer over the updated range: the updates are written before that draw, which reads them from memory (green)
    { tex_t t = tex(8, NVN_FORMAT_RGBA8); chain c = chain_new(); memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      uint64_t g0 = g_ren.guard;
      begin(&c, &t, NULL); scissor(&c, 0, 4); draw3(&c); update(&c, g_ubo, GREEN);
      emit(&c, OP_BIND_PROGRAM, &(p_bind_program){&g_pg3, 3}, sizeof(p_bind_program));
      p_bind_buf sb = {NVN_SHADER_STAGE_FRAGMENT, 0, g_ubo, 256}; emit(&c, OP_BIND_SSBO, &sb, sizeof sb);
      scissor(&c, 4, 4); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("storage buffer over an updated range: left red", R_, lp);
      EXPECT("... the storage read sees the update (green)", G_, rp);
      if (rename) EXPECT("... the updates were written before that draw", 1, g_ren.guard - g0); }
    // 4. depth: a submission clears the depth target to 0; the next one (no depth clear: depth comes only from the lookahead) draws the left half testing
    //    nothing, updates, then draws the right half testing LESS: rejected only if the pass kept its depth attachment past the update (ds_scan)
    { tex_t t = tex(8, NVN_FORMAT_RGBA8), d = tex(8, NVN_FORMAT_DEPTH32F); memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      chain c0 = chain_new(); begin(&c0, &t, &d); uint32_t l0, r0; uint64_t n0; run(&c0, &t, &l0, &r0, &n0);
      chain c = chain_new(); begin_z(&c, &t, &d, 0); scissor(&c, 0, 4); draw3(&c); update(&c, g_ubo, GREEN); ds(&c, 1, NVN_DEPTH_FUNC_LESS); scissor(&c, 4, 4); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("depth: left drawn red", R_, lp);
      EXPECT("depth: the depth-tested draw after the update is rejected (depth attached)", 0, rp);
      EXPECT("depth: one render pass [DM_NVN_UB_RENAME=0 fails; with --expect-split: two]", passes, np); }
    // 5. replay-time read (pend_read, as for storage buffer addresses in uniform data): sees the update logged before it
    if (rename) { memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      chain c = chain_new(); p_bind_buf ub = {NVN_SHADER_STAGE_FRAGMENT, 3, g_ubo, 256}; emit(&c, OP_BIND_UBO, &ub, sizeof ub); emit(&c, OP_END, NULL, 0);
      state_reset(); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_snapshot(&hh, 1);
      float out[4]; pend_read(g_ubo, out, 16); int before = !memcmp(out, RED, 16);
      nvn_be_update_ub(GREEN, g_ubo, 16); pend_read(g_ubo, out, 16); int after = !memcmp(out, GREEN, 16);
      float f1; uint16_t ref = 3 << 8 | 0 << 2 | 1; S.ubo[1][3] = ub; int okf = ubo_float(1, ref, &f1);
      mtl_wait(mtl_sub_commit(), 5000000000ull);
      EXPECT("replay-time read before the update: red", 1, before);
      EXPECT("replay-time read after the update: green (the snapshot with the update over it)", 1, after);
      EXPECT("ubo_float after the update: green's g = 1", 1, okf && f1 == 1.0f); }
    // 5b. the same once the update is written to memory (a copy or a dispatch in the submission would): the snapshot alone is still red
    if (rename) { memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      chain c = chain_new(); p_bind_buf ub = {NVN_SHADER_STAGE_FRAGMENT, 3, g_ubo, 256}; emit(&c, OP_BIND_UBO, &ub, sizeof ub); emit(&c, OP_END, NULL, 0);
      state_reset(); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_snapshot(&hh, 1);
      nvn_be_update_ub(GREEN, g_ubo, 16); upd_flush();
      float out[4]; pend_read(g_ubo, out, 16); int after = !memcmp(out, GREEN, 16);
      float f1; uint16_t ref = 3 << 8 | 0 << 2 | 1; S.ubo[1][3] = ub; int okf = ubo_float(1, ref, &f1);
      mtl_wait(mtl_sub_commit(), 5000000000ull);
      EXPECT("replay-time read after the update was written: green [-DDM_TEST_UB_FLUSHED_SNAPSHOT: red]", 1, after);
      EXPECT("ubo_float after the update was written: green's g = 1 [same]", 1, okf && f1 == 1.0f); }
    // 6. a compute uniform pointer after an inline update: dispatch() writes the update first, and the kernel must read it (green), not the snapshot
    { memcpy((void *)(uintptr_t)g_ubo, RED, 16); float *o = (float *)(g_mem + 0x2000); memset(o, 0, 16);
      chain c = chain_new();
      emit(&c, OP_BIND_PROGRAM, &(p_bind_program){&g_pgc, NVN_SHADER_STAGE_COMPUTE_BIT}, sizeof(p_bind_program));
      p_bind_buf sb = {NVN_SHADER_STAGE_COMPUTE, 0, (uint64_t)(uintptr_t)o, 256}; emit(&c, OP_BIND_SSBO, &sb, sizeof sb);
      p_bind_buf ub = {NVN_SHADER_STAGE_COMPUTE, 0, g_ubo, 256}; emit(&c, OP_BIND_UBO, &ub, sizeof ub);
      update(&c, g_ubo, GREEN);
      p_dispatch d = {1, 1, 1, 0}; emit(&c, OP_DISPATCH, &d, sizeof d); emit(&c, OP_END, NULL, 0);
      state_reset(); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); mtl_sub_snapshot(&hh, 1); nvn_exec(c.p);
      int done = mtl_wait(mtl_sub_commit(), 5000000000ull);
      EXPECT("compute: submission completed", 1, done);
      EXPECT("compute: the kernel reads the inline update (green) [-DDM_TEST_UB_FLUSHED_SNAPSHOT: the snapshot, red]", 1, !memcmp(o, GREEN, 16)); }
    // 7. a copy from the range's previous copy: draw (red), update to green, draw the middle (a copy: green), update b, a to 1, 1, draw the right half
    //    (a copy made from the green one with only that update over it: 0, 1, 1, 1)
    if (rename) { tex_t t = tex(8, NVN_FORMAT_RGBA8); chain c = chain_new(); memcpy((void *)(uintptr_t)g_ubo, RED, 16);
      uint64_t i0 = g_ren.incr, s0 = g_ren.shadows, b0 = g_ren.copied;
      begin(&c, &t, NULL); scissor(&c, 0, 2); draw3(&c); update(&c, g_ubo, GREEN); scissor(&c, 2, 2); draw3(&c);
      float half[4] = {0, 0, 1, 1}; uint8_t up[16 + 8]; uint64_t dst = g_ubo + 8, len = 8; memcpy(up, &dst, 8); memcpy(up + 8, &len, 8); memcpy(up + 16, half + 2, 8); emit(&c, OP_UPDATE_UB, up, sizeof up);
      scissor(&c, 4, 4); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("7. from the previous copy: left red", R_, lp);
      EXPECT("... right: green's r, g with b, a of the update after it (0, 1, 1, 1)", 0xffffff00, rp);
      EXPECT("... that copy made from the previous one", 1, g_ren.incr - i0);
      EXPECT("... two copies, 512 bytes counted (the bound 256 each) [-DDM_TEST_UB_COPIED_FULL_ONLY: the second not, 256]", 512, g_ren.shadows - s0 == 2 ? g_ren.copied - b0 : 0); }
    // 8. the next submission: memory blue (its snapshot), b, a updated: the copy is blue with them (0, 0, 1, 1), not case 7's copy (green) left in the index
    if (rename) { tex_t t = tex(8, NVN_FORMAT_RGBA8); chain c = chain_new(); memcpy((void *)(uintptr_t)g_ubo, BLUE, 16);
      begin(&c, &t, NULL); scissor(&c, 0, 4); draw3(&c);
      float half[4] = {0, 0, 1, 1}; uint8_t up[16 + 8]; uint64_t dst = g_ubo + 8, len = 8; memcpy(up, &dst, 8); memcpy(up + 8, &len, 8); memcpy(up + 16, half + 2, 8); emit(&c, OP_UPDATE_UB, up, sizeof up);
      scissor(&c, 4, 4); draw3(&c);
      uint32_t lp, rp; uint64_t np; run(&c, &t, &lp, &rp, &np);
      EXPECT("8. next submission: left blue", B_, lp);
      EXPECT("... right blue with the update (its own snapshot) [-DDM_TEST_UBSH_ANY_SUB: from case 7's copy]", B_, rp); }
    // 9. 200 updates of the same 16 bytes and no draw after them: written at the end in one copy, the newest last; the sort of the 200 takes few moves
    if (rename) { memcpy((void *)(uintptr_t)g_ubo, RED, 16); chain c = chain_new();
      uint64_t m0 = g_ren.ul_moves;
      for (int k = 0; k < 200; k++) { float v[4] = {(float)k, 0, 0, 1}; update(&c, g_ubo, v); }
      emit(&c, OP_END, NULL, 0);
      state_reset(); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); mtl_sub_snapshot(&hh, 1); nvn_exec(c.p);
      mtl_wait(mtl_sub_commit(), 5000000000ull);
      float m[4]; memcpy(m, (const void *)(uintptr_t)g_ubo, 16);
      EXPECT("9. 200 updates of one range: memory holds the newest (r = 199)", 1, m[0] == 199.0f);
      EXPECT("... sorted in under 1,000 moves [-DDM_TEST_UL_NO_REVERSE: 19,900]", 1, g_ren.ul_moves - m0 < 1000);
      if (g_ren.ul_moves - m0 >= 1000) printf("    %llu moves\n", (unsigned long long)(g_ren.ul_moves - m0)); }
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
