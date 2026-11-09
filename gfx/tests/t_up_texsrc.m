// Texture uploads from a pool not yet resident (plan item 315c; mtl/upload.inc up_texsrc): the GPU deswizzle reads the texture's storage through a
// zero-copy buffer over just its pages, made after the dirty tracker protected them, resident for its upload batch only.
//   pool A (textures only, never addressed otherwise): a 64x64 RGBA8 texture at a non-page-aligned offset uploaded right, pool A not resident, the
//   upload read through a buffer of its own pages; that buffer gone once its batch has retired (the next batch begins); the storage written (the
//   host's pre-write hook, as the game's writes go) and uploaded again: the new texels, through a buffer of its own again, pool A still not resident;
//   then a GPU fill through pool A's own buffer (made over writable pages before) still lands, on the texture's protected pages too;
//   pool B, resident already (its address taken): its texture read through the pool's buffer, no buffer of its own.
// The pool's own buffer, as before (-DDM_TEST_UP_SRC_POOL): 5 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-96s expected %-10llx → got %-10llx %s\n", name, (unsigned long long)w, (unsigned long long)g, w==g?"✓":"✗"); fail += w != g; } while (0)
static id<MTLCommandQueue> g_tq;
static pool_t g_pl = {.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED};
static void fill(tex_t *t, uint32_t v) {   // every texel v, swizzled into the storage (the host's pre-write hook first: the pages may be protected)
    nvn_texlayout L; nvn_tex_layout(&t->d, &L); dm_guest_write_prepare(t->mem, L.size);
    uint32_t *lin = malloc((size_t)t->d.width * t->d.height * 4); for (size_t i = 0; i < (size_t)t->d.width * t->d.height; i++) lin[i] = v;
    nvn_swizzle(&t->d, 0, 0, lin, t->d.width * 4, t->mem); free(lin);
}
static tex_t tex_at(uint8_t *at, uint64_t serial, uint32_t v) {
    tex_t t = {.serial = serial, .pool = &g_pl, .has_layout = 1}; t.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = 64, .height = 64, .depth = 1, .levels = 1, .samples = 1};
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.mem = at; fill(&t, v); return t;
}
static id<MTLTexture> use(uint32_t tid) {   // the texture a submission binds for an ID
    mtl_sub_begin(); pthread_mutex_lock(&g_rmu); id<MTLTexture> mt = tex_by_id(tid); pthread_mutex_unlock(&g_rmu); mtl_wait(mtl_sub_commit(), 5000000000ull);
    while (atomic_load(&g_up_pending)) usleep(200);
    return mt;
}
static uint32_t texel(id<MTLTexture> mt, unsigned x, unsigned y) {
    id<MTLBuffer> rb = [g_dev newBufferWithLength:4 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_tq commandBuffer]; id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
    [be copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x, y, 0) sourceSize:MTLSizeMake(1, 1, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:4 destinationBytesPerImage:4];
    [be endEncoding]; [cb commit]; [cb waitUntilCompleted]; uint32_t v; memcpy(&v, rb.contents, 4); return v;
}
static int resident(const void *p) { uintptr_t b; uint64_t bl; int r = -1; return mtl_pool_peek((uint64_t)(uintptr_t)p, 1, &b, &bl, &r) ? r : -1; }
static int gpu_fill_ok(uint8_t *at, size_t n, uint8_t v) {   // a GPU fill of [at, at + n) through the pool buffer holding it, then whether the memory holds it
    void *bp = NULL; uint64_t off = 0; if (!mtl_addr((uint64_t)(uintptr_t)at, n, &bp, &off) || !bp) return 0;
    id<MTLBuffer> b = (__bridge id<MTLBuffer>)bp; id<MTLCommandBuffer> cb = [g_tq commandBuffer]; id<MTLBlitCommandEncoder> e = [cb blitCommandEncoder];
    [e fillBuffer:b range:NSMakeRange(off, n) value:v]; [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
    for (size_t i = 0; i < n; i++) if (at[i] != v) return 0;
    return 1;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); g_tq = [g_dev newCommandQueue];
    size_t psz = (size_t)1 << 20, pg = (size_t)getpagesize(); uint8_t *pa, *pb;
    if (posix_memalign((void **)&pa, pg, psz) || posix_memalign((void **)&pb, pg, psz)) abort();
    memset(pa, 0, psz); memset(pb, 0, psz);
    mtl_pool_add(pa, psz); nvn_dirty_add_range(pa, psz); mtl_pool_add(pb, psz); nvn_dirty_add_range(pb, psz);
    g_pl.mem = pa; g_pl.size = psz;

    // pool A: textures only
    tex_t ta = tex_at(pa + 0x5200, 9700, 0x11223344u); mtl_tex_live(&ta); mtl_reg_tex(70, &ta, NULL);
    uint64_t n0 = g_up_src_n; __weak id w1;
    @autoreleasepool { id<MTLTexture> m = use(70); w1 = g_up_src_last;
        EXPECT("pool A (textures only): its texture at a non-page-aligned offset uploaded right (texel 40,9)", 0x11223344u, texel(m, 40, 9)); }
    EXPECT("... pool A not resident [-DDM_TEST_UP_SRC_POOL: resident]", 0, resident(pa));
    EXPECT("... the upload read through a buffer of the texture's own pages [-DDM_TEST_UP_SRC_POOL: none]", 1, g_up_src_n - n0);
    EXPECT("... that buffer kept while its batch has not retired [-DDM_TEST_UP_SRC_POOL: none]", 1, w1 != nil);
    EXPECT("... over pages the dirty tracker had protected", 1, range_ro((uintptr_t)(pa + 0x5200) & ~(pg - 1), ((uintptr_t)(pa + 0x5200) & ~(pg - 1)) + pg));
    fill(&ta, 0x55667788u);   // written: the next use uploads it again, in a batch of its own (the first one's retired as it begins)
    @autoreleasepool { id<MTLTexture> m = use(70);
        EXPECT("written and used again: the new texels (texel 63,63)", 0x55667788u, texel(m, 63, 63)); }
    EXPECT("... the first upload's buffer gone with its batch", 1, w1 == nil);
    EXPECT("... read through a buffer of its own pages again [-DDM_TEST_UP_SRC_POOL: none]", 2, g_up_src_n - n0);
    EXPECT("... pool A still not resident [-DDM_TEST_UP_SRC_POOL: resident]", 0, resident(pa));
    EXPECT("a GPU fill through pool A's own buffer lands, on a page of the texture (protected)", 1, gpu_fill_ok(pa + 0x4000 + 0x1000, 0x100, 0x5a));
    EXPECT("... and on a page outside it", 1, gpu_fill_ok(pa + 0x80000, 0x100, 0xa5));

    // pool B: resident already
    { void *bp; uint64_t off; (void)mtl_addr((uint64_t)(uintptr_t)(pb + 0x80000), 16, &bp, &off); }
    g_pl.mem = pb; tex_t tb = tex_at(pb + 0x1200, 9701, 0x99aabbccu); mtl_tex_live(&tb); mtl_reg_tex(71, &tb, NULL);
    uint64_t n1 = g_up_src_n;
    @autoreleasepool { id<MTLTexture> m = use(71);
        EXPECT("pool B (resident already): its texture uploaded right (texel 5,60)", 0x99aabbccu, texel(m, 5, 60)); }
    EXPECT("... read through the pool's own buffer", 0, g_up_src_n - n1);
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
