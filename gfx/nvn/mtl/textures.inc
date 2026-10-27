// gfx/nvn/mtl/textures.inc, part of nvn_mtl.m (included there in order, one translation unit): Textures as Metal textures, and GPU textures sharing one storage
// ---------- Texture → MTLTexture ----------
static int fmt_compressed(int f) { return (f >= 0x41 && f <= 0x50) || (f >= 0x79 && f <= 0x94); }
static int fmt_depth(int f) { return f >= 0x31 && f <= 0x36; }
static int tex_need_pfv(MTLPixelFormat base, MTLPixelFormat view) {
    if (base == view) return 0;
    for (size_t i = 0; i < sizeof g_pfv_srgb / sizeof *g_pfv_srgb; i++)
        if ((base == g_pfv_srgb[i].a && view == g_pfv_srgb[i].b) || (base == g_pfv_srgb[i].b && view == g_pfv_srgb[i].a)) return 0;
    return 1;
}
// Cache tables on the replay hot path always use nvn_hmap (C hash, keys not boxed); the table stores CFBridgingRetain'ed objects, CFRelease'd on replace / delete
static void hm_rel(void *v) { CFRelease(v); }
static __unsafe_unretained id hm_get_id(const nvn_hmap *m, const void *k) { return (__bridge id)nvn_hm_get(m, k); }
static void hm_put_id(nvn_hmap *m, const void *k, id o) { void *old = nvn_hm_put(m, k, (void *)CFBridgingRetain(o)); if (old) CFRelease(old); }
static void hm_del_id(nvn_hmap *m, const void *k) { void *old = nvn_hm_del(m, k); if (old) CFRelease(old); }
static nvn_hmap g_by_serial = {.klen = 8};     // Serial → MTLTexture / NvPkg / NvProg
static pthread_mutex_t g_smu = PTHREAD_MUTEX_INITIALIZER;
// Serial table generation: +1 when entries are added / removed or a packaged texture is replaced. Downstream caches (texture views, programs) record the generation at fetch time; if it is unchanged they can be used directly without another table lookup
static _Atomic uint64_t g_ser_gen = 1;
// Which serial each generation step was for (g_ser_chg[gen % SER_CHG_N]; 0 = every serial): a texture ID's view whose serial is not among the steps
// since it was checked stays valid (ser_unaffected). Every release (fin_drain, 30 a submission in Tears of the Kingdom) and every re-upload stepped
// the generation, and every texture ID went down the slow path after each (MacNativeGames g_ser_chg, MIT; written anew here). Steps happen under g_smu
#define SER_CHG_N 1024
static _Atomic uint64_t g_ser_chg[SER_CHG_N];
static void ser_bump_locked(uint64_t serial) {
    uint64_t g = atomic_load_explicit(&g_ser_gen, memory_order_relaxed) + 1;
    atomic_store_explicit(&g_ser_chg[g % SER_CHG_N], serial, memory_order_relaxed);
    atomic_store_explicit(&g_ser_gen, g, memory_order_release);   // the step's serial is visible before the step
}
static void ser_bump(uint64_t serial) { pthread_mutex_lock(&g_smu); ser_bump_locked(serial); pthread_mutex_unlock(&g_smu); }
static int ser_unaffected(uint64_t from, uint64_t to, uint64_t serial) {   // 1: none of the steps (from, to] was for serial or for every serial
    if (!serial || to - from >= SER_CHG_N) return 0;
    for (uint64_t g = from + 1; g <= to; g++) { uint64_t s = atomic_load_explicit(&g_ser_chg[g % SER_CHG_N], memory_order_relaxed); if (!s || s == serial) return 0; }
    return atomic_load_explicit(&g_ser_gen, memory_order_acquire) - from < SER_CHG_N;   // no step read here was overwritten meanwhile
}
// ---------- GPU textures sharing one storage (MacNativeGames' stor_*, MIT; written anew here) ----------
// Several live texture objects can describe the same GPU-only memory with the same layout and format: on the console what is rendered through one is
// what the others sample. Every serial had its own MTLTexture, so the others read their own, stale contents (Tears of the Kingdom, 2026-10-19: a
// 564x81 R8 texture and 25x25 pairs in a CPU_NO_ACCESS pool, sampled 139 times after the other object was rendered into). Textures that are not
// CPU-filled are now keyed by (storage, layout, format): one MTLTexture for all of them. The table holds its own reference, so a serial leaving lets go
// of its reference only (defer_obj_unref, the texture stays resident for the others); an entry nobody holds is kept STOR_IDLE submissions (objects
// re-initialized at the same storage every frame take it back) and then retired. DM_NVN_TEX_SHARE=0: one texture per serial
typedef struct { uint64_t mem, size; uint32_t w, h, d, levels, target, samples, fmt, pad; } stor_key;   // 48 bytes, no padding left uninitialized
typedef struct { stor_key k; void *tex; uint64_t *ser; int nser, cap; uint64_t idle_fence, gone_fence; } stor_ent;   // tex: CFBridgingRetain'd; gone_fence: its pool was finalized then (0: alive)
static nvn_hmap g_stor = {.klen = sizeof(stor_key)}, g_stor_obj = {.klen = 8};   // key → entry; texture → entry (under g_smu)
static stor_ent **g_stor_list; static int g_nstor, g_stor_cap;
static uint64_t g_stor_shared, g_stor_entries, g_stor_swept, g_stor_usage_miss;
// An entry whose pool was finalized (stor_drop_range, from mtl_pool_del) is kept only STOR_GONE_IDLE submissions once nobody holds it (plan item 280:
// manosaba gives each render target of a trial's start a pool of its own and finalizes both when it lets the target go; the entries kept for
// STOR_IDLE held about 650 MB more at the trial's peak on the Mac, 3.6 GB of footprint against 3.0 with sharing off). A texture ID used just
// after its release, or a new object at the same storage, still finds it within that window; a new holder makes it live again
enum { STOR_IDLE = 600, STOR_GONE_IDLE = 16 };
static int stor_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_NVN_TEX_SHARE"); v = !(e && !strcmp(e, "0")); if (!v) MLOG("DM_NVN_TEX_SHARE=0: one texture per serial"); } return v; }
static int stor_drop_holder_locked(id o, uint64_t ser) {   // 1: o belongs to the table (the caller only unrefs it)
    uint64_t ok = (uint64_t)(uintptr_t)(__bridge void *)o; stor_ent *e = nvn_hm_get(&g_stor_obj, &ok); if (!e) return 0;
    for (int i = 0; i < e->nser; i++) if (e->ser[i] == ser) { e->ser[i] = e->ser[--e->nser]; break; }
    if (!e->nser) e->idle_fence = g_fence;
    return 1;
}
static void stor_add_holder_locked(id o, uint64_t ser) {
    uint64_t ok = (uint64_t)(uintptr_t)(__bridge void *)o; stor_ent *e = nvn_hm_get(&g_stor_obj, &ok); if (!e) return;
    e->gone_fence = 0;   // held again: its storage is in use
    for (int i = 0; i < e->nser; i++) if (e->ser[i] == ser) return;
    if (e->nser == e->cap) { e->cap = e->cap ? e->cap * 2 : 4; e->ser = realloc(e->ser, (size_t)e->cap * sizeof *e->ser); if (!e->ser) MDIE("storage holder list"); }
    e->ser[e->nser++] = ser;
}
static __unsafe_unretained id serial_get(uint64_t k) { pthread_mutex_lock(&g_smu); __unsafe_unretained id o = hm_get_id(&g_by_serial, &k); pthread_mutex_unlock(&g_smu); return o; }
static void serial_put(uint64_t k, id o) { pthread_mutex_lock(&g_smu); id old = hm_get_id(&g_by_serial, &k); if (old) { if (stor_drop_holder_locked(old, k)) defer_obj_unref(old); else defer_obj_release(old); }
    hm_put_id(&g_by_serial, &k, o); stor_add_holder_locked(o, k); ser_bump_locked(k); pthread_mutex_unlock(&g_smu); }
static void serial_del(uint64_t k) { pthread_mutex_lock(&g_smu); id old = hm_get_id(&g_by_serial, &k); if (old) { if (stor_drop_holder_locked(old, k)) defer_obj_unref(old); else defer_obj_release(old); }
    hm_del_id(&g_by_serial, &k); ser_bump_locked(k); pthread_mutex_unlock(&g_smu); }
static void stor_sweep(void) {   // fin_drain: entries nobody has held for STOR_IDLE submissions are retired
    pthread_mutex_lock(&g_smu);
    for (int i = 0; i < g_nstor;) { stor_ent *e = g_stor_list[i];
        uint64_t since = e->gone_fence > e->idle_fence ? e->gone_fence : e->idle_fence;
#ifdef DM_TEST_STOR_KEEP_GONE   // negative control of t_stor only: an entry of a finalized pool kept STOR_IDLE submissions, as before
        since = e->idle_fence; e->gone_fence = 0;
#endif
        if (e->nser || (e->gone_fence ? since + STOR_GONE_IDLE : e->idle_fence + STOR_IDLE) > g_fence) { i++; continue; }
        uint64_t ok = (uint64_t)(uintptr_t)e->tex; nvn_hm_del(&g_stor_obj, &ok); nvn_hm_del(&g_stor, &e->k);
        defer_obj_release(CFBridgingRelease(e->tex)); free(e->ser); free(e); g_stor_list[i] = g_stor_list[--g_nstor]; g_stor_swept++; }
    pthread_mutex_unlock(&g_smu);
}
static void stor_drop_range(uintptr_t lo, uintptr_t hi) {   // mtl_pool_del: the entries on a finalized pool's memory
    pthread_mutex_lock(&g_smu);
    for (int i = 0; i < g_nstor; i++) { stor_ent *e = g_stor_list[i]; if (e->k.mem >= lo && e->k.mem < hi && !e->gone_fence) e->gone_fence = g_fence ? g_fence : 1; }
    pthread_mutex_unlock(&g_smu);
}
// Host record of a packaged texture: the current MTLTexture, the upload stamp, and the global write generation at the last check
@interface NvPkg : NSObject { @public id<MTLTexture> tex, spare; uint64_t stamp, seen, hash, csub, chash, spare_fence; int tracked, hashed, dspan; } @end   // hash: full hash of the uploaded content under DM_NVN_DIRTYCHECK
// csub: the submission that last checked it (tex_mtl); tracked: its storage was tracked at that check; hashed / chash: a small texture checked by its content (pkg_hashed);
// dspan: the slot of the dirty-tracking range holding its storage (nvn_dirty_state_hint, -1 until found); spare: the texture before the last re-upload,
// written by the next one once spare_fence (the last submission that could read it) has completed
@implementation NvPkg
- (void)dealloc {
#ifndef DM_TEST_PKG_DEALLOC_KEEPS_RESIDENT   // negative control of t_upsoak only: released textures left in the residency set
    res_remove(tex); res_remove(spare);
#endif
}
@end
static uint64_t g_pkg_uploads, g_pkg_reuploads, g_pkg_miss, g_pkg_spare_reuse, g_pkg_spare_drop;   // re-uploads written into the spare; spares dropped unused
// DM_NVN_COUNTS (plan item 56, measuring what dropping cold CPU-filled textures could free): every 300 submissions, on the submit thread, the bytes of
// the uploaded textures by how many submissions ago they were last used (csub): [0] 0-30, [1] 31-300, [2] 301-1800, [3] more; [4] how many
static NSHashTable *g_pkgs; static uint64_t g_pkg_age[5];
static void pkg_census(void) {
    uint64_t a[5] = {0};
    for (NvPkg *pk in g_pkgs) { if (!pk->tex) continue; uint64_t age = g_sub_serial - pk->csub, b = (uint64_t)pk->tex.allocatedSize + (pk->spare ? (uint64_t)pk->spare.allocatedSize : 0);
        a[age <= 30 ? 0 : age <= 300 ? 1 : age <= 1800 ? 2 : 3] += b; a[4]++; }
    memcpy(g_pkg_age, a, sizeof a);
}
void mtl_pkg_age(uint64_t out[5]) { memcpy(out, g_pkg_age, sizeof g_pkg_age); }
enum { PKG_SPARE_KEEP = 120 };   // submissions a spare is kept without a re-upload (a texture re-uploaded once would otherwise hold two)
static uint64_t g_pfv_upgrades;
// DM_NVN_DIRTYCHECK=1 (diagnostic, compared against ground truth): fully hash the pool content on every use; changed but not detected by dirty-page tracking = a missed detection
static int dirtycheck(void) { static int v = -1; if (v < 0) v = getenv("DM_NVN_DIRTYCHECK") != NULL; return v; }
static uint64_t full_hash(const uint8_t *m, uint64_t n) { uint64_t h = 1469598103934665603ull; for (uint64_t i = 0; i < n; i++) h = (h ^ m[i]) * 1099511628211ull; return h; }
// The GPU does not support BC compressed formats (A15 / A16 etc.): BC textures are created in the decompressed format and decompressed on the CPU at upload / buffer→texture copy time (nvn_bcdec.c).
// BC3 among the game textures was converted to ASTC offline by the Legacy Device Pack (tools/astc_pack.py); what reaches here is the small remainder of BC. The test switch DM_TEST_NO_BC=1 forces this path on any machine
static int no_bc(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("DM_TEST_NO_BC"); v = (e && *e && strcmp(e, "0")) || ![g_dev supportsBCTextureCompression];
        if (v) MLOG("GPU does not support BC compressed formats%s: BC textures are created in decompressed formats (BC1–5 decompressed on the GPU, BC6H / BC7 / 3D on the CPU)", e && *e && strcmp(e, "0") ? " (test switch DM_TEST_NO_BC)" : ""); }
    return v;
}
// No BC, BC1 (plan item 62c): kept compressed as ASTC 4x4, twice BC1's size rather than RGBA8's eight times (gfx/tex/nvn_bc1astc.h). The GPU
// deswizzle writes the BC1 blocks into the staging area, the kernel b1a writes an ASTC block for each behind them, and after a barrier a copy takes
// those into the ASTC texture (up_post); the CPU paths use nvn_bc1astc.c. DM_NVN_BC1_ASTC=0: decompressed as the other BC formats
static int bc1_astc(uint32_t f, int *rgba, int *srgb) {
    static int on = -1; if (on < 0) { const char *e = getenv("DM_NVN_BC1_ASTC"); on = !(e && !strcmp(e, "0")); }
    return no_bc() && on && nvn_bc1_astc_fmt(f, rgba, srgb);
}
static int bc_unpack(uint32_t nvn_fmt, uint32_t *pf, int *bpp) { int r, sr; return no_bc() && !bc1_astc(nvn_fmt, &r, &sr) && nvn_bc_unpacked(nvn_fmt, pf, bpp); }
static int pf_is_depth_stencil(MTLPixelFormat f);
static MTLTextureDescriptor *tex_desc(const tex_t *t, int packaged) {
    uint32_t pf = 0; uint8_t swz[4];
    if (!nvn_fmt_to_mtl(t->d.format, &pf, swz) || !pf) { RED_ONCE(0, "texture format %#x has no Metal equivalent (first case)", t->d.format); return nil; }
    // Over Metal's size limits: newTextureWithDescriptor would abort the process (failed assertion in MTLTextureDescriptor validation),
    // so the texture is not created and reads as unbound. Logged with every field (first 16) to find where such a description comes from.
    const char *bad = nvn_texdesc_metal_check(&t->d);
    if (bad) {
        static _Atomic int n;
        if (atomic_fetch_add(&n, 1) < 16)
            MLOG("[red] texture not created (%s): ser %llu magic %#x target %d format %#x flags %#x samples %d size %ux%ux%u levels %u layout %d mem %p",
                 bad, t->serial, t->magic, t->d.target, t->d.format, t->d.flags, t->d.samples, t->d.width, t->d.height, t->d.depth, t->d.levels,
                 t->has_layout, (void *)t->mem);
        return nil;
    }
    int unp = 0; { uint32_t upf; int ubpp; if (bc_unpack(t->d.format, &upf, &ubpp)) { pf = upf; unp = 1; } }
    { int r, sr; if (bc1_astc(t->d.format, &r, &sr)) pf = sr ? MTLPixelFormatASTC_4x4_sRGB : MTLPixelFormatASTC_4x4_LDR; }
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.pixelFormat = (MTLPixelFormat)pf;
    d.width = t->d.width ? t->d.width : 1; d.height = t->d.height ? t->d.height : 1; d.depth = 1; d.arrayLength = 1;
    d.mipmapLevelCount = t->d.levels ? t->d.levels : 1;
    switch (t->d.target) {
        // 1D / 1D array are created as one-row 2D / 2D array textures (nxnative 42a50ef): translated shaders never declare texture1d (none in manosaba's
        // library) and sample them as 2D, and a Metal 1D texture bound to a texture2d argument is a type mismatch with undefined reads. On Maxwell a 2D
        // sample of a 1D texture reads its one row
        case 0: d.textureType = MTLTextureType2D; d.height = 1; break;
        case 2: d.textureType = MTLTextureType3D; d.depth = t->d.depth; break;
        case 3: d.textureType = MTLTextureType2DArray; d.arrayLength = t->d.height; d.height = 1; break;
        case 4: d.textureType = MTLTextureType2DArray; d.arrayLength = t->d.depth; break;
        case 5: d.textureType = MTLTextureType2DMultisample; d.sampleCount = t->d.samples; d.mipmapLevelCount = 1; break;
        case 6: d.textureType = MTLTextureType2DMultisampleArray; d.sampleCount = t->d.samples; d.arrayLength = t->d.depth; d.mipmapLevelCount = 1; break;
        case 8: d.textureType = MTLTextureTypeCube; break;
        case 9: d.textureType = MTLTextureTypeCubeArray; d.arrayLength = MAX(t->d.depth / 6, 1u);   // NVN's depth counts faces, Metal's array length cubes
#ifndef DM_TEST_CUBE_DEPTH_SILENT   // negative control of t_cubearr only: nothing said, as before
            // faces past the last whole cube have no slice (blade2-mobile 4ebabb2 names them, MIT; plan item 198)
            if (t->d.depth % 6) RED_ONCE(115, "cube array texture %llu: a depth of %u faces is not whole cubes, made with %lu (the last %u faces have no slice)",
                                         (unsigned long long)t->serial, t->d.depth, (unsigned long)d.arrayLength, t->d.depth < 6 ? 0 : t->d.depth % 6);
#endif
            break;
        case 10: RED_ONCE(1, "buffer texture (TARGET_BUFFER) not implemented"); return nil;
        default: d.textureType = MTLTextureType2D; break;
    }
    d.storageMode = MTLStorageModePrivate;   // Render targets and assets both go in GPU private storage (lossless compression available)
    d.usage = MTLTextureUsageShaderRead | (fmt_compressed(t->d.format) || packaged ? 0 : MTLTextureUsageRenderTarget)
            | (t->d.flags & NVN_TEXTURE_FLAGS_IMAGE ? MTLTextureUsageShaderWrite : 0)   // Image usage: shader-writable (compute kernels generating the 3D color lookup table, etc.)
            | (unp ? MTLTextureUsageShaderWrite | MTLTextureUsagePixelFormatView : 0);   // No BC: textures in the decompressed format are written by the GPU decompression kernel (up_dec) / receive whole-block copies from ASTC sources (copy_t2t_astc)
    return d;
}
// In-place upgrade: when the original texture was not created with PixelFormatView (off by default, to keep Apple GPU lossless compression) but a different-format view is needed, create a new one with the original spec + PixelFormatView, copy over every slice × level, and replace the registration.
// Count: across the 4 screens character select / camp / wilderness / combat, 17473 textures needed a different-format view 0 times, so this is a fallback and does not rely on predicting which textures need it
static id<MTLTexture> tex_upgrade_pfv(const tex_t *t, id<MTLTexture> base, MTLPixelFormat view_pf) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.textureType = base.textureType; d.pixelFormat = base.pixelFormat; d.width = base.width; d.height = base.height; d.depth = base.depth;   // Keep the original format, only add the format-view usage
    d.arrayLength = base.arrayLength; d.mipmapLevelCount = base.mipmapLevelCount; d.sampleCount = base.sampleCount;
    d.storageMode = base.storageMode; d.cpuCacheMode = base.cpuCacheMode; d.hazardTrackingMode = base.hazardTrackingMode;
    d.resourceOptions = base.resourceOptions; d.usage = base.usage | MTLTextureUsagePixelFormatView;
    id<MTLTexture> upgraded = [g_dev newTextureWithDescriptor:d];
    if (!upgraded) { RED_ONCE(45, "texture upgrade failed ser=%llu format %lu → %lu", t->serial, (unsigned long)base.pixelFormat, (unsigned long)view_pf); return base; }
    res_add(upgraded);
    BENC b = blit();
    NSUInteger slices = base.textureType == MTLTextureType3D ? 1 : base.arrayLength * (base.textureType == MTLTextureTypeCube || base.textureType == MTLTextureTypeCubeArray ? 6 : 1);
    for (NSUInteger l = 0; l < base.mipmapLevelCount; l++) {
        NSUInteger w = MAX(base.width >> l, 1u), h = MAX(base.height >> l, 1u), z = base.textureType == MTLTextureType3D ? MAX(base.depth >> l, 1u) : 1;
        if (base.textureType == MTLTextureType3D)
            enc_b_copy_texture(b, base, 0, l, MTLOriginMake(0, 0, 0), MTLSizeMake(w, h, z), upgraded, 0, l, MTLOriginMake(0, 0, 0));
        else for (NSUInteger s = 0; s < slices; s++)
            enc_b_copy_texture(b, base, s, l, MTLOriginMake(0, 0, 0), MTLSizeMake(w, h, 1), upgraded, s, l, MTLOriginMake(0, 0, 0));
    }
    id holder = serial_get(t->serial);
    if ([holder isKindOfClass:[NvPkg class]]) { id<MTLTexture> old = ((NvPkg *)holder)->tex; ((NvPkg *)holder)->tex = upgraded; defer_obj_release(old); ser_bump(t->serial); }
    else serial_put(t->serial, upgraded);
    MLOG("texture upgraded for format-changing views ser=%llu base format %lu → view format %lu (upgrades=%llu)", t->serial, (unsigned long)base.pixelFormat, (unsigned long)view_pf, ++g_pfv_upgrades);
    return upgraded;
}
// Packaged texture upload: first set read-only, then take the stamp, then read the pool memory and deswizzle into a staging buffer, and blit into a new private texture.
