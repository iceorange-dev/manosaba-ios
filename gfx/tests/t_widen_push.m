// The HUD's edge push (gfx/nvn/mtl/draw.inc, encode_args.inc; plan item 320f, S2Arm's rule): with DM_WIDEN_SCREEN=2868x1320 (k 1.221875) a HUD
// draw (its vertex block the orthographic projection for 1280x720) covering the whole canvas into a widened 1280x720 target (1564 wide), that
// samples nothing, is drawn with the whole viewport and its vertex stage given widen_push = (1 − 1/k, ((1 − 3‰)/k)², 1 − 1/k): the vertex tail
// (written here as the chain's EmitterContext.EmitWidenPush emits it) shrinks it to the middle and moves the canvas' edges to the screen's, so
// columns 10 and 1560 are drawn (a black fade over the whole screen); a quad over the canvas' middle half stays there (columns 462-1101: 300 and
// 1300 not drawn, 700 drawn); the same full quad sampling a 16x16 texture stays centred (142-1421: 10 and 1560 not drawn, 700 drawn).
// Every HUD draw centred (-DDM_TEST_WIDEN_PUSH_OFF): 1 fails.
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
static prog_t g_pf, g_ph, g_pt; static uint8_t *g_cm, *g_ubo; static uint8_t g_px[1564 * 4];
static void run(prog_t *pg, const float *quad, int tid) {   // one pass into a fresh 1280x720 target: a quad (x0, x1 in canvas pixels, block cb0 of the vertex stage: the ortho, then the quad); row 360 read
    memset(g_cm, 0, 512 << 10);
    float U[16] = {2.0f / 1280, 0, 0, -1,  0, -2.0f / 720, 0, 1,  0, 0, 1, 0,  0, 0, 0, 1}; memcpy(g_ubo, U, 64); memcpy(g_ubo + 64, quad, 16);
    chain c = {g_cm, 0};
    static uint64_t ser = 9700; tex_t t = {.serial = ser++}; t.d.width = 1280; t.d.height = 720; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    p_bind_program bp = {pg, 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    p_bind_buf ub = {NVN_SHADER_STAGE_VERTEX, 0, (uint64_t)(uintptr_t)g_ubo, 256}; emit(&c, OP_BIND_UBO, &ub, sizeof ub);
    if (tid) { p_bind_handles b = {.stage = 1, .first = 0, .n = 1}; b.h[0] = tid; emit(&c, OP_BIND_TEX, &b, sizeof b); }
    p_set_rt rt = {.n = 1}; rt.color[0] = &t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_scissor sc = {0, 0, 1280, 720}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
    p_clear_color c0 = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(&c, OP_CLEAR_COLOR, &c0, sizeof c0);
    p_viewports v = {.n = 1}; v.v[0][2] = 1280; v.v[0][3] = 720; emit(&c, OP_VIEWPORTS, &v, sizeof v);
    p_draw dr = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 6}; emit(&c, OP_DRAW, &dr, sizeof dr);
    p_set_rt none = {.n = 0}; emit(&c, OP_SET_RT, &none, sizeof none);
    emit(&c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLTexture> mt = tex_mtl(&t); NSUInteger w = mt.width;
    id<MTLBuffer> rb = [g_dev newBufferWithLength:w * 4 options:MTLResourceStorageModeShared]; res_add(rb);
    [blit() copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 360, 0) sourceSize:MTLSizeMake(w, 1, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:w * 4 destinationBytesPerImage:w * 4];
    mtl_wait(mtl_sub_commit(), 5000000000ull); memset(g_px, 0, sizeof g_px); memcpy(g_px, rb.contents, MIN(w, 1564u) * 4);
}
static int red(int x) { return g_px[x * 4]; }
static void prog(prog_t *pg, uint64_t serial, id<MTLLibrary> l, NSString *fs, int tex) {
    pg->serial = serial; NvProg *p = [NvProg new];
    NvShader *v = [NvShader new]; v->fn = [l newFunctionWithName:@"vp"]; v->rf.nb = 2;
    v->rf.b[0] = (nvn_rbind){.kind = NVN_RB_SUPPORT, .aid = 0}; v->rf.b[1] = (nvn_rbind){.kind = NVN_RB_CBUF, .reg = 0, .aid = 1}; v->ae = [v->fn newArgumentEncoderWithBufferIndex:0]; p->sh[0] = v;
    NvShader *f = [NvShader new]; f->stage = 1; f->fn = [l newFunctionWithName:fs];
    if (tex) { f->rf.nb = 1; f->rf.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .aid = 0, .dtt = MTLTextureType2D, .decl_tt = MTLTextureType2D}; f->ae = [f->fn newArgumentEncoderWithBufferIndex:0]; }
    p->sh[1] = f; serial_put(serial, p);
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_TBDR", "0", 1); mtl_init();
    if (posix_memalign((void **)&g_cm, getpagesize(), 1 << 20)) abort(); mtl_pool_add(g_cm, 1 << 20); g_ubo = g_cm + (512 << 10);
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "struct SB { uint a; uint4 b[8]; float4 c; float4 d; int e; float4 rs[73]; int4 f; int g; float4 rsy[73]; float4 push; };\n"   // the chain's support buffer (widen_push at 2560)
        "struct VA { constant SB *s [[id(0)]]; constant float4 *u [[id(1)]]; };\n"
        "vertex float4 vp(uint n [[vertex_id]], constant VA &a [[buffer(0)]]) {\n"
        "  float4 q = a.u[4]; float2 c[6] = {float2(q.x, 0), float2(q.y, 0), float2(q.x, 720), float2(q.y, 0), float2(q.y, 720), float2(q.x, 720)};\n"
        "  float4 p = float4(c[n], 0, 1); float4 o = float4(dot(a.u[0], p), dot(a.u[1], p), 0, 1);\n"
        // the tail as EmitterContext.EmitWidenPush emits it
        "  float push = a.s->push.x, thr2 = a.s->push.y, shrink = a.s->push.z; float xs = o.x * (1.0 - shrink); float t = xs / o.w, t2 = t * t;\n"
        "  bool move = thr2 < t2 && t2 < 0.994009 && (0.0 < shrink || t2 < thr2 * 1.012073); float by = push * o.w;\n"
        "  o.x = move ? (0.0 < t ? xs + by : xs - by) : xs; return o; }\n"
        "fragment float4 fsolid() { return float4(1, 0, 0, 1); }\n"
        "struct FT { texture2d<float> t [[id(0)]]; };\n"
        "fragment float4 ftex(constant FT &a [[buffer(0)]]) { return float4(1, 0, 0, 1) + a.t.read(uint2(0, 0)) * 0.0f; }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    prog(&g_pf, 8980, l, @"fsolid", 0); prog(&g_pt, 8981, l, @"ftex", 1);
    tex_t small = {.serial = 9790}; small.d.width = small.d.height = 16; small.d.depth = small.d.samples = small.d.levels = 1; small.d.target = NVN_TEXTURE_TARGET_2D; small.d.format = NVN_FORMAT_RGBA8;
    mtl_reg_tex(1890, &small, NULL);
    float full[4] = {0, 1280, 0, 0}, mid[4] = {320, 960, 0, 0};
    run(&g_pf, full, 0);
    EXPECT("a solid HUD quad over the whole canvas: pushed to the screen's edges, columns 10 and 1560 drawn [-DDM_TEST_WIDEN_PUSH_OFF: not]", 1, red(10) == 255 && red(1560) == 255 && red(700) == 255);
    run(&g_pf, mid, 0);
    EXPECT("a solid HUD quad over the canvas' middle half: where the centred band puts it (462-1101: 300 and 1300 not, 700 drawn)", 1, red(300) == 0 && red(1300) == 0 && red(700) == 255);
    run(&g_pt, full, 1890);
    EXPECT("the whole-canvas quad sampling a 16x16 texture: centred (142-1421: 10 and 1560 not, 700 drawn)", 1, red(10) == 0 && red(1560) == 0 && red(700) == 255);
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
