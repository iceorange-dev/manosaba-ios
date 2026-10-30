// gfx/nvn/mtl/writeback.inc, part of nvn_mtl.m (included there in order, one translation unit): CPU-filled textures, and GPU results written back when the CPU invalidates a pool
// ---------- CPU-filled textures without a packaged layout ----------
// A texture's content is its storage: on the Switch the CPU and the GPU both write that memory. The host keeps a Metal copy, so it has to know who wrote
// last. Textures with a packaged layout were always uploaded from the pool (and re-uploaded when its pages change); every other texture was GPU-only and
// started as zeros. Tears of the Kingdom gives no packaged layout to its assets (26 SetPackagedTextureLayout calls against 757 registered textures): it
// writes the block-linear data into CPU-cached pools and flushes the range (8536 FlushMappedRange calls at startup), so the title mural's albedo and
// every other asset sampled as black. Now, as in LASRecomp (Peiikai, MIT: a texture is uploaded from guest memory when sampled until it is rendered
// into), a texture in a CPU-visible pool, not compressible, and not yet written by the GPU is uploaded from its storage once the CPU has filled it:
// non-zero storage at first use (the host never writes texture storage and a fresh pool reads as zero), or written pages later (it is watched). Such
// an upload is created render-target capable; when the GPU first writes the texture (render target, copy or clear destination, image store) that
// MTLTexture becomes the GPU's own and is no longer re-uploaded.
static pthread_mutex_t g_cpuf_mu = PTHREAD_MUTEX_INITIALIZER;
static CFMutableSetRef g_gpu_owned;          // serials the GPU has written
static CFMutableDictionaryRef g_cpuf_watch;  // candidates still GPU-only (storage empty at first use) → dirty stamp + 1
static CFMutableSetRef g_wt_since;           // serials WriteTexels wrote into their MTLTexture since their last FlushTexels (mtl_flush_texels)
static int tex_cpu_candidate(const tex_t *t) {   // linear textures are made on the pool buffer instead (tex_linear)
    return t->mem && t->pool && !(t->pool->flags & NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS) && !(t->d.flags & (NVN_TEXTURE_FLAGS_COMPRESSIBLE | NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET))
        && t->d.target != 10 && !t->has_layout;
}
// A linear texture in a CPU-visible pool is made on the pool's MTLBuffer, so what the CPU writes is what the GPU reads, with no upload: games rasterize
// glyphs on the CPU into one (nxnative found it in Mario's 2048x8192 R8 glyph cache, flags IMAGE | LINEAR: menus without text; tex_linear in its
// nvn_mtl.m, seedds, MIT). Tears of the Kingdom's is IMAGE | LINEAR in a CPU_UNCACHED pool; as a private texture its menu text never showed.
static id<MTLTexture> tex_linear(const tex_t *t) {
    if (!(t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)) || !t->mem || !t->pool || (t->pool->flags & NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS)) return nil;
    if (t->d.target != NVN_TEXTURE_TARGET_2D || t->d.levels > 1 || t->d.samples > 1) return nil;
    uint32_t pf = 0; uint8_t swz[4]; int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    if (!nvn_fmt_to_mtl(t->d.format, &pf, swz) || !pf || !bpb || bw != 1 || bh != 1) return nil;
    size_t row = t->d.stride ? t->d.stride : (size_t)t->d.width * bpb;
    void *mb; uint64_t mo;
    if (!mtl_addr((uint64_t)t->mem, (uint64_t)row * (t->d.height ? t->d.height : 1), &mb, &mo)) { RED_ONCE(81, "linear texture storage is not in a memory pool (%ux%u format %#x)", t->d.width, t->d.height, t->d.format); return nil; }
    id<MTLBuffer> b = (__bridge id<MTLBuffer>)mb;
    NSUInteger al = [g_dev minimumLinearTextureAlignmentForPixelFormat:(MTLPixelFormat)pf];
    if (mo % al || row % al) { RED_ONCE(82, "linear texture offset %#llx / row pitch %zu not %lu-byte aligned: made as a private texture (CPU writes not seen)", (unsigned long long)mo, row, (unsigned long)al); return nil; }
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(MTLPixelFormat)pf width:t->d.width ? t->d.width : 1 height:t->d.height ? t->d.height : 1 mipmapped:NO];
    d.resourceOptions = b.resourceOptions;
    d.usage = MTLTextureUsageShaderRead | (t->d.flags & NVN_TEXTURE_FLAGS_IMAGE ? MTLTextureUsageShaderWrite : 0) | (t->d.flags & NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET ? MTLTextureUsageRenderTarget : 0);
    return [b newTextureWithDescriptor:d offset:mo bytesPerRow:row];
}
static int gpu_owned(uint64_t ser) {
    pthread_mutex_lock(&g_cpuf_mu); int r = g_gpu_owned && CFSetContainsValue(g_gpu_owned, (const void *)(uintptr_t)ser); pthread_mutex_unlock(&g_cpuf_mu);
    return r;
}
typedef struct { const tex_t *t; int r; } filled_t;
static void filled_run(void *c) {
    filled_t *x = c; const uint64_t *w = (const uint64_t *)(const void *)x->t->mem; size_t nw = (size_t)x->t->size / 8;
    for (size_t i = 0; i < nw; i++) if (w[i]) { x->r = 1; return; }
}
static int storage_filled(const tex_t *t) {   // its storage read under the guard (core/guard.h, plan item 290): taken back, it counts as not filled
    filled_t x = {t, 0};
    if (!guard_run(t->mem, t->size, filled_run, &x)) { RED_ONCE(117, "texture storage %p + %#x unreadable after its submission (the game took the memory back): the texture kept as it was", (void *)t->mem, t->size); return 0; }
    return x.r;
}
static void cpuf_watch(const tex_t *t) {   // submit thread: protect the storage, so a later CPU write shows as dirty pages
    uint64_t st = nvn_dirty_protect_stamp(t->mem, t->size);
    pthread_mutex_lock(&g_cpuf_mu);
    if (!g_cpuf_watch) g_cpuf_watch = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    CFDictionarySetValue(g_cpuf_watch, (const void *)(uintptr_t)t->serial, (const void *)(uintptr_t)(st + 1));
    pthread_mutex_unlock(&g_cpuf_mu);
}
static int cpuf_written(const tex_t *t) {   // a watched candidate whose storage the CPU has written since: leaves the watch list
    pthread_mutex_lock(&g_cpuf_mu);
    uintptr_t v = g_cpuf_watch ? (uintptr_t)CFDictionaryGetValue(g_cpuf_watch, (const void *)(uintptr_t)t->serial) : 0;
    int w = v && nvn_dirty_since(t->mem, t->size, (uint64_t)v - 1);
    if (w) CFDictionaryRemoveValue(g_cpuf_watch, (const void *)(uintptr_t)t->serial);
    pthread_mutex_unlock(&g_cpuf_mu);
    return w;
}
// The pool memory is this texture's content (ReadTexels / WriteTexels go through it): packaged, or a candidate the GPU has not written that is not GPU-only yet
static int tex_membacked(const tex_t *t) {
    if (!t->mem) return 0;
    if (t->has_layout) return 1;
    if (!tex_cpu_candidate(t) || gpu_owned(t->serial)) return 0;
    __unsafe_unretained id have = serial_get(t->serial);
    return !have || [have isKindOfClass:[NvPkg class]];
}
// ---------- GPU results written back when the CPU invalidates a pool (MacNativeGames' wb_note / mtl_pool_invalidate, MIT; written anew here) ----------
// On the console the GPU writes a texture's storage, and the game reads it with the CPU after nvnMemoryPoolInvalidateMappedRange (CPU-cached pools).
// Here a CPU-filled texture the GPU then writes keeps the result only in its MTLTexture, and the CPU read what it had uploaded before: Tears of the
// Kingdom bakes terrain tile normals on the GPU and copies them with the CPU into new tiles (MacNativeGames: after travelling, the ground turned black
// block by block). Each GPU write of such a texture is noted (storage protected, a reference kept until its serial is released); an invalidate over
// its storage waits for the GPU and writes every level and layer back in the NVN layout, unless the CPU wrote that memory since (its bytes are newer)
// or a later rendering of the same memory supersedes it (t_wb)
typedef struct { tex_t t; uint64_t stamp; void *obj; uint64_t sub, idle_stamp, fence, stage_fence; uint8_t *snap; uint32_t snap_n; void *stage; } wb_ent;   // sub: submission of the last GPU write; idle_stamp: written back after it (DM_NVN_WB_IDLE), 0 = not yet; fence: the one its submission completes with; snap: its own bytes then; stage / stage_fence: its levels copied at the end of the submission of that fence (286, CFBridgingRetain'd)
enum { WB_FENCE_MAX = 64 * 1024 };   // 278's targets: up to 64 KiB
static nvn_hmap g_wb = {.klen = 8}; static pthread_mutex_t g_wb_mu = PTHREAD_MUTEX_INITIALIZER; static uint64_t g_wb_done, g_wb_skip_cpu, g_wb_bytes, g_wb_exact, g_wb_snap_total;
// The texture's own bytes when the GPU write is noted (plan item 203; MacNativeGames 8a29e0a and PR #188, MIT, written anew): the dirty tracking is
// per 16 KiB page, and a page the CPU wrote often holds other data next to the texture (Tears of the Kingdom's 260x260 terrain normal maps: the
// iPhone's log of 2026-10-22 skipped 1 of the first 4 write-backs, MacNativeGames saw the tiles turn dark), so a write-back whose pages were written
// but whose own bytes are as they were still goes ahead. Textures up to 256 KiB, 64 MiB of copies in all; DM_NVN_WB_EXACT=0 decides by pages only
enum { WB_SNAP_MAX = 256 * 1024 }; static const uint64_t WB_SNAP_TOTAL = 64ull << 20;
static int wb_exact_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_NVN_WB_EXACT"); v = !(e && !strcmp(e, "0")); if (!v) MLOG("DM_NVN_WB_EXACT=0: GPU write-backs decided by pages only"); } return v; }
static void wb_snap(wb_ent *e) {   // g_wb_mu held
    uint32_t n = e->t.size;
#ifndef DM_TEST_WB_PAGE_ONLY   // negative control of t_wb only: no copy, the pages decide as before
    if (wb_exact_on() && e->t.mem && n && n <= WB_SNAP_MAX && (e->snap_n == n || g_wb_snap_total + n <= WB_SNAP_TOTAL)) {
        if (e->snap_n != n) { free(e->snap); g_wb_snap_total -= e->snap_n; e->snap = malloc(n); e->snap_n = e->snap ? n : 0; g_wb_snap_total += e->snap_n; }
        if (e->snap) { memcpy(e->snap, e->t.mem, n); return; }
    }
#endif
    free(e->snap); g_wb_snap_total -= e->snap_n; e->snap = NULL; e->snap_n = 0;
}
static void wb_free(wb_ent *e) { if (!e) return; if (e->obj) { defer_obj_unref((__bridge id)e->obj); CFRelease(e->obj); } if (e->stage) defer_obj_release(CFBridgingRelease(e->stage));
    if (e->snap) { pthread_mutex_lock(&g_wb_mu); g_wb_snap_total -= e->snap_n; pthread_mutex_unlock(&g_wb_mu); free(e->snap); } free(e); }
static id<MTLTexture> tex_mtl(const tex_t *ct);
// Plan item 286: 278's write-back after a fence wait waited for a copy on the GPU on the game's thread (0.6-1 ms each). The submission that renders such
// a target now copies it to a buffer of its own at its end (wb_stage_encode, from mtl_sub_commit), so the write-back after the wait only puts those
// bytes into the storage, when the copy is of the target's newest rendering; otherwise it copies on the GPU as before
static int wb_fence_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("DM_NVN_WB_FENCE"); on = !(e && !strcmp(e, "0")); if (!on) MLOG("DM_NVN_WB_FENCE=0: GPU results written back on an invalidate only"); } return on; }
static int wb_fence_kind(const tex_t *t) { uint32_t upf; int ubpp, r_, s_; return t->size <= WB_FENCE_MAX && t->d.target != 2 && t->d.samples <= 1 && !bc_unpack(t->d.format, &upf, &ubpp) && !bc1_astc(t->d.format, &r_, &s_); }
static uint64_t g_wb_stage_ser[64]; static int g_wb_stage_n; static uint64_t g_wb_staged, g_wb_stage_used;
static unsigned wb_layers(const tex_t *t); static void wb_level_size(const tex_t *t, unsigned level, uint32_t *lw, uint32_t *lh, size_t *lp, size_t *lr);
static void wb_note(const tex_t *t) {   // submit thread: the GPU writes t in this submission (every caller makes its MTLTexture right after)
    if (!t->mem || !t->size) return;
    id o = serial_get(t->serial); if (!o) { tex_mtl(t); o = serial_get(t->serial); } if (!o) return;
    uint64_t stamp = nvn_dirty_protect_stamp(t->mem, t->size), k = t->serial;   // CPU writes after this rendering show in nvn_dirty_since
    pthread_mutex_lock(&g_wb_mu);
    wb_ent *e = nvn_hm_get(&g_wb, &k);
    if (e) { e->stamp = stamp; e->t = *t; e->sub = g_sub_serial; e->idle_stamp = 0; e->fence = g_fence + 1; wb_snap(e); }
    else if (g_wb.n < 8192) { e = calloc(1, sizeof *e); if (!e) MDIE("write-back record"); e->t = *t; e->stamp = stamp; e->sub = g_sub_serial; e->fence = g_fence + 1; e->obj = (void *)CFBridgingRetain(o); wb_snap(e); nvn_hm_put(&g_wb, &k, e); }
    else RED_ONCE(87, "GPU write-back table full (8192 textures): texture %llu is not written back on an invalidate", (unsigned long long)t->serial);
    pthread_mutex_unlock(&g_wb_mu);
#ifndef DM_TEST_WB_STAGE_OFF   // negative control of t_wb only: no copy at the submission's end, the write-back copies on the GPU as before
    if (wb_fence_on() && wb_fence_kind(t)) { int have = 0; for (int i = 0; i < g_wb_stage_n && !have; i++) have = g_wb_stage_ser[i] == k;
        if (!have && g_wb_stage_n < 64) g_wb_stage_ser[g_wb_stage_n++] = k; }
#endif
}
static void wb_stage_encode(void) {   // mtl_sub_commit, after the last pass: this submission's targets of 278 copied to staging (286)
    int n = g_wb_stage_n; g_wb_stage_n = 0;
    for (int i = 0; i < n; i++) {
        uint64_t k = g_wb_stage_ser[i];
        pthread_mutex_lock(&g_wb_mu); wb_ent *e = nvn_hm_get(&g_wb, &k); tex_t t = e ? e->t : (tex_t){0}; uint64_t f = e ? e->fence : 0; id o = e && e->obj ? (__bridge id)e->obj : nil; pthread_mutex_unlock(&g_wb_mu);
        if (!e || !o || f != g_fence + 1) continue;   // written back meanwhile, or its newest rendering is not this submission's
        id<MTLTexture> mt = [o isKindOfClass:[NvPkg class]] ? ((NvPkg *)o)->tex : o; if (!mt) continue;
        size_t len = 0; for (unsigned l = 0; l < (t.d.levels ? t.d.levels : 1); l++) for (unsigned y = 0; y < wb_layers(&t); y++) { uint32_t lw, lh; size_t lp, lr; wb_level_size(&t, l, &lw, &lh, &lp, &lr); len += lp * lr; }
        id<MTLBuffer> b = len ? [g_dev newBufferWithLength:len options:MTLResourceStorageModeShared] : nil; if (!b) continue;
        res_add(b);
        BENC bl = blit(); size_t off = 0;
        for (unsigned l = 0; l < (t.d.levels ? t.d.levels : 1); l++) for (unsigned y = 0; y < wb_layers(&t); y++) { uint32_t lw, lh; size_t lp, lr; wb_level_size(&t, l, &lw, &lh, &lp, &lr);
            [bl copyFromTexture:mt sourceSlice:y sourceLevel:l sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(lw, lh, 1) toBuffer:b destinationOffset:off destinationBytesPerRow:lp destinationBytesPerImage:lp * lr];
            off += lp * lr; }
        void *old = NULL;
        pthread_mutex_lock(&g_wb_mu); wb_ent *e2 = nvn_hm_get(&g_wb, &k);
        if (e2 == e && e->fence == f) { old = e->stage; e->stage = (void *)CFBridgingRetain(b); e->stage_fence = f; g_wb_staged++; b = nil; }
        pthread_mutex_unlock(&g_wb_mu);
        if (old) defer_obj_release(CFBridgingRelease(old));
        if (b) defer_obj_release(b);   // the entry went meanwhile
    }
}
static int wb_from_stage(wb_ent *e) {   // 286: the staged copy of the newest rendering put into the storage; 0: none, or it does not fit
    if (!e->stage || e->stage_fence != e->fence) return 0;
    id<MTLBuffer> b = (__bridge id<MTLBuffer>)e->stage; const tex_t *u = &e->t; size_t off = 0;
    dm_guest_write_prepare(u->mem, u->size);
    for (unsigned l = 0; l < (u->d.levels ? u->d.levels : 1); l++) for (unsigned y = 0; y < wb_layers(u); y++) { uint32_t lw, lh; size_t lp, lr; wb_level_size(u, l, &lw, &lh, &lp, &lr);
        if (off + lp * lr > b.length || nvn_swizzle(&u->d, l, y, (const uint8_t *)b.contents + off, lp, u->mem)) return 0;
        g_wb_bytes += lp * lr; off += lp * lr; }
    g_wb_stage_used++;
    return 1;
}
// DM_NVN_TEXCENSUS (item 94): textures the GPU writes in CPU-visible pools (exactly the write-back table's), each new shape logged; then, every 300
// submissions, those whose storage the CPU wrote after the GPU did (a tile moved into a rendered slot), and at a CPU-filled texture's first use one
// whose storage lies on a GPU-written texture's (a tile copied out of one: its pages hold what the CPU last left there, not what the GPU drew)
static int wb_census_on(void) { static int on = -1; if (on < 0) on = getenv("DM_NVN_TEXCENSUS") && strcmp(getenv("DM_NVN_TEXCENSUS"), "0"); return on; }
static void wb_census_shape(const char *what, const tex_t *t, uint64_t n) {
    static struct { uint32_t w, h, f, flags; const char *what; } seen[48]; static int ns;
    for (int i = 0; i < ns; i++) if (seen[i].what == what && seen[i].w == t->d.width && seen[i].h == t->d.height && seen[i].f == (uint32_t)t->d.format && seen[i].flags == (uint32_t)t->d.flags) return;
    if (ns < 48) seen[ns++] = (typeof(seen[0])){t->d.width, t->d.height, (uint32_t)t->d.format, (uint32_t)t->d.flags, what};
    MLOG("[texture census] %s #%llu: %ux%ux%u format %#x flags %#x levels %u, pool flags %#x, size %#x", what, (unsigned long long)n, t->d.width, t->d.height, t->d.depth,
         t->d.format, t->d.flags, t->d.levels, t->pool ? t->pool->flags : 0u, t->size);
}
static uint64_t g_wbc_gpu, g_wbc_cpu_after, g_wbc_over;
static void wb_census_tick(void) {   // submit thread, every 300 submissions
    if (!wb_census_on() || g_sub_serial % 300) return;
    static CFMutableSetRef counted; if (!counted) counted = CFSetCreateMutable(NULL, 0, NULL);   // serials already counted (the table's stamps are left alone)
    pthread_mutex_lock(&g_wb_mu); uint64_t n = g_wb.n, hit = 0;
    for (uint32_t i = 0; i < g_wb.cap; i++) { wb_ent *e = g_wb.vals ? g_wb.vals[i] : NULL;
        if (!e || CFSetContainsValue(counted, (const void *)(uintptr_t)e->t.serial) || !nvn_dirty_since(e->t.mem, e->t.size, e->stamp)) continue;
        hit++; CFSetAddValue(counted, (const void *)(uintptr_t)e->t.serial); wb_census_shape("the CPU wrote the storage of a GPU-written texture", &e->t, ++g_wbc_cpu_after);
        size_t nz = 0; for (uint32_t b = 0; b < e->t.size; b++) nz += e->t.mem[b] != 0;   // what it wrote: real texels, or the zeros of storage no GPU write ever reached
        MLOG("[texture census] ... serial %llu: %.1f %% of its storage bytes not zero now", (unsigned long long)e->t.serial, e->t.size ? 100.0 * (double)nz / e->t.size : 0); }
    pthread_mutex_unlock(&g_wb_mu);
    MLOG("[texture census] at submission %llu: %llu GPU-written textures in CPU-visible pools now (%llu seen), %llu more written by the CPU since, %llu CPU-filled ones on GPU-written storage",
         (unsigned long long)g_sub_serial, (unsigned long long)n, (unsigned long long)g_wbc_gpu, (unsigned long long)hit, (unsigned long long)g_wbc_over);
}
static void wb_census_first(const tex_t *t) {   // a CPU-filled texture's first use: on a GPU-written texture's storage?
    if (!wb_census_on()) return;
    pthread_mutex_lock(&g_wb_mu); int over = 0;
    for (uint32_t i = 0; i < g_wb.cap && !over; i++) { wb_ent *e = g_wb.vals ? g_wb.vals[i] : NULL;
        over = e && e->t.serial != t->serial && e->t.mem < t->mem + t->size && t->mem < e->t.mem + e->t.size; }
    pthread_mutex_unlock(&g_wb_mu);
    if (over) wb_census_shape("a CPU-filled texture on a GPU-written texture's storage", t, ++g_wbc_over);
}
// DM_NVN_WB_IDLE=1 (item 94, an experiment): a texture the GPU wrote in a CPU-visible pool and then left alone for 30 submissions is written back to
// its storage once (totk-mobile's nvn_texture_writeback.inc writes back the first GPU write after 30 frames without one), so a CPU copy out of it
// carries the texels the GPU drew
static int wb_level(const tex_t *t, id<MTLTexture> mt, unsigned level, unsigned layer);
static void wb_textures(const tex_t *const *t, __unsafe_unretained id<MTLTexture> const *mt, int n, int *ok);
static uint64_t g_wb_idle_n;
static void wb_idle_pass(void) {   // submit thread, before the submission's command buffer exists
    static int on = -1, subs = 30; if (on < 0) { on = getenv("DM_NVN_WB_IDLE") && atoi(getenv("DM_NVN_WB_IDLE"));
        const char *s = getenv("DM_NVN_WB_IDLE_SUBS"); if (s && atoi(s) > 0) subs = atoi(s); }   // diagnostic: the idle submissions before a write-back
    if (!on || (subs >= 8 && g_sub_serial % 8)) return;
    wb_ent *list[16]; int nl = 0;
    pthread_mutex_lock(&g_wb_mu);
    for (uint32_t i = 0; i < g_wb.cap && nl < 16; i++) { wb_ent *e = g_wb.vals ? g_wb.vals[i] : NULL;
        if (e && !e->idle_stamp && g_sub_serial - e->sub >= (uint64_t)subs && !nvn_dirty_since(e->t.mem, e->t.size, e->stamp)) { e->idle_stamp = 1; list[nl++] = e; } }
    pthread_mutex_unlock(&g_wb_mu);
    const tex_t *wt[16]; __unsafe_unretained id<MTLTexture> wm[16]; wb_ent *we[16]; int nw = 0, wok[16];   // the entries hold the textures
    for (int i = 0; i < nl; i++) {
        wb_ent *e = list[i]; const tex_t *t = &e->t; id o = (__bridge id)e->obj; id<MTLTexture> mt = [o isKindOfClass:[NvPkg class]] ? ((NvPkg *)o)->tex : o; uint32_t upf; int ubpp;
        { int r_, s_; if (!mt || bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &r_, &s_) || t->d.target == 2 || t->d.samples > 1) continue; }
        wt[nw] = t; wm[nw] = mt; we[nw++] = e;
    }
    wb_textures(wt, wm, nw, wok);
    for (int i = 0; i < nw; i++) {
        wb_ent *e = we[i]; const tex_t *t = wt[i]; int ok = wok[i];
        nvn_dirty_protect(t->mem, t->size);
        pthread_mutex_lock(&g_wb_mu); e->idle_stamp = nvn_dirty_now(); e->stamp = e->idle_stamp; pthread_mutex_unlock(&g_wb_mu);   // a later CPU write shows against this
        uint64_t n = ++g_wb_idle_n; if (n <= 4 || !(n % 100)) MLOG("idle write-back: texture %llu %ux%u format %#x (%llu so far, %s)", (unsigned long long)t->serial, t->d.width, t->d.height, t->d.format, (unsigned long long)n, ok ? "ok" : "failed");
    }
}
// With it, the other half: a texture the GPU wrote whose storage the CPU wrote since (a tile copied in) takes the CPU's bytes, as on the console where
// both write the same memory: it leaves the GPU-owned set and is watched from its last GPU write, so its next use uploads it from storage
// (tex_mtl: cpuf_written); the GPU writing it again makes it the GPU's once more
static uint64_t g_wb_cpu_n;
static void wb_cpu_pass(void) {   // submit thread, before the submission
    static int on = -1; if (on < 0) on = getenv("DM_NVN_WB_IDLE") && atoi(getenv("DM_NVN_WB_IDLE"));
    if (!on) return;
    wb_ent *list[64]; int nl = 0;
    pthread_mutex_lock(&g_wb_mu);
    for (int again = 1; again && nl < 64;) { again = 0;   // a deletion shifts entries: scan again
        for (uint32_t i = 0; i < g_wb.cap && nl < 64; i++) { wb_ent *e = g_wb.vals ? g_wb.vals[i] : NULL;
            if (!e || !nvn_dirty_since(e->t.mem, e->t.size, e->stamp)) continue;
            uint64_t k = e->t.serial; nvn_hm_del(&g_wb, &k); list[nl++] = e; again = 1; break; } }
    pthread_mutex_unlock(&g_wb_mu);
    for (int i = 0; i < nl; i++) {
        wb_ent *e = list[i]; uint64_t ser = e->t.serial;
        pthread_mutex_lock(&g_cpuf_mu);
        if (g_gpu_owned) CFSetRemoveValue(g_gpu_owned, (const void *)(uintptr_t)ser);
        if (!g_cpuf_watch) g_cpuf_watch = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
        CFDictionarySetValue(g_cpuf_watch, (const void *)(uintptr_t)ser, (const void *)(uintptr_t)(e->stamp + 1));   // written since its last GPU write: cpuf_written
        pthread_mutex_unlock(&g_cpuf_mu);
        ser_bump(ser);   // its texture IDs fetch it again
        uint64_t n = ++g_wb_cpu_n; if (n <= 4 || !(n % 100)) MLOG("CPU-written GPU texture taken from storage: %llu %ux%u format %#x (%llu so far)", (unsigned long long)ser, e->t.d.width, e->t.d.height, e->t.d.format, (unsigned long long)n);
        wb_free(e);
    }
}
static void wb_released(uint64_t serial) { pthread_mutex_lock(&g_wb_mu); wb_ent *e = nvn_hm_del(&g_wb, &serial); pthread_mutex_unlock(&g_wb_mu); wb_free(e); }   // fin_drain
// A pool finalized: the records of textures in its memory go with it (MacNativeGames 0930478's wb_drop_range, MIT, written anew). A texture's
// own release can come later (fin_drain) or never, and an invalidate of a pool made later on that memory would write the old rendering over what
// the game put there next: sead heap blocks, an allocator node, index buffers (MacNativeGames #529 / #764, Tears of the Kingdom after a warp; t_wb)
static void wb_drop_range(uintptr_t lo, uintptr_t hi) {
#ifndef DM_TEST_WB_KEEP_DEAD   // negative control of t_wb only: the records stay, as before
    for (int full = 1; full;) {
        wb_ent *list[256]; int nl = 0;
        pthread_mutex_lock(&g_wb_mu);
        for (uint32_t i = 0; i < g_wb.cap && nl < 256; i++) { wb_ent *e = g_wb.vals ? g_wb.vals[i] : NULL;
            if (e && (uintptr_t)e->t.mem < hi && (uintptr_t)e->t.mem + e->t.size > lo) list[nl++] = e; }
        for (int k = 0; k < nl; k++) { uint64_t key = list[k]->t.serial; nvn_hm_del(&g_wb, &key); }
        pthread_mutex_unlock(&g_wb_mu);
        for (int k = 0; k < nl; k++) wb_free(list[k]);
        full = nl == 256;
    }
#else
    (void)lo; (void)hi;
#endif
}
static uint64_t g_wb_cbs;   // command buffers the write-backs committed (t_wb)
static void wb_level_size(const tex_t *t, unsigned level, uint32_t *lw, uint32_t *lh, size_t *lp, size_t *lr) {
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    *lw = MAX(t->d.width >> level, 1u); *lh = MAX(t->d.height >> level, 1u);
    *lp = (size_t)((*lw + bw - 1) / bw) * bpb; *lr = (*lh + bh - 1) / bh;
}
static unsigned wb_layers(const tex_t *t) { return t->d.target == NVN_TEXTURE_TARGET_2D || !t->layer_size ? 1 : t->size / t->layer_size; }
static int wb_level(const tex_t *t, id<MTLTexture> mt, unsigned level, unsigned layer) {   // one level of one layer, its own command buffer (DM_NVN_WB_BATCH=0)
    uint32_t lw, lh; size_t lp, lr; wb_level_size(t, level, &lw, &lh, &lp, &lr);
    id<MTLBuffer> buf = [g_dev newBufferWithLength:lp * lr options:MTLResourceStorageModeShared]; if (!buf) return 0;
    id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> b = enc_b_begin(cb); g_wb_cbs++;
    [b copyFromTexture:mt sourceSlice:layer sourceLevel:level sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(lw, lh, 1) toBuffer:buf destinationOffset:0 destinationBytesPerRow:lp destinationBytesPerImage:lp * lr];
    enc_b_end(b); [cb commit]; [cb waitUntilCompleted];
    if (cb.status == MTLCommandBufferStatusError || nvn_swizzle(&t->d, level, layer, buf.contents, lp, t->mem)) return 0;
    g_wb_bytes += lp * lr; return 1;
}
// Every level and layer of n textures back to their storage with one command buffer and one wait (plan item 191; DavidZande/MacNativeGames 8c46687,
// MIT, written anew: Tears of the Kingdom invalidates a pool holding a 9-level GPU-rendered texture, and a command buffer per level, ~1.25 ms each and
// almost all of it start delay, made ~110 ms stalls after spawning there). ok[i]: texture i written back. DM_NVN_WB_BATCH=0: wb_level per level
static int wb_batch_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_NVN_WB_BATCH"); v = !(e && !strcmp(e, "0")); if (!v) MLOG("DM_NVN_WB_BATCH=0: a command buffer per level of a GPU write-back"); } return v; }
static void wb_textures(const tex_t *const *t, __unsafe_unretained id<MTLTexture> const *mt, int n, int *ok) {
    if (!wb_batch_on()) {
        for (int i = 0; i < n; i++) { dm_guest_write_prepare(t[i]->mem, t[i]->size); ok[i] = 1;
            for (unsigned l = 0; l < (t[i]->d.levels ? t[i]->d.levels : 1) && ok[i]; l++) for (unsigned y = 0; y < wb_layers(t[i]) && ok[i]; y++) ok[i] = wb_level(t[i], mt[i], l, y); }
        return;
    }
    typedef struct { int i; unsigned level, layer; size_t lp, lr; } wb_job;
    int nj = 0; for (int i = 0; i < n; i++) nj += (int)((t[i]->d.levels ? t[i]->d.levels : 1) * wb_layers(t[i]));
    if (!nj) { for (int i = 0; i < n; i++) ok[i] = 0; return; }
    wb_job *job = malloc(sizeof *job * (size_t)nj); NSMutableArray<id<MTLBuffer>> *bufs = [NSMutableArray arrayWithCapacity:(NSUInteger)nj];
    id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> b = enc_b_begin(cb); g_wb_cbs++; int k = 0;
    for (int i = 0; i < n; i++) { ok[i] = 1;
        for (unsigned l = 0; l < (t[i]->d.levels ? t[i]->d.levels : 1); l++) for (unsigned y = 0; y < wb_layers(t[i]); y++) {
            uint32_t lw, lh; size_t lp, lr; wb_level_size(t[i], l, &lw, &lh, &lp, &lr);
            id<MTLBuffer> buf = ok[i] ? [g_dev newBufferWithLength:lp * lr options:MTLResourceStorageModeShared] : nil;
            if (!buf) { ok[i] = 0; continue; }
            [b copyFromTexture:mt[i] sourceSlice:y sourceLevel:l sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(lw, lh, 1) toBuffer:buf destinationOffset:0 destinationBytesPerRow:lp destinationBytesPerImage:lp * lr];
            job[k] = (wb_job){i, l, y, lp, lr}; [bufs addObject:buf]; k++;
        } }
    enc_b_end(b); [cb commit]; [cb waitUntilCompleted];
    int bad = cb.status == MTLCommandBufferStatusError;
    for (int i = 0; i < n; i++) if (ok[i] && !bad) dm_guest_write_prepare(t[i]->mem, t[i]->size);
    for (int j = 0; j < k; j++) { wb_job *x = &job[j]; const tex_t *u = t[x->i];
        if (bad || !ok[x->i]) { ok[x->i] = 0; continue; }
        if (nvn_swizzle(&u->d, x->level, x->layer, bufs[(NSUInteger)j].contents, x->lp, u->mem)) ok[x->i] = 0; else g_wb_bytes += x->lp * x->lr; }
    free(job);
}
// The entries taken from the table written back (the GPU work that wrote them has completed): of several renderings of one memory only the newest,
// none whose storage the CPU wrote since (its bytes are newer), formats the deswizzle cannot write skipped; the entries freed. t0..t1: the wait before
static void wb_write_list(wb_ent **list, int nl, const char *why, uint64_t t0, uint64_t t1, int staged) {   // staged: written from 286's copies where there are
    int fresh[512];   // decided for all before anything is written (a write-back writes the pages); of several renderings of one memory only the newest
    for (int i = 0; i < nl; i++) { fresh[i] = !nvn_dirty_since(list[i]->t.mem, list[i]->t.size, list[i]->stamp);
        if (!fresh[i] && list[i]->snap && list[i]->snap_n == list[i]->t.size && !memcmp(list[i]->snap, list[i]->t.mem, list[i]->snap_n)) {   // its pages written, its bytes not
            fresh[i] = 1; if (++g_wb_exact <= 4 || !(g_wb_exact % 1000)) MLOG("GPU write-back %s: texture %llu %ux%u format %#x: its pages were written since, its own bytes not: written back (%llu such)", why,
                (unsigned long long)list[i]->t.serial, list[i]->t.d.width, list[i]->t.d.height, list[i]->t.d.format, (unsigned long long)g_wb_exact); } }
    for (int i = 0; i < nl; i++) for (int j = 0; j < nl; j++) if (i != j && fresh[i] == 1 && fresh[j] && list[j]->t.mem == list[i]->t.mem && list[j]->stamp > list[i]->stamp) { fresh[i] = 2; break; }
    // per call (job threads invalidate at the same time); the entries hold the textures until wb_free below
    const tex_t **wt = malloc(sizeof *wt * (size_t)nl); __unsafe_unretained id<MTLTexture> *wm = (__unsafe_unretained id<MTLTexture> *)calloc((size_t)nl, sizeof(id));
    wb_ent **we = malloc(sizeof *we * (size_t)nl); int *wok = malloc(sizeof *wok * (size_t)nl), nw = 0;
    for (int i = 0; i < nl; i++) {
        wb_ent *e = list[i]; const tex_t *t = &e->t; id o = (__bridge id)e->obj; id<MTLTexture> mt = [o isKindOfClass:[NvPkg class]] ? ((NvPkg *)o)->tex : o; uint32_t upf; int ubpp;
        if (fresh[i] != 1) { if (!fresh[i]) g_wb_skip_cpu++; wb_free(e); continue; }
        int a1r_, a1s_; if (!mt || bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &a1r_, &a1s_) || t->d.target == 2 || t->d.samples > 1) { RED_ONCE(88, "GPU write-back: texture format %#x target %d not supported (first case)", t->d.format, t->d.target); wb_free(e); continue; }
        wt[nw] = t; wm[nw] = mt; we[nw++] = e;
    }
    int ns = 0; wb_ent **se = staged ? malloc(sizeof *se * (size_t)(nw ? nw : 1)) : NULL;
    if (staged) for (int i = 0; i < nw;) { if (wb_from_stage(we[i])) { se[ns++] = we[i]; we[i] = we[--nw]; wt[i] = wt[nw]; wm[i] = wm[nw]; } else i++; }   // those off the GPU list
    uint64_t cbs = g_wb_cbs; wb_textures(wt, wm, nw, wok); uint64_t t2 = bp_now();
    static _Atomic uint64_t slow; if (t2 - t0 > 20000000ull) { uint64_t k = atomic_fetch_add(&slow, 1) + 1;   // as DavidZande/MacNativeGames 8c46687's [invalidate] line
        if (k <= 8 || !(k % 100)) MLOG("write-back %s %.1f ms: waiting for the GPU %.1f ms, %d of %d textures written back with %llu command buffers in %.1f ms (%llu such)", why, (t2 - t0) / 1e6,
            (t1 - t0) / 1e6, nw, nl, (unsigned long long)(g_wb_cbs - cbs), (t2 - t1) / 1e6, (unsigned long long)k); }
    for (int i = 0; i < nw; i++) {
        wb_ent *e = we[i]; const tex_t *t = wt[i]; int ok = wok[i];
        uint64_t d = ++g_wb_done;
        if (!ok) RED_ONCE(89, "GPU write-back of texture %llu (format %#x) failed (first case)", (unsigned long long)t->serial, t->d.format);
        else if (d <= 4 || !(d % 1000)) MLOG("GPU write-back %s: texture %llu %ux%u format %#x (%llu so far, %.1f MB; %llu skipped, the CPU had written them since)", why, (unsigned long long)t->serial,
                                            t->d.width, t->d.height, t->d.format, (unsigned long long)d, g_wb_bytes / 1048576.0, (unsigned long long)g_wb_skip_cpu);
        wb_free(e);
    }
    for (int i = 0; i < ns; i++) { uint64_t d = ++g_wb_done; if (d <= 4 || !(d % 1000)) MLOG("GPU write-back %s: texture %llu %ux%u format %#x from the copy made in its submission (%llu so far)", why,
        (unsigned long long)se[i]->t.serial, se[i]->t.d.width, se[i]->t.d.height, se[i]->t.d.format, (unsigned long long)d); wb_free(se[i]); }
    free(se); free(wt); free(wm); free(we); free(wok);
}
// The wait for the GPU gives the guest thread's virtual core up meanwhile (game.json virtual_cores; MacNativeGames 0fd9358e, MIT, written anew; plan
// item 297): the game's other workers could not use the core it slept on. Only that wait: the write-back after it writes the pages, and a worker
// faulting on them must not hold the core the writer needs back. Weak defaults (hle_nvn.c has the same): the graphics tests have no guest threads
#ifndef DM_CORE_AWAY_WEAK   // (once in a unit that includes both the backend and hle_nvn.c: the tests)
#define DM_CORE_AWAY_WEAK
__attribute__((weak)) int dm_core_away(void) { return 0; }
__attribute__((weak)) void dm_core_back(int *away) { (void)away; }
#endif
static uint64_t g_wb_core_away;   // write-back waits that gave a core up (t_wb)
void mtl_pool_invalidate(const pool_t *p, uint64_t off, uint64_t n) { @autoreleasepool {   // hle_nvn.c nvnMemoryPoolInvalidateMappedRange (game thread)
    if (!p || !p->mem || !n) return;
    const uint8_t *a = p->mem + off, *z = a + n;
    wb_ent *list[512]; int nl = 0;
    pthread_mutex_lock(&g_wb_mu);
    for (int again = 1; again && nl < 512;) { again = 0;   // a deletion shifts entries: scan again
        for (uint32_t i = 0; i < g_wb.cap && nl < 512; i++) { wb_ent *e = g_wb.vals ? g_wb.vals[i] : NULL;
            if (!e || e->t.mem >= z || e->t.mem + e->t.size <= a) continue;
            uint64_t k = e->t.serial; nvn_hm_del(&g_wb, &k); list[nl++] = e; again = 1; break; } }
    pthread_mutex_unlock(&g_wb_mu);
    if (!nl) return;
    uint64_t t0 = bp_now();
#ifndef DM_TEST_WB_KEEPS_CORE   // negative control of t_wb only: the core kept, as before
    { int away = dm_core_away(); g_wb_core_away += (uint64_t)away; mtl_finish_gpu(); dm_core_back(&away); }
#else
    mtl_finish_gpu();
#endif
    uint64_t t1 = bp_now();
    wb_write_list(list, nl, "on invalidate", t0, t1, 0);
} }
// After the game's own fence waits (plan item 278; MacNativeGames 1bffa28, MIT, written anew): on the console the GPU's bytes are in the pool once
// the fence has passed, and a game may read a small target from the CPU with no invalidate: Tears of the Kingdom decides the cut reaction of grass
// from a 128x128 RGBA8 target its grass pass renders into a CPU-visible pool every frame (MacNativeGames: no blades, no insects, the grass not cut
// while the bytes stayed in the Metal texture). nvnSyncWait (signalled) and nvnQueueFinish write back the targets up to 64 KiB whose rendering has
// completed; a later CPU write keeps its bytes as on an invalidate. Each new shape written back this way is logged. DM_NVN_WB_FENCE=0 turns it off.
// Standing on Great Sky Island (M5, 2026-10-27) one wait in 18 writes back a 96x96 target: 0.6-1 ms on the game's thread for a copy on the GPU,
// 0.043 ms from the copy its submission made (286)
static uint64_t g_wb_fence_n;
void mtl_wb_fence_done(void) { @autoreleasepool {   // hle_nvn.c, the game's thread
#ifdef DM_TEST_WB_INVALIDATE_ONLY   // negative control of t_wb only: written back on an invalidate only, as before
    return;
#endif
    if (!wb_fence_on() || !g_wb.n) return;
    uint64_t done = mtl_done_fence();
    wb_ent *list[64]; int nl = 0;
    pthread_mutex_lock(&g_wb_mu);
    for (int again = 1; again && nl < 64;) { again = 0;   // a deletion shifts entries: scan again
        for (uint32_t i = 0; i < g_wb.cap && nl < 64; i++) { wb_ent *e = g_wb.vals ? g_wb.vals[i] : NULL;
            if (!e || e->t.size > WB_FENCE_MAX || !e->fence || e->fence > done) continue;
            if (!wb_fence_kind(&e->t)) continue;   // not written back anyway (an invalidate says so)
            uint64_t k = e->t.serial; nvn_hm_del(&g_wb, &k); list[nl++] = e; again = 1; break; } }
    pthread_mutex_unlock(&g_wb_mu);
    if (!nl) return;
    static struct { uint32_t w, h, f; } seen[32]; static int ns; static pthread_mutex_t seen_mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&seen_mu);
    for (int i = 0; i < nl; i++) { const tex_t *t = &list[i]->t; int k = 0; while (k < ns && !(seen[k].w == t->d.width && seen[k].h == t->d.height && seen[k].f == (uint32_t)t->d.format)) k++;
        if (k == ns && ns < 32) { seen[ns++] = (typeof(seen[0])){t->d.width, t->d.height, (uint32_t)t->d.format};
            MLOG("GPU write-back after a fence wait: a target of a new shape, %ux%ux%u format %#x, %u bytes, pool flags %#x (%d shapes)", t->d.width, t->d.height, t->d.depth, t->d.format, t->size, t->pool ? t->pool->flags : 0u, ns); } }
    pthread_mutex_unlock(&seen_mu);
    g_wb_fence_n += (uint64_t)nl;
    uint64_t t0 = bp_now();
    wb_write_list(list, nl, "after a fence wait", t0, t0, 1);
} }
// The GPU is about to write this texture: from now on its MTLTexture is the content. An upload of a CPU-filled texture carries on as that texture
static void tex_gpu_write(const tex_t *t) {
    if (!t || !t->serial || !tex_cpu_candidate(t)) return;
    pthread_mutex_lock(&g_cpuf_mu);
    if (!g_gpu_owned) g_gpu_owned = CFSetCreateMutable(NULL, 0, NULL);
    int had = CFSetContainsValue(g_gpu_owned, (const void *)(uintptr_t)t->serial);
    if (!had) { CFSetAddValue(g_gpu_owned, (const void *)(uintptr_t)t->serial); if (g_cpuf_watch) CFDictionaryRemoveValue(g_cpuf_watch, (const void *)(uintptr_t)t->serial); }
    pthread_mutex_unlock(&g_cpuf_mu);
    if (!had && wb_census_on()) wb_census_shape("GPU-written texture in a CPU-visible pool", t, ++g_wbc_gpu);
    if (!had) {
        id have = serial_get(t->serial);
        if ([have isKindOfClass:[NvPkg class]]) { id<MTLTexture> tx = ((NvPkg *)have)->tex; ((NvPkg *)have)->tex = nil; if (tx) serial_put(t->serial, tx); }   // NvPkg's dealloc would take tx out of residency
    }
    wb_note(t);
}
void tex_forget_serial(uint64_t ser) {   // the texture object was finalized: its serial is never used again
    pthread_mutex_lock(&g_cpuf_mu);
    if (g_gpu_owned) CFSetRemoveValue(g_gpu_owned, (const void *)(uintptr_t)ser);
    if (g_cpuf_watch) CFDictionaryRemoveValue(g_cpuf_watch, (const void *)(uintptr_t)ser);
    if (g_wt_since) CFSetRemoveValue(g_wt_since, (const void *)(uintptr_t)ser);
    pthread_mutex_unlock(&g_cpuf_mu);
}
static int stor_key_of(const tex_t *t, stor_key *k) {   // a GPU-only texture (not CPU-filled, not on a pool buffer) that can share by storage
    if (!stor_on() || !t->mem || !t->size || t->has_layout || tex_cpu_candidate(t) || t->d.target == NVN_TEXTURE_TARGET_BUFFER || (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET))) return 0;
    memset(k, 0, sizeof *k);
    k->mem = (uint64_t)(uintptr_t)t->mem; k->size = t->size; k->w = t->d.width; k->h = t->d.height; k->d = t->d.depth; k->levels = t->d.levels; k->target = (uint32_t)t->d.target;
    k->samples = t->d.samples; k->fmt = (uint32_t)t->d.format;
    return 1;
}
