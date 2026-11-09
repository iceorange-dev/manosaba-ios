// gfx/nvn/mtl/copies.inc, part of nvn_mtl.m (included there in order, one translation unit): Copies between buffers and textures, texel reads and writes, texture clears
// GPU: texture -> buffer (row / image strides from SetCopyRowStride / SetCopyImageStride, 0 = tightly packed; nvn_copy_pitch.h)
static void copy_t2b(const p_copy_b2t *c) {
    if (c->tex) xv_check(c->tex->serial, c->has_view && c->view.set_levels ? c->view.base_level : 0, c->has_view && c->view.set_layers ? c->view.min_layer : 0, "texture-to-buffer copy");
    id<MTLTexture> t = tex_mtl(c->tex);
    if (!t) return;
    int bw, bh, bpb = nvn_fmt_bpb(c->tex->d.format, &bw, &bh);
    unsigned level = c->has_view && c->view.set_levels ? c->view.base_level : 0, layer0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    size_t row, img, extent;
    if (!nvn_copy_pitch(c->r.width, c->r.height, c->r.depth, bw, bh, bpb, S.copy_stride, S.copy_image_stride, &row, &img, &extent)) {
        RED_ONCE(67, "texture→buffer copy with invalid strides (row %lld, image %lld, %dx%dx%d)", (long long)S.copy_stride, (long long)S.copy_image_stride, c->r.width, c->r.height, c->r.depth); return; }
    int is3d = c->tex->d.target == 2, nz = c->r.depth ? c->r.depth : 1;
    void *mb; uint64_t mo;
    if (!mtl_addr(c->src, extent, &mb, &mo)) { RED_ONCE(55, "texture→buffer copy destination is not in a memory pool"); return; }
    { uint32_t upf; int ubpp, r_, s_;   // no BC: the texture holds decompressed texels or ASTC blocks, not the BC blocks the copy's pitch is for
      if (bc_unpack(c->tex->d.format, &upf, &ubpp) || bc1_astc(c->tex->d.format, &r_, &s_)) { RED_ONCE(104, "texture→buffer copy of BC format %#x on a GPU without BC: not done (first case)", c->tex->d.format); return; } }
    us_drop(c->src, extent);
    BENC b = blit();
    if (is3d)
        enc_b_copy_texture_buffer(b, t, 0, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, c->r.zoffset), MTLSizeMake(c->r.width, c->r.height, nz), (__bridge id<MTLBuffer>)mb, mo, row, img);
    else
        for (int z = 0; z < nz; z++)
            enc_b_copy_texture_buffer(b, t, layer0 + c->r.zoffset + z, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, 0), MTLSizeMake(c->r.width, c->r.height, 1), (__bridge id<MTLBuffer>)mb, mo + z * img, row, img);
}
// CPU texture read (ReadTexels): for packaged textures the pool memory is the ground truth, de-layout and read directly; the rest are read back from the MTLTexture by blit (the caller has already waited for the GPU per NVN rules)
// CPU Read / WriteTexels on storage (plan item 206): the region's bytes alone (nvn_region_move; MacNativeGames PR #185, MIT: frames over 40 ms
// 25.9 → 1.0 a minute on an M5 Max, 1.1.2, a 64x64 region of a 2048² texture 1.9 → 0.0025 ms), the whole level as before when the region cannot
// be moved alone; counted, a line at the 1st, 2nd, 4th, ... call
static uint64_t g_tx_n, g_tx_region, g_tx_level, g_tx_bytes, g_tx_ns;
static void texels_count(int whole, size_t bytes, uint64_t t0) {
    uint64_t n = ++g_tx_n; whole ? g_tx_level++ : g_tx_region++; g_tx_bytes += bytes; g_tx_ns += bp_now() - t0;
    if (!(n & (n - 1))) MLOG("CPU Read / WriteTexels on storage: %llu calls, %llu by region, %llu by whole level, %.1f MB moved, %.2f ms in all",
                             (unsigned long long)n, (unsigned long long)g_tx_region, (unsigned long long)g_tx_level, g_tx_bytes / 1048576.0, g_tx_ns / 1e6);
}
static int texels_region(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *lin, size_t row, size_t img, int to_linear) {
#ifdef DM_TEST_TEXELS_WHOLE_LEVEL   // negative control of t_writetexels only: the whole level, as before
    (void)t; (void)level; (void)layer0; (void)r; (void)lin; (void)row; (void)img; (void)to_linear; return -1;
#else
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh); if (!bpb) return -1;
    uint32_t x0 = (uint32_t)(r->xoffset / bw) * (uint32_t)bpb, y0 = (uint32_t)(r->yoffset / bh), wb = (uint32_t)((r->width + bw - 1) / bw) * (uint32_t)bpb, hb = (uint32_t)((r->height + bh - 1) / bh);
    uint32_t nz = r->depth ? (uint32_t)r->depth : 1;
    if (t->d.target == 2) return nvn_region_move(&t->d, level, 0, t->mem, lin, row, img, x0, y0, (uint32_t)r->zoffset, wb, hb, nz, to_linear);
    for (uint32_t z = 0; z < nz; z++) if (nvn_region_move(&t->d, level, layer0 + (unsigned)r->zoffset + z, t->mem, lin + z * img, row, img, x0, y0, 0, wb, hb, 1, to_linear)) return -1;
    return 0;
#endif
}
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *dst, size_t row, size_t img) { @autoreleasepool {
    xv_check(t->serial, level, layer0, "CPU readback");
    PD("U cpu_read ser=%llu lv%u sl%u xyz=%d,%d,%d size=%d,%d,%d dst=%p row=%lu img=%lu", t->serial, level, layer0, r->xoffset, r->yoffset, r->zoffset, r->width, r->height, r->depth, dst, (unsigned long)row, (unsigned long)img);
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    size_t wb = (size_t)((r->width + bw - 1) / bw) * bpb, hb = (r->height + bh - 1) / bh, x0 = (size_t)(r->xoffset / bw) * bpb, y0 = r->yoffset / bh;
    int nz = r->depth ? r->depth : 1, is3d = t->d.target == 2;
    if (tex_membacked(t)) {   // packaged or CPU-filled: the pool memory is the content
        uint64_t t0 = bp_now();
        if (!texels_region(t, level, layer0, r, dst, row, img, 1)) { texels_count(0, wb * hb * nz, t0); return; }
        uint32_t lw = MAX(t->d.width >> level, 1u), lh = MAX(t->d.height >> level, 1u), lz = is3d ? MAX(t->d.depth >> level, 1u) : 1;
        size_t lp = (size_t)((lw + bw - 1) / bw) * bpb, lr = (lh + bh - 1) / bh;
        uint8_t *lin = malloc(lp * lr * lz);
        for (int z = 0; z < nz; z++) {
            unsigned layer = is3d ? 0 : layer0 + r->zoffset + z;
            if ((is3d && z == 0) || !is3d) if (nvn_deswizzle(&t->d, level, layer, t->mem, lin, lp)) { RED_ONCE(56, "ReadTexels deswizzle failed"); free(lin); return; }
            const uint8_t *src = lin + (is3d ? (size_t)(r->zoffset + z) * lp * lr : 0);
            for (size_t y = 0; y < hb; y++) memcpy(dst + z * img + y * row, src + (y0 + y) * lp + x0, wb);
        }
        free(lin); texels_count(1, lp * lr * lz, t0);
        return;
    }
    id<MTLTexture> mt = tex_mtl(t);
    if (!mt) { memset(dst, 0, img * nz); return; }
    { uint32_t upf; int ubpp, r_, s_;   // no BC (as copy_t2b)
      if (bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &r_, &s_)) { RED_ONCE(105, "ReadTexels of BC format %#x on a GPU without BC: zeros (first case)", t->d.format); memset(dst, 0, img * nz); return; } }
    mtl_finish_gpu();
    id<MTLBuffer> buf = [g_dev newBufferWithLength:wb * hb * nz options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> b = enc_b_begin(cb);
    for (int z = 0; z < nz; z++)
        [b copyFromTexture:mt sourceSlice:is3d ? 0 : layer0 + r->zoffset + z sourceLevel:level sourceOrigin:MTLOriginMake(r->xoffset, r->yoffset, is3d ? r->zoffset + z : 0)
                sourceSize:MTLSizeMake(r->width, r->height, 1) toBuffer:buf destinationOffset:z * wb * hb destinationBytesPerRow:wb destinationBytesPerImage:wb * hb];
    enc_b_end(b); [cb commit]; [cb waitUntilCompleted];
    for (int z = 0; z < nz; z++) for (size_t y = 0; y < hb; y++) memcpy(dst + z * img + y * row, (uint8_t *)buf.contents + z * wb * hb + y * wb, wb);
} }
// A region of packed rows into a texture, through a shared staging buffer, committed synchronously (later submissions are queued after it on the same queue)
static void region_into(id<MTLTexture> mt, unsigned level, unsigned layer0, const NVNcopyRegion *r, const uint8_t *src, size_t row, size_t img, size_t wb, size_t hb, int nz, int is3d) {
    id<MTLBuffer> buf = [g_dev newBufferWithLength:wb * hb * nz options:MTLResourceStorageModeShared];
    for (int z = 0; z < nz; z++) for (size_t y = 0; y < hb; y++) memcpy((uint8_t *)buf.contents + z * wb * hb + y * wb, src + z * img + y * row, wb);
    id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> b = enc_b_begin(cb);
    for (int z = 0; z < nz; z++)
        [b copyFromBuffer:buf sourceOffset:z * wb * hb sourceBytesPerRow:wb sourceBytesPerImage:wb * hb sourceSize:MTLSizeMake(r->width, r->height, 1)
                toTexture:mt destinationSlice:is3d ? 0 : layer0 + r->zoffset + z destinationLevel:level destinationOrigin:MTLOriginMake(r->xoffset, r->yoffset, is3d ? r->zoffset + z : 0)];
    enc_b_end(b); [cb commit]; [cb waitUntilCompleted];
}
static uint64_t g_flush_up;          // FlushTexels regions taken from storage (mtl_flush_texels)
// CPU texture write (WriteTexels): for packaged textures the pool memory is the ground truth -- deswizzle this level / layer, patch the region, swizzle back; the pool write triggers a re-upload through dirty-page tracking;
// the rest are blitted into the MTLTexture through a shared staging buffer, committed synchronously (later submissions are queued after it on the same queue)
void mtl_write_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, const uint8_t *src, size_t row, size_t img) { @autoreleasepool {
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    if (!bpb) { RED_ONCE(64, "WriteTexels unknown format %#x", t->d.format); return; }
    size_t wb = (size_t)((r->width + bw - 1) / bw) * bpb, hb = (r->height + bh - 1) / bh, x0 = (size_t)(r->xoffset / bw) * bpb, y0 = r->yoffset / bh;
    int nz = r->depth ? r->depth : 1, is3d = t->d.target == 2;
    if (tex_membacked(t)) {   // packaged or CPU-filled: patch the pool memory, dirty-page tracking re-uploads it
        uint64_t t0 = bp_now();
        if (!texels_region(t, level, layer0, r, (uint8_t *)src, row, img, 0)) { texels_count(0, wb * hb * nz, t0); return; }
        uint32_t lw = MAX(t->d.width >> level, 1u), lh = MAX(t->d.height >> level, 1u), lz = is3d ? MAX(t->d.depth >> level, 1u) : 1;
        size_t lp = (size_t)((lw + bw - 1) / bw) * bpb, lr = (lh + bh - 1) / bh;
        uint8_t *lin = malloc(lp * lr * lz);
        for (int z = 0; z < nz; z++) {
            unsigned layer = is3d ? 0 : layer0 + r->zoffset + z;
            if ((is3d && z == 0) || !is3d) if (nvn_deswizzle(&t->d, level, layer, t->mem, lin, lp)) { RED_ONCE(65, "WriteTexels deswizzle failed format %#x target %d", t->d.format, t->d.target); free(lin); return; }
            uint8_t *dst = lin + (is3d ? (size_t)(r->zoffset + z) * lp * lr : 0);
#ifndef DM_TEST_WT_ROWS_AT_REGION   // negative control of t_writetexels only: rows placed at the region's pitch, not the level's
            for (size_t y = 0; y < hb; y++) memcpy(dst + (y0 + y) * lp + x0, src + z * img + y * row, wb);
#else
            for (size_t y = 0; y < hb; y++) memcpy(dst + (y0 + y) * wb + x0, src + z * img + y * row, wb);
#endif
            if ((is3d && z == nz - 1) || !is3d) if (nvn_swizzle(&t->d, level, layer, lin, lp, t->mem)) { RED_ONCE(57, "WriteTexels swizzle failed format %#x target %d", t->d.format, t->d.target); free(lin); return; }
        }
        free(lin); texels_count(1, 2 * lp * lr * lz, t0);
        return;
    }
    uint32_t upf; int ubpp;
    { int r_, s_; if (bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &r_, &s_)) { RED_ONCE(66, "WriteTexels: compressed format %#x on a device without BC not implemented", t->d.format); return; } }
    id<MTLTexture> mt = tex_mtl(t);
    if (!mt) return;
    region_into(mt, level, layer0, r, src, row, img, wb, hb, nz, is3d);
    pthread_mutex_lock(&g_cpuf_mu);   // the MTLTexture holds these texels, the storage does not: the FlushTexels after it takes nothing from storage
    if (!g_wt_since) g_wt_since = CFSetCreateMutable(NULL, 0, NULL);
    CFSetAddValue(g_wt_since, (const void *)(uintptr_t)t->serial);
    pthread_mutex_unlock(&g_cpuf_mu);
} }
// FlushTexels (plan item 64j; dr3-mobile's reading of it, MIT, written anew): on the console the CPU writes a texture's storage and FlushTexels
// makes the region visible to the GPU. A texture whose storage is its content here (packaged, CPU-filled, linear on its pool buffer) needs nothing:
// its pages are watched or read in place. One whose content is its MTLTexture (the GPU has written it) takes the flushed region from its storage,
// unless WriteTexels wrote it since its last flush: ours writes such a texture's MTLTexture and not its storage (Tears of the Kingdom pairs each of
// its 42 WriteTexels with a FlushTexels; dr3-mobile's game writes the storage itself, 491 flushes before its first frame)
void mtl_flush_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r) { @autoreleasepool {
    if (!t || !t->serial || !t->mem || !t->pool || (t->pool->flags & NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS) || t->d.target == NVN_TEXTURE_TARGET_BUFFER
        || (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)) || !r->width || !r->height || tex_membacked(t)) return;
    pthread_mutex_lock(&g_cpuf_mu);
    int wt = g_wt_since && CFSetContainsValue(g_wt_since, (const void *)(uintptr_t)t->serial);
    if (wt) CFSetRemoveValue(g_wt_since, (const void *)(uintptr_t)t->serial);
    pthread_mutex_unlock(&g_cpuf_mu);
#ifndef DM_TEST_FLUSH_AFTER_WT   // negative control of t_flushtexels only: the region taken from storage after a WriteTexels too
    if (wt) return;
#endif
#ifdef DM_TEST_FLUSH_IGNORED   // negative control of t_flushtexels only: FlushTexels does nothing, as before
    return;
#endif
    __unsafe_unretained id have = serial_get(t->serial);
    if (!have || [have isKindOfClass:[NvPkg class]]) return;   // not made yet: a CPU-filled texture's first use reads its storage
    { uint32_t upf; int ubpp, r_, s_; if (bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &r_, &s_)) { RED_ONCE(109, "FlushTexels of BC format %#x on a GPU without BC: the region is not taken from storage", t->d.format); return; } }
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    if (!bpb) return;
    int nz = r->depth ? r->depth : 1, is3d = t->d.target == 2;
    uint32_t lw = MAX(t->d.width >> level, 1u), lh = MAX(t->d.height >> level, 1u), lz = is3d ? MAX(t->d.depth >> level, 1u) : 1;
    size_t lp = (size_t)((lw + bw - 1) / bw) * bpb, lr = (lh + bh - 1) / bh;
    size_t wb = (size_t)((r->width + bw - 1) / bw) * bpb, hb = (r->height + bh - 1) / bh, x0 = (size_t)(r->xoffset / bw) * bpb, y0 = r->yoffset / bh;
    if (x0 + wb > lp || y0 + hb > lr) return;
    uint8_t *lin = malloc(lp * lr * lz), *reg = malloc(wb * hb * nz);
    for (int z = 0; z < nz; z++) {
        unsigned layer = is3d ? 0 : layer0 + r->zoffset + z;
        if ((is3d && z == 0) || !is3d) if (nvn_deswizzle(&t->d, level, layer, t->mem, lin, lp)) { free(lin); free(reg); return; }
        const uint8_t *from = lin + (is3d ? (size_t)(r->zoffset + z) * lp * lr : 0);
        for (size_t y = 0; y < hb; y++) memcpy(reg + z * wb * hb + y * wb, from + (y0 + y) * lp + x0, wb);
    }
    region_into((id<MTLTexture>)have, level, layer0, r, reg, wb, wb * hb, wb, hb, nz, is3d);
    free(lin); free(reg);
    uint64_t k = ++g_flush_up; if (k <= 4 || !(k % 100)) MLOG("FlushTexels: texture %llu %ux%u format %#x takes a %ux%u region from its storage (%llu so far)", (unsigned long long)t->serial, t->d.width, t->d.height, t->d.format, r->width, r->height, (unsigned long long)k);
} }
// Last buffer→texture copy handed to the driver, for the crash report (crash.c calls mtl_crash_note; plain values only, written before each encode):
// a crash inside the driver's copy encoder was seen a few times in repeated scene loads on the Mac, with a garbage or near-null address
static struct { uint64_t seq, tid, ser, src, off, row, img, buf_len; const void *buf, *tex; unsigned w, h, d, arr, mips, pf, type, storage, usage, fmt, target, cw, ch, cz, x, y, slice, level, in_pools; } g_last_b2t;
void mtl_crash_note(void) {
    if (!g_last_b2t.seq) return;
    fprintf(stderr, "[dm] last buffer→texture copy #%llu (thread %llu): texture %p ser=%llu %ux%ux%u array %u mips %u Metal format %u type %u storage %u usage %#x (NVN format %#x target %u); "
            "buffer %p length %llu offset %llu (guest %#llx, %s the pool table); row %llu image %llu; region %ux%ux%u at %u,%u slice %u level %u\n",
            (unsigned long long)g_last_b2t.seq, (unsigned long long)g_last_b2t.tid, g_last_b2t.tex, (unsigned long long)g_last_b2t.ser, g_last_b2t.w, g_last_b2t.h, g_last_b2t.d, g_last_b2t.arr, g_last_b2t.mips,
            g_last_b2t.pf, g_last_b2t.type, g_last_b2t.storage, g_last_b2t.usage, g_last_b2t.fmt, g_last_b2t.target, g_last_b2t.buf, (unsigned long long)g_last_b2t.buf_len, (unsigned long long)g_last_b2t.off,
            (unsigned long long)g_last_b2t.src, g_last_b2t.in_pools ? "in" : "NOT in", (unsigned long long)g_last_b2t.row, (unsigned long long)g_last_b2t.img, g_last_b2t.cw, g_last_b2t.ch, g_last_b2t.cz,
            g_last_b2t.x, g_last_b2t.y, g_last_b2t.slice, g_last_b2t.level);
}
static int pool_buf_listed(const void *buf) { int hit = 0; pthread_mutex_lock(&g_mu); for (int i = 0; i < g_npool && !hit; i++) hit = (__bridge const void *)g_pools[i].buf == buf; pthread_mutex_unlock(&g_mu); return hit; }
static void b2t_note(const p_copy_b2t *c, id<MTLTexture> t, id<MTLBuffer> b, uint64_t off, size_t row, size_t img, unsigned cz, unsigned slice, unsigned level) {
    static uint64_t seq; uint64_t tid; pthread_threadid_np(NULL, &tid);
    int listed = pool_buf_listed((__bridge const void *)b);
    if (!listed) RED_ONCE(58, "buffer→texture copy: the source buffer %p is no longer in the memory pool table at encode time (texture ser=%llu)", (__bridge const void *)b, c->tex->serial);
    g_last_b2t = (typeof(g_last_b2t)){++seq, tid, c->tex->serial, c->src, off, row, img, (uint64_t)b.length, (__bridge const void *)b, (__bridge const void *)t,
        (unsigned)t.width, (unsigned)t.height, (unsigned)t.depth, (unsigned)t.arrayLength, (unsigned)t.mipmapLevelCount, (unsigned)t.pixelFormat, (unsigned)t.textureType, (unsigned)t.storageMode, (unsigned)t.usage,
        c->tex->d.format, (unsigned)c->tex->d.target, c->r.width, c->r.height, cz, (unsigned)c->r.xoffset, (unsigned)c->r.yoffset, slice, level, (unsigned)listed};
}
static void copy_b2t(const p_copy_b2t *c) {
    if (c->tex) zskip_del(c->tex->serial);
    tex_gpu_write(c->tex);
    id<MTLTexture> t = tex_mtl(c->tex);
    PD("C ser=%llu", c->tex->serial);
    if (!t) return;
    int bw, bh, bpb = nvn_fmt_bpb(c->tex->d.format, &bw, &bh);
    unsigned level = c->has_view && c->view.set_levels ? c->view.base_level : 0, layer0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    size_t row, img, extent;
    if (!nvn_copy_pitch(c->r.width, c->r.height, c->r.depth, bw, bh, bpb, S.copy_stride, S.copy_image_stride, &row, &img, &extent)) {
        RED_ONCE(68, "buffer→texture copy with invalid strides (row %lld, image %lld, %dx%dx%d)", (long long)S.copy_stride, (long long)S.copy_image_stride, c->r.width, c->r.height, c->r.depth); return; }
    int is3d = c->tex->d.target == 2, nz = c->r.depth ? c->r.depth : 1;
    void *mb; uint64_t mo;
    if (!mtl_addr(c->src, extent, &mb, &mo)) { RED_ONCE(23, "buffer→texture copy source is not in a memory pool"); return; }
    uint32_t upf; int ubpp, a1r, a1s;
    if (bc1_astc(c->tex->d.format, &a1r, &a1s)) {   // No BC, BC1: the source's blocks transcoded to ASTC on the CPU into a temporary buffer, copied as blocks
        const uint8_t *src = [(__bridge id<MTLBuffer>)mb contents];
        if (!src) { RED_ONCE(59, "no-BC decompression: the source buffer of the buffer→texture copy is not CPU-readable"); return; }
        uint32_t bw4 = (c->r.width + 3) / 4, bh4 = (c->r.height + 3) / 4; size_t ap = (size_t)bw4 * 16, aimg = ap * bh4;
        id<MTLBuffer> tb = [g_dev newBufferWithLength:aimg * nz ?: 16 options:MTLResourceStorageModeShared];
        for (int z = 0; z < nz; z++) nvn_bc1_astc_image(src + mo + z * img, row, bw4, bh4, (uint8_t *)tb.contents + z * aimg, a1r);
        res_add(tb);
        BENC b = blit();
        if (is3d) enc_b_copy_buffer_texture(b, tb, 0, ap, aimg, MTLSizeMake(c->r.width, c->r.height, nz), t, 0, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, c->r.zoffset));
        else for (int z = 0; z < nz; z++) enc_b_copy_buffer_texture(b, tb, z * aimg, ap, aimg, MTLSizeMake(c->r.width, c->r.height, 1), t, layer0 + c->r.zoffset + z, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, 0));
        defer_obj_release(tb);
        return;
    }
    if (bc_unpack(c->tex->d.format, &upf, &ubpp)) {   // No BC: the compressed blocks in the source are decompressed on the CPU into a temporary buffer, then copied pixel-wise into the decompressed-format texture
        const uint8_t *src = [(__bridge id<MTLBuffer>)mb contents];
        if (!src) { RED_ONCE(59, "no-BC decompression: the source buffer of the buffer→texture copy is not CPU-readable"); return; }
        size_t up = (size_t)c->r.width * ubpp, uimg = up * c->r.height;
        id<MTLBuffer> tb = [g_dev newBufferWithLength:uimg * nz ?: 16 options:MTLResourceStorageModeShared];
        for (int z = 0; z < nz; z++) nvn_bc_decode(c->tex->d.format, src + mo + z * img, row, c->r.width, c->r.height, (uint8_t *)tb.contents + z * uimg, up);
        res_add(tb);
        BENC b = blit();
        if (is3d) enc_b_copy_buffer_texture(b, tb, 0, up, uimg, MTLSizeMake(c->r.width, c->r.height, nz), t, 0, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, c->r.zoffset));
        else for (int z = 0; z < nz; z++) enc_b_copy_buffer_texture(b, tb, z * uimg, up, uimg, MTLSizeMake(c->r.width, c->r.height, 1), t, layer0 + c->r.zoffset + z, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, 0));
        defer_obj_release(tb);   // Release after this submit has finished running (and remove from residency)
        return;
    }
    BENC b = blit();
    if (is3d) {
        b2t_note(c, t, (__bridge id<MTLBuffer>)mb, mo, row, img, (unsigned)nz, 0, level);
        enc_b_copy_buffer_texture(b, (__bridge id<MTLBuffer>)mb, mo, row, img, MTLSizeMake(c->r.width, c->r.height, nz), t, 0, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, c->r.zoffset));
    } else
        for (int z = 0; z < nz; z++) {
            b2t_note(c, t, (__bridge id<MTLBuffer>)mb, mo + z * img, row, img, 1, layer0 + c->r.zoffset + z, level);
            enc_b_copy_buffer_texture(b, (__bridge id<MTLBuffer>)mb, mo + z * img, row, img, MTLSizeMake(c->r.width, c->r.height, 1), t, layer0 + c->r.zoffset + z, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, 0));
        }
}

// No BC + Legacy Device Pack: the game copies textures from files (already ASTC 4x4) whole into a BC3 texture array it creates itself (created as RGBA8 when there is no BC). On Switch the two formats have the same block size and
// the copy moves raw blocks; here it is changed to a compute kernel that decodes pixel by pixel and writes them in, with sRGB sources first read out as linear values and then encoded back to sRGB values, so the result = the values read in the destination format after moving the blocks as-is
static const char k_a2u_msl[] =
"#include <metal_stdlib>\nusing namespace metal;\n"
"static float3 enc(float3 c) { return select(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, c <= 0.0031308); }\n"
"kernel void a2u(texture2d<float> src [[texture(0)]], texture2d<float, access::write> dst [[texture(1)]], constant uint4 *p [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= p[1].x || g.y >= p[1].y) return; float4 v = src.read(g + p[0].xy); if (p[1].z) v.rgb = enc(v.rgb); dst.write(v, g + p[0].zw); }\n";
static int copy_t2t_astc(id<MTLTexture> s, id<MTLTexture> d, unsigned sl, unsigned ss, unsigned dl, unsigned ds, int sx, int sy, int dx, int dy, int w, int h) {
    static int failed; static id<MTLComputePipelineState> ps;
#if !DM_NVN_M3
    static id<MTL4ArgumentTable> at;
#endif
    if (failed) return 0;
    if (!ps) { NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_a2u_msl) options:nil error:&e]; id<MTLFunction> f = lib ? [lib newFunctionWithName:@"a2u"] : nil;
        ps = f ? [g_dev newComputePipelineStateWithFunction:f error:&e] : nil;
#if !DM_NVN_M3
        MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxTextureBindCount = 2; ad.maxBufferBindCount = 1; at = [g_dev newArgumentTableWithDescriptor:ad error:&e];
#endif
        if (!ps M4_ONLY(|| !at)) { failed = 1; MLOG("[red] no BC: cannot create the compute pipeline for the ASTC → decompressed format copy %s", e.localizedDescription.UTF8String); return 0; } }
    MTLTextureType tt = MTLTextureType2D;
    id<MTLTexture> sv = [s newTextureViewWithPixelFormat:s.pixelFormat textureType:tt levels:NSMakeRange(sl, 1) slices:NSMakeRange(ss, 1)];
    id<MTLTexture> dv = [d newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:tt levels:NSMakeRange(dl, 1) slices:NSMakeRange(ds, 1)];
    id<MTLBuffer> pb = [g_dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
    if (!sv || !dv || !pb) { RED_ONCE(47, "no BC: ASTC → decompressed format copy cannot create view / parameter block"); return 0; }
    uint32_t *p = pb.contents; p[0] = sx; p[1] = sy; p[2] = dx; p[3] = dy; p[4] = w; p[5] = h; p[6] = s.pixelFormat == MTLPixelFormatASTC_4x4_sRGB; p[7] = 0;
    res_add(pb);
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    CENC ce = (CENC)enc_c_begin(NULL);
#if DM_NVN_M3
    [ce setTexture:sv atIndex:0]; [ce setTexture:dv atIndex:1]; [ce setBuffer:pb offset:0 atIndex:0];
#else
    [ce setArgumentTable:at];
    [at setTexture:sv.gpuResourceID atIndex:0]; [at setTexture:dv.gpuResourceID atIndex:1]; [at setAddress:pb.gpuAddress atIndex:0];
#endif
    enc_c_pso(ce, ps); enc_c_dispatch(ce, MTLSizeMake((w + 7) / 8, (h + 7) / 8, 1), MTLSizeMake(8, 8, 1)); enc_c_end(ce);
    args_dirty_all();
    defer_obj_unref(sv); defer_obj_unref(dv); defer_obj_release(pb);   // Release after this submit has finished running (views are not made resident separately; the parameter block is removed from residency)
    return 1;
}
// The blocks drawn into an auxiliary texture decoded into the BC texture kept unpacked (plan item 62c3, see blk_dec_kind): the level / slice's blocks
// copied into a buffer, then bcd writes the texels through a 2D view of the texture (an sRGB RGBA8 texture through an RGBA8Unorm view, as the
// upload), or b1a writes ASTC blocks behind them in the buffer, copied into the texture
static void blk_decode(const blk_wb *w) {
#if !DM_NVN_M3
    static id<MTL4ArgumentTable> at; static int failed;
    if (!at && !failed) { NSError *e = nil; MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxTextureBindCount = 1; ad.maxBufferBindCount = 2; at = [g_dev newArgumentTableWithDescriptor:ad error:&e];
        if (!at) { failed = 1; MLOG("[red] no BC: no argument table for decoding drawn blocks %s", e.localizedDescription.UTF8String); } }
    if (!at) return;
#endif
    NSUInteger row = w->bx * w->bpb, size = row * w->by, asz = w->dec == 2 ? w->bx * w->by * 16 : 0;
    id<MTLBuffer> b = [g_dev newBufferWithLength:size + asz options:MTLResourceStorageModePrivate], pb = [g_dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
    id<MTLTexture> v = nil; int is3d = w->dst.textureType == MTLTextureType3D;
    if (w->dec == 1) { MTLPixelFormat vf = w->dst.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB ? MTLPixelFormatRGBA8Unorm : w->dst.pixelFormat;   // 3D: a 3D view of the level, the plane in the parameters
                       v = is3d ? [w->dst newTextureViewWithPixelFormat:vf textureType:MTLTextureType3D levels:NSMakeRange(w->level, 1) slices:NSMakeRange(0, 1)]
                                : [w->dst newTextureViewWithPixelFormat:vf textureType:MTLTextureType2D levels:NSMakeRange(w->level, 1) slices:NSMakeRange(w->slice, 1)]; }
    if (!b || !pb || (w->dec == 1 && !v)) { RED_ONCE(114, "no BC: drawn blocks not decoded: no buffer or view (first case)"); return; }
    res_add(b); res_add(pb);
    uint32_t *p = pb.contents;
    if (w->dec == 1) { p[0] = 0; p[1] = (uint32_t)row; p[2] = (uint32_t)w->lw; p[3] = (uint32_t)w->lh; p[4] = w->fmt; p[5] = (uint32_t)w->plane;   // bcd: off, pitch, w, h, fmt, z
#ifdef DM_TEST_BLKUNP_3D_PLANE0   // negative control of t_blkunp only: every plane decoded into plane 0
                       p[5] = 0;
#endif
    }
    else { p[0] = 0; p[1] = (uint32_t)row; p[2] = (uint32_t)w->bx; p[3] = (uint32_t)w->by; p[4] = (uint32_t)size; p[5] = w->fmt; }   // b1a: src, pitch, bw, bh, dst, rgba
    if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    BENC e = blit();
    enc_b_copy_texture_buffer(e, w->aux, w->slice, w->level, MTLOriginMake(0, 0, w->plane), MTLSizeMake(w->bx, w->by, 1), b, 0, row, size);
    enc_b_end(g_blit); g_blit = nil;
    CENC ce = (CENC)enc_c_begin(NULL);
#if DM_NVN_M3
    [ce setBuffer:b offset:0 atIndex:0]; [ce setBuffer:pb offset:0 atIndex:1]; if (v) [ce setTexture:v atIndex:0];
#else
    [ce setArgumentTable:at]; [at setAddress:b.gpuAddress atIndex:0]; [at setAddress:pb.gpuAddress atIndex:1]; if (v) [at setTexture:v.gpuResourceID atIndex:0];
#endif
    enc_c_pso(ce, w->dec == 1 ? (is3d ? bcd3_pso() : bcd_pso()) : b1a_pso()); enc_c_dispatch(ce, MTLSizeMake((w->bx + 7) / 8, (w->by + 7) / 8, 1), MTLSizeMake(8, 8, 1)); enc_c_end(ce);
    if (w->dec == 2) { BENC e2 = blit(); enc_b_copy_buffer_texture(e2, b, size, w->bx * 16, w->bx * 16 * w->by, MTLSizeMake(w->lw, w->lh, 1), w->dst, w->slice, w->level, MTLOriginMake(0, 0, w->plane));
                       enc_b_end(g_blit); g_blit = nil; }
    args_dirty_all();
    defer_obj_release(b); defer_obj_release(pb); if (v) defer_obj_unref(v);
}
// GPU: texture -> texture (previously mislisted as a no-op, 3 times per frame on the character select page). Same size, not mirrored, same format -> blit layer by layer; the other semantics (scaling with filtering, mirroring, format change) cannot be expressed by blit: report a failure, to be implemented
static void copy_t2t(const p_copy_t2t *c) {
    if (c->src) xv_check(c->src->serial, c->has_sv && c->sv.set_levels ? c->sv.base_level : 0, c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0, "texture copy source");
    rp_flush();   // Land the pending pure-clear render pass first (zero-source candidates only enter the set when the render pass ends); the blit() below would end the render pass anyway
    if (c->dst) zskip_del(c->dst->serial);
    if (c->src && c->dst && c->sr.xoffset == 0 && c->sr.yoffset == 0 && c->dr.xoffset == 0 && c->dr.yoffset == 0
        && c->sr.width == (int)c->src->d.width && c->sr.height == (int)c->src->d.height && c->dr.width == (int)c->dst->d.width && c->dr.height == (int)c->dst->d.height) {
        int sl = c->has_sv && c->sv.set_levels ? c->sv.base_level : 0, dl = c->has_dv && c->dv.set_levels ? c->dv.base_level : 0;
        int ss = c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0, ds = c->has_dv && c->dv.set_layers ? c->dv.min_layer : 0;
        if (zskip_has(c->src->serial, sl, ss)) zskip_add(c->dst->serial, dl, ds);
    }
    tex_gpu_write(c->dst);
    id<MTLTexture> s = tex_mtl(c->src), d = tex_mtl(c->dst);
    if (__builtin_expect(widen_get()->k != 1 || widen_get()->ky != 1, 0) && s && d) {   // the census (320e)
        int ws = tex_is_widened(s, c->src), wd = tex_is_widened(d, c->dst); g_wce[WCE_T2T_BOTH] += ws && wd; g_wce[WCE_T2T_SRC] += ws && !wd; g_wce[WCE_T2T_DST] += !ws && wd; }
    PD("T src=%llu dst=%llu %dx%dx%d→%dx%dx%d flags=%#x", c->src->serial, c->dst->serial, c->sr.width, c->sr.height, c->sr.depth, c->dr.width, c->dr.height, c->dr.depth, c->flags);
    if (!s || !d) return;
    unsigned sl = c->has_sv && c->sv.set_levels ? c->sv.base_level : 0, dl = c->has_dv && c->dv.set_levels ? c->dv.base_level : 0;
    unsigned sz0 = c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0, dz0 = c->has_dv && c->dv.set_layers ? c->dv.min_layer : 0;
    PD("t src=%llu lv%u sl%u xyz=%d,%d,%d dst=%llu lv%u sl%u xyz=%d,%d,%d size=%d,%d,%d", c->src->serial, sl, sz0, c->sr.xoffset, c->sr.yoffset, c->sr.zoffset,
       c->dst->serial, dl, dz0, c->dr.xoffset, c->dr.yoffset, c->dr.zoffset, c->sr.width, c->sr.height, c->sr.depth);
    int s3 = c->src->d.target == 2, d3 = c->dst->d.target == 2, nz = c->sr.depth ? c->sr.depth : 1;
    if (no_bc() && (s.pixelFormat == MTLPixelFormatASTC_4x4_LDR || s.pixelFormat == MTLPixelFormatASTC_4x4_sRGB) && (d.pixelFormat == MTLPixelFormatRGBA8Unorm || d.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB)
        && !s3 && !d3 && c->sr.width == c->dr.width && c->sr.height == c->dr.height && (d.usage & MTLTextureUsageShaderWrite)) {   // No BC: ASTC source -> decompressed-format destination (see copy_t2t_astc)
        int ok = 1;
        for (int z = 0; z < nz && ok; z++) ok = copy_t2t_astc(s, d, sl, sz0 + c->sr.zoffset + z, dl, dz0 + c->dr.zoffset + z, c->sr.xoffset, c->sr.yoffset, c->dr.xoffset, c->dr.yoffset, c->sr.width, c->sr.height);
        if (ok) return;
    }
    if (c->sr.width != c->dr.width || c->sr.height != c->dr.height || (c->sr.depth ? c->sr.depth : 1) != (c->dr.depth ? c->dr.depth : 1) || (c->flags & ~NVN_COPY_FLAGS_ENGINE_2D_BIT & ~NVN_COPY_FLAGS_LINEAR_FILTER_BIT)
        || s.pixelFormat != d.pixelFormat || s3 != d3) {
        RED_ONCE(33, "texture→texture copy shape not implemented: %dx%dx%d→%dx%dx%d flags %#x format %lu→%lu 3D %d→%d", c->sr.width, c->sr.height, c->sr.depth, c->dr.width, c->dr.height, c->dr.depth,
                 c->flags, (unsigned long)s.pixelFormat, (unsigned long)d.pixelFormat, s3, d3);
        return;
    }
    // Widened storage (plan item 320e): the regions are the game's texels; a texture whose storage is wider or taller has its own ratio at the level
    // (the Metal level's size over the game's), and the regions are mapped by it as scissors are (the start floored, the end ceiled; one size for
    // both, kept inside both levels). Two different ratios (widened to plain, plain to widened) would scale: reported once, copied as before
    long sx = c->sr.xoffset, sy = c->sr.yoffset, dx = c->dr.xoffset, dy = c->dr.yoffset, cw = c->sr.width, ch = c->sr.height;
#ifndef DM_TEST_WIDEN_COPY_GAME   // negative control of t_widen_copy only: the game's region copied as it is
    if (__builtin_expect(widen_get()->k != 1 || widen_get()->ky != 1, 0) && !s3 && !d3) {
        double rsx = (double)mip_extent(s.width, sl) / mip_extent(c->src->d.width, sl), rsy = (double)mip_extent(s.height, sl) / mip_extent(c->src->d.height, sl);
        double rdx = (double)mip_extent(d.width, dl) / mip_extent(c->dst->d.width, dl), rdy = (double)mip_extent(d.height, dl) / mip_extent(c->dst->d.height, dl);
        if (rsx != 1 || rsy != 1 || rdx != 1 || rdy != 1) {
            if (fabs(rsx - rdx) < 1e-9 && fabs(rsy - rdy) < 1e-9) {
                long x1 = (long)ceil((sx + cw) * rsx), y1 = (long)ceil((sy + ch) * rsy);
                sx = (long)floor(sx * rsx); sy = (long)floor(sy * rsy); dx = (long)floor(dx * rdx); dy = (long)floor(dy * rdy); cw = x1 - sx; ch = y1 - sy;
                cw = MIN(cw, MIN((long)mip_extent(s.width, sl) - sx, (long)mip_extent(d.width, dl) - dx)); ch = MIN(ch, MIN((long)mip_extent(s.height, sl) - sy, (long)mip_extent(d.height, dl) - dy));
                g_wce[WCE_T2T_MAPPED]++;
                if (cw <= 0 || ch <= 0) return;
            } else RED_ONCE(123, "texture copy between storage widened by different ratios (%.4g x %.4g → %.4g x %.4g, textures %llu → %llu %dx%d): the game's region copied as it is (first case)",
                            rsx, rsy, rdx, rdy, (unsigned long long)c->src->serial, (unsigned long long)c->dst->serial, c->sr.width, c->sr.height);
        }
    }
#endif
    BENC b = blit();
    if (s3)
        enc_b_copy_texture(b, s, 0, sl, MTLOriginMake(c->sr.xoffset, c->sr.yoffset, c->sr.zoffset), MTLSizeMake(c->sr.width, c->sr.height, nz), d, 0, dl, MTLOriginMake(c->dr.xoffset, c->dr.yoffset, c->dr.zoffset));
    else
        for (int z = 0; z < nz; z++)
            enc_b_copy_texture(b, s, sz0 + c->sr.zoffset + z, sl, MTLOriginMake((NSUInteger)sx, (NSUInteger)sy, 0), MTLSizeMake((NSUInteger)cw, (NSUInteger)ch, 1), d, dz0 + c->dr.zoffset + z, dl, MTLOriginMake((NSUInteger)dx, (NSUInteger)dy, 0));
}

// GPU: clear a texture region with the given value (previously mislisted as a no-op, called 5 times at initialization; the initial value of a Metal private texture is undefined, and the game relies on the cleared initial value).
// Whole level and whole face, all four channels written -> open one clear-only render pass per layer / depth slice (the hardware converts the clear value by the texture format, including integer and depth);
// view with changed format: clear on a view of that format; partial region, partial channels, compressed / packaged textures (no render target usage): report a failure, to be implemented
static void clear_tex(const p_clear_tex *c) {
    if (counts_on()) g_cnt_cleartex++;
    if (c->tex) zskip_del(c->tex->serial);
    tex_gpu_write(c->tex);
    id<MTLTexture> t = tex_mtl(c->tex);
    if (__builtin_expect(widen_get()->k != 1 || widen_get()->ky != 1, 0) && t && tex_is_widened(t, c->tex)) g_wce[WCE_CLEAR_WIDE]++;   // the census (320e)
    PD("X ser=%llu view format %#x region %d,%d,%d %dx%dx%d mask %#x %s %g,%g,%g,%g", c->tex->serial, c->has_view && c->view.set_format ? c->view.format : -1, c->r.xoffset, c->r.yoffset, c->r.zoffset, c->r.width, c->r.height, c->r.depth, c->mask,
       c->is_uint ? "ui" : "f", c->is_uint ? (double)c->c.u[0] : c->c.f[0], c->is_uint ? (double)c->c.u[1] : c->c.f[1], c->is_uint ? (double)c->c.u[2] : c->c.f[2], c->is_uint ? (double)c->c.u[3] : c->c.f[3]);
    if (!t) return;
    unsigned lv = c->has_view && c->view.set_levels ? c->view.base_level : 0, l0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    NSUInteger w = MAX(t.width >> lv, 1u), h = MAX(t.height >> lv, 1u);
    int is3d = t.textureType == MTLTextureType3D, nz = c->r.depth ? c->r.depth : 1;
    if (c->has_view && c->view.set_format) {   // View with changed format: the clear value is interpreted in the view format (all 5 of the game's calls are the ui version with an integer view), and the clear is done on a view of the same format
        uint32_t vpf = 0; uint8_t swz[4];
        if (!nvn_fmt_to_mtl(c->view.format, &vpf, swz) || !vpf) { RED_ONCE(42, "clear-texture view format %#x has no equivalent", c->view.format); return; }
        if (vpf != (uint32_t)t.pixelFormat && tex_stored_unpacked(c->tex)) { RED_ONCE(112, "clear-texture view format %#x of a BC texture kept as Metal %lu on a GPU without BC: not cleared (first case)", c->view.format, (unsigned long)t.pixelFormat); return; }
        if (tex_need_pfv(t.pixelFormat, (MTLPixelFormat)vpf) && !(t.usage & MTLTextureUsagePixelFormatView)) t = tex_upgrade_pfv(c->tex, t, (MTLPixelFormat)vpf);
        if (tex_need_pfv(t.pixelFormat, (MTLPixelFormat)vpf)) {
            id<MTLTexture> v = [t newTextureViewWithPixelFormat:(MTLPixelFormat)vpf];
            if (!v) { RED_ONCE(43, "clear-texture cannot create view: format %lu → %u", (unsigned long)t.pixelFormat, vpf); return; }
            t = v;
        }
    }
    // Widened storage (plan item 320e): a region of the game's whole level is the whole level of the storage
    int whole_game = !c->r.xoffset && !c->r.yoffset && (NSUInteger)c->r.width == mip_extent(c->tex->d.width, lv) && (NSUInteger)c->r.height == mip_extent(c->tex->d.height, lv);
#ifdef DM_TEST_WIDEN_CLEAR_GAME   // negative control of t_widen_copy only: the region compared with the storage's size alone, as before
    whole_game = 0;
#endif
    if (whole_game && (w != (NSUInteger)c->r.width || h != (NSUInteger)c->r.height)) g_wce[WCE_CLEAR_MAPPED]++;
    MTLPixelFormat pf = t.pixelFormat;
    int depth = pf == MTLPixelFormatDepth32Float || pf == MTLPixelFormatDepth16Unorm || pf == MTLPixelFormatDepth32Float_Stencil8, stencil = pf == MTLPixelFormatStencil8 || pf == MTLPixelFormatDepth32Float_Stencil8;
    if ((!whole_game && (c->r.xoffset || c->r.yoffset || (NSUInteger)c->r.width != w || (NSUInteger)c->r.height != h)) || (c->mask & 0xf) != 0xf
        || !(t.usage & MTLTextureUsageRenderTarget)) {
        RED_ONCE(44, "clear-texture shape not implemented: region %d,%d %dx%d (this level %lux%lu) mask %#x renderable %d", c->r.xoffset, c->r.yoffset, c->r.width, c->r.height,
                 (unsigned long)w, (unsigned long)h, c->mask, !!(t.usage & MTLTextureUsageRenderTarget));
        return;
    }
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    for (int z = 0; z < nz; z++) {
        RPD *rp = [RPD new];
        MTLRenderPassAttachmentDescriptor *a = depth ? rp.depthAttachment : stencil ? rp.stencilAttachment : rp.colorAttachments[0];
        a.texture = t; a.level = lv; a.loadAction = MTLLoadActionClear; a.storeAction = MTLStoreActionStore;
        if (is3d) a.depthPlane = c->r.zoffset + z; else a.slice = l0 + c->r.zoffset + z;
        if (depth) { rp.depthAttachment.clearDepth = t.pixelFormat == MTLPixelFormatDepth16Unorm ? nvn_depth16_clear_value(c->c.f[0]) : c->c.f[0];
            if (stencil) { rp.stencilAttachment.texture = t; rp.stencilAttachment.level = lv; rp.stencilAttachment.slice = a.slice; rp.stencilAttachment.loadAction = MTLLoadActionClear;
                           rp.stencilAttachment.storeAction = MTLStoreActionStore; rp.stencilAttachment.clearStencil = c->c.u[1]; } }
        else if (stencil) rp.stencilAttachment.clearStencil = c->c.u[0];
        else rp.colorAttachments[0].clearColor = c->is_uint ? MTLClearColorMake(c->c.u[0], c->c.u[1], c->c.u[2], c->c.u[3]) : MTLClearColorMake(c->c.f[0], c->c.f[1], c->c.f[2], c->c.f[3]);
        enc_r_end(enc_r_begin(rp));
    }
}

