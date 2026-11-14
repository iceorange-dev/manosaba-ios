// gfx/nvn/mtl/metalfx_scene.inc, part of nvn_mtl.m (included there in order, one translation unit): MetalFX for the game's own upscale of its scene (plan item 63)
// ---------- MetalFX for the game's own upscale of its scene into the canvas (plan item 63) ----------
// After MacNativeGames' mfxup_dock / mfxup_draw (totk_mfx_up.m and nvn_mtl.m, ly05010419/MacNativeGames, MIT; written anew). With MetalFX upscaling
// on (DM_UPSCALE=metalfx, the iOS setting), two draws the profile names (game.json upscale_pass / compose_pass) get MetalFX's spatial scaler:
//  - the upscale pass (fxp_dock; Tears of the Kingdom docked: tone mapping and anti-aliasing at the scene size, 1600x896 for a 1920x1080 canvas,
//    2128x1200 for 2560x1440, then fragment shader ac58fc18, an FSR 1 EASU-style filter of twelve texel reads, writes the canvas before the UI):
//    MetalFX writes the canvas and the draw is left out. The draw's constants (fragment uniform slot 0: c[0].xy = the valid region over the output
//    size, c[0].zw = 0.5 c[0].xy - 0.5, c[4].xy = the output size as integers) give the valid region of the scene, so a smaller one (dynamic
//    resolution) is upscaled as the game would. Only when the draw would write the whole canvas alone (one target at its level 0 / layer 0 in its
//    own format; no blending, a full write mask, no alpha to coverage, no depth or stencil test; viewport and scissor over the whole target), the
//    scene is a 2D texture smaller than the canvas and the constants have that layout. A pending clear of the canvas is dropped (MetalFX writes every
//    pixel).
//  - the compose pass (fxp_scene; Tears of the Kingdom handheld, where there is no upscale pass: fragment shader 0ac9a549 tone maps the scene into
//    the canvas, sampling it with normalised coordinates, bilinearly when DM_RENDER_RES makes it smaller): MetalFX first upscales the scene at its
//    texture slot 0 to the canvas size into a texture of its own, which that draw samples instead (fxp_sub, at the argument encoding).
// Neither while a TBDR rewrite holds draws back (the game's own then). The profile's colour processing for each (Tears of the Kingdom's HDR for
// both: the scene keeps highlights above 1, as MacNativeGames found). Metal 4: a fence after all earlier work, which the scaler waits on; the next
// encoder's queue barrier orders it after the scaler. Each scaler is made on this thread the first time (3-50 ms, once per sizes / formats).
static char g_up_fs[2][17]; static int g_up_color[2];   // the profile's passes (hle_nvn.c, at DeviceInitialize): fragment shader key, MetalFX's colour processing
void mtl_set_upscale_pass(const char *fs, int color, const char *compose_fs, int compose_color) {
    g_up_fs[0][0] = g_up_fs[1][0] = 0; if (fs) strlcpy(g_up_fs[0], fs, sizeof g_up_fs[0]); if (compose_fs) strlcpy(g_up_fs[1], compose_fs, sizeof g_up_fs[1]);
    g_up_color[0] = color; g_up_color[1] = compose_color;
}
typedef struct {
    FX_SCALER sc; id<MTLTexture> out; NSUInteger iw, ih, ow, oh; MTLPixelFormat fi, fo; int mode;   // the scaler in use, what it was made for, its own output (compose)
    NSUInteger bad_iw, bad_ih, bad_ow, bad_oh; MTLPixelFormat bad_fi, bad_fo;                                         // the last configuration that could not be made (not tried again)
    uint64_t done, kept[7];   // kept: draws left to the game, by reason (fxp_why)
} fxp_ctx;
static struct {
    fxp_ctx c[2]; id<MTLFence> fence; int unavailable;
#if !DM_NVN_M3
    id<MTL4Compiler> comp;
#endif
    __strong id<MTLTexture> sub; int sub_tid; uint64_t sub_bound;   // fxp_scene: this draw's scene texture ID and what it samples instead; bindings made with it
} g_fxp;
static const char *const fxp_name[2] = {"in place of the game's upscale", "before the game's compose"};
static const char *const fxp_why[7] = {"the scaler not made", "the targets", "blending / write mask / depth test", "the textures", "viewport / scissor", "the constants", "TBDR rewrites"};
// The scaler for src → ow x oh: into dst (its format), or into a texture of its own (dst nil: the scene's format)
static int fxp_ready(fxp_ctx *c, int which, id<MTLTexture> src, id<MTLTexture> dst, NSUInteger ow, NSUInteger oh, int mode) {
    MTLPixelFormat fo = dst ? dst.pixelFormat : src.pixelFormat;
#if !DM_METALFX
    (void)c; (void)which; (void)ow; (void)oh; (void)mode; (void)fo;
    if (!g_fxp.unavailable) { g_fxp.unavailable = 1; MLOG("MetalFX for the game's upscale: the iOS Simulator has no MetalFX; the game's own"); }
    return 0;
#else
    if (c->sc && c->iw == src.width && c->ih == src.height && c->ow == ow && c->oh == oh && c->fi == src.pixelFormat && c->fo == fo && c->mode == mode) return 1;
    if (g_fxp.unavailable || (c->bad_iw == src.width && c->bad_ih == src.height && c->bad_ow == ow && c->bad_oh == oh && c->bad_fi == src.pixelFormat && c->bad_fo == fo)) return 0;
    const char *why = NULL;
    if (![MTLFXSpatialScalerDescriptor supportsDevice:g_dev]) why = "the device has no spatial scaler";
#if !DM_NVN_M3
    else if (![MTLFXSpatialScalerDescriptor supportsMetal4FX:g_dev]) why = "the device has no Metal 4 MetalFX";
    else if (!g_fxp.comp) { NSError *e = nil; if (!(g_fxp.comp = [g_dev newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&e])) why = "no Metal 4 compiler"; }
#endif
    if (why) { g_fxp.unavailable = 1; MLOG("[red] MetalFX for the game's upscale: %s; the game's own", why); return 0; }
    if (!g_fxp.fence) g_fxp.fence = [g_dev newFence];
    MTLFXSpatialScalerDescriptor *d = [MTLFXSpatialScalerDescriptor new];
    d.colorTextureFormat = src.pixelFormat; d.outputTextureFormat = fo;
    d.inputWidth = src.width; d.inputHeight = src.height; d.outputWidth = ow; d.outputHeight = oh;
    d.colorProcessingMode = (MTLFXSpatialScalerColorProcessingMode)mode;
    uint64_t t0 = now_ns();
#if DM_NVN_M3
    FX_SCALER sc = [d newSpatialScalerWithDevice:g_dev];
#else
    FX_SCALER sc = [d newSpatialScalerWithDevice:g_dev compiler:g_fxp.comp];
#endif
    id<MTLTexture> out = nil;
    if (!sc) why = "no scaler for them";
    else if ((src.usage & sc.colorTextureUsage) != sc.colorTextureUsage) why = "the scene's usage lacks what MetalFX reads with";
    else if (dst && ((dst.usage & sc.outputTextureUsage) != sc.outputTextureUsage || dst.storageMode == MTLStorageModeMemoryless)) why = "the canvas's usage lacks what MetalFX writes with";
    else if (!dst) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fo width:ow height:oh mipmapped:NO];
        td.storageMode = MTLStorageModePrivate; td.usage = sc.outputTextureUsage | MTLTextureUsageShaderRead;
        if (!(out = [g_dev newTextureWithDescriptor:td])) why = "no texture for its output";
    }
    if (why) {
        c->bad_iw = src.width; c->bad_ih = src.height; c->bad_ow = ow; c->bad_oh = oh; c->bad_fi = src.pixelFormat; c->bad_fo = fo;
        MLOG("[red] MetalFX %s: %s (%lux%lu format %lu → %lux%lu format %lu); the game's own", fxp_name[which], why, (unsigned long)src.width, (unsigned long)src.height,
             (unsigned long)src.pixelFormat, (unsigned long)ow, (unsigned long)oh, (unsigned long)fo);
        return 0;
    }
    defer_obj_unref(c->sc); defer_obj_release(c->out);   // replaced: encoded work may still use them
    if (out) res_add(out);
    c->sc = sc; c->out = out; c->iw = src.width; c->ih = src.height; c->ow = ow; c->oh = oh; c->fi = src.pixelFormat; c->fo = fo; c->mode = mode;
    MLOG("MetalFX %s: a scaler for %lux%lu (format %lu) → %lux%lu (format %lu)%s, colour processing %d, made in %.1f ms", fxp_name[which], (unsigned long)src.width, (unsigned long)src.height,
         (unsigned long)src.pixelFormat, (unsigned long)ow, (unsigned long)oh, (unsigned long)fo, out ? " of its own" : "", mode, (now_ns() - t0) / 1e6);
    return 1;
#endif
}
static void fxp_encode(fxp_ctx *c, id<MTLTexture> src, NSUInteger cw, NSUInteger ch, id<MTLTexture> out) {
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
#if !DM_METALFX
    (void)c; (void)src; (void)cw; (void)ch; (void)out;   // (never reached: fxp_ready makes no scaler)
#else
    FX_SCALER sc = c->sc;
    sc.colorTexture = src; sc.outputTexture = out; sc.inputContentWidth = cw; sc.inputContentHeight = ch;
#if DM_NVN_M3
    [(id<MTLFXSpatialScaler>)sc encodeToCommandBuffer:(CBUF)g_cb];
#else
    CENC e = enc_c_begin(NULL); [e updateFence:g_fxp.fence afterEncoderStages:MTLStageDispatch]; enc_c_end(e);   // (enc_c_begin: after everything before, on this command buffer)
    sc.fence = g_fxp.fence;
    [(id<MTL4FXSpatialScaler>)sc encodeToCommandBuffer:(CBUF)g_cb];
#endif
#endif
}
static void fxp_log(int which, NvShader *fs, NSUInteger cw, NSUInteger ch, id<MTLTexture> src, NSUInteger ow, NSUInteger oh) {
    fxp_ctx *c = &g_fxp.c[which]; uint64_t nd = ++c->done, kept = 0; for (int i = 0; i < 7; i++) kept += c->kept[i];
    char sb[64] = ""; if (which == 1) snprintf(sb, sizeof sb, "; the compose pass bound to it %llu times", (unsigned long long)g_fxp.sub_bound);   // (before this one's)
    if (!(nd & (nd - 1))) MLOG("MetalFX %s (%.16s): %llu times so far (the scene's %lux%lu of %lux%lu to the %lux%lu canvas; the game's own %llu times%s)", fxp_name[which], fs->key,
                               (unsigned long long)nd, (unsigned long)cw, (unsigned long)ch, (unsigned long)src.width, (unsigned long)src.height, (unsigned long)ow, (unsigned long)oh, (unsigned long long)kept, sb);
}
static int fxp_refuse(int which, int why, NvShader *fs, id<MTLTexture> src, id<MTLTexture> dst) {
    if (!g_fxp.c[which].kept[why]++) MLOG("MetalFX %s (%.16s): the game's own (%s; first case: %d targets, scene %lux%lu, canvas %lux%lu)", fxp_name[which], fs->key, fxp_why[why],
                                          R.rt.n, (unsigned long)src.width, (unsigned long)src.height, (unsigned long)dst.width, (unsigned long)dst.height);
    return 0;
}
static int fxp_tbdr_busy(void) { return defer_has_in_p() || g_fz.active || g_lay.active; }
static id<MTLTexture> fxp_scene_tex(void) {   // the texture at fragment slot 0
    int tid = h_tid(S.tex[NVN_SHADER_STAGE_FRAGMENT][0]); id<MTLTexture> t = nil;
    pthread_mutex_lock(&g_rmu); if (tid) t = tex_by_id(tid); pthread_mutex_unlock(&g_rmu);
    return t;
}
// 1: MetalFX wrote the canvas and the draw is left out
static int fxp_dock(NvProg *pr) {
    __unsafe_unretained NvShader *fs = pr ? pr->sh[1] : nil;
    if (!fs || strncmp(fs->key, g_up_fs[0], 16) || !upscale_on()) return 0;
    const view_t *cv = &R.rt.cview[0]; int hv = R.rt.has_cview[0], why = 0;
    if (R.rt.n < 1 || !R.rt.color[0]) why = 1;
    for (int i = 1; !why && i < R.rt.n && i < 8; i++) if (R.rt.color[i]) why = 1;
    if (!why && hv && ((cv->set_levels && cv->base_level) || (cv->set_layers && cv->min_layer) || (cv->set_format && cv->format != R.rt.color[0]->d.format))) why = 1;
    if (!why && ((S.color.blend_en & 1) || (S.chmask.mask & 0xf) != 0xf || S.ms.a2c || (R.rt.depth && (S.ds.dt || S.ds.st)))) why = 2;
    id<MTLTexture> src = nil, dst = nil;
    if (!why) {
        src = fxp_scene_tex(); dst = tex_mtl(R.rt.color[0]);
        if (!src || !dst || src.textureType != MTLTextureType2D || dst.textureType != MTLTextureType2D || src.width >= dst.width || src.height >= dst.height) why = 3;
    }
    if (!why && ((S.has_vp && !(S.vp[0][0] == 0 && S.vp[0][1] == 0 && S.vp[0][2] == (float)dst.width && S.vp[0][3] == (float)dst.height)) || !scissor_full(R.rt.color[0], cv, hv))) why = 4;
    NSUInteger cw = 0, ch = 0;
    if (!why) {
        float k[4], o[2]; uint32_t ow = 0, oh = 0; const int f = NVN_SHADER_STAGE_FRAGMENT;   // (ubo_float's reference: slot << 8 | vector << 2 | component)
        if (!ubo_float(f, 0, &k[0]) || !ubo_float(f, 1, &k[1]) || !ubo_float(f, 2, &k[2]) || !ubo_float(f, 3, &k[3]) || !ubo_float(f, 16, &o[0]) || !ubo_float(f, 17, &o[1])) why = 5;
        else {
            memcpy(&ow, &o[0], 4); memcpy(&oh, &o[1], 4);
            double fw = (double)k[0] * ow, fh = (double)k[1] * oh; cw = (NSUInteger)llround(fw); ch = (NSUInteger)llround(fh);
#ifndef DM_TEST_FXP_NO_CONST   // negative control of t_fxp only: the constants' layout not checked
            if (ow != dst.width || oh != dst.height || !cw || !ch || cw > src.width || ch > src.height || fabs(fw - (double)cw) > 0.02 || fabs(fh - (double)ch) > 0.02 ||
                fabsf(k[2] - (0.5f * k[0] - 0.5f)) > 1e-4f || fabsf(k[3] - (0.5f * k[1] - 0.5f)) > 1e-4f) why = 5;
#else
            if (!cw || !ch || cw > src.width || ch > src.height) { cw = src.width; ch = src.height; }
#endif
        }
    }
    if (!why && fxp_tbdr_busy()) why = 6;
    if (why) return fxp_refuse(0, why, fs, src, dst);
    if (!fxp_ready(&g_fxp.c[0], 0, src, dst, dst.width, dst.height, g_up_color[0])) { g_fxp.c[0].kept[0]++; return 0; }
#ifdef DM_TEST_FXP_NO_CONTENT   // negative control of t_fxp only: the whole scene texture upscaled, not its valid region
    cw = src.width; ch = src.height;
#endif
    fxp_encode(&g_fxp.c[0], src, cw, ch, dst);
#ifndef DM_TEST_FXP_CLEAR_KEPT   // negative control of t_fxp only: a pending clear of the canvas kept (it wipes the picture when the next pass opens)
    R.clear_c[0] = 0;
#endif
    fxp_log(0, fs, cw, ch, src, dst.width, dst.height);
#ifdef DM_TEST_FXP_DRAWN   // negative control of t_fxp only: the game's draw encoded as well
    return 0;
#endif
    return 1;
}
// The compose pass: MetalFX upscales the scene it samples at slot 0 to the canvas size first; fxp_sub binds that for this draw only
static void fxp_sub_end(void) {   // the last draw's substitute
#ifndef DM_TEST_FXP_SUB_KEPT   // negative control of t_fxp only: the substitute kept for the draws after the compose pass
    if (g_fxp.sub) { g_fxp.sub = nil; g_fxp.sub_tid = 0; args_dirty_all(); }
#endif
}
static void fxp_scene(NvProg *pr) {
    __unsafe_unretained NvShader *fs = pr ? pr->sh[1] : nil;
    if (!fs || strncmp(fs->key, g_up_fs[1], 16) || !upscale_on()) return;
    int why = R.rt.n < 1 || !R.rt.color[0] ? 1 : 0;
    id<MTLTexture> src = nil, dst = nil;
    if (!why) {
        src = fxp_scene_tex(); dst = tex_mtl(R.rt.color[0]);
        if (!src || !dst || src.textureType != MTLTextureType2D || src.width >= dst.width || src.height >= dst.height) {
            if (src && dst && src.width >= dst.width && src.height >= dst.height) return;   // not smaller: nothing to upscale, not counted
            why = 3;
        }
    }
    if (!why && fxp_tbdr_busy()) why = 6;
    if (why) { fxp_refuse(1, why, fs, src, dst); return; }
    fxp_ctx *c = &g_fxp.c[1];
    if (!fxp_ready(c, 1, src, nil, dst.width, dst.height, g_up_color[1])) { c->kept[0]++; return; }
    fxp_encode(c, src, src.width, src.height, c->out);
#ifndef DM_TEST_FXP_SCENE_NOT_BOUND   // negative control of t_fxp only: upscaled, but the draw samples the scene as it was
    g_fxp.sub = c->out; g_fxp.sub_tid = h_tid(S.tex[NVN_SHADER_STAGE_FRAGMENT][0]); args_dirty_all();
#endif
    fxp_log(1, fs, src.width, src.height, src, dst.width, dst.height);
}
// At the argument encoding: what the compose pass samples in place of the scene (fxp_scene), else t
static inline id<MTLTexture> fxp_sub(int stage, int tid, id<MTLTexture> t, uint64_t *rid) {
    if (__builtin_expect(g_fxp.sub != nil, 0) && stage == NVN_SHADER_STAGE_FRAGMENT && tid == g_fxp.sub_tid) { g_fxp.sub_bound++; *rid = g_fxp.sub.gpuResourceID._impl; return g_fxp.sub; }
    return t;
}
