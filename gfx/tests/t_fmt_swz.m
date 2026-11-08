// A format's own components as the replay samples them (nvn_mtl.m tex_by_id with nvn_fmt_to_mtl's swizzle): an RGB565 and an RGB5A1 texture laid
// out in guest memory with NVN's red in the low bits (the order totk-mobile d3dd76c found for Tears of the Kingdom's terrain textures) read red,
// green and blue where they were written; an RGBX8 texel whose X byte is 0 reads alpha 1; a guest swizzle (G, R, 1, 0) on RGB565 picks NVN's
// green and red, not Metal's; an RGBA4 texture reads red from its low four bits and alpha from its top four (Metal's ABGR4 holds them reversed). GPU
// reads through a compute kernel. The format's swizzle dropped (-DDM_TEST_FMT_SWZ_DROP), as before: 8 fail; RGBA4 read in ABGR4's own order
// (-DDM_TEST_RGBA4_AS_ABGR4, as before): 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
#define EXPECT(name, want, got) do { long long w_ = (want), g_ = (got); n++; printf("%-84s expected %-6llx → got %-6llx %s\n", name, (unsigned long long)w_, (unsigned long long)g_, w_ == g_ ? "✓" : "✗"); fail += w_ != g_; } while (0)
static id<MTLComputePipelineState> g_ps; static id<MTLCommandQueue> g_cq; static uint8_t *g_mem; static size_t g_at; static pool_t g_pl;
// A 4x1 texture of the format at a fresh place in the pool, its texels written in NVN's layout, registered as texture ID tid
static void tex4(int tid, int format, const void *texels, size_t bytes, const view_t *v) {
    static uint64_t ser = 7700; tex_t t = {.serial = ser++}; t.d.width = 4; t.d.height = t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = format;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.mem = g_mem + g_at; t.pool = &g_pl; g_at += (L.size + 0xfff) & ~0xfffull;
    nvn_swizzle(&t.d, 0, 0, texels, bytes, t.mem); mtl_tex_live(&t); mtl_reg_tex(tid, &t, v);
}
static uint32_t rd(int tid, int x) {   // RGBA8 of texel x as the kernel samples it
    mtl_sub_begin(); pthread_mutex_lock(&g_rmu); id<MTLTexture> t = tex_by_id(tid); pthread_mutex_unlock(&g_rmu);   // its upload goes with this submission
    mtl_wait(mtl_sub_commit(), 5000000000ull);
    id<MTLBuffer> out = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]; int xx = x;
    id<MTLCommandBuffer> cb = [g_cq commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:g_ps]; [e setTexture:t atIndex:0]; [e setBuffer:out offset:0 atIndex:0]; [e setBytes:&xx length:4 atIndex:1];
    [e dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)]; [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
    const float *f = out.contents; uint32_t v = 0; for (int c = 0; c < 4; c++) v |= (uint32_t)lroundf(f[c] * 255.f) << (24 - 8 * c);
    return v;   // 0xRRGGBBAA
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); NSError *e = nil;
    id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void k(texture2d<float, access::read> t [[texture(0)]], device float4 *o [[buffer(0)]], constant int &x [[buffer(1)]]) { o[0] = t.read(uint2(x, 0)); }\n" options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    g_ps = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e]; g_cq = [g_dev newCommandQueue];
    if (posix_memalign((void **)&g_mem, getpagesize(), 1 << 20)) abort(); memset(g_mem, 0, 1 << 20); mtl_pool_add(g_mem, 1 << 20);
    g_pl.mem = g_mem; g_pl.size = 1 << 20; g_pl.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;   // a CPU-visible pool: the texels are uploaded
    const uint16_t c565[4] = {0x001f, 0x07e0, 0xf800, 0};          // red in bits 0-4, green 5-10, blue 11-15
    tex4(1, NVN_FORMAT_RGB565, c565, sizeof c565, NULL);
    EXPECT("RGB565: the low five bits read as red", 0xff0000ff, rd(1, 0));
    EXPECT("RGB565: the middle six as green", 0x00ff00ff, rd(1, 1));
    EXPECT("RGB565: the high five as blue", 0x0000ffff, rd(1, 2));
    const uint16_t c5551[4] = {0x801f, 0x03e0, 0x7c00, 0x8000};    // red 0-4, green 5-9, blue 10-14, alpha 15
    tex4(2, NVN_FORMAT_RGB5A1, c5551, sizeof c5551, NULL);
    EXPECT("RGB5A1: red with alpha 1", 0xff0000ff, rd(2, 0));
    EXPECT("RGB5A1: blue with alpha 0", 0x0000ff00, rd(2, 2));
    const uint32_t cx[4] = {0x00332211, 0, 0, 0};                    // R 0x11, G 0x22, B 0x33, X 0
    tex4(3, NVN_FORMAT_RGBX8, cx, sizeof cx, NULL);
    EXPECT("RGBX8: alpha reads 1 whatever the X byte holds", 0x112233ff, rd(3, 0));
    view_t v; memset(&v, 0, sizeof v); v.set_swz = 1; v.swz_r = NVN_TEXTURE_SWIZZLE_G; v.swz_g = NVN_TEXTURE_SWIZZLE_R; v.swz_b = NVN_TEXTURE_SWIZZLE_ONE; v.swz_a = NVN_TEXTURE_SWIZZLE_ZERO;
    tex4(4, NVN_FORMAT_RGB565, c565, sizeof c565, &v);
    EXPECT("RGB565 viewed (G, R, 1, 0): NVN's red texel reads in green", 0x00ffff00, rd(4, 0));
    EXPECT("... NVN's green texel reads in red", 0xff00ff00, rd(4, 1));
    const uint16_t c4444[4] = {0x4321, 0xf000, 0, 0};               // red 0-3, green 4-7, blue 8-11, alpha 12-15 [-DDM_TEST_RGBA4_AS_ABGR4]
    tex4(6, NVN_FORMAT_RGBA4, c4444, sizeof c4444, NULL);
    EXPECT("RGBA4: 0x4321 reads red 1, green 2, blue 3, alpha 4 (of 15)", 0x11223344, rd(6, 0));
    EXPECT("RGBA4: the top four bits are alpha", 0x000000ff, rd(6, 1));
    tex4(5, NVN_FORMAT_RGBA8, cx, sizeof cx, NULL);
    EXPECT("RGBA8 (identity format): the X byte is alpha, untouched", 0x11223300, rd(5, 0));
    printf("FMT_SWZ %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
} }
