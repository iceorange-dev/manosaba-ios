// Render targets of virtual pools in a heap (nvn_rtheap.h rt_heap_place, nvn_mtl.m rt_heap_texture; plan item 138, totk-mobile's placement rule,
// Apache-2.0, written anew). The rule, on 20,000 random pairs of targets in a 64 MiB pool with Metal sizes 0.8-1.6 times their guest size: no two
// targets disjoint in guest memory overlap in the heap; a target fitting its guest range × 1.25 is placed at its offset × 1.25, one that does not is
// refused; the heap is the pool × 1.25 rounded up to 64 KiB. On the GPU (Metal 4, DM_NVN_RT_HEAP=1, the TBDR rewrites off) through tex_mtl: two
// targets at one guest offset with different layouts share heap memory, one 1 MiB further does not; cleared blue, red, then green in that order the
// disjoint one is still blue and the last one green (validation layer, no command buffer error); a target whose guest range is too small for its
// Metal texture, and one in a pool that is not virtual, keep textures of their own; the census counts 3 placed and 1 not fitting of the 4
// virtual-pool targets. On Metal 3 every target keeps a texture of its own. A target allowed past its scaled range (-DDM_TEST_RTHEAP_NO_LIMIT):
// 2 fail (Metal 4: 4). The default (plan item 166): on with Metal 4 and the TBDR rewrites off, off with =0, on Metal 3 or with the rewrites;
// off unless asked (-DDM_TEST_RTHEAP_DEFAULT_OFF): 1 fails. Metal 4: another description over a target's heap bytes reads its texels' bits with
// lossless compression on (cleared, and blitted).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static uint8_t *g_cm;
static void submit(chain *c) { emit(c, OP_END, NULL, 0); state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)(uintptr_t)c->p; mtl_sub_index(&hh, 1); nvn_exec(c->p); mtl_wait(mtl_sub_commit(), 5000000000ull); }
static void render_clear(const tex_t *t, float r, float g, float b) {
    chain c = {g_cm, 0}; memset(g_cm, 0, 65536);
    p_set_rt rt = {.n = 1}; rt.color[0] = t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_clear_color x = {.index = 0, .mask = 15, .rgba = {r, g, b, 1}}; emit(&c, OP_CLEAR_COLOR, &x, sizeof x);
    submit(&c);
}
static uint32_t pixel(const tex_t *t) {   // texel (3, 3)
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(b);
    state_reset(); mtl_sub_begin();
    [blit() copyFromTexture:tex_mtl(t) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    mtl_wait(mtl_sub_commit(), 5000000000ull); uint32_t v; memcpy(&v, (uint8_t *)b.contents + 3 * 32 + 3 * 4, 4); return v;
}
static uint8_t *g_mem; static pool_t g_vpool, g_plain;
static tex_t tex_at(int64_t off, unsigned w, unsigned h, pool_t *pl) {
    static uint64_t s = 9700; tex_t t = {.serial = s++}; t.d.width = w; t.d.height = h; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.off = off; t.mem = pl->mem + off; t.pool = pl; return t;
}
static int overlap(id<MTLTexture> a, id<MTLTexture> b) { return a.heapOffset < b.heapOffset + b.allocatedSize && b.heapOffset < a.heapOffset + a.allocatedSize; }
static uint64_t rnd(uint64_t *s) { *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17; return *s; }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    // the rule
    { const uint64_t pool = 64u << 20, heap = rt_heap_size(pool, RT_HEAP_SCALE); uint64_t s = 0x9e3779b97f4a7c15ull; int bad = 0, both = 0;
      static const uint64_t al[4] = {256, 4096, 16384, 65536};
      for (int i = 0; i < 20000; i++) {
          uint64_t o[2], z[2], p[2], n[2]; int ok[2];
          for (int k = 0; k < 2; k++) { z[k] = 0x1000 + rnd(&s) % (4u << 20); o[k] = (rnd(&s) % (pool - z[k])) & ~0xfffull;
              n[k] = (uint64_t)((double)z[k] * (0.8 + (double)(rnd(&s) % 801) / 1000.0)); ok[k] = rt_heap_place(o[k], z[k], RT_HEAP_SCALE, n[k], al[rnd(&s) & 3], heap, &p[k]); }
          if (!ok[0] || !ok[1]) continue;
          both++;
          int gdis = o[0] + z[0] <= o[1] || o[1] + z[1] <= o[0], hov = p[0] < p[1] + n[1] && p[1] < p[0] + n[0];
          bad += gdis && hov;
      }
      CHECK(bad == 0 && both > 1000, "20,000 random pairs: none disjoint in guest memory overlaps in the heap (%d of %d placed pairs)", bad, both);
      uint64_t at = 0; int ok = rt_heap_place(0x100000, 0x100000, RT_HEAP_SCALE, 0x100000, 0x10000, heap, &at);
      CHECK(ok && at == 0x140000, "a 1 MiB target at 1 MiB needing 1 MiB (64 KiB aligned): placed at 1.25 MiB (%#llx)", (unsigned long long)at);
      ok = rt_heap_place(0x100000, 0x100000, RT_HEAP_SCALE, 0x150000, 0x10000, heap, &at);
      CHECK(!ok, "... needing 1.31 MiB (more than its range × 1.25): refused");
      CHECK(heap == 80u << 20 && rt_heap_size(1000001, RT_HEAP_SCALE) == 0x140000, "the heap: pool × 1.25 rounded up to 64 KiB (%llu MiB)", (unsigned long long)(heap >> 20)); }
    // the default (plan item 166): on unless DM_NVN_RT_HEAP=0, with Metal 4 and the TBDR rewrites off
    CHECK(rt_heap_decide(NULL, 0, 0) && rt_heap_decide("1", 0, 0) && !rt_heap_decide("0", 0, 0) && !rt_heap_decide(NULL, 0, 1) && !rt_heap_decide("1", 0, 1) && !rt_heap_decide(NULL, 1, 0),
          "default: on with Metal 4 and the TBDR rewrites off; off with =0, on Metal 3 or with the rewrites on");
    // on the GPU through tex_mtl
    setenv("DM_NVN_TBDR", "0", 1); setenv("DM_NVN_RT_HEAP", "1", 1);
    mtl_init();
    if (posix_memalign((void **)&g_mem, getpagesize(), 4 << 20)) abort(); memset(g_mem, 0, 4 << 20); mtl_pool_add(g_mem, 4 << 20);
    if (posix_memalign((void **)&g_cm, getpagesize(), 65536)) abort(); mtl_pool_add(g_cm, 65536);
    g_vpool.mem = g_mem; g_vpool.size = 2 << 20;
    g_vpool.flags = NVN_MEMORY_POOL_FLAGS_VIRTUAL | NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_GPU_CACHED | NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE;
    g_plain = g_vpool; g_plain.mem = g_mem + (2 << 20); g_plain.flags = NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;
    tex_t a = tex_at(0, 64, 64, &g_vpool), b = tex_at(0, 32, 128, &g_vpool), c = tex_at(0x100000, 64, 64, &g_vpool);
    tex_t d = tex_at(0x40000, 64, 64, &g_vpool); d.size = 0x1000;   // a guest range too small for its Metal texture
    tex_t e = tex_at(0, 64, 64, &g_plain);
    id<MTLTexture> ta = tex_mtl(&a), tb = tex_mtl(&b), tc = tex_mtl(&c), td = tex_mtl(&d), te = tex_mtl(&e);
#if DM_NVN_M3
    CHECK(!ta.heap && !tb.heap && !tc.heap && !td.heap && !te.heap, "Metal 3: every target keeps a texture of its own");
#else
    CHECK(ta.heap && ta.heap == tb.heap && ta.heap == tc.heap && ta.heapOffset == 0, "three targets of the virtual pool in one heap, the first at 0");
    CHECK(overlap(ta, tb) && !overlap(ta, tc) && !overlap(tb, tc) && tc.heapOffset >= 0x140000,
          "the two at one guest offset share heap memory, the one 1 MiB further does not (at %#lx)", (unsigned long)tc.heapOffset);
    CHECK(!td.heap && !te.heap, "a guest range too small for its texture, and a pool that is not virtual: textures of their own");
    CHECK(g_rth_placed == 3 && g_rth_out == 1 && g_rtv_made == 4, "census: %llu placed, %llu not fitting, %llu virtual-pool targets made",
          (unsigned long long)g_rth_placed, (unsigned long long)g_rth_out, (unsigned long long)g_rtv_made);
#endif
    render_clear(&c, 0, 0, 1); render_clear(&a, 1, 0, 0); render_clear(&b, 0, 1, 0);
    uint32_t pc = pixel(&c), pb = pixel(&b);
    CHECK(pc == 0xffff0000 && pb == 0xff00ff00, "cleared blue, red, then green: the disjoint one still blue (%08x), the last green (%08x)", pc, pb);
    CHECK(mtl_last_commit_error() == 0, "no command buffer error (validation layer)");
#if !DM_NVN_M3
    // Another description over the same heap bytes reads them as plain texels with lossless compression left on (plan item 277: AnySwitch 8ba3c71 saw
    // garbage there and turned it off for heap members; on the M5, 2026-10-27, cleared and blitted texels read right through an R32UI target)
    { tex_t g = tex_at(0x80000, 64, 64, &g_vpool), f = tex_at(0x80000, 64, 64, &g_vpool); f.d.format = NVN_FORMAT_R32UI;
      id<MTLTexture> tg = tex_mtl(&g), tf = tex_mtl(&f);
      render_clear(&g, 1, 0, 0); uint32_t pf = pixel(&f);
      CHECK(tg.heap && tg.heapOffset == tf.heapOffset && pf == 0xff0000ff, "an RGBA8 target cleared red, read through an R32UI one at its heap offset: its bits (%08x)", pf);
      id<MTLBuffer> src = [g_dev newBufferWithLength:64 * 64 * 4 options:MTLResourceStorageModeShared]; res_add(src);
      uint32_t *px = (uint32_t *)src.contents; for (int i = 0; i < 64 * 64; i++) px[i] = (uint32_t)i * 2654435761u;
      state_reset(); mtl_sub_begin();
      [blit() copyFromBuffer:src sourceOffset:0 sourceBytesPerRow:256 sourceBytesPerImage:64 * 64 * 4 sourceSize:MTLSizeMake(64, 64, 1) toTexture:tg destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
      mtl_wait(mtl_sub_commit(), 5000000000ull);
      pf = pixel(&f);
      CHECK(pf == px[3 * 64 + 3] && tg.allowGPUOptimizedContents, "... texels blitted into it (compressible), read through the R32UI one: the same bits (%08x, written %08x)", pf, px[3 * 64 + 3]); }
#endif
    char line[256]; rt_census_line(line, sizeof line); printf("  %s\n", line + 2);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
