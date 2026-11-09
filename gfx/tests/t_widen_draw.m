// The widening's rewrite and the HUD (gfx/nvn/mtl/encode_args.inc widen_cb_rewrite, draw.inc; plan item 320d): with DM_WIDEN_SCREEN=2868x1320
// (k 1.221875) a draw into a 1280x720 target (1564 wide) whose uniform block holds a 16:9 projection reads it rewritten (its P00 written out as
// red: 0.974 / k → 203 of 255, not 248) and is widened (column 1560 drawn); a draw whose block holds the HUD's orthographic projection for
// 1280x720 is centred at its own size (columns 142-1421: column 100 not drawn, 700 drawn, 1500 not); with DM_WIDEN_RW=0 the projection reads
// as it is (248). The HUD widened with the rest (-DDM_TEST_WIDEN_HUD_WIDE): 1 fails.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-96s expected %-6lld → got %-6lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static prog_t g_p3d, g_phud;
static uint8_t g_px[1564 * 4]; static uint8_t *g_ubo;
static void run(prog_t *pg, const float *m) {   // one pass into a fresh 1280x720 target: a full-screen triangle of pg with the block m at cb0; row 360 read
    static uint8_t *cm; if (!cm) { if (posix_memalign((void **)&cm, getpagesize(), 1 << 20)) abort(); mtl_pool_add(cm, 1 << 20); g_ubo = cm + (512 << 10); }
    memset(cm, 0, 512 << 10); memcpy(g_ubo, m, 64); chain c = {cm, 0};
    static uint64_t ser = 9400; tex_t t = {.serial = ser++}; t.d.width = 1280; t.d.height = 720; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    p_bind_program bp = {pg, 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    p_bind_buf ub = {NVN_SHADER_STAGE_FRAGMENT, 0, (uint64_t)(uintptr_t)g_ubo, 256}; emit(&c, OP_BIND_UBO, &ub, sizeof ub);
    p_set_rt rt = {.n = 1}; rt.color[0] = &t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_scissor sc = {0, 0, 1280, 720}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
    p_clear_color c0 = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(&c, OP_CLEAR_COLOR, &c0, sizeof c0);
    p_viewports v = {.n = 1}; v.v[0][2] = 1280; v.v[0][3] = 720; emit(&c, OP_VIEWPORTS, &v, sizeof v);
    p_draw dr = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(&c, OP_DRAW, &dr, sizeof dr);
    p_set_rt none = {.n = 0}; emit(&c, OP_SET_RT, &none, sizeof none);
    emit(&c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLTexture> mt = tex_mtl(&t); NSUInteger w = mt.width;
    id<MTLBuffer> rb = [g_dev newBufferWithLength:w * 4 options:MTLResourceStorageModeShared]; res_add(rb);
    [blit() copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 360, 0) sourceSize:MTLSizeMake(w, 1, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:w * 4 destinationBytesPerImage:w * 4];
    mtl_wait(mtl_sub_commit(), 5000000000ull); memset(g_px, 0, sizeof g_px); memcpy(g_px, rb.contents, MIN(w, 1564u) * 4);
}
static int red(int x) { return g_px[x * 4]; }
static void prog(prog_t *pg, uint64_t serial, id<MTLLibrary> l, NSString *fs) {
    pg->serial = serial; NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"];
    NvShader *f = [NvShader new]; f->stage = 1; f->fn = [l newFunctionWithName:fs]; f->rf.nb = 1; f->rf.b[0] = (nvn_rbind){.kind = NVN_RB_CBUF}; f->ae = [f->fn newArgumentEncoderWithBufferIndex:0];
    p->sh[1] = f; serial_put(serial, p);
}
int main(int argc, char **argv) { @autoreleasepool {
    int norw = argc > 1 && !strcmp(argv[1], "--norw");
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_TBDR", "0", 1); if (norw) setenv("DM_WIDEN_RW", "0", 1); mtl_init();
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal; vertex float4 v(uint n[[vertex_id]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[n],0,1);}\n"
        "struct U{constant float4 *c [[id(0)]];}; fragment float4 f3d(constant U&a [[buffer(0)]]){return float4((*a.c).x, 0, 0, 1);}\n"
        "fragment float4 fhud(constant U&a [[buffer(0)]]){return float4(1, 0, 0, 1) + (*a.c) * 0.0f;}\n" options:nil error:&e];
    prog(&g_p3d, 8960, l, @"f3d"); prog(&g_phud, 8961, l, @"fhud");
    double f = 1 / tan(30 * M_PI / 180), nn = 0.1, ff = 1000; float P[16] = {(float)(f / (16.0 / 9)), 0, 0, 0,  0, (float)f, 0, 0,  0, 0, (float)((ff + nn) / (nn - ff)), (float)(2 * ff * nn / (nn - ff)),  0, 0, -1, 0};
    float U[16] = {2.0f / 1280, 0, 0, -1,  0, -2.0f / 720, 0, 1,  0, 0, 1, 0,  0, 0, 0, 1};
    run(&g_p3d, P);
    if (norw) { EXPECT("DM_WIDEN_RW=0: the projection read as it is (P00 0.974 → 248)", 1, abs(red(700) - 248) <= 2); }
    else {
        EXPECT("a 3D draw (its block a 16:9 projection): P00 read rewritten, 0.974 / k → 203", 1, abs(red(700) - 203) <= 2);
        EXPECT("... widened: column 1560 drawn", 1, red(1560) > 0);
        run(&g_phud, U);
        EXPECT("a HUD draw (its block the HUD's orthographic projection): centred, column 700 drawn", 255, red(700));
        EXPECT("... columns 100 and 1500 not (142-1421) [-DDM_TEST_WIDEN_HUD_WIDE: drawn]", 0, red(100) + red(1500));
    }
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
