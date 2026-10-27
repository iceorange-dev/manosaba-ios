// GPU textures sharing one storage (nvn_mtl.m stor_*; MacNativeGames, MIT): two live objects describing the same GPU-only memory with the same layout
// and format get one MTLTexture, so what is rendered through one is sampled through the other; another format or a CPU-filled texture keeps its own.
// The first object's release leaves the texture resident for the second (validation layer: no fault); objects re-initialized at the storage within
// STOR_IDLE submissions after the last holder left get the texture back with its contents; after that it is retired.
// An entry whose pool was finalized goes STOR_GONE_IDLE submissions after its last holder left (plan item 280); an object made there within that window
// takes it back and keeps it.
// An ID registered again with another object of the storage and the same view takes the view made before; an unused view goes after VIEW_IDLE
// submissions (plan item 271).
// Negative control: DM_NVN_TEX_SHARE=0 fails the checks marked so; an entry of a finalized pool kept STOR_IDLE (-DDM_TEST_STOR_KEEP_GONE): 1 fails;
// every view made anew (-DDM_TEST_VIEW_NOCACHE): 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-90s expected %-8llx → got %-8llx %s\n", name, (unsigned long long)w, (unsigned long long)g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static uint8_t *g_cm;
static void submit(chain *c) { emit(c, OP_END, NULL, 0); state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)(uintptr_t)c->p; mtl_sub_index(&hh, 1); nvn_exec(c->p); mtl_wait(mtl_sub_commit(), 5000000000ull); }
static void render_clear(const tex_t *t, float r, float g, float b) {   // one submission: a render pass on t cleared to (r, g, b, 1)
    chain c = {g_cm, 0}; memset(g_cm, 0, 65536);
    p_set_rt rt = {.n = 1}; rt.color[0] = t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_clear_color x = {.index = 0, .mask = 15, .rgba = {r, g, b, 1}}; emit(&c, OP_CLEAR_COLOR, &x, sizeof x);
    submit(&c);
}
static uint32_t pixel(const tex_t *t) {   // texel (3, 3) as the GPU holds it (a copy in a submission of its own)
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(b);
    state_reset(); mtl_sub_begin();
    [blit() copyFromTexture:tex_mtl(t) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    mtl_wait(mtl_sub_commit(), 5000000000ull); uint32_t v; memcpy(&v, (uint8_t *)b.contents + 3 * 32 + 3 * 4, 4); return v;
}
static uint8_t *g_mem; static pool_t g_gpu_pool, g_cpu_pool;
static tex_t tex_at(size_t off, int fmt, pool_t *pl) {
    static uint64_t s = 9300; tex_t t = {.serial = s++}; t.d.width = t.d.height = 8; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = fmt;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.mem = g_mem + off; t.pool = pl; return t;
}
static void release(tex_t *t) { mtl_tex_release(t); for (int i = 0; i < FIN_GRACE + 2; i++) { chain c = {g_cm, 0}; memset(g_cm, 0, 64); submit(&c); } }   // past the finalize grace
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    if (posix_memalign((void **)&g_mem, getpagesize(), 1 << 20)) abort(); memset(g_mem, 0, 1 << 20); mtl_pool_add(g_mem, 1 << 20);
    if (posix_memalign((void **)&g_cm, getpagesize(), 65536)) abort(); mtl_pool_add(g_cm, 65536);
    g_gpu_pool.mem = g_mem; g_gpu_pool.size = 1 << 20; g_gpu_pool.flags = NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;
    g_cpu_pool = g_gpu_pool; g_cpu_pool.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;
    int on = stor_on();
    // 1. two objects of one GPU-only storage: rendered through A, sampled through B
    tex_t a = tex_at(0x10000, NVN_FORMAT_RGBA8, &g_gpu_pool), b = tex_at(0x10000, NVN_FORMAT_RGBA8, &g_gpu_pool);
    render_clear(&a, 1, 0, 0);
    EXPECT("same storage, layout and format: one texture [DM_NVN_TEX_SHARE=0 fails]", 1, tex_mtl(&a) == tex_mtl(&b));
    EXPECT("... B reads what was rendered through A (red) [DM_NVN_TEX_SHARE=0 fails]", 0xff0000ff, pixel(&b));
    // 2. another format at the same storage, and a CPU-filled texture: their own
    tex_t c = tex_at(0x10000, NVN_FORMAT_RGBA16F, &g_gpu_pool);
    EXPECT("another format at the storage: a texture of its own", 1, tex_mtl(&c) != tex_mtl(&a));
    tex_t p1 = tex_at(0x20000, NVN_FORMAT_RGBA8, &g_cpu_pool), p2 = tex_at(0x20000, NVN_FORMAT_RGBA8, &g_cpu_pool);
    render_clear(&p1, 0, 1, 0); render_clear(&p2, 0, 0, 1);
    EXPECT("CPU-visible pool (CPU-filled candidates): textures of their own", 1, tex_mtl(&p1) != tex_mtl(&p2));
    // 3. A released: B keeps the texture, resident (rendered and read under the validation layer)
    id<MTLTexture> ta = tex_mtl(&a);
    release(&a);
    EXPECT("after A's release B still has the texture", 1, tex_mtl(&b) == ta || !on);
    pthread_mutex_lock(&g_res_mu); int resident = [g_res_objects containsObject:tex_mtl(&b)]; pthread_mutex_unlock(&g_res_mu);
    EXPECT("... still in the residency set (A's release let go of its reference only)", 1, resident);
    render_clear(&b, 0, 1, 0);
    EXPECT("... and renders into it and reads it back (green)", 0xff00ff00, pixel(&b));
    EXPECT("... with no command buffer error (still resident)", 0, mtl_last_commit_error());
    // 4. B released too; an object initialized at the storage soon after takes the texture back with its contents
    release(&b);
    tex_t d = tex_at(0x10000, NVN_FORMAT_RGBA8, &g_gpu_pool);
    EXPECT("re-initialized within the idle window: the same texture [DM_NVN_TEX_SHARE=0 fails]", 1, tex_mtl(&d) == ta);
    EXPECT("... with its contents (green) [DM_NVN_TEX_SHARE=0 fails]", 0xff00ff00, pixel(&d));
    // 5. nobody holds it for STOR_IDLE submissions: retired; a new object gets a new texture
    release(&d);
    uint64_t sw0 = g_stor_swept;
    for (int i = 0; i < STOR_IDLE + 2; i++) { chain e = {g_cm, 0}; memset(g_cm, 0, 64); submit(&e); }
    if (on) EXPECT("idle for STOR_IDLE submissions: the entry is retired", 1, g_stor_swept > sw0);
    tex_t f = tex_at(0x10000, NVN_FORMAT_RGBA8, &g_gpu_pool);
    EXPECT("an object after that: a new texture", 1, tex_mtl(&f) != ta);
    // 6. A texture ID used after its texture's release (the loading screen's two BC4 textures): made again; with the storage's texture still kept it has
    //    its contents and is no red line, and it stays RECREATE_GRACE submissions. After the entry is retired the same use comes back blank (red)
    { tex_t tg = tex_at(0x30000, NVN_FORMAT_RGBA8, &g_gpu_pool); mtl_reg_tex(21, &tg, NULL);
      render_clear(&tg, 0, 0, 1);
      pthread_mutex_lock(&g_rmu); tex_by_id(21); pthread_mutex_unlock(&g_rmu);
      release(&tg);
      uint64_t k0 = g_late_kept, b0 = g_late_blank;
      pthread_mutex_lock(&g_rmu); id<MTLTexture> v = tex_by_id(21); pthread_mutex_unlock(&g_rmu);
      EXPECT("ID used after its texture's release, storage texture kept: made again from it (no red line) [DM_NVN_TEX_SHARE=0 fails]", 1, (long long)(g_late_kept - k0));
      EXPECT("... none blank [DM_NVN_TEX_SHARE=0 fails]", 0, (long long)(g_late_blank - b0));
      EXPECT("... with its contents (blue) [DM_NVN_TEX_SHARE=0 fails]", 0xffff0000, pixel(&tg));
      int due_ok = 0; pthread_mutex_lock(&g_fin_mu); for (int i = 0; i < g_nfin; i++) if (g_fin[i].serial == tg.serial) due_ok = g_fin[i].due >= g_sub_serial + RECREATE_GRACE - 2; pthread_mutex_unlock(&g_fin_mu);
      EXPECT("... released again only RECREATE_GRACE submissions later", 1, due_ok);
      (void)v; }
    // 7. Plan item 280: an entry whose pool was finalized is retired STOR_GONE_IDLE submissions after nobody holds it (manosaba's trial: a pool per
    //    render target, both finalized together); an object made at that storage within the window still gets the texture with its contents
    if (on) { uint8_t *m2; if (posix_memalign((void **)&m2, getpagesize(), 1 << 20)) abort(); memset(m2, 0, 1 << 20); mtl_pool_add(m2, 1 << 20);
      static pool_t p2; p2 = g_gpu_pool; p2.mem = m2;
      tex_t g = tex_at(0, NVN_FORMAT_RGBA8, &p2); g.mem = m2 + 0x1000; render_clear(&g, 1, 1, 0); release(&g);
      mtl_pool_del(m2); uint64_t sw1 = g_stor_swept;
      for (int i = 0; i < STOR_GONE_IDLE + 2; i++) { chain e = {g_cm, 0}; memset(g_cm, 0, 64); submit(&e); }
      EXPECT("280: its pool finalized, nobody holding it: retired within STOR_GONE_IDLE submissions [-DDM_TEST_STOR_KEEP_GONE]", 1, g_stor_swept > sw1);
      uint8_t *m3; if (posix_memalign((void **)&m3, getpagesize(), 1 << 20)) abort(); memset(m3, 0, 1 << 20); mtl_pool_add(m3, 1 << 20);
      static pool_t p3; p3 = g_gpu_pool; p3.mem = m3;
      tex_t h = tex_at(0, NVN_FORMAT_RGBA8, &p3); h.mem = m3 + 0x1000; render_clear(&h, 0, 1, 1); id<MTLTexture> th = tex_mtl(&h); release(&h);
      mtl_pool_del(m3); mtl_pool_add(m3, 1 << 20);   // finalized, and a pool made again on that memory
      tex_t j = tex_at(0, NVN_FORMAT_RGBA8, &p3); j.mem = m3 + 0x1000;
      EXPECT("280: ... an object at that storage within the window: the same texture", 1, tex_mtl(&j) == th);
      for (int i = 0; i < STOR_GONE_IDLE + 2; i++) { chain e = {g_cm, 0}; memset(g_cm, 0, 64); submit(&e); }
      EXPECT("280: ... held again, kept past the window, with its contents (cyan)", 0xffffff00, pixel(&j)); }
    // 8. Plan item 271: an ID registered again with a new object on the same storage and the same view takes the view made before (one MTLTexture, the
    //    same view of it); a view unused for VIEW_IDLE submissions is let go
    if (on) { tex_t va = tex_at(0x50000, NVN_FORMAT_RGBA8, &g_gpu_pool), vb = tex_at(0x50000, NVN_FORMAT_RGBA8, &g_gpu_pool);
      view_t vw; memset(&vw, 0, sizeof vw); vw.set_swz = 1; vw.swz_r = 4; vw.swz_g = 3; vw.swz_b = 2; vw.swz_a = 5;   // B G R A
      mtl_reg_tex(31, &va, &vw); pthread_mutex_lock(&g_rmu); id<MTLTexture> v1 = tex_by_id(31); pthread_mutex_unlock(&g_rmu);
      uint64_t made0 = g_view_made; mtl_reg_tex(31, &vb, &vw); pthread_mutex_lock(&g_rmu); id<MTLTexture> v2 = tex_by_id(31); pthread_mutex_unlock(&g_rmu);
      EXPECT("271: the ID registered again with another object of the storage, the same view: the view made before [-DDM_TEST_VIEW_NOCACHE]", 1, v1 && v1 == v2 && g_view_made == made0);
      uint32_t n0 = g_views.n; mtl_reg_tex(31, NULL, NULL);
      for (int i = 0; i < VIEW_IDLE + 2; i++) { chain e = {g_cm, 0}; memset(g_cm, 0, 64); submit(&e); }
      EXPECT("271: ... unused for VIEW_IDLE submissions: let go", 1, n0 > 0 && g_views.n < n0); }
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
