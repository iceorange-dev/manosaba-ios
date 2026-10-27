// Upload batches bounded (nvn_mtl.m up_bound / up_retire; seedds/nxnative 7dc250e): with DM_NVN_UP_BATCH_MB=1, twelve 256 KiB uploads in one
// submission go in three batches, and what they upload is right; in one submission, the one-off staging block of a committed batch is released
// once that batch has completed, not after the submission's render fence. -DDM_TEST_UP_UNBOUNDED (one batch a submission, as before): 3 fail
// (one batch; g1's batch not committed, so neither completed nor released); -DDM_TEST_UP_RETIRE_AT_FENCE: 1 fails.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-80s expected %-10llx → got %-10llx %s\n", name, (unsigned long long)w, (unsigned long long)g, w==g?"✓":"✗"); fail += w != g; } while (0)
static uint8_t *g_mem; static pool_t g_pl;
static tex_t filled(uint64_t ser, unsigned w, unsigned h, size_t at, uint32_t v) {   // a CPU-filled RGBA8 texture at g_mem + at, every texel v
    tex_t t = {.serial = ser}; t.d.width = w; t.d.height = h; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.mem = g_mem + at; t.pool = &g_pl;
    uint32_t *lin = malloc((size_t)w * h * 4); for (size_t i = 0; i < (size_t)w * h; i++) lin[i] = v; nvn_swizzle(&t.d, 0, 0, lin, w * 4, t.mem); free(lin);
    return t;
}
static uint32_t texel(id<MTLTexture> mt, unsigned x, unsigned y) {
    id<MTLBuffer> rb = [g_dev newBufferWithLength:4 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
    [be copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x, y, 0) sourceSize:MTLSizeMake(1, 1, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:4 destinationBytesPerImage:4];
    [be endEncoding]; [cb commit]; [cb waitUntilCompleted]; uint32_t v; memcpy(&v, rb.contents, 4); return v;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_UP_BATCH_MB", "1", 1); mtl_init();
    size_t big = (size_t)96 << 20;
    if (posix_memalign((void **)&g_mem, getpagesize(), big)) abort(); memset(g_mem, 0, big); mtl_pool_add(g_mem, big); nvn_dirty_add_range(g_mem, big);
    g_pl.mem = g_mem; g_pl.size = big; g_pl.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;

    // twelve 256 KiB uploads in one submission
    tex_t t[12]; for (int k = 0; k < 12; k++) t[k] = filled(9800 + (uint64_t)k, 256, 256, (size_t)k * 0x40000, 0x0a0b0c00u + (uint32_t)k);
    uint64_t b0 = g_up_batch_no;
    mtl_sub_begin(); for (int k = 0; k < 12; k++) @autoreleasepool { (void)tex_mtl(&t[k]); } mtl_wait(mtl_sub_commit(), 5000000000ull);
    while (atomic_load(&g_up_pending)) usleep(200);
    EXPECT("twelve 256 KiB uploads in one submission, a 1 MiB bound: batches [-DDM_TEST_UP_UNBOUNDED]", 3, g_up_batch_no - b0);
    EXPECT("... the first texture right (texel 200,17)", 0x0a0b0c00u, texel(tex_mtl(&t[0]), 200, 17));
    EXPECT("... the last texture right (texel 3,250)", 0x0a0b0c0bu, texel(tex_mtl(&t[11]), 3, 250));

    // one-off staging blocks (36 MiB each, more than a pooled block) in one submission, a batch each
    tex_t g1 = filled(9900, 4096, 2304, (size_t)4 << 20, 0x11111111u), g2 = filled(9901, 4096, 2304, (size_t)44 << 20, 0x22222222u);
    tex_t g3 = filled(9902, 256, 256, (size_t)90 << 20, 0x33333333u);
    __weak id w1;
    mtl_sub_begin();
    @autoreleasepool { (void)tex_mtl(&g1); w1 = g_up_block; }
    uint64_t n1 = g_up_batch_no;
    @autoreleasepool { (void)tex_mtl(&g2); }   // g1's batch committed at the bound, g2's begun
    for (int i = 0; i < 5000 && atomic_load(&g_up_done_batch) < n1; i++) usleep(1000);
    EXPECT("g1's batch completed on the GPU", 1, atomic_load(&g_up_done_batch) >= n1);
    @autoreleasepool { (void)tex_mtl(&g3); }   // the next batch begins: completed batches' staging released
    EXPECT("... its 36 MiB one-off staging block released within the submission [-DDM_TEST_UP_RETIRE_AT_FENCE]", 1, w1 == nil);
    mtl_wait(mtl_sub_commit(), 5000000000ull); while (atomic_load(&g_up_pending)) usleep(200);
    EXPECT("... g1 right (texel 4000,2000)", 0x11111111u, texel(tex_mtl(&g1), 4000, 2000));
    EXPECT("... g2 right (texel 17,9)", 0x22222222u, texel(tex_mtl(&g2), 17, 9));
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
