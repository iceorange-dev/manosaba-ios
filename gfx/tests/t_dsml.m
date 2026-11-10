// Depth and stencil targets made without memory (DM_NVN_DS_MEMORYLESS, gfx/nvn/mtl/rtheap.inc dsml_*; plan item 280), on the GPU through the
// replay. A depth target a pass clears at its start is a memoryless MTLTexture (no allocated size), the depth test works in the pass (two
// overlapping triangles, the nearer drawn first stays) and the pass ends without storing it (Metal's validation, the second run of this test:
// MTL_DEBUG_LAYER=1). A pass that attaches it without clearing it makes it again with memory, counted; so does any use that is not a pass's
// (tex_mtl: a texture binding, a copy, a clear, a read). Another object of a storage given memory follows at its next pass, not counted again.
// After DSML_MAX targets needed memory the rest are made with it. A target first asked for by a pass that loads it, or by anything but a pass,
// is made with memory from the start, not counted.
// -DDM_TEST_DSML_NO_PROMOTE (kept without memory whatever asks for it): 5 fail. -DDM_TEST_DSML_ANY_FIRST_USE (made without memory whatever
// asks first, the first version): 2 fail. A look ahead (the pipeline prediction, the empty-scissor precheck) only describes a depth target:
// -DDM_TEST_DSML_PEEK_MAKES (it makes it, as before) fails 2.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { uint64_t w = (want), g = (got); count++; printf("%s expected %llu → got %llu %s\n", name, (unsigned long long)w, (unsigned long long)g, w == g ? "✓" : "✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static chain chain_new(void) { chain c = {0}; if (posix_memalign((void **)&c.p, getpagesize(), 65536)) abort(); memset(c.p, 0, 65536); mtl_pool_add(c.p, 65536); return c; }
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z = (8 + n + 7) & ~7ull; op_hdr h = {op, (uint32_t)z}; memcpy(c->p + c->n, &h, 8); if (n) memcpy(c->p + c->n + 8, a, n); c->n += z; }
static tex_t tex(int fmt) { static uint64_t s = 9700; tex_t t = {.serial = s++}; t.d.width = t.d.height = 8; t.d.depth = t.d.samples = 1; t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = fmt; return t; }
static NvProg *g_tkeep[8]; static prog_t t_pg[2];   // 0: a triangle over the target at depth 0.25, red; 1: at depth 0.75, green
static void setup_progs(void) {
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "static float4 tri(uint n, float z) { float2 p[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)}; return float4(p[n], z, 1); }\n"
        "vertex float4 vnear(uint n [[vertex_id]]) { return tri(n, 0.25); }\nvertex float4 vfar(uint n [[vertex_id]]) { return tri(n, 0.75); }\n"
        "fragment float4 red() { return float4(1, 0, 0, 1); }\nfragment float4 green() { return float4(0, 1, 0, 1); }\n" options:nil error:&e];
    if (!l) { fprintf(stderr, "%s\n", e.localizedDescription.UTF8String); abort(); }
    for (int i = 0; i < 2; i++) { t_pg[i].serial = 8100 + (uint64_t)i; NvProg *p = [NvProg new];
        p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:i ? @"vfar" : @"vnear"];
        p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:i ? @"green" : @"red"];
        g_tkeep[i] = p; serial_put(t_pg[i].serial, p); }
}
// One submission: targets a + d, the colour cleared, the depth and stencil cleared (clear_ds) or left to be loaded, then the nearer triangle
// (near) and the farther one under a depth test that writes; a read back (64 RGBA8 pixels)
static void run(const tex_t *a, const tex_t *d, int clear_ds, int near, uint8_t px[256]) {
    chain c = chain_new();
    p_set_rt r = {.n = 1}; r.color[0] = a; r.depth = d; emit(&c, OP_SET_RT, &r, sizeof r);
    p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = 8; emit(&c, OP_VIEWPORTS, &v, sizeof v); p_scissor sc = {0, 0, 8, 8}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
    p_clear_color cc = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 1}}; emit(&c, OP_CLEAR_COLOR, &cc, sizeof cc);
    if (clear_ds) { p_clear_ds cd = {1.0f, 1, 0, 0xff}; emit(&c, OP_CLEAR_DS, &cd, sizeof cd); }
    ds_st ds = {1, 1, NVN_DEPTH_FUNC_LESS, 0, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP};
    p_bind_state bs = {.kind = 5}; memcpy(bs.bytes, &ds, sizeof ds); emit(&c, OP_BIND_STATE, &bs, sizeof bs);
    p_draw dr = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3};
    if (near) { p_bind_program b = {&t_pg[0], 3}; emit(&c, OP_BIND_PROGRAM, &b, sizeof b); emit(&c, OP_DRAW, &dr, sizeof dr); }
    { p_bind_program b = {&t_pg[1], 3}; emit(&c, OP_BIND_PROGRAM, &b, sizeof b); emit(&c, OP_DRAW, &dr, sizeof dr); }
    emit(&c, OP_END, NULL, 0); state_reset(); memset(&R, 0, sizeof R); vis_clear(); g_vis_on = g_vis_counting = 0;
    mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(b);
    [blit() copyFromTexture:tex_mtl(a) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 8, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    uint64_t f = mtl_sub_commit(); EXPECT("  (the submission completed)", 1, mtl_wait(f, 5000000000ull)); EXPECT("  (no error in its feedback)", 0, mtl_last_commit_error());
    memcpy(px, b.contents, 256);
}
static int memless(const tex_t *t) { id<MTLTexture> m = serial_get(t->serial); return m && m.storageMode == MTLStorageModeMemoryless; }
int main(void) { @autoreleasepool { setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_TBDR", "1", 1); setenv("DM_NVN_DS_MEMORYLESS", "1", 1);
    mtl_init(); setup_progs(); uint8_t px[256]; uint64_t st[2];
    tex_t a = tex(NVN_FORMAT_RGBA8), d = tex(NVN_FORMAT_DEPTH32F_STENCIL8);
    // ① cleared at its pass's start
    run(&a, &d, 1, 1, px); id<MTLTexture> m = serial_get(d.serial); mtl_dsml_stats(st);
    EXPECT("a depth target its pass clears first: made without memory", 1, memless(&d));
    EXPECT("... no allocated size", 0, (uint64_t)m.allocatedSize);
    EXPECT("... the depth test works in the pass: the nearer triangle, drawn first, stays (red)", 1, px[0] == 255 && px[1] == 0 && px[4 * 27] == 255 && px[4 * 27 + 1] == 0);
    EXPECT("... one made, none given memory", 1, st[0] == 1 && st[1] == 0);
    run(&a, &d, 1, 1, px); EXPECT("the next submission: the same texture, the same picture", 1, serial_get(d.serial) == m && px[0] == 255 && px[1] == 0);
    // ② a pass that loads it
    run(&a, &d, 0, 0, px); mtl_dsml_stats(st);
    EXPECT("a pass that does not clear it: made again with memory, counted [-DDM_TEST_DSML_NO_PROMOTE]", 1, !memless(&d) && serial_get(d.serial) != nil && st[1] == 1);
    run(&a, &d, 1, 1, px); mtl_dsml_stats(st);
    EXPECT("... and it stays so: cleared and drawn again, the picture right, not counted again", 1, !memless(&d) && px[0] == 255 && px[1] == 0 && st[1] == (memless(&d) ? 0 : 1));
    // ③ any use that is not a pass's
    tex_t e = tex(NVN_FORMAT_DEPTH32F_STENCIL8); run(&a, &e, 1, 1, px);
    EXPECT("a second target: without memory", 1, memless(&e));
    id<MTLTexture> r = tex_mtl(&e); mtl_dsml_stats(st);
    EXPECT("asked for by anything but a pass (a binding, a copy, a read): with memory, counted [-DDM_TEST_DSML_NO_PROMOTE]", 1, r && r.storageMode != MTLStorageModeMemoryless && serial_get(e.serial) == r && st[1] == 2);
    // (what asks first decides: a pass that loads, or anything but a pass, gets a texture with memory and nothing is counted)
    { tex_t y = tex(NVN_FORMAT_DEPTH32F_STENCIL8), z = tex(NVN_FORMAT_DEPTH24); uint64_t s0[2], s1[2]; mtl_dsml_stats(s0);
      run(&a, &y, 0, 0, px); id<MTLTexture> zm = tex_mtl(&z); mtl_dsml_stats(s1);
      EXPECT("first asked for by a pass that loads it: with memory from the start, not counted [-DDM_TEST_DSML_ANY_FIRST_USE]", 1, !memless(&y) && serial_get(y.serial) != nil && s1[0] == s0[0] && s1[1] == s0[1]);
      EXPECT("first asked for by anything but a pass: with memory from the start [-DDM_TEST_DSML_ANY_FIRST_USE]", 1, zm && zm.storageMode != MTLStorageModeMemoryless && s1[0] == s0[0] && s1[1] == s0[1]); }
    { tex_t q = tex(NVN_FORMAT_DEPTH32F_STENCIL8); MTLPixelFormat pf = MTLPixelFormatInvalid; NSUInteger sc = 0, pw = 0, ph = 0; int ok = tex_peek(&q, &pf, &sc, &pw, &ph);
      EXPECT("a look ahead describes a depth target without making it [-DDM_TEST_DSML_PEEK_MAKES]", 1, ok && pf == MTLPixelFormatDepth32Float_Stencil8 && sc == 1 && pw == 8 && ph == 8 && serial_get(q.serial) == nil);
      run(&a, &q, 1, 1, px); EXPECT("... and its pass, which clears it, makes it without memory [-DDM_TEST_DSML_PEEK_MAKES]", 1, memless(&q)); }
    // ④ two objects of one storage: the second follows the first at its next pass
    static uint8_t store[4096]; tex_t f1 = tex(NVN_FORMAT_DEPTH32F_STENCIL8), f2 = tex(NVN_FORMAT_DEPTH32F_STENCIL8); f1.mem = f2.mem = (void *)store; f1.size = f2.size = 320;
    run(&a, &f1, 1, 1, px); run(&a, &f2, 1, 1, px);
    EXPECT("two objects of one storage: each without memory", 1, memless(&f1) && memless(&f2));
    tex_mtl(&f1); run(&a, &f2, 1, 1, px); mtl_dsml_stats(st);
    EXPECT("one of them needs memory: the other gets it at its next pass, counted once [-DDM_TEST_DSML_NO_PROMOTE]", 1, !memless(&f1) && !memless(&f2) && st[1] == 3 && px[0] == 255);
    // ⑤ the limit
    for (int k = 3; k < DSML_MAX; k++) { tex_t g = tex(NVN_FORMAT_DEPTH32F_STENCIL8); run(&a, &g, 1, 1, px); tex_mtl(&g); }
    tex_t h = tex(NVN_FORMAT_DEPTH32F_STENCIL8); run(&a, &h, 1, 1, px); mtl_dsml_stats(st);
    EXPECT("after DSML_MAX targets needed memory the next is made with it [-DDM_TEST_DSML_NO_PROMOTE]", 1, st[1] == DSML_MAX && !memless(&h) && px[0] == 255);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0; } }
