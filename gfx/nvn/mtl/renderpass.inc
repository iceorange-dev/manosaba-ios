// gfx/nvn/mtl/renderpass.inc, part of nvn_mtl.m (included there in order, one translation unit): Render passes: encoder labels, block-compressed targets, attachments, pass begin / end, clears
// DM_NVN_ENC_LABEL=1: label every render encoder with 'p<index of the render pass within the frame> <width>x<height> <first 8 digits of the first draw's fragment key> d<draw count>'; in Instruments (xctrace Game Performance / Metal System Trace)
// the encoder track and the GPU interval table carry this name directly, and shader_map.tsv maps it back to the shader; off by default (one string allocation per pass)
static int g_el_w, g_el_h, g_el_draws; static char g_el_key[12];
static int g_nblk_wb; static void blk_writeback(void);
static int enc_label_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_NVN_ENC_LABEL"); v = e && atoi(e); } return v; }
static inline id<MTLTexture> fxp_sub(int stage, int tid, id<MTLTexture> t, uint64_t *rid);   // (plan item 63, with fxp_scene)
static int g_fbx_nact, g_fbx_self; static inline void args_dirty_all(void);   // g_fbx_self: this draw samples what its pass draws (fbx_prepare)
static void rp_end(void) { if (g_fbx_nact) { g_fbx_nact = 0; args_dirty_all(); }   // the attachment copies of a draw stand in only for it (fbx_prepare)
    if (g_enc) {
    if (g_cnt_on > 0 && g_stat_draw == g_cnt_pass_d0) g_cnt_nodraw++;
    if (g_cnt_on > 0 && g_cnt_ext_x) { uint64_t tw = g_pass.tw, th = g_pass.th, ew = MIN(g_cnt_ext_x, tw), eh = MIN(g_cnt_ext_y, th); g_cnt_rp++; g_cnt_rp_px += tw * th;
        if (ew < tw || eh < th) { if (g_cnt_ext_clr) g_cnt_rp_outc += tw * th - ew * eh; else g_cnt_rp_out += tw * th - ew * eh;
            if ((g_cnt_ext_clr ? g_cnt_rp_subc++ : g_cnt_rp_sub++) < 12) MLOG("[counts] pass %llu %llux%llu%s: its draws reach %llux%llu (%d colour targets, depth %d)", (unsigned long long)g_stat_pass,
                (unsigned long long)tw, (unsigned long long)th, g_cnt_ext_clr ? " (a clear on load)" : "", (unsigned long long)ew, (unsigned long long)eh, R.rt.n, R.rt.depth != NULL); } }
    g_cnt_ext_x = g_cnt_ext_y = 0;
    if (enc_label_on()) { static uint64_t lf = ~0ull, base; uint64_t f = atomic_load(&g_cur_frame); if (f != lf) { lf = f; base = g_stat_pass - 1; }   // Intra-frame serial: the same pass has the same name every frame, so the table can aggregate by name
        [(id)g_enc setLabel:[NSString stringWithFormat:@"p%02llu %dx%d %s d%d", (unsigned long long)(g_stat_pass - base), g_el_w, g_el_h, g_el_key[0] ? g_el_key : "-", g_el_draws]]; }
    sd_finish(); pg_render_end(g_enc); enc_r_end(g_enc); zskip_commit_clears(); g_enc = nil; g_rt_merged = 0; g_lay.active = 0; ts_after(); if (g_lp.count) lp_finish(); fz_end();
    if (g_nblk_wb) blk_writeback();   // blocks rendered into auxiliary textures → the compressed textures
} }
// A compressed texture as a render target (Tears of the Kingdom, as nxnative found in Breath of the Wild: a BC4 texture written through an RGBA16UI
// view, one texel = one 8-byte block, i.e. BC4 encoded on the GPU): Metal can neither attach a compressed format as a colour attachment nor make
// compressed <-> uncompressed views (pipeline descriptor validation aborts the process). The pass renders into an auxiliary texture in the view's
// format, one texel per block, and the blocks are copied into the compressed texture through a buffer when the pass ends (blk_aux; nxnative, seedds,
// MIT; MacNativeGames, ly05010419, MIT). Any other view of a compressed render target is left out with a red line (its draws land nowhere)
static int mtl_fmt_block(MTLPixelFormat f) { return (f >= MTLPixelFormatBC1_RGBA && f <= MTLPixelFormatBC7_RGBAUnorm_sRGB) || (f >= MTLPixelFormatPVRTC_RGB_2BPP && f < MTLPixelFormatGBGR422); }
static int mtl_bc_block_bytes(MTLPixelFormat f) {   // bytes per 4x4 block of a BC format, 0 for other formats
    switch (f) { case MTLPixelFormatBC1_RGBA: case MTLPixelFormatBC1_RGBA_sRGB: case MTLPixelFormatBC4_RUnorm: case MTLPixelFormatBC4_RSnorm: return 8;
        case MTLPixelFormatBC2_RGBA: case MTLPixelFormatBC2_RGBA_sRGB: case MTLPixelFormatBC3_RGBA: case MTLPixelFormatBC3_RGBA_sRGB: case MTLPixelFormatBC5_RGUnorm:
        case MTLPixelFormatBC5_RGSnorm: case MTLPixelFormatBC6H_RGBFloat: case MTLPixelFormatBC6H_RGBUfloat: case MTLPixelFormatBC7_RGBAUnorm: case MTLPixelFormatBC7_RGBAUnorm_sRGB: return 16;
        default: return 0; }
}
// The format a BC render target is drawn in: the view's, when it is uncompressed with one block per texel (0 otherwise)
static MTLPixelFormat blk_view_fmt(id<MTLTexture> base, const view_t *v, int has_v) {
    int bb = base ? mtl_bc_block_bytes(base.pixelFormat) : 0, vbw = 0, vbh = 0; uint32_t vpf = 0; uint8_t swz[4];
    if (!bb || !has_v || !v->set_format || nvn_fmt_bpb(v->format, &vbw, &vbh) != bb || vbw != 1 || vbh != 1 || !nvn_fmt_to_mtl(v->format, &vpf, swz) || !vpf || mtl_fmt_block((MTLPixelFormat)vpf)) return MTLPixelFormatInvalid;
    return (MTLPixelFormat)vpf;
}
// The format a colour attachment is actually attached with: the view's format if the view changes the format and it can be attached, otherwise the base texture's format (shared by attaching and pipeline prediction, so both agree). warn: log red when it cannot be attached
// A BC texture kept decompressed or as ASTC on a GPU without BC (bc_unpack, bc1_astc) has no Metal view of another format: Tears of the Kingdom's
// BC encoders write their blocks through a one-texel-per-block integer view (an RGBA16UI view of BC4), and that view of an R8 texture aborts the
// process in Metal's view validation (found with DM_TEST_NO_BC=1). Such a view is not made: the attachment or clear is left out, reported once
// (the blocks would need decoding into the texture: 62c3)
static int tex_stored_unpacked(const tex_t *t) {
#ifdef DM_TEST_UNPACKED_VIEW   // negative control of t_mtlguard only: views made as before (Metal aborts)
    (void)t; return 0;
#endif
    uint32_t upf; int ubpp, r_, s_; return t && (bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &r_, &s_));
}
// A BC texture kept unpacked, drawn through a one-texel-per-block view (plan item 62c3): its blocks go into an auxiliary texture as with BC, which
// stays the texture's compressed content, and after the pass they are decoded into it (blk_decode): BC1-5 by bcd into the decompressed texture,
// BC1 kept as ASTC by b1a into ASTC blocks. BC6H / BC7 (decoded on the CPU) are left out as before
static int blk_dec_kind(const tex_t *t, uint32_t *fmt) {   // 1: bcd (fmt its format), 2: b1a (fmt: the RGBA variant), 0: none
#ifdef DM_TEST_BLKUNP_OFF   // negative control of t_blkunp only: left out, as before
    (void)t; (void)fmt; return 0;
#endif
    int rgba, srgb; uint32_t upf; int ubpp;
    if (bc1_astc(t->d.format, &rgba, &srgb)) { *fmt = (uint32_t)rgba; return b1a_pso() ? 2 : 0; }
    if (bc_unpack(t->d.format, &upf, &ubpp) && bcd_fmt(t->d.format)) { *fmt = (uint32_t)bcd_fmt(t->d.format); return bcd_pso() ? 1 : 0; }
    return 0;
}
static MTLPixelFormat blk_view_fmt_unp(const tex_t *t, const view_t *v, int has_v) {
    int bw, bh, bb = nvn_fmt_bpb(t->d.format, &bw, &bh), vbw = 0, vbh = 0; uint32_t vpf = 0, k; uint8_t swz[4];
    if (t->d.target == NVN_TEXTURE_TARGET_3D && !bcd3_pso()) return MTLPixelFormatInvalid;   // 3D: bcd3 writes its depth plane (left out without it: red 111)
    if (!bb || bw != 4 || bh != 4 || !has_v || !v->set_format || nvn_fmt_bpb(v->format, &vbw, &vbh) != bb || vbw != 1 || vbh != 1 || !nvn_fmt_to_mtl(v->format, &vpf, swz) || !vpf
        || mtl_fmt_block((MTLPixelFormat)vpf) || !blk_dec_kind(t, &k)) return MTLPixelFormatInvalid;
    return (MTLPixelFormat)vpf;
}
static MTLPixelFormat rt_color_fmt(const tex_t *t, id<MTLTexture> base, const view_t *v, int has_v, int warn) {
#ifndef DM_TEST_COLOR_ANY_FORMAT   // negative control of t_mtlguard only: a depth / stencil texture attached as a colour target, as before (Metal aborts)
    // A depth / stencil texture bound as a colour target (manosaba at a trial's start: a Depth32Float_Stencil8 target in the colour slot and an
    // RGBA8 one in the depth slot, 2026-10-27; Metal's pipeline validation aborted the process): not attached, its draws land nowhere, as 64m
    // leaves out a depth target that is not depth-renderable. The parallel precompile takes its formats from here as well
    if (base && pf_is_depth_stencil(base.pixelFormat)) {
        if (warn) RED_ONCE(116, "a depth / stencil texture (Metal %lu, %lux%lu, serial %llu) bound as a colour target: not attached, its draws land nowhere (first case)",
                           (unsigned long)base.pixelFormat, (unsigned long)base.width, (unsigned long)base.height, (unsigned long long)t->serial);
        return MTLPixelFormatInvalid;
    }
#endif
    if (base && tex_stored_unpacked(t) && has_v && v->set_format) {   // before the compressed check: BC1 is kept as ASTC (62c3)
        MTLPixelFormat bf = blk_view_fmt_unp(t, v, has_v); if (bf != MTLPixelFormatInvalid) return bf; }
    if (base && mtl_fmt_block(base.pixelFormat)) {
        MTLPixelFormat bf = blk_view_fmt(base, v, has_v);
        if (bf != MTLPixelFormatInvalid) return bf;
        if (warn) RED_ONCE(71, "render target is a compressed texture (Metal format %lu, %lux%lu, view format %#x) without a view of one block per texel: not attached, its draws land nowhere (first case)",
                           (unsigned long)base.pixelFormat, (unsigned long)base.width, (unsigned long)base.height, has_v && v->set_format ? v->format : 0);
        return MTLPixelFormatInvalid;
    }
    if (!base || !has_v || !v->set_format) return base.pixelFormat;
    uint32_t pf = 0; uint8_t swz[4];
    if (!nvn_fmt_to_mtl(v->format, &pf, swz) || !pf) return base.pixelFormat;
    if (pf != (uint32_t)base.pixelFormat && tex_stored_unpacked(t)) {
        MTLPixelFormat bf = blk_view_fmt_unp(t, v, has_v); if (bf != MTLPixelFormatInvalid) return bf;   // decoded after the pass (62c3)
        if (warn) RED_ONCE(111, "render target view format %#x of a BC texture kept as Metal %lu on a GPU without BC: not attached, its draws land nowhere (first case)", v->format, (unsigned long)base.pixelFormat);
        return MTLPixelFormatInvalid; }
    if (pf != (uint32_t)base.pixelFormat && tex_need_pfv(base.pixelFormat, (MTLPixelFormat)pf) && !(base.usage & MTLTextureUsagePixelFormatView)) {
        if (warn) RED_ONCE(48, "render target view format %#x cannot be attached (base texture %lu lacks the pixel-format-view usage), drawing in the base format (first case)", v->format, (unsigned long)base.pixelFormat);
        return base.pixelFormat; }
    return (MTLPixelFormat)pf;
}
// color: when the view of a colour attachment changes the format (e.g. an RGBA8 base texture written through an RGBA8_SRGB view), attach a Metal view of that format - on the Switch writes and clears are encoded by the view's format,
// and attaching only the base texture drops the sRGB encoding entirely, making the picture too dark (public repository #36, measured by seedds on another game); the pipeline colour formats take the attached texture's format (g_pass.cfmt), so they follow.
// The view lives only until this submission completes (Metal 4 does not retain resources; the view shares storage with the base texture and is not made resident separately). Depth / stencil attachments do not change format
// The same level / layer of one texture attached at more than one colour slot: only one slot is attached (rt_dup_slot, gfx/nvn/core/rt.c), the
// others are left out (their writes are dropped). A pending clear on a slot left out moves to the slot kept; a draw writing a slot left out is
// reported once.
static uint8_t g_rt_dup_skip;       // slots this pass left out
typedef struct { __unsafe_unretained id<MTLTexture> aux, dst; NSUInteger level, slice, plane, lw, lh, bx, by, bpb; int dec, fresh; uint32_t fmt; } blk_wb;   // plane: depth plane of a 3D texture; dec: 0 copied back, 1 / 2 decoded (blk_decode)
static NSMutableDictionary<NSNumber *, id<MTLTexture>> *g_blkaux;   // texture serial → auxiliary block texture (dropped when the texture is finalized)
static blk_wb g_blk_wb[8];                                          // the open pass's attachments to copy back at rp_end (count g_nblk_wb)
// Copies one level / slice between the compressed texture and its auxiliary texture through a buffer (Metal copies whole blocks between a BC texture and
// a buffer, and texels between an uncompressed texture and a buffer: the same bytes). from_aux: auxiliary → compressed, else compressed → auxiliary
static void blk_copy(id<MTLTexture> from, int from_aux, id<MTLTexture> to, const blk_wb *w) {
    NSUInteger row = w->bx * w->bpb, size = row * w->by;
    id<MTLBuffer> b = [g_dev newBufferWithLength:size options:MTLResourceStorageModePrivate];
    if (!b) { RED_ONCE(79, "block render target copy buffer of %lu bytes not allocated (first case)", (unsigned long)size); return; }
    res_add(b);
    BENC e = blit();
    MTLSize blocks = MTLSizeMake(w->bx, w->by, 1), texels = MTLSizeMake(w->lw, w->lh, 1);
    enc_b_copy_texture_buffer(e, from, w->slice, w->level, MTLOriginMake(0, 0, w->plane), from_aux ? blocks : texels, b, 0, row, size);
#if !DM_NVN_M3
    [e barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];
#endif
    enc_b_copy_buffer_texture(e, b, 0, row, size, from_aux ? texels : blocks, to, w->slice, w->level, MTLOriginMake(0, 0, w->plane));
    defer_obj_release(b);
}
// The auxiliary texture attachment i renders into (nil when the attachment is not a BC texture with a block-sized view), *w filled for the copies
static id<MTLTexture> blk_aux(const tex_t *t, const view_t *v, int has_v, blk_wb *w) {
    id<MTLTexture> base = tex_mtl(t);
    if (!base) return nil;
    int dec = 0, bb = 0, fresh = 0; uint32_t dfmt = 0; MTLPixelFormat vpf;
    if (tex_stored_unpacked(t) && (vpf = blk_view_fmt_unp(t, v, has_v)) != MTLPixelFormatInvalid) { int bw, bh; bb = nvn_fmt_bpb(t->d.format, &bw, &bh); dec = blk_dec_kind(t, &dfmt); }   // (BC1 kept as ASTC is a block format too)
    else if (mtl_fmt_block(base.pixelFormat)) { vpf = blk_view_fmt(base, v, has_v); bb = mtl_bc_block_bytes(base.pixelFormat); }
    else return nil;
    if (vpf == MTLPixelFormatInvalid) return nil;   // attach() reports it
    tex_gpu_write(t); base = tex_mtl(t);            // as attach(): the first GPU write can replace a CPU-filled texture's MTLTexture
    if (!base) return nil;
    int is3d = base.textureType == MTLTextureType3D;
    if (base.textureType != MTLTextureType2D && base.textureType != MTLTextureType2DArray && !is3d) { RED_ONCE(6, "compressed render target of texture type %lu not drawn (first case)", (unsigned long)base.textureType); return nil; }
    if (!g_blkaux) g_blkaux = [NSMutableDictionary dictionary];
    NSNumber *key = @(t->serial); id<MTLTexture> a = g_blkaux[key];
    NSUInteger aw = (base.width + 3) / 4, ah = (base.height + 3) / 4;
    if (!a || a.pixelFormat != vpf || a.width != aw || a.height != ah) {
        if (a) defer_obj_release(a);
        MTLTextureDescriptor *d = [MTLTextureDescriptor new];
        d.textureType = base.textureType; d.pixelFormat = vpf; d.width = aw; d.height = ah;
        d.arrayLength = base.arrayLength; d.depth = base.depth; d.mipmapLevelCount = base.mipmapLevelCount;
        d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        a = [g_dev newTextureWithDescriptor:d];
        if (!a) { RED_ONCE(7, "auxiliary block render texture %lux%lu not allocated (first case)", (unsigned long)aw, (unsigned long)ah); return nil; }
        res_add(a); g_blkaux[key] = a; fresh = 1;
        static int once; if (!dec && !once++) MLOG("render target on a compressed texture (Metal format %lu, %lux%lu) drawn through its view format %lu into a %lux%lu block texture, copied back after the pass (first case)",
                                           (unsigned long)base.pixelFormat, (unsigned long)base.width, (unsigned long)base.height, (unsigned long)vpf, (unsigned long)aw, (unsigned long)ah);
    }
    NSUInteger lv = v->set_levels ? v->base_level : 0, sl = v->set_layers ? v->min_layer : 0;
    NSUInteger lw = MAX(base.width >> lv, 1u), lh = MAX(base.height >> lv, 1u);
    if (lv >= base.mipmapLevelCount) { RED_ONCE(14, "compressed render target level %lu of %lu not drawn (first case)", (unsigned long)lv, (unsigned long)base.mipmapLevelCount); return nil; }
    *w = (blk_wb){a, base, lv, is3d ? 0 : sl, is3d ? sl : 0, lw, lh, (lw + 3) / 4, (lh + 3) / 4, (NSUInteger)bb, dec, fresh, dfmt};
    if (dec) { static int once; if (!once++) MLOG("no BC: a BC texture (NVN format %#x) drawn through a block view (Metal %lu) into a %lux%lu block texture, decoded into it after the pass (first case)",
                                                  t->d.format, (unsigned long)vpf, (unsigned long)w->bx, (unsigned long)w->by); }
    return a;
}
static void blk_decode(const blk_wb *w);
static void blk_writeback(void) { int n = g_nblk_wb; g_nblk_wb = 0; for (int i = 0; i < n; i++) { if (g_blk_wb[i].dec) blk_decode(&g_blk_wb[i]); else blk_copy(g_blk_wb[i].aux, 1, g_blk_wb[i].dst, &g_blk_wb[i]); } }
static void blk_drop(uint64_t serial) { if (!g_blkaux) return; NSNumber *k = @(serial); id<MTLTexture> a = g_blkaux[k]; if (a) { defer_obj_release(a); [g_blkaux removeObjectForKey:k]; } }
static void attach(MTLRenderPassAttachmentDescriptor *a, const tex_t *t, const view_t *v, int has_v, int color) {
    tex_gpu_write(t);
    id<MTLTexture> base = tex_mtl(t); a.texture = base;
    if (color && base && mtl_fmt_block(base.pixelFormat)) { if (blk_view_fmt(base, v, has_v) == MTLPixelFormatInvalid) rt_color_fmt(t, base, v, has_v, 1); a.texture = nil; return; }   // see mtl_fmt_block: rp_begin attaches the auxiliary texture
    if (has_v && v->set_levels) a.level = v->base_level;
    if (has_v && v->set_layers) { if (t->d.target == 2) a.depthPlane = v->min_layer; else a.slice = v->min_layer; }
    if (!color || !base) return;
    MTLPixelFormat pf = rt_color_fmt(t, base, v, has_v, 1);
    if (pf == MTLPixelFormatInvalid) { a.texture = nil; return; }
    if (pf == base.pixelFormat) return;
    id<MTLTexture> vw = [base newTextureViewWithPixelFormat:pf];
    if (!vw) { RED_ONCE(49, "cannot create render target view: format %#x (base texture %lu), drawing in the base format (first case)", v->format, (unsigned long)base.pixelFormat); return; }
    a.texture = vw; defer_obj_unref(vw);
    { static int once; if (!once++) MLOG("render target attached through a format-changing view: view format %#x, base texture %lu → attached %lu (first case)", v->format, (unsigned long)base.pixelFormat, (unsigned long)pf); }
}
// mode 1 (clear_only): a pass opened only to land pending clears, attaching only the attachments that have clears; mode 2: the lookahead proves this pass does not use depth-stencil, so depth is not attached.
// clear_only: a pass opened only to land pending clears, attaching only the attachments that have clears - the other attachments are not touched by this pass, and attaching them costs a pointless load and store on TBDR
// (Mac character select screen: the color-clear pass drags along a 400x225 depth load and store)
static RENC rp_begin_ex(int clear_only);
static RENC rp_begin(void) { return rp_begin_ex(0); }
static RENC rp_begin_ex(int clear_only) {
    if (g_enc) return g_enc;
    memset(g_zskip_cand_ok, 0, sizeof g_zskip_cand_ok);
    g_enc_mode = clear_only;
    if (!R.has_rt) { RED_ONCE(3, "no render target set before draw"); return nil; }
    int lay_first = 0, lay_n = lay_decide(clear_only, &lay_first);
    int any = 0; g_rt_dup_skip = 0;
    for (int i = 0; i < R.rt.n && i < 8; i++) if (R.clear_c[i] && rt_dup_slot(&R.rt, i))   // a pending clear on a slot left out goes to the slot kept (see rt_dup_slot)
        for (int j = 0; j < R.rt.n && j < 8; j++) if (j != i && rt_same_sub(&R.rt, i, j) && !rt_dup_slot(&R.rt, j)) { if (!R.clear_c[j]) { R.clear_c[j] = 1; R.cc[j] = R.cc[i]; } break; }
    blk_wb pre[8]; id<MTLTexture> preaux[8] = {nil};   // compressed render targets: auxiliary textures, loaded with the old blocks unless cleared (a blit, before the render encoder)
    for (int i = 0; i < R.rt.n && i < 8; i++) {
        if (!R.rt.color[i] || (clear_only == 1 && !R.clear_c[i]) || rt_dup_slot(&R.rt, i)) continue;
        preaux[i] = blk_aux(R.rt.color[i], &R.rt.cview[i], R.rt.has_cview[i], &pre[i]);
        if (preaux[i] && !R.clear_c[i]) {
            if (!pre[i].dec) blk_copy(pre[i].dst, 0, preaux[i], &pre[i]);
            else if (pre[i].fresh) { R.clear_c[i] = 1; R.cc[i] = nvn_rgba_make(0, 0, 0, 0); }   // the auxiliary texture holds the content (62c3): zero blocks when new
        }
    }
    if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    RPD *pd = [RPD new];
    for (int i = 0; i < R.rt.n; i++) {
        if (!R.rt.color[i] || (clear_only == 1 && !R.clear_c[i])) continue;
        if (i < 8 && rt_dup_slot(&R.rt, i)) { g_rt_dup_skip |= (uint8_t)(1u << i); PD("P %llu c%d ser=%llu left out: the same texture is attached at another slot", g_stat_pass + 1, i, R.rt.color[i]->serial); continue; }
        if (i < 8 && preaux[i]) {
            pd.colorAttachments[i].texture = preaux[i]; pd.colorAttachments[i].level = pre[i].level;
            if (preaux[i].textureType == MTLTextureType3D) pd.colorAttachments[i].depthPlane = pre[i].plane; else pd.colorAttachments[i].slice = pre[i].slice;
            if (g_nblk_wb < 8) g_blk_wb[g_nblk_wb++] = pre[i];
        } else attach(pd.colorAttachments[i], R.rt.color[i], &R.rt.cview[i], R.rt.has_cview[i], 1);
        if (!pd.colorAttachments[i].texture) continue;
        any = 1;
        pd.colorAttachments[i].loadAction = R.clear_c[i] ? MTLLoadActionClear : MTLLoadActionLoad;
        pd.colorAttachments[i].clearColor = MTLClearColorMake(R.cc[i].red, R.cc[i].green, R.cc[i].blue, R.cc[i].alpha);
        { static long long rtc = -2; if (rtc == -2) { const char *e = getenv("DM_NVN_RTCLEAR"); rtc = e ? atoll(e) : -1; }   // diagnostic: passes on this colour serial start cleared to magenta (does the attachment reach the texture?)
          if (rtc >= 0 && R.rt.color[i]->serial == (uint64_t)rtc) { pd.colorAttachments[i].loadAction = MTLLoadActionClear; pd.colorAttachments[i].clearColor = MTLClearColorMake(1, 0, 1, 1); } }
        int cov = tbdr_part(TBP_COVER) && !R.clear_c[i] && (g_fd_dw || g_fd_di) && fz_load_covers(i, pd.colorAttachments[i].texture, pd.colorAttachments[i].level);
        if (!R.clear_c[i] && (g_fd_dw || g_fd_di) && !cov) PD("Q %llu c%d load not elidable why=%d", g_stat_pass + 1, i, g_fd_why);
        if (cov) {
            static int probe = -1; if (probe < 0) { probe = getenv("DM_NVN_LOADPROBE") ? atoi(getenv("DM_NVN_LOADPROBE")) : 0; if (probe) MLOG("DM_NVN_LOADPROBE: attachments with an elidable load are cleared to a sentinel color instead and read back for verification"); }
            if (probe) { pd.colorAttachments[i].loadAction = MTLLoadActionClear; int pm = probe == 1 && pf_is_float(pd.colorAttachments[i].texture.pixelFormat) ? 3 : probe;   // Float formats use a NaN sentinel by default: a fixed sentinel value can be hit by real output (it has been hit in practice)
                pd.colorAttachments[i].clearColor = pm == 3 ? MTLClearColorMake(NAN, NAN, NAN, NAN) : probe == 2 ? MTLClearColorMake(0.777, 0.222, 0.555, 0.888) : MTLClearColorMake(0.123, 0.456, 0.789, 0.321);
                if (!g_lp) g_lp = [NSMutableArray array];
                [g_lp addObject:@[pd.colorAttachments[i].texture, @(pd.colorAttachments[i].level), @(pd.colorAttachments[i].slice), @(g_stat_pass + 1), @(i), @(R.rt.color[i]->serial), @(pm)]]; }
            else pd.colorAttachments[i].loadAction = MTLLoadActionDontCare;
            g_fd_n++;
        }
        xv_touch_add(R.rt.color[i]->serial, (uint32_t)pd.colorAttachments[i].level, (uint32_t)pd.colorAttachments[i].slice,
                     pd.colorAttachments[i].loadAction != MTLLoadActionLoad);
        if (g_xv.n) { xv_ent *xe = xv_find(R.rt.color[i]->serial, (uint32_t)pd.colorAttachments[i].level, (uint32_t)pd.colorAttachments[i].slice);
            if (xe && (xe->state == XV_INVALID || xe->state == XV_PROBE)) { if (pd.colorAttachments[i].loadAction == MTLLoadActionLoad) { xv_ent copy = *xe; xv_violation(&copy, "loads old contents as an attachment"); } else xv_good(xe); } }
        pd.colorAttachments[i].storeAction = MTLStoreActionStore;
    }
    // clear_only: 0 attach all 1 only for clears 2 neither depth nor stencil attached 3 stencil only 4 depth only (3 / 4: the pass_ds_need lookahead proves the whole pass uses only that one)
    if (R.rt.depth && clear_only == 3) {   // Stencil only: the depth plane of the combined format does not enter the tile and is neither read nor written (Metal allows an empty depth attachment with the combined texture on the stencil attachment)
        attach(pd.stencilAttachment, R.rt.depth, &R.rt.dview, R.rt.has_dview, 0);
        MTLPixelFormat pf = pd.stencilAttachment.texture.pixelFormat;
        if (pf == MTLPixelFormatDepth32Float_Stencil8 || pf == MTLPixelFormatStencil8) {
            any = 1; pd.stencilAttachment.loadAction = R.clear_s ? MTLLoadActionClear : MTLLoadActionLoad;
            pd.stencilAttachment.clearStencil = R.cs; pd.stencilAttachment.storeAction = MTLStoreActionStore;
        } else pd.stencilAttachment.texture = nil;   // No stencil plane: the stencil test always passes, attach nothing
    } else if (R.rt.depth && clear_only != 2 && !(clear_only == 1 && !R.clear_d && !R.clear_s)) {
        attach(pd.depthAttachment, R.rt.depth, &R.rt.dview, R.rt.has_dview, 0);
#ifndef DM_TEST_DEPTH_ANY_FORMAT   // negative control of t_mtlguard only: any format attached as depth, as before (Metal aborts)
        // A depth target whose format is not depth-renderable (super-mario-odyssey-ios 693bc6b, MIT: an R11G11B10F texture bound as the depth
        // target, and Metal's validation aborts the process; plan item 64m): the pass draws its colours without depth, reported once
        if (pd.depthAttachment.texture && !pf_is_depth_stencil(pd.depthAttachment.texture.pixelFormat)) {
            RED_ONCE(110, "depth target of NVN format %#x (Metal %lu, %ux%u, serial %llu) is not depth-renderable: the pass draws without depth (first case)", R.rt.depth->d.format,
                     (unsigned long)pd.depthAttachment.texture.pixelFormat, R.rt.depth->d.width, R.rt.depth->d.height, (unsigned long long)R.rt.depth->serial);
            pd.depthAttachment.texture = nil;
        }
#endif
        if (pd.depthAttachment.texture) {
            any = 1;
            pd.depthAttachment.loadAction = R.clear_d ? MTLLoadActionClear : MTLLoadActionLoad;
            MTLPixelFormat pf = pd.depthAttachment.texture.pixelFormat;
            pd.depthAttachment.clearDepth = pf == MTLPixelFormatDepth16Unorm ? nvn_depth16_clear_value(R.cd) : R.cd; pd.depthAttachment.storeAction = MTLStoreActionStore;
            if (clear_only != 4 && (pf == MTLPixelFormatDepth32Float_Stencil8 || pf == MTLPixelFormatStencil8)) {   // 4: depth only
                pd.stencilAttachment.texture = pd.depthAttachment.texture; pd.stencilAttachment.level = pd.depthAttachment.level;
                pd.stencilAttachment.slice = pd.depthAttachment.slice;
                pd.stencilAttachment.loadAction = R.clear_s ? MTLLoadActionClear : MTLLoadActionLoad;
                pd.stencilAttachment.clearStencil = R.cs; pd.stencilAttachment.storeAction = MTLStoreActionStore;
            }
        }
    }
    // Nothing attached although the pass draws: no colour or depth target at all (NVN rasterizes without attachments, side effects through storage
    // buffers / images; Tears of the Kingdom draws one under a 1x1 scissor), or only a depth target the look-ahead proved unused (mode 2; its
    // occlusion query draws). Metal: a pass with only a size, the depth target's, else the scissor's
    if (!any && (clear_only == 0 || clear_only == 2) && (!R.rt.depth || clear_only == 2)) {
        int none = 1;
        for (int i = 0; i < R.rt.n && i < 8; i++) none &= !R.rt.color[i];
        if (none) {
            float vp0[4]; int ms0[3]; state_vp_ms(vp0, ms0); unsigned w0, h0; nvn_attless_size(R.has_sc, R.sc.x, R.sc.y, R.sc.w, R.sc.h, vp0, &w0, &h0);   // nvn_obj.h
            NSUInteger w = w0, h = h0;
            if (R.rt.depth) { NSUInteger lv = R.rt.has_dview && R.rt.dview.set_levels ? R.rt.dview.base_level : 0; w = mip_extent(R.rt.depth->d.width, lv); h = mip_extent(R.rt.depth->d.height, lv); }
            pd.renderTargetWidth = MIN(w, 16384u); pd.renderTargetHeight = MIN(h, 16384u); pd.defaultRasterSampleCount = 1; any = 1;
            static int once[2]; if (!once[!!R.rt.depth]++) MLOG("render pass without attachments, %lux%lu from the %s (first case)", (unsigned long)pd.renderTargetWidth, (unsigned long)pd.renderTargetHeight, R.rt.depth ? "unused depth target" : R.has_sc ? "scissor" : "viewport");
            {   // measured for plan item 121: the viewport reaching past this size, or a multisample state with more samples
                static uint64_t nm; float vp[4]; int ms[3]; state_vp_ms(vp, ms);
                if (!R.rt.depth && (vp[0] + vp[2] > (float)pd.renderTargetWidth || vp[1] + vp[3] > (float)pd.renderTargetHeight || ms[1] > 1 || ms[2] > 1) && ++nm <= 8)
                    MLOG("render pass without attachments %lux%lu: viewport %.0f,%.0f %.0fx%.0f, multisample enable %d samples %d raster samples %d (case %llu)", (unsigned long)pd.renderTargetWidth,
                         (unsigned long)pd.renderTargetHeight, vp[0], vp[1], vp[2], vp[3], ms[0], ms[1], ms[2], (unsigned long long)nm); }
        }
    }
    if (!any) {
        if (!g_red_once[4]) { char d[400] = ""; size_t o = 0;
            for (int i = 0; i < R.rt.n && i < 8; i++) if (R.rt.color[i]) { id<MTLTexture> m = tex_mtl(R.rt.color[i]);
                o += snprintf(d + o, sizeof d - o, " c%d ser %llu fmt %#x %ux%u view %#x mtl %lu%s;", i, (unsigned long long)R.rt.color[i]->serial, R.rt.color[i]->d.format, R.rt.color[i]->d.width, R.rt.color[i]->d.height,
                              R.rt.has_cview[i] && R.rt.cview[i].set_format ? R.rt.cview[i].format : 0, (unsigned long)m.pixelFormat, rt_dup_slot(&R.rt, i) ? " dup" : ""); if (o >= sizeof d) break; }
            if (o < sizeof d) snprintf(d + o, sizeof d - o, " depth %s, clear_only %d, occlusion counting %d, scissor %dx%d", R.rt.depth ? "set" : "none", clear_only, g_vis_on,
                                      R.has_sc ? R.sc.w : -1, R.has_sc ? R.sc.h : -1);
            RED_ONCE(4, "none of the render targets can create an MTLTexture:%s", d); }
        return nil; }
    for (int i = 0; i < R.rt.n && i < 8; i++) if (R.rt.color[i] && zskip_zero_clear(i)) {
        g_zskip_cand_ok[i] = 1; g_zskip_cand[i] = (zskip_zero){R.rt.color[i]->serial, (uint16_t)(R.rt.has_cview[i] && R.rt.cview[i].set_levels ? R.rt.cview[i].base_level : 0), (uint16_t)(R.rt.has_cview[i] && R.rt.cview[i].set_layers ? R.rt.cview[i].min_layer : 0)};
    }
    // Bandwidth ledger: attachments with Load count as load, all attachments count as store (a pass opens with Store; a store the TBDR store
    // decision turns into DontCare is taken off again at the pass end, g_sd_dead_bytes)
    int nload = 0, natt = 0;
    for (int i = 0; i < R.rt.n; i++) {
        MTLRenderPassColorAttachmentDescriptor *c = pd.colorAttachments[i];
        if (!c.texture) continue;
        uint64_t b = att_bytes(c.texture, c.level); natt++; g_bw_store += b; if (c.loadAction == MTLLoadActionLoad) { g_bw_load += b; nload++; }
        if (R.rt.has_cview[i] && R.rt.cview[i].set_format) PD("V %llu c%d view format %#x (base texture NVN format %#x)", g_stat_pass + 1, i, R.rt.cview[i].format, R.rt.color[i]->d.format);
        PD("P %llu c%d ser=%llu fmt=%lu %lux%lu ms%lu lv%lu sl%lu %s %g,%g,%g,%g", g_stat_pass + 1, i, R.rt.color[i]->serial, (unsigned long)c.texture.pixelFormat,
           (unsigned long)c.texture.width, (unsigned long)c.texture.height, (unsigned long)c.texture.sampleCount, (unsigned long)c.level, (unsigned long)c.slice,
           c.loadAction == MTLLoadActionClear ? "clear" : c.loadAction == MTLLoadActionDontCare ? "dontcare" : "load", c.clearColor.red, c.clearColor.green, c.clearColor.blue, c.clearColor.alpha);
    }
    {   // Depth and stencil planes are counted separately (combined format 5 bytes = depth 4 + stencil 1; when only one is attached only that one is counted)
        id<MTLTexture> dt = pd.depthAttachment.texture, stt = pd.stencilAttachment.texture;
        uint64_t whole = att_bytes(dt ? dt : stt, dt ? pd.depthAttachment.level : pd.stencilAttachment.level);
        int comb = (dt ? dt : stt).pixelFormat == MTLPixelFormatDepth32Float_Stencil8;
        uint64_t db = dt ? (comb ? whole / 5 * 4 : whole) : 0, sb = stt && stt != dt ? (comb ? whole / 5 : whole) : stt ? whole - db : 0;
        if (dt) { natt++; g_bw_store += db; if (pd.depthAttachment.loadAction == MTLLoadActionLoad) { g_bw_load += db; nload++; } }
        if (stt) { natt += !dt; g_bw_store += sb; if (pd.stencilAttachment.loadAction == MTLLoadActionLoad) { g_bw_load += sb; nload += !dt; } }
        if (dt) PD("P %llu d ser=%llu fmt=%lu %lux%lu %s", g_stat_pass + 1, R.rt.depth->serial, (unsigned long)dt.pixelFormat,
                   (unsigned long)dt.width, (unsigned long)dt.height, pd.depthAttachment.loadAction == MTLLoadActionClear ? "clear" : "load");
        else if (stt) PD("P %llu s ser=%llu fmt=%lu %lux%lu %s (stencil only)", g_stat_pass + 1, R.rt.depth->serial, (unsigned long)stt.pixelFormat,
                   (unsigned long)stt.width, (unsigned long)stt.height, pd.stencilAttachment.loadAction == MTLLoadActionClear ? "clear" : "load");
    }
    if (natt && nload == natt) g_pass_full_rw++;
    g_nsd = 0;
    {
        for (int i = 0; i < R.rt.n && i < 8; i++) { MTLRenderPassColorAttachmentDescriptor *c = pd.colorAttachments[i]; if (!c.texture) continue;
            g_sd[g_nsd] = (sd_att){0}; g_sd[g_nsd].ser = R.rt.color[i]->serial; g_sd[g_nsd].bytes = att_bytes(c.texture, c.level); g_sd[g_nsd].lv = (uint32_t)c.level; g_sd[g_nsd].sl = (uint32_t)(c.texture.textureType == MTLTextureType3D ? c.depthPlane : c.slice); g_sd[g_nsd].idx = i; g_sd[g_nsd].load = c.loadAction == MTLLoadActionLoad; g_sd[g_nsd].wrote = R.clear_c[i] != 0; g_nsd++;
            c.storeAction = MTLStoreActionUnknown; }
        id<MTLTexture> dt = pd.depthAttachment.texture, stt = pd.stencilAttachment.texture;
        uint64_t db = att_bytes(dt, pd.depthAttachment.level), sb = 0;
        if (stt) { int comb = stt.pixelFormat == MTLPixelFormatDepth32Float_Stencil8; sb = att_bytes(stt, pd.stencilAttachment.level) / (comb ? 5 : 1); if (stt == dt) db -= sb; }   // Combined format 5 bytes = depth 4 + stencil 1 (when only stencil is attached, only 1 is counted)
        if (dt) { g_sd[g_nsd] = (sd_att){0}; g_sd[g_nsd].ser = R.rt.depth->serial; g_sd[g_nsd].bytes = db; g_sd[g_nsd].lv = (uint32_t)pd.depthAttachment.level; g_sd[g_nsd].sl = (uint32_t)pd.depthAttachment.slice; g_sd[g_nsd].idx = -1; g_sd[g_nsd].depth = 1; g_sd[g_nsd].load = pd.depthAttachment.loadAction == MTLLoadActionLoad; g_sd[g_nsd].wrote = R.clear_d != 0; g_nsd++;
                  pd.depthAttachment.storeAction = MTLStoreActionUnknown; }
        if (stt) { g_sd[g_nsd] = (sd_att){0}; g_sd[g_nsd].ser = R.rt.depth->serial; g_sd[g_nsd].bytes = sb; g_sd[g_nsd].lv = (uint32_t)pd.stencilAttachment.level; g_sd[g_nsd].sl = (uint32_t)pd.stencilAttachment.slice; g_sd[g_nsd].idx = -1; g_sd[g_nsd].depth = 2; g_sd[g_nsd].load = pd.stencilAttachment.loadAction == MTLLoadActionLoad; g_sd[g_nsd].wrote = R.clear_s != 0; g_nsd++;
                   pd.stencilAttachment.storeAction = MTLStoreActionUnknown; }
    }
    for (int i = 0; i < g_nsd; i++) g_sd[i].pw = g_sd[i].wrote;   // a clear at load: newest in tile memory
    memset(R.clear_c, 0, sizeof R.clear_c); R.clear_d = R.clear_s = 0;
    {   // Pass info (same convention as the former per-draw fetch: colors take each attachment's base texture format; sample count takes the last color attachment, or depth if there is depth)
        uint64_t ser = g_pass.serial + 1; memset(&g_pass, 0, sizeof g_pass); g_pass.serial = ser;
        for (int i = 0; i < R.rt.n && i < 8; i++) { id<MTLTexture> t = R.rt.color[i] ? pd.colorAttachments[i].texture : nil;
            if (t) { g_pass.cfmt[i] = (uint32_t)t.pixelFormat; g_pass.samples = (uint32_t)t.sampleCount; } }
        id<MTLTexture> dt = R.rt.depth ? pd.depthAttachment.texture : nil, stt = R.rt.depth ? pd.stencilAttachment.texture : nil;   // Depth / stencil attachments are fetched separately (possibly only one of them is attached)
        g_pass.hd = dt && dt.pixelFormat != MTLPixelFormatStencil8; g_pass.hs = stt != nil;
        g_pass.dfmt = g_pass.hd ? (uint32_t)dt.pixelFormat : 0; g_pass.sfmt = stt ? (uint32_t)stt.pixelFormat : 0;
        if (dt || stt) g_pass.samples = (uint32_t)(dt ? dt : stt).sampleCount;
        pass_dims(pd, (NSUInteger)R.rt.n, &g_pass.tw, &g_pass.th);
        g_enc_pso = nil; g_enc_dss = nil;
    }
    if (g_vis_on) {
        // Slots used up (one submission exceeds VIS_SLOTS counting passes): create an overflow buffer and continue with it. Buffers in the ring must not be handed to retirement (they are still used in rotation, and retiring would remove them from the residency set);
        // only the previous overflow buffer may be retired. The overflow buffer itself is retired when the next submission switches back to a ring buffer (mtl_sub_begin)
        if (!g_vis || g_vis_n >= VIS_SLOTS) { if (g_vis && !vis_in_ring(g_vis)) defer_obj_release(g_vis); g_vis = [g_dev newBufferWithLength:VIS_SLOTS * 8 options:MTLResourceStorageModeShared]; res_add(g_vis); g_vis_n = 0; }
        pd.visibilityResultBuffer = g_vis;
    }
    g_enc_vis = 0; g_enc_has_vis = g_vis_on;
    { void td_note(id pd, NSUInteger nc); td_note(pd, (NSUInteger)R.rt.n);
      void ts_note(id pd, NSUInteger nc); ts_note(pd, (NSUInteger)R.rt.n); }
    if (lay_n > 1 && pd.colorAttachments[0].texture) {   // the attachment covers the layer range; the draws pick their layer (lay_route)
        pd.colorAttachments[0].slice = (NSUInteger)lay_first; pd.renderTargetArrayLength = (NSUInteger)lay_n;
        g_lay.active = 1; g_lay.first = lay_first; g_lay.count = lay_n; g_lay.cur = R.rt.cview[0].min_layer; g_lay.amp = -1;
        g_lay.ser = R.rt.color[0]->serial; g_lay.v = R.rt.cview[0];
        if (!g_lay.passes++) MLOG("layered pass: first one covers layers %d-%d of a %lux%lu level", lay_first, lay_first + lay_n - 1, (unsigned long)g_pass.tw, (unsigned long)g_pass.th);
    }
    chunk_maybe_split();
    pg_render(pd, (NSUInteger)R.rt.n);
    g_enc = enc_r_begin(pd);
    pg_render_begin(g_enc);
    args_dirty_all();
    g_stat_pass++; g_cnt_pass_d0 = g_stat_draw;
    if (counts_on()) { g_cnt_ext_x = g_cnt_ext_y = 0; g_cnt_ext_clr = pd.depthAttachment.loadAction == MTLLoadActionClear && pd.depthAttachment.texture;
        g_cnt_ext_clr |= pd.stencilAttachment.loadAction == MTLLoadActionClear && pd.stencilAttachment.texture;
        for (int i = 0; i < R.rt.n && i < 8; i++) g_cnt_ext_clr |= pd.colorAttachments[i].texture && pd.colorAttachments[i].loadAction == MTLLoadActionClear; }
    if (enc_label_on()) { id<MTLTexture> lt = pd.colorAttachments[0].texture ?: pd.depthAttachment.texture ?: pd.stencilAttachment.texture; g_el_w = (int)lt.width; g_el_h = (int)lt.height; g_el_draws = 0; g_el_key[0] = 0; }
    return g_enc;
}
// Empty-scissor precheck before opening a pass: the size of the selected level of the first valid target is an upper bound on the minimum bounds of the actual pass.
static void rt_dims(NSUInteger *tw, NSUInteger *th) {
    id<MTLTexture> t0 = nil; NSUInteger level = 0;
    for (int i = 0; i < R.rt.n && i < 8 && !t0; i++) if (R.rt.color[i]) {
        t0 = tex_mtl(R.rt.color[i]); level = R.rt.has_cview[i] && R.rt.cview[i].set_levels ? R.rt.cview[i].base_level : 0;
    }
    if (!t0 && R.rt.depth) { t0 = tex_mtl(R.rt.depth); level = R.rt.has_dview && R.rt.dview.set_levels ? R.rt.dview.base_level : 0; }
    *tw = t0 ? mip_extent(t0.width, level) : 1; *th = t0 ? mip_extent(t0.height, level) : 1;
}
// There are pending clears but no draw in the pass: open a pass once to land the clears (counted with the bytes they store: plan item 187)
static uint64_t g_clear_only_n, g_clear_only_bytes;
void mtl_clear_only_stats(uint64_t *n, uint64_t *bytes) { *n = g_clear_only_n; *bytes = g_clear_only_bytes; }
static void rp_flush(void) {
    int pend = R.clear_d || R.clear_s;
    for (int i = 0; i < 8; i++) pend |= R.clear_c[i];
    if (pend && !g_enc && g_cb) { uint64_t st0 = g_bw_store; rp_begin_ex(1); rp_end(); g_clear_only_n++; g_clear_only_bytes += g_bw_store - st0; return; }
    rp_end();
}
static int scissor_full(const tex_t *t, const view_t *v, int has_v) { return scissor_full_of(&R.sc, R.has_sc, t, v, has_v); }
// Clears a load action cannot do (inside a render pass, a partial channel mask, a partial scissor, a partial stencil mask) are drawn as one
// full-target triangle under the scissor: colour writes only that attachment's masked channels, depth / stencil write the clear value with compare
// Always (stencil Replace under the stencil mask); the triangle is not counted by occlusion queries. NVN clears honour the scissor and the masks.
// Ported from MacNativeGames (ly05010419/MacNativeGames, MIT: clear_quad / clear_ds_quad); our draws set all dynamic state again, the pipeline and
// depth-stencil caches are dropped here
static int clear_scissor(MTLScissorRect *out) {
    NSUInteger tw = g_pass.tw, th = g_pass.th; *out = (MTLScissorRect){0, 0, tw, th};
    if (!R.has_sc) return 1;
    int64_t x0 = MAX(R.sc.x, 0), y0 = MAX(R.sc.y, 0), x1 = MIN((int64_t)R.sc.x + R.sc.w, (int64_t)tw), y1 = MIN((int64_t)R.sc.y + R.sc.h, (int64_t)th);
    if (x1 <= x0 || y1 <= y0) return 0;
    *out = (MTLScissorRect){(NSUInteger)x0, (NSUInteger)y0, (NSUInteger)(x1 - x0), (NSUInteger)(y1 - y0)}; return 1;
}
static void clear_quad_draw(RENC en, id<MTLRenderPipelineState> ps, id<MTLDepthStencilState> ds, uint32_t ref, MTLScissorRect sc) {
    enc_r_pso(en, ps); g_enc_pso = nil; enc_r_dss(en, ds); g_enc_dss = nil;
    if (g_enc_vis) { enc_r_visibility(en, MTLVisibilityResultModeDisabled, 0); g_enc_vis = 0; }
    enc_r_stencil_ref(en, ref, ref); enc_r_viewport(en, (MTLViewport){0, 0, (double)g_pass.tw, (double)g_pass.th, 0, 1}); enc_r_scissor(en, sc);
#ifndef DM_TEST_EXTENT_BROKEN   // (and clear quads not counted)
    if (g_cnt_on > 0) cnt_extent(sc, 0, 0, g_pass.tw, g_pass.th);
#endif
    enc_r_cull(en, MTLCullModeNone); enc_r_fill(en, MTLTriangleFillModeFill); enc_r_depth_bias(en, 0, 0, 0); enc_r_depth_clip(en, YES); enc_r_depth_bounds(en, 0, 1);
    enc_r_draw_instanced(en, MTLPrimitiveTypeTriangle, 0, 3, 1, 0);
}
// A clear drawn as a quad writes its attachments like a draw (plan item 62j): without this a pass that loaded them and had only such clears stored
// DontCare (udc_dead), and the clear was lost. Tears of the Kingdom with the TBDR rewrites on: the prologue's glow over half the picture and the
// trail behind Zelda's torch (iPhone and Mac, product60). t_clearquad; -DDM_TEST_CLEARQUAD_UNNOTED as before
static void sd_note_quad(int color_idx, int depth, int stencil) {
#ifndef DM_TEST_CLEARQUAD_UNNOTED
    for (int i = 0; i < g_nsd; i++) { sd_att *t = &g_sd[i];
        if ((t->depth == 0 && color_idx >= 0 && t->idx == color_idx) || (t->depth == 1 && depth) || (t->depth == 2 && stencil)) { t->wrote = 1; t->wwhy = 6; t->pw = 1; } }
#else
    (void)color_idx; (void)depth; (void)stencil;
#endif
}
static void clear_quad(const p_clear_color *c) {
    if (!(c->mask & 0xf) || c->index < 0 || c->index >= 8) return;
    // A slot attached at two indices is attached once (rt_dup_slot): its clear goes to the index kept (seedds/nxnative 7dde056, MIT; t_nvn 6c3)
    int at = c->index;
    if (at < R.rt.n && rt_dup_slot(&R.rt, at)) for (int j = 0; j < R.rt.n && j < 8; j++) if (j != at && rt_same_sub(&R.rt, at, j) && !rt_dup_slot(&R.rt, j)) { at = j; break; }
    if (g_enc && !g_pass.cfmt[at]) rp_end();
    RENC en = rp_begin(); if (!en) return;
    if (!g_pass.cfmt[at]) { RED_ONCE(75, "partial color clear: attachment %d is not in the render pass (first case)", c->index); return; }
    MTLScissorRect sc; if (!clear_scissor(&sc)) return;
    static NSMutableDictionary<NSString *, id<MTLRenderPipelineState>> *cache; static id<MTLDepthStencilState> dss; static id<MTLLibrary> libs[8];
    if (!cache) { cache = [NSMutableDictionary dictionary]; MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new]; dd.depthCompareFunction = MTLCompareFunctionAlways; dd.depthWriteEnabled = NO; dss = [g_dev newDepthStencilStateWithDescriptor:dd]; }
    int i = at;
    if (!libs[i]) {
        NSString *src = [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float4 cc [[function_constant(0)]];\n"
            "vertex float4 cq_vs(uint id [[vertex_id]]) { float2 p = float2((id << 1) & 2, id & 2); return float4(p * 2 - 1, 0, 1); }\n"
            "struct O { float4 c [[color(%d)]]; };\nfragment O cq_fs() { O o; o.c = cc; return o; }\n", i];
        NSError *e = nil; libs[i] = [g_dev newLibraryWithSource:src options:nil error:&e];
        if (!libs[i]) { RED_ONCE(17, "clear shader: %s", e.localizedDescription.UTF8String); return; }
    }
    NSString *key = [NSString stringWithFormat:@"%d/%x/%u,%u,%u,%u,%u,%u,%u,%u/%u/%u/%u/%a,%a,%a,%a", i, c->mask, g_pass.cfmt[0], g_pass.cfmt[1], g_pass.cfmt[2], g_pass.cfmt[3],
                     g_pass.cfmt[4], g_pass.cfmt[5], g_pass.cfmt[6], g_pass.cfmt[7], g_pass.hd ? g_pass.dfmt : 0, g_pass.hs ? g_pass.sfmt : 0, g_pass.samples, c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3]];
    id<MTLRenderPipelineState> ps = cache[key];
    if (!ps) {
        MTLFunctionConstantValues *cv = [MTLFunctionConstantValues new]; float v[4] = {c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3]};
        [cv setConstantValue:v type:MTLDataTypeFloat4 atIndex:0];
        NSError *e = nil; MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [libs[i] newFunctionWithName:@"cq_vs"]; d.fragmentFunction = [libs[i] newFunctionWithName:@"cq_fs" constantValues:cv error:&e];
        for (int k = 0; k < 8; k++) if (g_pass.cfmt[k]) { d.colorAttachments[k].pixelFormat = (MTLPixelFormat)g_pass.cfmt[k]; d.colorAttachments[k].writeMask = MTLColorWriteMaskNone; }
        d.colorAttachments[i].writeMask = (c->mask & 1 ? MTLColorWriteMaskRed : 0) | (c->mask & 2 ? MTLColorWriteMaskGreen : 0) | (c->mask & 4 ? MTLColorWriteMaskBlue : 0) | (c->mask & 8 ? MTLColorWriteMaskAlpha : 0);
        d.depthAttachmentPixelFormat = g_pass.hd ? (MTLPixelFormat)g_pass.dfmt : MTLPixelFormatInvalid; d.stencilAttachmentPixelFormat = g_pass.hs ? (MTLPixelFormat)g_pass.sfmt : MTLPixelFormatInvalid;
        d.rasterSampleCount = g_pass.samples ? g_pass.samples : 1;
        ps = d.fragmentFunction ? [g_dev newRenderPipelineStateWithDescriptor:d error:&e] : nil;
        if (!ps) { RED_ONCE(76, "partial color clear of attachment %d (Metal format %u) not drawn: %s (first case; an integer format needs an integer clear shader)", i, g_pass.cfmt[i], e.localizedDescription.UTF8String); return; }
        cache[key] = ps;
    }
    clear_quad_draw(en, ps, dss, 0, sc); sd_note_quad(at, 0, 0);
}
static void clear_ds_quad(const p_clear_ds *c) {
    if (g_enc && ((c->depth_mask && !g_pass.hd) || ((c->stencil_mask & 255) && !g_pass.hs))) rp_end();
    RENC en = rp_begin(); if (!en) return;
    MTLScissorRect sc; if (!clear_scissor(&sc)) return;
    uint32_t dfmt = g_pass.hd ? g_pass.dfmt : 0, sfmt = g_pass.hs ? g_pass.sfmt : 0;
    static NSMutableDictionary<NSString *, id<MTLRenderPipelineState>> *pipelines; static NSMutableDictionary<NSNumber *, id<MTLDepthStencilState>> *states; static id<MTLLibrary> lib;
    if (!lib) {
        NSError *e = nil; lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float depth [[function_constant(0)]];\n"
            "vertex float4 ds_vs(uint id [[vertex_id]]) { float2 p = float2((id << 1) & 2, id & 2); return float4(p * 2 - 1, depth, 1); }\n" options:nil error:&e];
        if (!lib) { RED_ONCE(24, "depth-stencil clear shader: %s", e.localizedDescription.UTF8String); return; }
        pipelines = [NSMutableDictionary dictionary]; states = [NSMutableDictionary dictionary];
    }
    float depth = c->depth_mask && dfmt ? fminf(1, fmaxf(0, c->depth)) : 0;
    NSString *key = [NSString stringWithFormat:@"%u,%u,%u,%u,%u,%u,%u,%u/%u/%u/%u/%a", g_pass.cfmt[0], g_pass.cfmt[1], g_pass.cfmt[2], g_pass.cfmt[3], g_pass.cfmt[4], g_pass.cfmt[5],
                     g_pass.cfmt[6], g_pass.cfmt[7], dfmt, sfmt, g_pass.samples, depth];
    id<MTLRenderPipelineState> ps = pipelines[key];
    if (!ps) {
        MTLFunctionConstantValues *cv = [MTLFunctionConstantValues new]; [cv setConstantValue:&depth type:MTLDataTypeFloat atIndex:0];
        NSError *e = nil; MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [lib newFunctionWithName:@"ds_vs" constantValues:cv error:&e];
        for (int k = 0; k < 8; k++) if (g_pass.cfmt[k]) { d.colorAttachments[k].pixelFormat = (MTLPixelFormat)g_pass.cfmt[k]; d.colorAttachments[k].writeMask = MTLColorWriteMaskNone; }
        d.depthAttachmentPixelFormat = (MTLPixelFormat)dfmt; d.stencilAttachmentPixelFormat = (MTLPixelFormat)sfmt; d.rasterSampleCount = g_pass.samples ? g_pass.samples : 1;
        ps = d.vertexFunction ? [g_dev newRenderPipelineStateWithDescriptor:d error:&e] : nil;
        if (!ps) { RED_ONCE(25, "partial depth-stencil clear not drawn: %s (first case)", e.localizedDescription.UTF8String); return; }
        pipelines[key] = ps;
    }
    unsigned sm = sfmt ? c->stencil_mask & 255 : 0, dw = dfmt && c->depth_mask;
    if (!sm && !dw) return;
    NSNumber *dk = @(sm | (dw << 8)); id<MTLDepthStencilState> ds = states[dk];
    if (!ds) {
        MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new]; d.depthCompareFunction = MTLCompareFunctionAlways; d.depthWriteEnabled = dw;
        if (sm) { MTLStencilDescriptor *st = [MTLStencilDescriptor new]; st.stencilCompareFunction = MTLCompareFunctionAlways;
            st.depthStencilPassOperation = MTLStencilOperationReplace; st.writeMask = sm; d.frontFaceStencil = d.backFaceStencil = st; }
        ds = [g_dev newDepthStencilStateWithDescriptor:d]; states[dk] = ds;
    }
    clear_quad_draw(en, ps, ds, (uint32_t)c->stencil, sc); sd_note_quad(-1, (int)dw, sm != 0);
}


// ================= Shaders / descriptor pool / pipelines / draws =================
static pthread_mutex_t g_rmu = PTHREAD_MUTEX_INITIALIZER;
