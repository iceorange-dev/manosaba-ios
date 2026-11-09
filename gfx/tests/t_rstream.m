// Encode stage (nvn_render_stream.h; MacNativeGames, MIT): with DM_NVN_TBDR=0 render passes are encoded on a worker thread. 4,096 draws in one pass,
// each bound to its own uniform block (the argument table address travels as a packet) and scissored to its own pixel, write a colour made of its
// index: every pixel must be its draw's, so packets kept their order and every draw read the address set before it (4,096 draws are more than the
// ring holds: A waited for B). A mid-submission commit at the next encoder boundary (rs_sync before the command buffer ends), a texture clear and a
// copy between passes (rs_sync before A's own encoder). Run with DM_NVN_RENDER_STREAM=1 and =0 (Makefile): the pixels must be the same; the "encode stage
// used" check fails with =0 unless --off is given. On by default on iOS only (plan item 245); off everywhere (-DDM_TEST_RS_DEFAULT_OFF): 1 fails.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
#if DM_NVN_M3
static int rs_enabled(void) { return 0; } static uint64_t g_rs_passes;   // no encode stage on the older API: the pixels are checked all the same
#endif
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-86s expected %-8llx → got %-8llx %s\n", name, (unsigned long long)w, (unsigned long long)g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static chain chain_new(size_t sz) { chain c={0}; if (posix_memalign((void **)&c.p,getpagesize(),sz)) abort(); memset(c.p,0,sz); mtl_pool_add(c.p,sz); return c; }
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static tex_t tex(unsigned w) { static uint64_t s=9500; tex_t t={.serial=s++}; t.d.width=t.d.height=w; t.d.depth=t.d.samples=t.d.levels=1; t.d.target=NVN_TEXTURE_TARGET_2D; t.d.format=NVN_FORMAT_RGBA8; return t; }
static uint32_t colour(int i) { return 0xff000000u | 0x5a0000u | (uint32_t)(i >> 8) << 8 | (uint32_t)(i & 255); }   // RGBA8 in memory: r = i & 255, g = i >> 8, b = 0x5a
int main(int argc, char **argv) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    setenv("DM_NVN_TBDR", "0", 1); setenv("DM_NVN_CHUNK_DRAWS", "500", 1); setenv("DM_NVN_CHUNK_FIRST", "300", 1);
    int off = argc > 1 && !strcmp(argv[1], "--off");
    mtl_init();
#if !DM_NVN_M3
    // The default (plan item 245): on on iOS, off elsewhere, the switch either way
    EXPECT("unset on iOS: on [-DDM_TEST_RS_DEFAULT_OFF: off]", 1, rs_default_on(NULL, 1));
    EXPECT("... unset elsewhere: off", 0, rs_default_on(NULL, 0));
    EXPECT("... DM_NVN_RENDER_STREAM=0 on iOS: off, =1 elsewhere: on", 1, !rs_default_on("0", 1) && rs_default_on("1", 0));
#endif
    enum { W = 64, N = W * W };
    uint8_t *ubo; if (posix_memalign((void **)&ubo, getpagesize(), (size_t)N * 256)) abort(); memset(ubo, 0, (size_t)N * 256); mtl_pool_add(ubo, (size_t)N * 256);
    for (int i = 0; i < N; i++) { float c[4] = {(i & 255) / 255.0f, (i >> 8) / 255.0f, 0x5a / 255.0f, 1}; memcpy(ubo + (size_t)i * 256, c, 16); }
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint id [[vertex_id]]) { float2 p[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)}; return float4(p[id], 0.5, 1); }\n"
        "struct A { constant float4 *fp_c3 [[id(0)]]; };\n"
        "fragment float4 f(constant A &a [[buffer(0)]]) { return a.fp_c3[0]; }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    NSDictionary *j = @{@"route": @"B", @"stage": @"fs", @"c2": @{@"tex": @[@(mtl_c2_tex_base()), @128], @"samp": @[@(mtl_c2_samp_base()), @128]},
        @"bindings": @[@{@"msl_name": @"fp_c3", @"kind": @"cbuf", @"reg": @3, @"arg_id": @0}], @"drv_reads": @{}};
    nvn_refl rf; char err[256] = ""; if (nvn_refl_from_json(j, &rf, err, sizeof err)) { printf("reflection: %s\n", err); return 1; }
    static prog_t pg = {.serial = 8950}; NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"];
    p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:@"f"]; p->sh[1]->rf = rf; p->sh[1]->ae = [p->sh[1]->fn newArgumentEncoderWithBufferIndex:0];
    serial_put(pg.serial, p);
    tex_t a = tex(W), b = tex(W), cl = tex(W);
    chain c = chain_new(2 << 20);
    p_set_rt r = {.n = 1}; r.color[0] = &a; emit(&c, OP_SET_RT, &r, sizeof r);
    p_viewports vp = {.n = 1}; vp.v[0][2] = vp.v[0][3] = W; emit(&c, OP_VIEWPORTS, &vp, sizeof vp);
    p_clear_color x = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(&c, OP_CLEAR_COLOR, &x, sizeof x);
    ds_st d = {0}; d.df = NVN_DEPTH_FUNC_ALWAYS; d.sf_f = d.sf_b = NVN_STENCIL_FUNC_ALWAYS; p_bind_state xs = {.kind = 5}; memcpy(xs.bytes, &d, sizeof d); emit(&c, OP_BIND_STATE, &xs, sizeof xs);
    p_bind_program bp = {&pg, 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    for (int i = 0; i < N; i++) {
        p_bind_buf ub = {NVN_SHADER_STAGE_FRAGMENT, 3, (uint64_t)(uintptr_t)(ubo + (size_t)i * 256), 256}; emit(&c, OP_BIND_UBO, &ub, sizeof ub);
        p_scissor sc = {i % W, i / W, 1, 1}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
        p_draw dw = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3, 0, 1}; emit(&c, OP_DRAW, &dw, sizeof dw);
    }
    p_clear_tex ct = {0}; ct.tex = &cl; ct.r = (NVNcopyRegion){0, 0, 0, W, W, 1}; ct.c.f[0] = 1; ct.c.f[3] = 1; ct.mask = 15; emit(&c, OP_CLEAR_TEX, &ct, sizeof ct);   // A's own encoder between passes
    p_set_rt r2 = {.n = 1}; r2.color[0] = &b; emit(&c, OP_SET_RT, &r2, sizeof r2);
    p_scissor full = {0, 0, W, W}; emit(&c, OP_SCISSOR, &full, sizeof full);
    p_bind_buf ub7 = {NVN_SHADER_STAGE_FRAGMENT, 3, (uint64_t)(uintptr_t)(ubo + 7 * 256), 256}; emit(&c, OP_BIND_UBO, &ub7, sizeof ub7);
    p_draw dw = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3, 0, 1}; emit(&c, OP_DRAW, &dw, sizeof dw);
    emit(&c, OP_END, NULL, 0);
    uint64_t pass0 = rs_enabled() ? g_rs_passes : 0, chunks0 = g_chunk_commits;
    for (int i = 0; i < 500 && atomic_load(&g_inflight); i++) usleep(10000);   // the GPU idle: a due mid-submission commit waits while the game's work is on it (315b)
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)(uintptr_t)c.p; mtl_sub_index(&hh, 1); mtl_sub_snapshot(&hh, 1); nvn_exec(c.p);
    id<MTLBuffer> rb = [g_dev newBufferWithLength:(size_t)3 * W * W * 4 options:MTLResourceStorageModeShared]; res_add(rb);
    const tex_t *ts[3] = {&a, &b, &cl};
    for (int k = 0; k < 3; k++) [blit() copyFromTexture:tex_mtl(ts[k]) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(W,W,1) toBuffer:rb destinationOffset:(size_t)k * W * W * 4 destinationBytesPerRow:W * 4 destinationBytesPerImage:W * W * 4];
    EXPECT("submission completed", 1, mtl_wait(mtl_sub_commit(), 10000000000ull));
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    const uint32_t *px = (const uint32_t *)rb.contents; int bad = 0, first = -1;
    for (int i = 0; i < N; i++) if (px[i] != colour(i)) { bad++; if (first < 0) first = i; }
    if (first >= 0) printf("first wrong pixel %d: %08x, want %08x\n", first, px[first], colour(first));
    EXPECT("4,096 draws, each pixel its own draw's colour", 0, bad);
    EXPECT("second pass: the whole target draw 7's colour (corner)", colour(7), px[W * W + W * W - 1]);
    EXPECT("texture clear between the passes: red", 0xff0000ff, px[2 * W * W + 5]);
    EXPECT("a mid-submission commit at the encoder boundary after the 4,096 draws (the texture clear)", 1, g_chunk_commits - chunks0 >= 1);
    if (!off && !DM_NVN_M3) EXPECT("encode stage used for the passes [DM_NVN_RENDER_STREAM=0 fails]", 1, rs_enabled() && g_rs_passes - pass0 >= 3);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
