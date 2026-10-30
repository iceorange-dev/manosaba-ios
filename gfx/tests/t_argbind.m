// Argument binding (plan item 296, after seedds/nxnative, MIT). A draw whose argument binding ends its render pass (81ccacf): an 8x8 RGBA8
// texture drawn (so made without format views allowed), then sampled through an R32Float view in a pass of its own target: binding that view
// makes the texture again with format views allowed and copies it on a blit encoder (tex_upgrade_pfv), which ends the render pass the draw
// opened. The draw is begun again in a new pass: its pixels drawn (green 0.25), counted once, the texture upgraded once. Encoded on the ended
// encoder as before (-DDM_TEST_DRAW_ON_ENDED): the process stops there (Metal 4: API validation's assertion at the draw; Metal 3: useResource on
// it crashes in the driver). An argument block reused after the serial's Metal object was replaced (the same program and bindings, g_ser_gen
// moved): encoded again (971d336, -DDM_TEST_ARGS_SER_IGNORED: 1 fails). A 2D texture in a 3D slot (2bccdcc): the zero 3D texture bound in its
// place, 0.25 drawn (-DDM_TEST_3D_SLOT_AS_IS, bound as it is: 1 fails; the M5 reads zero through the mismatched slot too, so only what was bound
// tells). A writable storage binding of size 0 covers the rest of its pool for the uniform snapshots it drops (f293a70,
// -DDM_TEST_SBUF_SIZE0_ONE, one byte as before: 1 fails). A direct draw whose vertex shader reads past a stream's bound size is counted (1c54ea0's
// case, reported here; Maxwell reads zeros there): 9 vertices of 16 bytes from a 36-byte stream yes, 2 no, the third instance of a per-instance
// stream yes, a stride-0 stream no; attribute sizes by format (-DDM_TEST_VB_PAST_FIRST, the first vertex taken for the last: 4 fail, the counts running on).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
void crash_install(void);
static int fail, count;
#define EXPECT(name, want, got) do { uint64_t w = (want), g = (got); count++; printf("%-84s expected %-4llu → got %-4llu %s\n", name, (unsigned long long)w, (unsigned long long)g, w == g ? "✓" : "✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static chain chain_new(void) { chain c = {0}; if (posix_memalign((void **)&c.p, getpagesize(), 65536)) abort(); memset(c.p, 0, 65536); mtl_pool_add(c.p, 65536); return c; }
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z = (8 + n + 7) & ~7ull; op_hdr h = {op, (uint32_t)z}; memcpy(c->p + c->n, &h, 8); if (n) memcpy(c->p + c->n + 8, a, n); c->n += z; }
static tex_t tex(void) { static uint64_t s = 9650; tex_t t = {.serial = s++}; t.d.width = t.d.height = 8; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8; return t; }
static void set_rt(chain *c, const tex_t *a) {
    p_set_rt r = {.n = 1}; r.color[0] = a; emit(c, OP_SET_RT, &r, sizeof r);
    p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = 8; emit(c, OP_VIEWPORTS, &v, sizeof v); p_scissor sc = {0, 0, 8, 8}; emit(c, OP_SCISSOR, &sc, sizeof sc);
    p_clear_color k = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(c, OP_CLEAR_COLOR, &k, sizeof k);
    ds_st d = {0, 0, NVN_DEPTH_FUNC_ALWAYS, 0, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP};
    p_bind_state x = {.kind = 5}; memcpy(x.bytes, &d, sizeof d); emit(c, OP_BIND_STATE, &x, sizeof x);
}
static void bind_prog(chain *c, prog_t *p) { p_bind_program b = {p, 3}; emit(c, OP_BIND_PROGRAM, &b, sizeof b); }
static void bind_tex(chain *c, int id) { p_bind_handles b = {.stage = 1, .first = 0, .n = 1}; b.h[0] = id; emit(c, OP_BIND_TEX, &b, sizeof b); }
static void draw_cmd(chain *c) { p_draw d = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(c, OP_DRAW, &d, sizeof d); }
static prog_t g_pC, g_pAdd, g_pVol; static NvProg *g_pkeep[3];
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); crash_install(); mtl_init();
    NSError *e = nil;
    id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint n [[vertex_id]]) { float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)}; return float4(p[n], 0, 1); }\n"
        "fragment float4 cC() { return float4(0.2, 0.4, 0.6, 1); }\n"
        "struct A { texture2d<float> t [[id(0)]]; };\n"
        "fragment float4 add(float4 pos [[position]], constant A &a [[buffer(0)]]) { return float4(a.t.read(uint2(pos.xy)).gba + 0.25, 1); }\n"
        "struct V { texture3d<float> t [[id(0)]]; };\n"
        "fragment float4 vol(constant V &a [[buffer(0)]]) { return float4(a.t.read(uint3(0, 0, 0)).rgb + 0.25, 1); }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    prog_t *pg[3] = {&g_pC, &g_pAdd, &g_pVol}; NSString *fn[3] = {@"cC", @"add", @"vol"};
    for (int i = 0; i < 3; i++) {
        NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"];
        p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:fn[i]];
        if (i) { MTLTextureType tt = i == 1 ? MTLTextureType2D : MTLTextureType3D;
                 p->sh[1]->rf.nb = 1; p->sh[1]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .dtt = (uint8_t)tt, .decl_tt = (uint8_t)tt}; p->sh[1]->ae = [p->sh[1]->fn newArgumentEncoderWithBufferIndex:0]; }
        p->sh[1]->rf.color_mask = 1; p->sh[1]->rf.color_known = 1;
        pg[i]->serial = 9750 + i; serial_put(pg[i]->serial, p); g_pkeep[i] = p;
    }
    tex_t src = tex(), dst = tex(); view_t vf = {.set_format = 1, .format = NVN_FORMAT_R32F}; mtl_reg_tex(1850, &src, &vf);
    chain c = chain_new();
    set_rt(&c, &src); bind_prog(&c, &g_pC); draw_cmd(&c);                         // the texture drawn: made without format views allowed
    set_rt(&c, &dst); bind_prog(&c, &g_pAdd); bind_tex(&c, 1850); draw_cmd(&c);   // sampled through a view of another format
    emit(&c, OP_END, NULL, 0);
    uint64_t up0 = g_pfv_upgrades, rb0 = g_draw_rebegun;
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(b);
    [blit() copyFromTexture:tex_mtl(&dst) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 8, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    mtl_wait(mtl_sub_commit(), 5000000000ull);
    const uint8_t *px = b.contents; int drawn = 0; for (int i = 0; i < 64; i++) drawn += px[4 * i + 1] >= 62 && px[4 * i + 1] <= 66 && px[4 * i + 3] == 255;
    EXPECT("the texture made again with format views allowed (tex_upgrade_pfv), once", 1, g_pfv_upgrades - up0);
    EXPECT("the draw begun again in a new pass, once", 1, g_draw_rebegun - rb0);
    EXPECT("its pixels drawn: green 0.25 (the view's g, b, a are 0, 0, 1)", 64, drawn);
    // a reused argument block after the serial's Metal object was replaced: the dirty marks cleared, a step of the serial table, then synced
    memset(g_args_dirty, 0, sizeof g_args_dirty); ser_bump(src.serial); args_sync_dirty();
    EXPECT("a serial's Metal object replaced (g_ser_gen moved): every stage's argument block encoded again", 1, g_args_dirty[0] && g_args_dirty[1]);
    // a 2D texture in a 3D slot
    tex_t d3 = tex(); mtl_reg_tex(1851, &src, NULL);
    chain c3 = chain_new(); set_rt(&c3, &d3); bind_prog(&c3, &g_pVol); bind_tex(&c3, 1851); draw_cmd(&c3); emit(&c3, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); hh = (uint64_t)c3.p; mtl_sub_index(&hh, 1); nvn_exec(c3.p);
    [blit() copyFromTexture:tex_mtl(&d3) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 8, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    mtl_wait(mtl_sub_commit(), 5000000000ull);
    drawn = 0; for (int i = 0; i < 64; i++) drawn += px[4 * i] >= 62 && px[4 * i] <= 66 && px[4 * i + 1] >= 62 && px[4 * i + 1] <= 66 && px[4 * i + 2] >= 62 && px[4 * i + 2] <= 66;
    EXPECT("a 2D texture in a 3D slot: the zero 3D texture bound in its place (its red line once)", 1, g_red_once[101]);
    EXPECT("... 0.25 in every channel, every pixel", 64, drawn);
    // a size-0 storage binding's extent: the rest of its pool
    uint8_t *pool = c.p; EXPECT("a storage binding of size 0 at 0x1000 into a 64 KB pool covers the rest of it (0xf000)", 0xf000, sbuf_extent((uint64_t)(uintptr_t)pool + 0x1000, 0));
    EXPECT("... one of 256 bytes, its size", 256, sbuf_extent((uint64_t)(uintptr_t)pool + 0x1000, 256));
    // a direct draw reading past a stream's bound size
    EXPECT("attribute sizes: RGBA32F 16, RGBA8 4, RG32F 8, R16F 2, R8_UI2F 1, RGBA16_I2F 8", 1,
           attr_bytes(0x2E) == 16 && attr_bytes(0x25) == 4 && attr_bytes(0x16) == 8 && attr_bytes(0x05) == 2 && attr_bytes(0x51) == 1 && attr_bytes(0x66) == 8);
    NvShader *vsx = [NvShader new]; vsx->rf.nv = 1; vsx->rf.v[0].nvn = 0;
    state_reset(); S.attr[0] = (va_st){0x2E, 0, 0}; S.nattr = 1; S.stream[0] = (vs_st){16, 0}; S.nstream = 1; S.vbuf[0].address = (uint64_t)(uintptr_t)pool; S.vbuf[0].size = 36;
    uint64_t vp0 = g_vb_past; p_draw d9 = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 9, 1, 0}, d2 = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 2, 1, 0}, d3i = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 1, 3, 0};
    vb_past_note(vsx, &d9); EXPECT("9 vertices of 16 bytes from a 36-byte stream: past its bound size", 1, g_vb_past - vp0);
    vb_past_note(vsx, &d2); EXPECT("2 vertices: within it", 1, g_vb_past - vp0);
    S.stream[0].divisor = 1; vb_past_note(vsx, &d3i); EXPECT("a per-instance stream, 3 instances: the third past it", 2, g_vb_past - vp0);
    S.stream[0] = (vs_st){0, 0}; vb_past_note(vsx, &d9); EXPECT("a stride-0 stream: element 0 only, within it", 2, g_vb_past - vp0);
    state_reset();
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
