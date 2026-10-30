// Quad strips drawn (plan item 291, gfx/nvn/nvn_quad_strip.h; they were skipped on Metal): an 8x8 target, vertices in pairs across it at x = -1, 0,
// 1 (the bottom, then the top of each), red. 6 vertices: two quads, every pixel drawn; 5: one quad, the left half only (a triangle strip of the
// same vertices drew a triangle into the right half); indexed (16-bit) 0 1 2 3 R 2 3 4 5 with restart at R: two strips, every pixel; 0 1 2 3 R 4 5:
// the second strip one pair, nothing (R is not a vertex). A plain quad's order (-DDM_TEST_QS_ABC): 4 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-96s expected %-4lld → got %-4lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static chain chain_new(void) { chain c={0}; if (posix_memalign((void **)&c.p,getpagesize(),65536)) abort(); memset(c.p,0,65536); mtl_pool_add(c.p,65536); return c; }
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static tex_t tex(unsigned w) { static uint64_t s=9800; tex_t t={.serial=s++}; t.d.width=t.d.height=w; t.d.depth=t.d.samples=t.d.levels=1; t.d.target=NVN_TEXTURE_TARGET_2D; t.d.format=NVN_FORMAT_RGBA8; return t; }
static void set_rt(chain *c, const tex_t *a) { p_set_rt r={.n=1}; r.color[0]=a; emit(c,OP_SET_RT,&r,sizeof r); p_viewports v={.n=1}; v.v[0][2]=v.v[0][3]=8; emit(c,OP_VIEWPORTS,&v,sizeof v); p_scissor sc={0,0,8,8}; emit(c,OP_SCISSOR,&sc,sizeof sc);
    p_clear_color x={.index=0,.mask=15,.rgba={0,0,0,0}}; emit(c,OP_CLEAR_COLOR,&x,sizeof x);
    ds_st d={0,0,NVN_DEPTH_FUNC_ALWAYS,0,NVN_STENCIL_FUNC_ALWAYS,NVN_STENCIL_FUNC_ALWAYS,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP}; p_bind_state xs={.kind=5}; memcpy(xs.bytes,&d,sizeof d); emit(c,OP_BIND_STATE,&xs,sizeof xs); }
static uint8_t *g_mem; static prog_t g_pg;
// The strip drawn (non-indexed with ix NULL: n vertices; else n 16-bit indices, restart at r), the red pixels of the left and the right half
static void strip_draw(const uint16_t *ix, int n, int r, int *left, int *right) {
    tex_t t = tex(8); chain c = chain_new(); set_rt(&c, &t);
    p_bind_program bp = {&g_pg, 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    if (ix) { p_prim_restart pr = {1, (uint32_t)r}; emit(&c, OP_PRIM_RESTART, &pr, sizeof pr); memcpy(g_mem, ix, (size_t)n * 2);
              p_draw_idx di = {NVN_DRAW_PRIMITIVE_QUAD_STRIP, 1, n, 0, 0, 1, (uint64_t)(uintptr_t)g_mem}; emit(&c, OP_DRAW_IDX, &di, sizeof di); }
    else { p_draw dw = {NVN_DRAW_PRIMITIVE_QUAD_STRIP, 0, n, 1, 0}; emit(&c, OP_DRAW, &dw, sizeof dw); }
    emit(&c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(b);
    [blit() copyFromTexture:tex_mtl(&t) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    mtl_wait(mtl_sub_commit(), 5000000000ull);
    *left = *right = 0; const uint32_t *px = b.contents;
    for (int i = 0; i < 64; i++) if (px[i] == 0xff0000ff) { if (i % 8 < 4) (*left)++; else (*right)++; }
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    if (posix_memalign((void **)&g_mem, getpagesize(), 65536)) abort(); memset(g_mem, 0, 65536); mtl_pool_add(g_mem, 65536);
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint id [[vertex_id]]) { return float4(id < 6 ? float2(float(id / 2) - 1, (id & 1) ? 1.0 : -1.0) : float2(1, 1), 0, 1); }\n"
        "fragment float4 red() { return float4(1, 0, 0, 1); }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    g_pg.serial = 8810; NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"]; p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:@"red"]; serial_put(g_pg.serial, p);
    int a, b;
    strip_draw(NULL, 6, 0, &a, &b); EXPECT("6 vertices: two quads, every pixel (left + right)", 64, a + b);
    strip_draw(NULL, 5, 0, &a, &b); EXPECT("5 vertices: one quad, the left half", 32, a); EXPECT("... nothing in the right half (the odd last vertex draws nothing)", 0, b);
    const uint16_t i9[9] = {0, 1, 2, 3, 0xffff, 2, 3, 4, 5}, i7[7] = {0, 1, 2, 3, 0xffff, 4, 5};
    strip_draw(i9, 9, 0xffff, &a, &b); EXPECT("indexed 0 1 2 3 R 2 3 4 5, restart at R: two strips, every pixel", 64, a + b);
    strip_draw(i7, 7, 0xffff, &a, &b); EXPECT("indexed 0 1 2 3 R 4 5: the left quad only (the second strip one pair)", 32, a); EXPECT("... nothing in the right half", 0, b);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
