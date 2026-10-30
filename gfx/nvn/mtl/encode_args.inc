// gfx/nvn/mtl/encode_args.inc, part of nvn_mtl.m (included there in order, one translation unit): Draws that sample what their own pass draws (plan item 42), argument encoding
// ---------- Draws that sample what their own pass draws (plan item 42; after tomodachi-native's attachment copies 4dbd30f / da08d11, MIT) ----------
// Tears of the Kingdom samples the level and layer a pass draws into in about 59 draws a frame in the prologue (full-screen passes at 1600x896,
// 800x448 and 400x224; DM_NVN_COUNTS). In a Metal render pass that is undefined; on the M5 such a draw reads the attachment as it was in memory
// before the pass: without the earlier draws of the pass (still in tile memory) and without a clear still pending when the pass opened. NVN defines
// only the case with a texture-invalidating barrier between the earlier draws and the read, which makes their results visible; without one, what
// the console shows is what the M5 reads: copying for every such draw (results of earlier draws and clears visible, tomodachi-native's choice for
// its game) put the prologue's shots outside the console's brightness range in two shots out of the video of the opening (s1300 / s1800: left
// third 39.0 against 44.2-54.5), the M5's own reads and the copies after a barrier inside it. So by default only a draw after such a barrier samples
// a copy: the pass ends there (the earlier draws stored), the whole texture is copied, the pass opens again loading it. DM_NVN_FBX=all copies for
// every such draw (a copy serves until a draw writes the attachment, a pending clear is applied first), DM_NVN_FBX=0 never. Every encoder starts
// with a full barrier, so one copy texture per shape is reused within a submission. Levels and layers the pass does not draw are not copied for
// (a downsample chain reading level k-1)
typedef struct { __strong id<MTLTexture> copy, src; uint64_t draw, sub; __strong id<MTLTexture> vsrc[4], view[4]; int nv; } fbx_ent;
static fbx_ent g_fbx[16]; static int g_nfbx;
static int g_fbx_act[9];   // this draw's: indices into g_fbx (g_fbx_nact of them, declared at rp_end), set when its pass is open (fbx_activate)
static int g_fbx_pend[9], g_fbx_npend;   // fbx_prepare's picks for the draw being prepared
static int g_fbx_on = -1;   // 0 off, 1 after a texture barrier (default), 2 all, 3 split (DM_NVN_FBX=split, plan item 165)
static uint64_t g_fbx_bar_at;   // g_stat_draw at the last barrier that invalidates textures (NVN_BARRIER_INVALIDATE_TEXTURE_BIT)
static uint64_t g_fbx_copies, g_fbx_draws, g_fbx_refused, g_fbx_splits, g_fbx_split_clears;
void mtl_fbx_stats(uint64_t out[3]) { out[0] = g_fbx_draws; out[1] = g_fbx_copies; out[2] = g_fbx_refused; }
void mtl_fbx_split_stats(uint64_t out[2]) { out[0] = g_fbx_splits; out[1] = g_fbx_split_clears; }
// The bound texture t, or the same view of it, on the copy of the attachment it is (nil: not this draw's attachment)
static id<MTLTexture> fbx_sub(id<MTLTexture> t, uint64_t *rid) {
    for (int a = 0; a < g_fbx_nact; a++) { fbx_ent *e = &g_fbx[g_fbx_act[a]];
        if (t == e->src) { *rid = e->copy.gpuResourceID._impl; return e->copy; }
        if (t.parentTexture != e->src) continue;
        for (int i = 0; i < e->nv; i++) if (e->vsrc[i] == t) { *rid = e->view[i].gpuResourceID._impl; return e->view[i]; }
        NSUInteger sl = t.textureType == MTLTextureTypeCube || t.textureType == MTLTextureTypeCubeArray ? t.arrayLength * 6 : t.textureType == MTLTextureType3D ? 1 : t.arrayLength;
        id<MTLTexture> v = [e->copy newTextureViewWithPixelFormat:t.pixelFormat textureType:t.textureType levels:NSMakeRange(t.parentRelativeLevel, t.mipmapLevelCount)
                                                           slices:NSMakeRange(t.textureType == MTLTextureType3D ? 0 : t.parentRelativeSlice, sl) swizzle:t.swizzle];
        if (!v) { RED_ONCE(96, "attachment copy: no view of the copy like the bound one (format %lu type %lu); the draw samples the attachment", (unsigned long)t.pixelFormat, (unsigned long)t.textureType); return nil; }
        int k = e->nv < 4 ? e->nv++ : 3; if (e->view[k]) defer_obj_unref(e->view[k]);
        e->vsrc[k] = t; e->view[k] = v; *rid = v.gpuResourceID._impl; return v;
    }
    return nil;
}
static int encode_args(id<MTLCommandEncoder> en, NvShader *sh, int nvn_stage, int metal_vertex) {
    if (!sh->ae) return 1;
    uint64_t slots[NVN_REFL_MAXB] = {0};
    args_sync_dirty();
    if (!passdump() && !g_args_dirty[nvn_stage] && sh->ab_buf && sh->ab_pass == g_pass.serial && nvn_stage != 5 && !sh->rf.ubo_addr) {   // ubo_addr: an address read from uniform data can change without a binding change
        bind_args(en, nvn_stage, metal_vertex, sh->ab_buf, sh->ab_gpu, sh->ab_off, sh->rf.abi);
        g_stat_args_reuse++;
        return 1;
    }
#if DM_NVN_M3
    // Older API: every resource a shader accesses indirectly through an argument block must have its usage declared on this encoder, so that the system knows its ordering dependency against other encoders (upload copies, other passes);
    // without the declaration a read-only texture gets out of order with the copy that writes it (measured before the move to Metal 4). Repeated declarations within one encoder are idempotent
    #define USE(r, u) do { id<MTLResource> r_ = (id<MTLResource>)(r); if (r_) { if (nvn_stage == 5) [(id<MTLComputeCommandEncoder>)en useResource:r_ usage:(u)]; else if (!m3_use_seen((__bridge void *)r_, (uint32_t)(u))) [(id<MTLRenderCommandEncoder>)en useResource:r_ usage:(u) stages:MTLRenderStageVertex | MTLRenderStageFragment]; } \
        if (__builtin_expect(g_res_check_on, 0)) res_check((r), sh->key, reg); } while (0)
    #define USE_SAMP(r, u) do { (void)(u); if (__builtin_expect(g_res_check_on, 0)) res_check((r), sh->key, reg); } while (0)   // samplers are not resources and need no declaration
#else
    #define USE(r, u) do { (void)(u); if (__builtin_expect(g_res_check_on, 0)) res_check((r), sh->key, reg); } while (0)
    #define USE_SAMP(r, u) USE(r, u)
#endif
    // DM_NVN_PASSDUMP: on the first new encode per pass per stage, records the shader key (K) and the actual source of each binding (B: UBO address / length / first 4 floats, texture ID and serial, or "dummy")
    static uint64_t bl_pass[6]; static int bl_all = -1; if (bl_all < 0) bl_all = getenv("DM_NVN_PASSDUMP_ALL") != NULL;   // Record on every new encode (otherwise only the first time per pass per stage)
    int bl = passdump() && (bl_all || bl_pass[nvn_stage] != g_stat_pass);
    if (bl) { bl_pass[nvn_stage] = g_stat_pass; PD("K %llu st%d key=%s nb=%d", g_stat_pass, nvn_stage, sh->key, sh->rf.nb); }
    pthread_mutex_lock(&g_rmu);   // The texture / sampler registration tables may be being grown or rewritten by the game thread
    for (int i = 0; i < sh->rf.nb; i++) {
        const nvn_rbind *b = &sh->rf.b[i];
        int reg = b->reg, aid = b->aid;
        if (b->kind == NVN_RB_CBUF) {
            const p_bind_buf *u = reg < 16 ? &S.ubo[nvn_stage][reg] : NULL;
            void *mb; uint64_t mo, gpu;
            if (!u || !u->addr || !mtl_gpu_addr(u->addr, 1, &mb, &mo, &gpu)) { RED_ONCE(16, "stage %d cb%d has no uniform buffer bound", nvn_stage, reg); continue; }
            use_rec("buf", u->addr, u->size, 0, NULL);
            if (bl) {
                PD("B %llu st%d cb reg%d addr=%#llx size=%llu pool=%#llx+%#llx f=%g,%g,%g,%g", g_stat_pass, nvn_stage, reg, (unsigned long long)u->addr, (unsigned long long)u->size,
                   (unsigned long long)(u->addr - mo), (unsigned long long)mo, ((float *)u->addr)[0], ((float *)u->addr)[1], ((float *)u->addr)[2], ((float *)u->addr)[3]);
                // DM_NVN_CBDUMP=directory: also saves the whole uniform block to disk as-is (F<frame>_p<pass>_st<stage>_cb<slot>_<address>.bin), for the ground-truth side to compare byte by byte by guest address
                static const char *cbd; static int cbd_init; if (!cbd_init) { cbd = getenv("DM_NVN_CBDUMP"); cbd_init = 1; }
                if (cbd) {
                    char p[1024]; snprintf(p, sizeof p, "%s/F%llu_p%llu_st%d_cb%d_%llx.bin", cbd, (unsigned long long)atomic_load(&g_cur_frame), g_stat_pass, nvn_stage, reg, (unsigned long long)u->addr);
                    FILE *f = fopen(p, "wb"); if (f) { fwrite((const void *)u->addr, 1, u->size, f); fclose(f); } else RED_ONCE(30, "CBDUMP cannot write %s", p);
                }
            }
            void *rb_ = NULL; uint64_t rg_ = upd_any() ? upd_shadow(u->addr, u->size, &rb_) : 0;   // logged updates over it: its private copy
            const us_ent *sc = rg_ ? NULL : us_find(u->addr, u->size);   // contents as of the submit call
            if (rg_) { gpu = rg_; mb = rb_; } else if (sc) { gpu = sc->gpu + (u->addr - sc->lo); mb = sc->buf; }
            slots[aid] = gpu;
            USE((__bridge id<MTLBuffer>)mb, MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_SAMP || b->kind == NVN_RB_SAMPT) {   // SAMPT: the sampler part of a combined handle (texture ID | sampler ID << 20)
            int sid = reg < 128 ? h_sid(b->kind == NVN_RB_SAMPT ? S.tex[nvn_stage][reg] : S.samp[nvn_stage][reg]) : 0;
            __unsafe_unretained id<MTLSamplerState> ss = sid ? (__bridge id<MTLSamplerState>)samp_fast(sid) : nil;
            slots[aid] = sid && sid < g_nsreg && g_srid[sid] ? g_srid[sid] : dummy_samp_rid();
            USE_SAMP(ss ? ss : dummy_samp(), MTLResourceUsageRead);
            if (bl && sid > 0) { int ok = sid < g_nsreg && g_sreg_ok[sid]; samp_par sp = ok ? g_sreg[sid] : (samp_par){0};   // g_rmu is already held in this section
                if (ok) PD("B %llu st%d samp reg%d sid=%d mag=%d min=%d (0 nearest / 1 bilinear; 2–5 with mip) lod %g..%g bias %g wrap %d,%d", g_stat_pass, nvn_stage, reg, sid, (int)sp.magf, (int)sp.minf, sp.lod_min, sp.lod_max, sp.lod_bias, (int)sp.ws, (int)sp.wt); }
        } else if (b->kind == NVN_RB_TEX) {
            int tid = reg < 128 ? h_tid(S.tex[nvn_stage][reg]) : 0;
            void *tv = NULL; uint64_t trid = tid ? tex_fast_rid(tid, &tv) : 0; __unsafe_unretained id<MTLTexture> t = (__bridge id<MTLTexture>)tv;
            if (t && tid < g_ntreg) use_rec("tex", (uint64_t)g_treg[tid].t.mem, g_treg[tid].t.size, tid, &g_treg[tid].t);
            if (t && passdump() && tid < g_ntreg) PD("R %llu st%d ser=%llu", g_stat_pass, nvn_stage, g_treg[tid].t.serial);
            if (bl) PD("B %llu st%d tex reg%d tid=%d %s ser=%llu fmt=%lu %lux%lux%lu view_lv=%lu+%lu view_sl=%lu array=%lu target=%lu", g_stat_pass, nvn_stage, reg, tid, t ? "real" : "dummy", t && tid < g_ntreg ? g_treg[tid].t.serial : 0ull,
                       (unsigned long)t.pixelFormat, (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.depth,
                       (unsigned long)(t.parentTexture ? t.parentRelativeLevel : 0), (unsigned long)t.mipmapLevelCount,
                       (unsigned long)(t.parentTexture ? t.parentRelativeSlice : 0), (unsigned long)t.arrayLength, (unsigned long)t.textureType);
            if (bl && t && tid < g_ntreg && g_treg[tid].t.mem && getenv("DM_NVN_TEXNZ")) {   // diagnostic: share of non-zero 8-byte words in the texture's guest storage
                const uint64_t *w = (const uint64_t *)(const void *)g_treg[tid].t.mem; size_t nw = MIN((size_t)g_treg[tid].t.size, (size_t)64 << 20) / 8, nz = 0;
                for (size_t i = 0; i < nw; i++) nz += w[i] != 0;
                PD("B %llu st%d tex reg%d storage %p size %#llx non-zero words %.4f", g_stat_pass, nvn_stage, reg, (const void *)g_treg[tid].t.mem, (unsigned long long)g_treg[tid].t.size, nw ? (double)nz / nw : 0.0);
                if (t.pixelFormat >= MTLPixelFormatBC1_RGBA && t.pixelFormat <= MTLPixelFormatBC7_RGBAUnorm_sRGB && t.textureType == MTLTextureType2D) {   // and of the Metal texture's level 0 (read back now, from a command buffer of its own)
                    size_t blk = t.pixelFormat <= MTLPixelFormatBC1_RGBA_sRGB || t.pixelFormat == MTLPixelFormatBC4_RUnorm || t.pixelFormat == MTLPixelFormatBC4_RSnorm ? 8 : 16;
                    NSUInteger tw = t.width, th = t.height; size_t row = (tw + 3) / 4 * blk, img = row * ((th + 3) / 4);
                    id<MTLBuffer> rb = [g_dev newBufferWithLength:img options:MTLResourceStorageModeShared];
                    id<MTLCommandBuffer> rc = q_cb(); id<MTLBlitCommandEncoder> be = [rc blitCommandEncoder];
                    [be copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(tw, th, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:img];
                    [be endEncoding]; [rc commit]; [rc waitUntilCompleted];
                    const uint64_t *mw = (const uint64_t *)rb.contents; size_t mn = img / 8, mz = 0; for (size_t i = 0; i < mn; i++) mz += mw[i] != 0;
                    PD("B %llu st%d tex reg%d Metal level 0 %lux%lu non-zero words %.4f", g_stat_pass, nvn_stage, reg, (unsigned long)tw, (unsigned long)th, mn ? (double)mz / mn : 0.0);
                }
            }
            if (t && t.textureType != (MTLTextureType)b->decl_tt) { ttm_note(sh->key, nvn_stage, reg, (MTLTextureType)b->decl_tt, t, tid); uint64_t r2 = 0; id<MTLTexture> fv = tex_fit(tid, t, (MTLTextureType)b->decl_tt, &r2); if (fv) { t = fv; trid = r2; } }
            if (t) t = fxp_sub(nvn_stage, tid, t, &trid);   // the compose pass: the scene MetalFX upscaled (plan item 63)
            if (t && g_fbx_nact) { uint64_t r2 = 0; id<MTLTexture> c = fbx_sub(t, &r2); if (c) { t = c; trid = r2; } }   // the draw samples its own attachment: the copy
            if (!t) { t = dummy_tex(b->dtt, b->dpf); trid = g_dummy_tex_rid[b->dtt & 7][b->dpf & 3]; }
            slots[aid] = trid;
            USE(t, MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_IMG) {   // Writable texture: NVN image slot r (BindImages) ↔ D3D u_r
            int tid = reg < 32 ? h_tid(S.img[nvn_stage][reg]) : 0;
            if (b->rw && tid && tid < g_ntreg) tex_gpu_write(&g_treg[tid].t);   // an image store writes the texture
            void *tv = NULL; uint64_t trid = tid ? tex_fast_rid(tid, &tv) : 0; __unsafe_unretained id<MTLTexture> t = (__bridge id<MTLTexture>)tv;
            if (passdump()) PD("W %llu st%d reg%d tid=%d %s ser=%llu", g_stat_pass, nvn_stage, reg, tid, t ? "real" : "dummy", t && tid < g_ntreg ? g_treg[tid].t.serial : 0ull);
            if (t && !(t.usage & MTLTextureUsageShaderWrite)) { RED_ONCE(32, "texture bound to an image slot lacks the shader-write usage (texture ID %d)", tid); t = nil; }
            if (!t) { t = dummy_img(b->dtt, b->dpf); trid = g_dummy_img_rid[b->dtt & 7][b->dpf & 3]; }   // Unbound: D3D semantics discard the write - write into a dedicated dummy texture so the set that reads 0 is not polluted
            slots[aid] = trid;
            USE(t, MTLResourceUsageRead | MTLResourceUsageWrite);
        } else if (b->kind == NVN_RB_TBUF) {   // Typed buffer: the buffer texture (target BUFFER) in texture slot r
            int tid = reg < 128 ? h_tid(S.tex[nvn_stage][reg]) : 0;
            void *tv = NULL; uint64_t trid = tid ? tex_fast_rid(tid, &tv) : 0; __unsafe_unretained id<MTLTexture> t = (__bridge id<MTLTexture>)tv;
            if (t && t.textureType != MTLTextureTypeTextureBuffer) { RED_ONCE(51, "texture bound to a typed buffer slot is not a buffer texture (texture ID %d type %lu)", tid, (unsigned long)t.textureType); t = nil; }
            if (bl) PD("B %llu st%d tbuf reg%d tid=%d %s ser=%llu", g_stat_pass, nvn_stage, reg, tid, t ? "real" : "dummy", t && tid < g_ntreg ? g_treg[tid].t.serial : 0ull);
            if (!t) { t = dummy_tbuf(b->dpf); trid = g_dummy_tbuf_rid[b->dpf & 3]; }
            slots[aid] = trid;
            USE(t, MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_SBUFP || b->kind == NVN_RB_UBOP) {   // B: storage buffers / compute-stage uniforms are device pointers; the address is taken by NVN slot
            const p_bind_buf *u = reg < 16 ? (b->kind == NVN_RB_UBOP ? &S.ubo[nvn_stage][reg] : &S.ssbo[nvn_stage][reg]) : NULL;
            void *mb; uint64_t mo, gpu;
            if (b->kind == NVN_RB_SBUFP && b->rw && u && u->addr) us_drop(u->addr, u->size ? u->size : 1);   // the shader may write it
            void *rb_ = NULL; uint64_t rg_ = b->kind == NVN_RB_UBOP && u && u->addr && upd_any() ? upd_shadow(u->addr, u->size, &rb_) : 0;   // with the submission's updates (see upd_any)
            const us_ent *sc = !rg_ && b->kind == NVN_RB_UBOP && u ? us_find(u->addr, u->size) : NULL;   // compute uniforms as of the submit call
            if (rg_) { slots[aid] = rg_; USE((__bridge id<MTLBuffer>)rb_, MTLResourceUsageRead); }
            else if (sc) { slots[aid] = sc->gpu + (u->addr - sc->lo); USE((__bridge id<MTLBuffer>)sc->buf, MTLResourceUsageRead); }
            else if (u && u->addr && (b->kind == NVN_RB_SBUFP ? sbuf_gpu_addr(u->addr, u->size, &mb, &mo, &gpu) : mtl_gpu_addr(u->addr, u->size ? u->size : 1, &mb, &mo, &gpu))) { use_rec("buf", u->addr, u->size, 0, NULL); slots[aid] = gpu; USE((__bridge id<MTLBuffer>)mb, b->rw ? MTLResourceUsageRead | MTLResourceUsageWrite : MTLResourceUsageRead); }
            else { slots[aid] = dummy_buf_gpu(); USE(dummy_buf(), MTLResourceUsageRead); }
            if (bl) PD("B %llu st%d sbufp reg%d %s addr=%#llx size=%llu %s", g_stat_pass, nvn_stage, reg, u && u->addr ? "real" : "dummy", u ? (unsigned long long)u->addr : 0ull, u ? (unsigned long long)u->size : 0ull, b->rw ? "rw" : "ro");
        } else if (b->kind == NVN_RB_SBUFU) {   // B: a storage buffer the shader addresses from uniform slot r: {address u64, size u32} at byte off (the translator's cb slot r + 3)
            const p_bind_buf *u = reg < 16 ? &S.ubo[nvn_stage][reg] : NULL; uint8_t rec[12] = {0}; uint64_t sa = 0; uint32_t sn = 0;
            int ok = u && u->addr && b->off + 12 <= u->size && pend_read(u->addr + b->off, rec, 12);
            if (u && u->addr && !ok) RED_ONCE(77, "storage buffer address in uniform slot %d +%#x: a GPU copy earlier in the submission writes there, or it lies outside the binding (first case): dummy buffer", reg, b->off);
            if (ok) { memcpy(&sa, rec, 8); memcpy(&sn, rec + 8, 4); }
            if (sa && b->rw) us_drop(sa, sn ? sn : 65536);   // the shader may write it (no size given: 64 KiB)
            void *mb; uint64_t mo, gpu;
            if (sa && sbuf_gpu_addr(sa, sn, &mb, &mo, &gpu)) { use_rec("buf", sa, sn, 0, NULL); slots[aid] = gpu; USE((__bridge id<MTLBuffer>)mb, b->rw ? MTLResourceUsageRead | MTLResourceUsageWrite : MTLResourceUsageRead); }
            else { if (sa) RED_ONCE(78, "storage buffer address %#llx read from uniform slot %d +%#x is not in any memory pool (first case): dummy buffer", (unsigned long long)sa, reg, b->off);
                   slots[aid] = dummy_buf_gpu(); USE(dummy_buf(), MTLResourceUsageRead); }
            if (bl) PD("B %llu st%d sbufu ubo%d+%#x addr=%#llx size=%u %s", g_stat_pass, nvn_stage, reg, b->off, (unsigned long long)sa, sn, b->rw ? "rw" : "ro");
        } else if (b->kind == NVN_RB_C1) {      // B: the shader's own constant segment, built when the shader is fetched
            id<MTLBuffer> cb = sh->c1buf ? sh->c1buf : zero_bank();   // c1 has length 0 (600 of them in the whole library) yet the shader still declares it: reads 0
            slots[aid] = (sh->c1buf ? sh->c1_gpu : zero_bank_gpu()) + (sh->c1buf ? sh->c1off : 0);
            USE(cb, MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_SUPPORT) {   // support_buffer: scale 1.0, BGRA swap 0, viewport transform not disabled (viewport fields are not read)
            slots[aid] = support_gpu();
            USE(support_buf(), MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_DRV0 || b->kind == NVN_RB_DRV2) {
            // NVN driver constant segments: those proven unread by static analysis at packaging time are supplied as 0; those read are filled on the spot from the current bindings - c0 = storage buffer table (base + 0x10 r: address 64 bits, size 32 bits),
            // c2 = texture handle table and sampler handle table (bases from the game's nvn_c2, 8 bytes per slot), the values being the NVN handles the game gave at bind time; positions read outside the tables are registered at packaging time and reported a failure at load time
            int bank = b->kind == NVN_RB_DRV2 ? 2 : 0;
            if (!sh->rf.drv_read[bank]) { slots[aid] = zero_bank_gpu(); USE(zero_bank(), MTLResourceUsageRead); }
            else {
                ARENA_OUT id<MTLBuffer> db; size_t doff = arena_alloc(0xA00, &db); uint8_t *d = g_cur_arena->cpu + doff;
                drv_fill(d, bank, nvn_stage);
                if (bank == 0) for (int r = 0; r < 16 && nvn_stage < 6; r++) { uint64_t a = S.ssbo[nvn_stage][r].addr; uint32_t n = (uint32_t)S.ssbo[nvn_stage][r].size;
                    if (a && n) us_drop(a, n); }   // reached through the table: whether the shader writes it is not known
                slots[aid] = g_cur_arena->gpu + doff;
                USE(db, MTLResourceUsageRead);
            }
        } else RED_ONCE(46, "binding kind %d not implemented", b->kind);
    }
    #undef USE
    #undef USE_SAMP
    pthread_mutex_unlock(&g_rmu);
    NSUInteger arg_len = sh->arg_len ? sh->arg_len : sh->ae.encodedLength;
    ARENA_OUT id<MTLBuffer> ab; size_t off = arena_alloc(arg_len, &ab);
    memcpy(g_cur_arena->cpu + off, slots, arg_len);
    bind_args(en, nvn_stage, metal_vertex, ab, g_cur_arena->gpu + off, off, sh->rf.abi);
    sh->ab_buf = ab; sh->ab_gpu = g_cur_arena->gpu + off; sh->ab_off = off; sh->ab_pass = g_pass.serial; g_args_dirty[nvn_stage] = 0;
    g_stat_args_enc++;
    return 1;
}
static id<MTLBuffer> g_quad_ib; static uint64_t g_quad_gpu;
static id<MTLBuffer> g_qs_ib;   // quad strips' shared indices (draw.inc)
static RENC rp_begin(void);
