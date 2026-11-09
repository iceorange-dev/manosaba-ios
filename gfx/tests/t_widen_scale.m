// The resolution scale per draw (gfx/nvn/mtl/encode_args.inc, plan item 320f; Ryujinx's support buffer render_scale / render_scale_y, filled as
// S2Arm's direct path does): with DM_WIDEN_SCREEN=2868x1320 (k 1.221875) a draw into a widened 1280x720 target (1564 wide) sampling a widened
// texture (its scale index 0: render_scale[1]) reads render_scale[0] = k (red 0.611 → 156), render_scale[1] = −k (the target's own ratio:
// Ryujinx's pixel-exact read; green 156) and render_scale_y[0] = 1 (blue 128); a draw that reads the fragment coordinate sees its block's screen
// size (1280, 720, 1/1280, 1/720) as the game's (alpha 1280 / 2560 → 128, not 1564's 156); a HUD draw (the orthographic projection, nothing
// sampled) reads render_scale[0] = 1 (128). The static buffer always (-DDM_TEST_WIDEN_SCALE_ONE): 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-100s expected %-6lld → got %-6lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static prog_t g_ps, g_pf, g_ph; static tex_t g_last; static uint8_t *g_cm, *g_ubo;
static uint8_t g_px[4];
static void run(prog_t *pg, const float *m, int tid) {   // one pass into a fresh 1280x720 target: a full-screen triangle, the block m at cb0, texture ID tid at t0; pixel (700, 360) read
    memset(g_cm, 0, 512 << 10); memcpy(g_ubo, m, 64); chain c = {g_cm, 0};
    static uint64_t ser = 9600; tex_t t = {.serial = ser++}; t.d.width = 1280; t.d.height = 720; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    p_bind_program bp = {pg, 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    p_bind_buf ub = {NVN_SHADER_STAGE_FRAGMENT, 0, (uint64_t)(uintptr_t)g_ubo, 256}; emit(&c, OP_BIND_UBO, &ub, sizeof ub);
    if (tid) { p_bind_handles b = {.stage = 1, .first = 0, .n = 1}; b.h[0] = tid; emit(&c, OP_BIND_TEX, &b, sizeof b); }
    p_set_rt rt = {.n = 1}; rt.color[0] = &t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_scissor sc = {0, 0, 1280, 720}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
    p_clear_color c0 = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(&c, OP_CLEAR_COLOR, &c0, sizeof c0);
    p_viewports v = {.n = 1}; v.v[0][2] = 1280; v.v[0][3] = 720; emit(&c, OP_VIEWPORTS, &v, sizeof v);
    p_draw dr = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(&c, OP_DRAW, &dr, sizeof dr);
    p_set_rt none = {.n = 0}; emit(&c, OP_SET_RT, &none, sizeof none);
    emit(&c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLTexture> mt = tex_mtl(&t);
    id<MTLBuffer> rb = [g_dev newBufferWithLength:4 options:MTLResourceStorageModeShared]; res_add(rb);
    [blit() copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(700, 360, 0) sourceSize:MTLSizeMake(1, 1, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:4 destinationBytesPerImage:4];
    mtl_wait(mtl_sub_commit(), 5000000000ull); memcpy(g_px, rb.contents, 4); g_last = t;
}
static void prog(prog_t *pg, uint64_t serial, id<MTLLibrary> l, NSString *fs, int tex, int fragc) {   // the fragment stage's bindings as the chain's: support buffer, cb0, (t0 with scale index 0)
    pg->serial = serial; NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"];
    NvShader *f = [NvShader new]; f->stage = 1; f->fn = [l newFunctionWithName:fs]; f->rf.nb = 2 + tex; f->rf.frag_coord = (uint8_t)fragc;
    f->rf.b[0] = (nvn_rbind){.kind = NVN_RB_SUPPORT, .aid = 0}; f->rf.b[1] = (nvn_rbind){.kind = NVN_RB_CBUF, .reg = 0, .aid = 1};
    if (tex) f->rf.b[2] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .aid = 2, .six = 1, .dtt = MTLTextureType2D, .decl_tt = MTLTextureType2D};
    f->ae = [f->fn newArgumentEncoderWithBufferIndex:0]; p->sh[1] = f; serial_put(serial, p);
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_TBDR", "0", 1); mtl_init();
    if (posix_memalign((void **)&g_cm, getpagesize(), 1 << 20)) abort(); mtl_pool_add(g_cm, 1 << 20); g_ubo = g_cm + (512 << 10);
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint n[[vertex_id]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[n],0,1);}\n"
        // the chain's support buffer struct (SupportBuffer.cs; gfx/tests/t_scale_msl.py measures it): render_scale at 192, render_scale_y at 1392
        "struct SB { uint a; uint4 b[8]; float4 c; float4 d; int e; float4 rs[73]; int4 f; int g; float4 rsy[73]; };\n"
        "struct A { constant SB *s [[id(0)]]; constant float4 *c [[id(1)]]; };\n"
        "struct AT { constant SB *s [[id(0)]]; constant float4 *c [[id(1)]]; texture2d<float> t [[id(2)]]; };\n"
        "fragment float4 fs(constant AT &a [[buffer(0)]]) { return float4(a.s->rs[0].x / 2, -a.s->rs[1].x / 2, a.s->rsy[0].x / 2, 1) + a.t.read(uint2(0, 0)) * 0.0f + (*a.c) * 0.0f; }\n"
        "fragment float4 ff(constant A &a [[buffer(0)]], float4 fc [[position]]) { return float4(a.s->rs[0].x / 2, 0, 0, (*a.c).x / 2560) + fc * 0.0f; }\n"
        "fragment float4 fh(constant A &a [[buffer(0)]]) { return float4(a.s->rs[0].x / 2, 0, 0, 1) + (*a.c) * 0.0f; }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    prog(&g_ps, 8970, l, @"fs", 1, 0); prog(&g_pf, 8971, l, @"ff", 0, 1); prog(&g_ph, 8972, l, @"fh", 0, 0);
    double f = 1 / tan(30 * M_PI / 180), nn = 0.1, ff = 1000; float P[16] = {(float)(f / (16.0 / 9)), 0, 0, 0,  0, (float)f, 0, 0,  0, 0, (float)((ff + nn) / (nn - ff)), (float)(2 * ff * nn / (nn - ff)),  0, 0, -1, 0};
    float U[16] = {2.0f / 1280, 0, 0, -1,  0, -2.0f / 720, 0, 1,  0, 0, 1, 0,  0, 0, 0, 1};
    float SZ[16] = {1280, 720, 1.0f / 1280, 1.0f / 720};
    run(&g_ph, P, 0); mtl_reg_tex(1880, &g_last, NULL);   // a widened texture to sample (the first target)
    run(&g_ps, P, 1880);
    EXPECT("a widened draw sampling widened storage: render_scale[0] = k (156) [-DDM_TEST_WIDEN_SCALE_ONE: 1, 128]", 156, g_px[0]);
    EXPECT("... its texture's render_scale[1] = -k, the target's own ratio (156), render_scale_y[0] = 1 (128) [-DDM_TEST_WIDEN_SCALE_ONE: 1]", 1, abs(g_px[1] - 156) <= 1 && abs(g_px[2] - 128) <= 1);
    run(&g_pf, SZ, 0);
    EXPECT("a draw reading the fragment coordinate: its block's screen size the game's (1280 → 128, not 156)", 1, abs(g_px[3] - 128) <= 1);
    run(&g_ph, U, 0);
    EXPECT("a HUD draw (the orthographic projection): render_scale[0] = 1 (128)", 1, abs(g_px[0] - 128) <= 1);
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
