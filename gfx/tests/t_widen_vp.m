// Viewports and scissors into a widened target (gfx/nvn/mtl/draw.inc, renderpass.inc; plan item 320c): with DM_WIDEN_SCREEN=2868x1320 a 1280x720
// target is 1564 wide; a draw with the game's viewport over its whole 1280x720 covers all 1564 columns (column 1560 drawn); one with a viewport of the
// left 640 columns covers the left 782 (= 640 × 1564 / 1280: column 780 drawn, 790 not); a scissor of the left 640 columns likewise; a clear under
// the same scissor likewise. The viewports and scissors left as the game set them (-DDM_TEST_WIDEN_VP_KEEP): 4 fail.
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
static prog_t g_pg;
static uint8_t g_px[1564 * 4];
// one pass into a fresh 1280x720 target: cleared to 0, then (clear = 0) one draw of a full-screen triangle in red with this viewport width and
// scissor width, or (clear = 1) a red clear under the scissor; row 360 of the target read back into g_px
static void run(int vpw, int scw, int clear) {
    static uint8_t *cm; if (!cm) { if (posix_memalign((void **)&cm, getpagesize(), 1 << 20)) abort(); mtl_pool_add(cm, 1 << 20); }
    memset(cm, 0, 1 << 20); chain c = {cm, 0};
    static uint64_t ser = 9500; tex_t t = {.serial = ser++}; t.d.width = 1280; t.d.height = 720; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    p_bind_program bp = {&g_pg, 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    p_set_rt rt = {.n = 1}; rt.color[0] = &t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_scissor full = {0, 0, 1280, 720}; emit(&c, OP_SCISSOR, &full, sizeof full);
    p_clear_color c0 = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(&c, OP_CLEAR_COLOR, &c0, sizeof c0);
    p_viewports v = {.n = 1}; v.v[0][2] = vpw; v.v[0][3] = 720; emit(&c, OP_VIEWPORTS, &v, sizeof v);
    p_scissor sc = {0, 0, scw, 720}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
    if (clear) { p_clear_color cc = {.index = 0, .mask = 15, .rgba = {1, 0, 0, 1}}; emit(&c, OP_CLEAR_COLOR, &cc, sizeof cc); }
    else { p_draw dr = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(&c, OP_DRAW, &dr, sizeof dr); }
    p_set_rt none = {.n = 0}; emit(&c, OP_SET_RT, &none, sizeof none);
    emit(&c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLTexture> mt = tex_mtl(&t); NSUInteger w = mt.width;
    id<MTLBuffer> rb = [g_dev newBufferWithLength:w * 4 options:MTLResourceStorageModeShared]; res_add(rb);
    [blit() copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 360, 0) sourceSize:MTLSizeMake(w, 1, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:w * 4 destinationBytesPerImage:w * 4];
    mtl_wait(mtl_sub_commit(), 5000000000ull); memset(g_px, 0, sizeof g_px); memcpy(g_px, rb.contents, MIN(w, 1564u) * 4);
}
static int red(int x) { return g_px[x * 4]; }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_TBDR", "0", 1); mtl_init();
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal; vertex float4 v(uint n[[vertex_id]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[n],0,1);}\n"
        "fragment float4 f() { return float4(1, 0, 0, 1); }\n" options:nil error:&e];
    g_pg.serial = 8950; NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"]; p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:@"f"]; serial_put(g_pg.serial, p);
    run(1280, 1280, 0); EXPECT("the game's viewport over its whole 1280x720: column 1560 of the 1564 drawn [-DDM_TEST_WIDEN_VP_KEEP: not]", 255, red(1560));
    run(640, 1280, 0); EXPECT("a viewport of the left 640 columns: column 780 drawn (640 × 1564 / 1280 = 782) [-DDM_TEST_WIDEN_VP_KEEP: not]", 255, red(780));
    EXPECT("... column 790 not", 0, red(790));
    run(1280, 640, 0); EXPECT("a scissor of the left 640 columns: column 780 drawn, 790 not [-DDM_TEST_WIDEN_VP_KEEP: 780 not]", 1, red(780) == 255 && red(790) == 0);
    run(1280, 640, 1); EXPECT("a clear under that scissor: column 780 red, 790 not [-DDM_TEST_WIDEN_VP_KEEP: 780 not]", 1, red(780) == 255 && red(790) == 0);
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
