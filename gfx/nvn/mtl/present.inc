// gfx/nvn/mtl/present.inc, part of nvn_mtl.m (included there in order, one translation unit): MetalFX upscaling of the presented picture, live shots, the perf line, present_now
// ---------- MetalFX spatial upscaling of the presented picture ----------
// Mode: mtl_upscale_set (iOS Settings page), else DM_UPSCALE=metalfx; off = the present pass scales bilinearly. With it on and the picture shown
// larger than the game renders it (crop rectangle at the texture's origin), MetalFX's spatial scaler first scales the picture to its on-screen size
// and the present pass copies that 1:1.
// Metal 4 (MTL4FXSpatialScaler): neither the headers nor the documentation describe its synchronization. Logging proxies around the command buffer
// and the encoders it opens (macOS 26, Apple M5) showed: it adds a residency set of its own (useResidencySet:), encodes two render passes, and with
// `fence` set its first pass waits on that fence before the fragment stage and its last pass updates it after the fragment stage; with no fence its
// first pass waits for nothing. So (1) the present command buffer goes behind a queue-wide signal / wait of g_fx.ev (MTL4CommandQueue signalEvent:
// "after all GPU work prior to this point is complete"; the game's frames and the presents share the one queue), ordering the scaler after the game's
// frame, and (2) the present pass waits on the fence before its fragment stage. The older API (DM_NVN_M3) tracks these hazards itself.
static _Atomic int g_upscale = -1;
void mtl_upscale_set(int on) { int v = on != 0; if (atomic_exchange(&g_upscale, v) != v) MLOG("MetalFX upscaling %s", v ? "on" : "off"); }
static int upscale_on(void) {
    int m = atomic_load(&g_upscale);
    if (m < 0) { const char *e = getenv("DM_UPSCALE"); int v = e && (!strcmp(e, "metalfx") || !strcmp(e, "1")), x = -1; atomic_compare_exchange_strong(&g_upscale, &x, v); m = atomic_load(&g_upscale); }
    return m;
}
static struct {   // present thread only
    FX_SCALER scaler; id<MTLTexture> out;
    NSUInteger iw, ih, ow, oh; MTLPixelFormat fmt;   // iw x ih: the whole input texture (MetalFX asserts it matches the descriptor); the crop is its content size
    id<MTLFence> fence;
#if !DM_NVN_M3
    id<MTL4Compiler> compiler; id<MTLEvent> ev; uint64_t ev_n; id<MTLResidencySet> rs;
#endif
    id old[8]; int old_i;   // replaced scalers and outputs: kept for four more replacements (at most 3 presents are in flight, one replacement per present)
    int broken, state;      // broken: MetalFX unavailable here, bilinear from now on; state: what was logged last (1 upscaling, 2 not needed, 3 crop not at origin)
    NSUInteger log_cw, log_ch, log_ow, log_oh;   // the content → output sizes last logged
} g_fx;
static void fx_retire(id o) {
    if (!o) return;
    id gone = g_fx.old[g_fx.old_i]; g_fx.old[g_fx.old_i] = o; g_fx.old_i = (g_fx.old_i + 1) % 8;
#if !DM_NVN_M3
    if (gone && [gone conformsToProtocol:@protocol(MTLTexture)]) { [g_fx.rs removeAllocation:gone]; [g_fx.rs commit]; }
#else
    (void)gone;
#endif
}
static void fx_broken(const char *why) { g_fx.broken = 1; MLOG("[red] MetalFX upscaling unavailable (%s): bilinear scaling", why); }
static int fx_ready(id<MTLTexture> src, NSUInteger ow, NSUInteger oh) {
    if (g_fx.broken) return 0;
#if !DM_METALFX
    (void)src; (void)ow; (void)oh; fx_broken("the iOS Simulator has no MetalFX"); return 0;
#else
    NSUInteger cw = src.width, ch = src.height;
    if (g_fx.scaler && g_fx.iw == cw && g_fx.ih == ch && g_fx.ow == ow && g_fx.oh == oh && g_fx.fmt == src.pixelFormat) return 1;
    if (![MTLFXSpatialScalerDescriptor supportsDevice:g_dev]) { fx_broken("the device does not support the spatial scaler"); return 0; }
    if (!g_fx.fence) g_fx.fence = [g_dev newFence];
#if !DM_NVN_M3
    if (![MTLFXSpatialScalerDescriptor supportsMetal4FX:g_dev]) { fx_broken("the device does not support MetalFX on Metal 4"); return 0; }
    NSError *e = nil;
    if (!g_fx.compiler && !(g_fx.compiler = [g_dev newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&e])) { fx_broken(e.localizedDescription.UTF8String ?: "no Metal 4 compiler"); return 0; }
    if (!g_fx.rs && !(g_fx.rs = [g_dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&e])) { fx_broken(e.localizedDescription.UTF8String ?: "no residency set"); return 0; }
    if (!g_fx.ev) g_fx.ev = [g_dev newEvent];
#endif
    MTLFXSpatialScalerDescriptor *d = [MTLFXSpatialScalerDescriptor new];
    d.colorTextureFormat = d.outputTextureFormat = src.pixelFormat;
    d.inputWidth = cw; d.inputHeight = ch; d.outputWidth = ow; d.outputHeight = oh;
    d.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;   // the picture's bytes are gamma encoded (the present samples the swapchain through a non-sRGB view)
#if DM_NVN_M3
    FX_SCALER sc = [d newSpatialScalerWithDevice:g_dev];
#else
    FX_SCALER sc = [d newSpatialScalerWithDevice:g_dev compiler:g_fx.compiler];
#endif
    if (!sc) { MLOG("[red] MetalFX: no spatial scaler for %lux%lu → %lux%lu format %lu", (unsigned long)cw, (unsigned long)ch, (unsigned long)ow, (unsigned long)oh, (unsigned long)src.pixelFormat); fx_broken("scaler creation failed"); return 0; }
    if ((src.usage & sc.colorTextureUsage) != sc.colorTextureUsage) { MLOG("[red] MetalFX: the game picture's usage %#lx lacks %#lx", (unsigned long)src.usage, (unsigned long)sc.colorTextureUsage); fx_broken("input texture usage"); return 0; }
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat width:ow height:oh mipmapped:NO];
    td.storageMode = MTLStorageModePrivate; td.usage = sc.outputTextureUsage | MTLTextureUsageShaderRead;
    id<MTLTexture> out = [g_dev newTextureWithDescriptor:td];
    if (!out) { fx_broken("output texture creation failed"); return 0; }
    fx_retire(g_fx.scaler); fx_retire(g_fx.out);
    g_fx.scaler = sc; g_fx.out = out; g_fx.iw = cw; g_fx.ih = ch; g_fx.ow = ow; g_fx.oh = oh; g_fx.fmt = src.pixelFormat;
#if !DM_NVN_M3
    [g_fx.rs addAllocation:out]; [g_fx.rs commit];
#endif
    MLOG("MetalFX spatial scaler for a %lux%lu texture → %lux%lu (format %lu)", (unsigned long)cw, (unsigned long)ch, (unsigned long)ow, (unsigned long)oh, (unsigned long)src.pixelFormat);
    return 1;
#endif
}
static void fx_state(int st) {
    if (st == g_fx.state) return;
    g_fx.state = st;
    if (st == 2) MLOG("MetalFX upscaling: the picture is not shown larger than the game renders it in both directions, bilinear");
    if (st == 3) MLOG("MetalFX upscaling: the game's crop rectangle does not start at the texture's origin, bilinear");
}
// Encodes the upscale into cb and returns its output (out_crop = the whole output; *wait = the fence the present pass must wait on), or nil: bilinear
static id<MTLTexture> fx_apply(CBUF cb, id<MTLTexture> src0, const int crop[4], id<MTLTexture> dst, int out_crop[4], id<MTLFence> *wait) {
    *wait = nil;
    if (!upscale_on()) { g_fx.state = 0; return nil; }
    if (g_fx.broken) return nil;
    id<MTLTexture> src = present_src(src0);
    NSUInteger cw = src.width, ch = src.height;
    if (crop && (crop[2] || crop[3])) {
        if (crop[0] || crop[1]) { fx_state(3); return nil; }
        if (crop[2] <= 0 || crop[3] <= 0 || (NSUInteger)crop[2] > src.width || (NSUInteger)crop[3] > src.height) return nil;   // present_encode_m4 reports it
        cw = (NSUInteger)crop[2]; ch = (NSUInteger)crop[3];
    }
    MTLViewport vp = present_vp(dst.width, dst.height, cw, ch);
    NSUInteger ow = (NSUInteger)vp.width, oh = (NSUInteger)vp.height;
    if (ow <= cw || oh <= ch) { fx_state(2); return nil; }   // (larger in both directions only: the wide picture stretches one way)
    if (!fx_ready(src, ow, oh)) return nil;
    fx_state(1);
    if (cw != g_fx.log_cw || ch != g_fx.log_ch || ow != g_fx.log_ow || oh != g_fx.log_oh) {   // the crop changes without a new scaler (e.g. the game lowers its resolution)
        g_fx.log_cw = cw; g_fx.log_ch = ch; g_fx.log_ow = ow; g_fx.log_oh = oh;
        MLOG("MetalFX upscales the game's %lux%lu picture to %lux%lu on screen", (unsigned long)cw, (unsigned long)ch, (unsigned long)ow, (unsigned long)oh);
    }
#if DM_METALFX
    FX_SCALER sc = g_fx.scaler;
    sc.colorTexture = src; sc.inputContentWidth = cw; sc.inputContentHeight = ch; sc.outputTexture = g_fx.out;
#if DM_NVN_M3
    [(id<MTLFXSpatialScaler>)sc encodeToCommandBuffer:cb];
#else
    sc.fence = g_fx.fence;
    [cb useResidencySet:g_fx.rs];
    [(id<MTL4FXSpatialScaler>)sc encodeToCommandBuffer:cb];
    *wait = g_fx.fence;
#endif
#endif
    out_crop[0] = out_crop[1] = 0; out_crop[2] = (int)ow; out_crop[3] = (int)oh;
    return g_fx.out;
}
#if !DM_NVN_M3
// Right before committing a command buffer that fx_apply encoded into: everything queued so far finishes before anything queued after starts
static void fx_commit_sync(void) { g_fx.ev_n++; [g_q4 signalEvent:g_fx.ev value:g_fx.ev_n]; [g_q4 waitForEvent:g_fx.ev value:g_fx.ev_n]; }
#endif
static int present_encode_m4(CBUF cb, id<MTLTexture> src0, id<MTLTexture> dst, const int crop[4], int uv_i, id<MTLFence> wait) {
    id<MTLTexture> src = present_src(src0);
    NSUInteger cx = 0, cy = 0, cw = src.width, ch = src.height;
    if (crop && (crop[2] || crop[3])) {
        if (crop[0] < 0 || crop[1] < 0 || crop[2] <= 0 || crop[3] <= 0 ||
            (uint64_t)crop[0] + crop[2] > src.width || (uint64_t)crop[1] + crop[3] > src.height) {
            MLOG("[red] present crop out of bounds: %d,%d %dx%d / source texture %lux%lu", crop[0], crop[1], crop[2], crop[3], (unsigned long)src.width, (unsigned long)src.height); return 0;
        }
        cx = crop[0]; cy = crop[1]; cw = crop[2]; ch = crop[3];
    }
    present_pipeline(dst.pixelFormat);
    double dw = dst.width, dh = dst.height;
    MTLViewport vp = present_vp(dw, dh, cw, ch);
    int full = vp.originX == 0 && vp.originY == 0 && vp.width == dw && vp.height == dh;
    RPD *rp = [RPD new];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = full ? MTLLoadActionDontCare : MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    RENC en = [cb renderCommandEncoderWithDescriptor:rp];
    if (!en) { MLOG("[red] M4 present render encoder creation failed"); return 0; }
#if DM_NVN_M3
    [en setFragmentTexture:src atIndex:0];
    [en setFragmentSamplerState:g_blit_samp atIndex:0];
#else
    [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    if (wait) [en waitForFence:wait beforeEncoderStages:MTLRenderStageFragment];   // MetalFX's output (fx_apply)
    [en setArgumentTable:g_present_args atStages:MTLRenderStageFragment];
    [g_present_args setTexture:src.gpuResourceID atIndex:0];
    [g_present_args setSamplerState:g_blit_samp.gpuResourceID atIndex:0];
#endif
    float *uv = g_present_uv[uv_i].contents;
    uv[0] = (float)cx / src.width; uv[1] = (float)cy / src.height; uv[2] = (float)cw / src.width; uv[3] = (float)ch / src.height;
    uv[4] = (cx + 0.5f) / src.width; uv[5] = (cy + 0.5f) / src.height; uv[6] = (cx + cw - 0.5f) / src.width; uv[7] = (cy + ch - 0.5f) / src.height;
#if DM_NVN_M3
    [en setFragmentBuffer:g_present_uv[uv_i] offset:0 atIndex:0];
#else
    [g_present_args setAddress:g_present_uv[uv_i].gpuAddress atIndex:0];
#endif
    [en setViewport:vp]; [en setRenderPipelineState:g_blit_pso];
    [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    m4_end(en);
    return 1;
}
static double g_link_it_tin;   // Enqueue time of the frame the display-link callback is presenting (read and written only on the display-link thread)
// DM_NVN_LIVESHOT=<first present>,<count>:<path with %d> (window mode, diagnostics): the game picture each present really reads, copied in the present's
// own command buffer after it (no GPU wait: the frames keep the pacing of play, unlike DM_NVN_SHOT, which waits for the GPU before each shot) and
// written as PNG once that command buffer completes; at most 64 consecutive presents. For what only shows on screen
static int g_live_first = -2, g_live_n; static char g_live_path[1024];
static void png_write_buf(id<MTLBuffer> buf, size_t w, size_t h, int bgra, const char *path) {
    size_t row = w * 4; CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGBitmapInfo bi = bgra ? (kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little) : (CGBitmapInfo)kCGImageAlphaNoneSkipLast;
    CGContextRef ctx = CGBitmapContextCreate(buf.contents, w, h, 8, row, cs, bi); CGImageRef img = ctx ? CGBitmapContextCreateImage(ctx) : NULL;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, strlen(path), false);
    CGImageDestinationRef dst = url ? CGImageDestinationCreateWithURL(url, (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL) : NULL;
    int ok = 0; if (img && dst) { CGImageDestinationAddImage(dst, img, NULL); ok = CGImageDestinationFinalize(dst); }
    if (dst) CFRelease(dst); if (url) CFRelease(url); CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
    MLOG("live shot %zux%zu → %s %s", w, h, path, ok ? "success" : "failed");
}
static id<MTLBuffer> live_copy(id cb, id<MTLTexture> src, uint64_t n, char *path, size_t pn) {   // M4 present command buffer; nil when this present is not captured
    if (g_live_first == -2) { g_live_first = -1; const char *e = getenv("DM_NVN_LIVESHOT"), *c = e ? strchr(e, ':') : NULL;
        if (c && sscanf(e, "%d,%d", &g_live_first, &g_live_n) == 2 && g_live_n > 0 && g_live_n <= 64) strlcpy(g_live_path, c + 1, sizeof g_live_path); else g_live_first = -1; }
    if (g_live_first < 0 || n < (uint64_t)g_live_first || n >= (uint64_t)(g_live_first + g_live_n)) return nil;
    MTLPixelFormat f = src.pixelFormat;
    if (f != MTLPixelFormatRGBA8Unorm && f != MTLPixelFormatRGBA8Unorm_sRGB && f != MTLPixelFormatBGRA8Unorm && f != MTLPixelFormatBGRA8Unorm_sRGB) { RED_ONCE(60, "live shot: format %lu not supported", (unsigned long)f); return nil; }
    size_t w = src.width, h = src.height; id<MTLBuffer> buf = [g_dev newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared]; if (!buf) return nil;
    res_add(buf); res_commit();   // resident before this command buffer is committed
    id e = enc_b_begin(cb);
#if !DM_NVN_M3
    [(id<MTL4ComputeCommandEncoder>)e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
#endif
    enc_b_copy_texture_buffer(e, src, 0, 0, MTLOriginMake(0, 0, 0), MTLSizeMake(w, h, 1), buf, 0, w * 4, w * h * 4);
    m4_end(e);
    snprintf(path, pn, g_live_path, (int)n);
    return buf;
}
// DM_NVN_LIVEROWS=<file> (window mode, diagnostics): at every present, 16 evenly spaced rows of the picture the present reads are copied in the present's
// command buffer (no GPU wait) and written as one line: present number, row means (luma), then each row subsampled every 8 pixels (hex). A frame that
// differs from both neighbours over the whole screen shows in these rows; DM_NVN_LIVESHOT then takes those presents in full
enum { LR_ROWS = 16, LR_RING = 8 };
static FILE *g_lr_f; static int g_lr_on = -1; static __strong id<MTLBuffer> g_lr_buf[LR_RING]; static pthread_mutex_t g_lr_mu = PTHREAD_MUTEX_INITIALIZER;
static id<MTLBuffer> live_rows(id cb, id<MTLTexture> src, uint64_t n) {
    if (g_lr_on < 0) { const char *e = getenv("DM_NVN_LIVEROWS"); g_lr_on = e && (g_lr_f = fopen(e, "w")) != NULL; if (e && !g_lr_on) MLOG("[red] DM_NVN_LIVEROWS: cannot open %s", e); }
    if (!g_lr_on) return nil;
    MTLPixelFormat f = src.pixelFormat;
    if (f != MTLPixelFormatRGBA8Unorm && f != MTLPixelFormatRGBA8Unorm_sRGB && f != MTLPixelFormatBGRA8Unorm && f != MTLPixelFormatBGRA8Unorm_sRGB) return nil;
    size_t w = src.width, h = src.height, row = w * 4; int k = (int)(n % LR_RING);
    if (!g_lr_buf[k] || g_lr_buf[k].length < row * LR_ROWS) { g_lr_buf[k] = [g_dev newBufferWithLength:row * LR_ROWS options:MTLResourceStorageModeShared]; res_add(g_lr_buf[k]); res_commit(); }
    id e = enc_b_begin(cb);
#if !DM_NVN_M3
    [(id<MTL4ComputeCommandEncoder>)e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
#endif
    for (int r = 0; r < LR_ROWS; r++) enc_b_copy_texture_buffer(e, src, 0, 0, MTLOriginMake(0, (h * (2 * r + 1)) / (2 * LR_ROWS), 0), MTLSizeMake(w, 1, 1), g_lr_buf[k], row * r, row, row);
    m4_end(e);
    return g_lr_buf[k];
}
static void live_rows_write(id<MTLBuffer> b, size_t w, uint64_t n) {   // completion handler: the presents of a ring slot never overlap (at most 3 in flight)
    const uint8_t *p = b.contents; char line[LR_ROWS * (1920 / 8 + 1) * 2 + LR_ROWS * 8 + 64]; size_t o = 0;
    o += (size_t)snprintf(line + o, sizeof line - o, "%llu", (unsigned long long)n);
    for (int r = 0; r < LR_ROWS; r++) { uint64_t s = 0; for (size_t x = 0; x < w; x++) { const uint8_t *q = p + (size_t)r * w * 4 + x * 4; s += q[0] + q[1] + q[2]; } o += (size_t)snprintf(line + o, sizeof line - o, "\t%.2f", s / (3.0 * w)); }
    for (int r = 0; r < LR_ROWS && o + 2 * (w / 8) + 4 < sizeof line; r++) { line[o++] = '\t';
        for (size_t x = 0; x < w; x += 8) { const uint8_t *q = p + (size_t)r * w * 4 + x * 4; o += (size_t)snprintf(line + o, sizeof line - o, "%02x", (q[0] + q[1] + q[2]) / 3); } }
    line[o] = 0; pthread_mutex_lock(&g_lr_mu); fprintf(g_lr_f, "%s\n", line); fflush(g_lr_f); pthread_mutex_unlock(&g_lr_mu);
}
// DM_PRESENT_LUMA=<file> (plan item 145; bayonetta3-mobile's copy of blade2-mobile compares the same frame's guest image and drawable, MIT, written
// anew): PL_ROWS rows of the present's guest image (its crop) and of the drawable it became, copied after the present pass and averaged ((r + g +
// b) / 3 of the stored bytes); a line per present ("present, source, output, output / source"), and a red line, once, when the output is under
// 35 % of a source of 16 or more: the present path (scaling, MetalFX, the drawable's format) darkened the picture. Needs a window: the layer is made
// readable for it (framebufferOnly off). 8-bit RGBA / BGRA only. Unit test gfx/tests/t_pluma.m
#define PL_ROWS 18
static FILE *g_pl_f; static int g_pl_on = -1; static pthread_mutex_t g_pl_mu = PTHREAD_MUTEX_INITIALIZER; static uint64_t g_pl_n, g_pl_dark;
static __strong id<MTLBuffer> g_pl_buf[3][2];
static int pl_on(void) {
    if (g_pl_on < 0) { const char *e = getenv("DM_PRESENT_LUMA"); g_pl_on = e && (g_pl_f = fopen(e, "w")) != NULL;
        if (e && !g_pl_on) MLOG("[red] DM_PRESENT_LUMA: cannot open %s", e);
        if (g_pl_on) { fprintf(g_pl_f, "present\tsource\toutput\tratio\n"); MLOG("DM_PRESENT_LUMA: the guest image and the drawable of each present compared in %s", e); } }
    return g_pl_on;
}
static int pl_fmt_ok(MTLPixelFormat f) { return f == MTLPixelFormatRGBA8Unorm || f == MTLPixelFormatRGBA8Unorm_sRGB || f == MTLPixelFormatBGRA8Unorm || f == MTLPixelFormatBGRA8Unorm_sRGB; }
static double pl_mean(const uint8_t *p, size_t w, int rows) { uint64_t sum = 0; for (size_t k = 0; k < w * (size_t)rows; k++) sum += p[4 * k] + p[4 * k + 1] + p[4 * k + 2]; return w ? sum / (3.0 * (double)w * rows) : 0; }
static int pl_dark(double src, double out) { return src >= 16 && out < 0.35 * src; }
static id<MTLBuffer> pl_buf(int slot, int k, size_t w) {
    if (!g_pl_buf[slot][k] || g_pl_buf[slot][k].length < w * 4 * PL_ROWS) { g_pl_buf[slot][k] = [g_dev newBufferWithLength:w * 4 * PL_ROWS options:MTLResourceStorageModeShared]; res_add(g_pl_buf[slot][k]); res_commit(); }
    return g_pl_buf[slot][k];
}
// Copies the rows into this slot's buffers (encoded on cb after the present pass); *ws / *wo the row widths, 0 when a format is not read
static void pl_encode(id cb, id<MTLTexture> src, const int crop[4], id<MTLTexture> out, int slot, id<MTLBuffer> *bs, id<MTLBuffer> *bo, size_t *ws, size_t *wo) {
    *ws = *wo = 0;
    if (!pl_fmt_ok(src.pixelFormat) || !pl_fmt_ok(out.pixelFormat) || out.framebufferOnly) { static int once; if (!once++) MLOG("DM_PRESENT_LUMA: formats %lu / %lu (or a drawable not readable): not compared", (unsigned long)src.pixelFormat, (unsigned long)out.pixelFormat); return; }
    NSUInteger x = 0, y = 0, w = src.width, h = src.height;
#ifndef DM_TEST_PLUMA_NO_CROP   // negative control of t_pluma only: the whole guest image, not the part shown
    if (crop && (crop[2] || crop[3])) { x = (NSUInteger)crop[0]; y = (NSUInteger)crop[1]; w = (NSUInteger)crop[2]; h = (NSUInteger)crop[3]; }
#endif
    *bs = pl_buf(slot, 0, w); *bo = pl_buf(slot, 1, out.width);
    id e = enc_b_begin(cb);
#if !DM_NVN_M3
    [(id<MTL4ComputeCommandEncoder>)e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
#endif
    for (int r = 0; r < PL_ROWS; r++) {
        enc_b_copy_texture_buffer(e, src, 0, 0, MTLOriginMake(x, y + (h * (2 * r + 1)) / (2 * PL_ROWS), 0), MTLSizeMake(w, 1, 1), *bs, w * 4 * r, w * 4, w * 4);
        enc_b_copy_texture_buffer(e, out, 0, 0, MTLOriginMake(0, (out.height * (2 * r + 1)) / (2 * PL_ROWS), 0), MTLSizeMake(out.width, 1, 1), *bo, out.width * 4 * r, out.width * 4, out.width * 4);
    }
    m4_end(e);
    *ws = w; *wo = out.width;
}
static void pl_write(uint64_t n, double src, double out) {   // the present's completion
    if (!g_pl_f) return;
    pthread_mutex_lock(&g_pl_mu); g_pl_n++;
    fprintf(g_pl_f, "%llu\t%.2f\t%.2f\t%.3f\n", (unsigned long long)n, src, out, src > 0 ? out / src : 0); fflush(g_pl_f);
    if (pl_dark(src, out) && !g_pl_dark++) MLOG("[red] DM_PRESENT_LUMA: present %llu shows %.1f where its guest image has %.1f (under 35 %%): the present path darkens the picture", (unsigned long long)n, out, src);
    pthread_mutex_unlock(&g_pl_mu);
}
static int present_now(const tex_t *t, double period, uint64_t frame, uint64_t ticket, const int crop[4], id<CAMetalDrawable> supplied, double target_ts) {
    if (g_present_stall_ns) { struct timespec ts = {0, (long)g_present_stall_ns}; nanosleep(&ts, NULL); }
    g_present_n++;
    if (g_use != USE_OFF && g_use != USE_DONE) use_tick();
    if (bootprof() && (g_present_n == 1 || g_present_n % 120 == 0)) {
        if (g_present_n == 1) g_bp.first_present = bp_now() - g_bp.t0;
        MLOG("[bootprof] t=%.3f s present #%llu: metallib loads %llu / %.1f ms; pipeline creations %llu / %.1f ms (all synchronous on the submit thread); warm-up claimed shaders %llu / render pipelines %llu / compute pipelines %llu; "
             "texture uploads %.1f MB / %llu first uploads + %llu re-uploads / %.1f ms; first frame presented t=%.3f s", (bp_now() - g_bp.t0) / 1e9, g_present_n,
             g_bp.lib_n, g_bp.lib_ns / 1e6, g_bp.pso_n, g_bp.pso_ns / 1e6, g_warm_claim_shaders, g_warm_claim_rps, g_warm_claim_cps,
             g_bp.up_bytes / 1048576.0, g_pkg_uploads, g_pkg_reuploads,
             g_up_total_ns / 1e6, g_bp.first_present / 1e9);
    }
    PD("X ser=%llu", t->serial);
    id<MTLTexture> src = tex_mtl(t);
    if (!src) { RED_ONCE(8, "cannot create MTLTexture for the present texture"); return 0; }
    // The game's crop on widened storage (plan item 320g): mapped by the storage's ratio, so the widened picture is shown, not its left 16:9 part
    int wcrop[4];
    if (__builtin_expect(crop && (crop[2] || crop[3]) && t->d.width && t->d.height && tex_is_widened(src, t), 0)) {
        widen_map_rect(crop, (double)src.width / t->d.width, (double)src.height / t->d.height, (int)src.width, (int)src.height, wcrop); crop = wcrop; }
    // DM_NVN_REGLUM=x0,y0,x1,y1:file (normalized coordinates): read back this region on every present and record "present sequence number, present time ns, region mean luminance, subsampled grayscale thumbnail (hex)" (diagnostic: verify that animation speed is independent of frame rate).
    // A separate command buffer is queued after this frame's rendering (the same queue executes in submit order); does not wait for the GPU and does not disturb frame pacing
    static int rl = -1; static float rr[4]; static FILE *rlf; static pthread_mutex_t rlmu = PTHREAD_MUTEX_INITIALIZER;
    if (rl < 0) { const char *e = getenv("DM_NVN_REGLUM"), *c = e ? strchr(e, ':') : NULL;
        rl = c && sscanf(e, "%f,%f,%f,%f", &rr[0], &rr[1], &rr[2], &rr[3]) == 4 && (rlf = fopen(c + 1, "w")) != NULL;
        if (e && !rl) MLOG("[red] DM_NVN_REGLUM argument or file invalid: %s", e); }
    if (rl && (src.pixelFormat == MTLPixelFormatRGBA8Unorm || src.pixelFormat == MTLPixelFormatBGRA8Unorm || src.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB || src.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB)) {
        NSUInteger x0 = (NSUInteger)(rr[0] * src.width), y0 = (NSUInteger)(rr[1] * src.height), w = (NSUInteger)(rr[2] * src.width) - x0, h = (NSUInteger)(rr[3] * src.height) - y0;
        id<MTLBuffer> rb = [g_dev newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
        [b copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x0, y0, 0) sourceSize:MTLSizeMake(w, h, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:w * 4 destinationBytesPerImage:w * h * 4];
        [b endEncoding];
        unsigned long long n = g_present_n, tns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        [cb addCompletedHandler:^(id<MTLCommandBuffer> c) { (void)c; const uint8_t *q = rb.contents; uint64_t sum = 0;
            uint8_t g[4096]; NSUInteger ng = 0;   // Subsampled grayscale thumbnail (every 4th row / column, at most 4096 points), recorded along with the mean, for per-frame image correlation analysis
            for (NSUInteger y = 0; y < h; y += 4) for (NSUInteger x = 0; x < w && ng < sizeof g; x += 4) { const uint8_t *px = q + 4 * (y * w + x); g[ng++] = (uint8_t)((px[0] + px[1] + px[2]) / 3); }
            for (NSUInteger k = 0; k < w * h; k++) sum += q[4 * k] + q[4 * k + 1] + q[4 * k + 2];
            pthread_mutex_lock(&rlmu); fprintf(rlf, "%llu\t%llu\t%.4f\t", n, tns, sum / (3.0 * w * h));
            for (NSUInteger k = 0; k < ng; k++) fprintf(rlf, "%02x", g[k]);
            fputc('\n', rlf); fflush(rlf); pthread_mutex_unlock(&rlmu); }];
        [cb commit];
    }
    // DM_NVN_SHOT=frame number[,frame number...]:path (%d in the path is replaced with the frame number)
    static long shots[16]; static int nshot = -1; static const char *shot_path;
    if (nshot < 0) {
        nshot = 0; const char *e = getenv("DM_NVN_SHOT"), *c = e ? strchr(e, ':') : NULL;
        if (c) { shot_path = c + 1; for (const char *q = e; q < c && nshot < 16; q = strchr(q, ',') ? strchr(q, ',') + 1 : c) shots[nshot++] = atol(q); }
    }
    for (int i = 0; i < nshot; i++) if ((long)g_present_n == shots[i]) { char path[1024]; snprintf(path, sizeof path, shot_path, (int)g_present_n); mtl_finish_gpu(); shot(src, path, 1, crop); }
    pthread_mutex_lock(&g_shotq_mu);
    for (int i = 0; i < g_nshotq;) if (g_present_n >= g_shotq[i].frame) { mtl_finish_gpu(); shot(src, g_shotq[i].path, 1, crop); g_shotq[i] = g_shotq[--g_nshotq]; } else i++;
    pthread_mutex_unlock(&g_shotq_mu);

    CAMetalLayer *layer = (__bridge CAMetalLayer *)front_metal_layer();
    if (!layer) return 0;
#if !DM_NVN_M3
    if (g_present_layer_resident != layer) { [g_q4 addResidencySet:layer.residencySet]; g_present_layer_resident = layer; }
#endif
#if TARGET_OS_OSX
    if (!layer.displaySyncEnabled) layer.displaySyncEnabled = YES;   // Windowed mode follows the display's vsync to prevent tearing
#endif
    if (pl_on() && layer.framebufferOnly) layer.framebufferOnly = NO;   // DM_PRESENT_LUMA reads the drawable
    present_pipeline(layer.pixelFormat);   // Same as the original path: the first frame compiles the pipeline before taking the drawable
    if (g_grab_cb) grab_present(src, crop, layer.drawableSize);   // Game picture needed by the whole-window screenshot (peek without the lock first; the real take locks inside the function)
    uint64_t dt0 = now_ns();
    id<CAMetalDrawable> dr = supplied ? supplied : [layer nextDrawable];
    if (!supplied) g_drawable_ns += now_ns() - dt0;
    if (!dr) return 0;
    int alloc_i = (int)(ticket % 3);
    if (g_present_alloc_fence[alloc_i]) {
        pthread_mutex_lock(&g_pq_mu);
        while (g_pq_done < g_present_alloc_fence[alloc_i]) pthread_cond_wait(&g_pq_cv, &g_pq_mu);
        pthread_mutex_unlock(&g_pq_mu);
    }
#if DM_NVN_M3
    CBUF cb = [g_q commandBuffer];   // older API: presentation is recorded on the command buffer (presentDrawable), so the frame reaches the screen only after its drawing is done
    int fx_crop[4]; id<MTLFence> fx_wait; id<MTLTexture> fx = fx_apply(cb, src, crop, dr.texture, fx_crop, &fx_wait);
    if (!present_encode_m4(cb, fx ?: src, dr.texture, fx ? fx_crop : crop, alloc_i, nil)) return 0;
    id<MTLBuffer> pl_s = nil, pl_o = nil; size_t pl_sw = 0, pl_ow = 0; uint64_t pl_n = g_present_n; if (pl_on()) pl_encode(cb, src, crop, dr.texture, alloc_i, &pl_s, &pl_o, &pl_sw, &pl_ow);
#else
    [g_present_alloc4[alloc_i] reset];
    [g_q4 waitForDrawable:dr];
    CBUF cb = g_present_cb[alloc_i];
    [cb beginCommandBufferWithAllocator:g_present_alloc4[alloc_i]];
    int fx_crop[4]; id<MTLFence> fx_wait; id<MTLTexture> fx = fx_apply(cb, src, crop, dr.texture, fx_crop, &fx_wait);
    if (!present_encode_m4(cb, fx ?: src, dr.texture, fx ? fx_crop : crop, alloc_i, fx_wait)) { [cb endCommandBuffer]; [g_q4 signalDrawable:dr]; return 0; }
    char live_path[1024]; id<MTLBuffer> live = live_copy(cb, src, g_present_n, live_path, sizeof live_path);
    size_t live_w = src.width, live_h = src.height; int live_bgra = src.pixelFormat == MTLPixelFormatBGRA8Unorm || src.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB;
    NSString *live_file = live ? @(live_path) : nil;
    id<MTLBuffer> rows = live_rows(cb, src, g_present_n); uint64_t rows_n = g_present_n;
    id<MTLBuffer> pl_s = nil, pl_o = nil; size_t pl_sw = 0, pl_ow = 0; uint64_t pl_n = g_present_n; if (pl_on()) pl_encode(cb, src, crop, dr.texture, alloc_i, &pl_s, &pl_o, &pl_sw, &pl_ow);
    [cb endCommandBuffer];
#endif
#if !DM_NVN_M3
    MTL4CommitOptions *o = [MTL4CommitOptions new];
#endif
    [FB_ON(o, cb) FB_SEL:^(FB_ARG f) {
        if (f.error) MLOG("[red] present command buffer error: %s", f.error.description.UTF8String);
        pthread_mutex_lock(&g_pq_mu); if (ticket > g_pq_done) g_pq_done = ticket; g_pq_inflight--; pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu);
        if (pl_sw && pl_ow) pl_write(pl_n, pl_mean(pl_s.contents, pl_sw, PL_ROWS), pl_mean(pl_o.contents, pl_ow, PL_ROWS));
#if !DM_NVN_M3
        if (live) dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ png_write_buf(live, live_w, live_h, live_bgra, live_file.UTF8String); });
        if (rows) live_rows_write(rows, live_w, rows_n);
#endif
    }];
#if !DM_NVN_M3
    CBUF buffers[] = {cb};
#endif
#ifndef DM_TEST_PRESENT_NO_RESCOMMIT   // negative control of t_present_res only
    res_commit();   // the source texture may have been made just now (tex_mtl → res_add), the scaler's output too: resident before this command buffer runs (MacNativeGames PR #30: a GPU page fault in 4 of 14 boots without it)
#endif
    pthread_mutex_lock(&g_pq_mu); g_pq_inflight++; pthread_mutex_unlock(&g_pq_mu);
#if !DM_NVN_M3
    if (fx) fx_commit_sync();   // the scaler reads the finished game frame (fx_apply)
    [g_q4 commit:buffers count:1 options:o];
#endif
    if (target_ts > 0) {
        static double leads[300], e2e[300];   // e2e: game enqueue -> system-scheduled present (including the time waiting in the queue for the display-link callback), the full span of input latency that our side can read
        static uint64_t lead_n;
        leads[lead_n % 300] = (target_ts - CACurrentMediaTime()) * 1000.0; e2e[lead_n % 300] = g_link_it_tin > 0 ? (target_ts - g_link_it_tin) * 1000.0 : 0;
        lead_n++;
        if (lead_n % 300 == 0) {
            double a[300]; memcpy(a, leads, sizeof a);
            for (int i = 1; i < 300; i++) { double v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; }
            double b[300]; memcpy(b, e2e, sizeof b);
            for (int i = 1; i < 300; i++) { double v = b[i]; int j = i - 1; while (j >= 0 && b[j] > v) { b[j + 1] = b[j]; j--; } b[j + 1] = v; }
            MLOG("display link up to %llu: ticks 300, presents %llu, empty ticks %llu, stale frames dropped by coalescing %llu; submit lead before target on-screen time p50 %.3f ms / min %.3f ms; game enqueue to target on-screen time p50 %.3f ms / min %.3f / max %.3f ms",
                 (unsigned long long)g_link_tick_n, (unsigned long long)lead_n, (unsigned long long)(g_link_tick_n - lead_n),   // only frames presented through the display link (not those presented with our own drawables during a rescue)
                 (unsigned long long)g_link_dropped, a[149], a[0], b[149], b[0], b[299]);
        }
    }
#if !DM_NVN_M3
    [g_q4 signalDrawable:dr];
#endif
    g_present_alloc_fence[alloc_i] = ticket;
#if TARGET_OS_SIMULATOR   // The iOS simulator SDK has no presentation-time callback and no minimum display duration (used only for picture acceptance, display-link ticks are not counted)
    (void)period; M3_PRESENT(cb, dr);
#else
    uint64_t pn = ticket;   // Record the present time by the game's present sequence number (ticket number): hle's present statistics are taken by "the game's nth present"; in the display link path the actual present count != the game's present count (old frames get merged and dropped)
    [dr addPresentedHandler:^(id<MTLDrawable> d) {   // Actual presentation time (the time this frame is really put on screen, CACurrentMediaTime base)
        CFTimeInterval tt = d.presentedTime;
        g_pres[pn % PRING] = (pres_ent){pn, tt > 0 ? tt : -1}; trace_at(TR_DISPLAY, tt, tt, pn, 0);
    }];
    // Each frame is displayed for at least one present period: when the GPU completion time jitters around a 120 Hz refresh boundary, the paired "25 ms + 8.33 ms" stutter no longer appears
#if DM_NVN_M3
    if (target_ts <= 0 && period > 0) [cb presentDrawable:dr afterMinimumDuration:period]; else [cb presentDrawable:dr];
#else
    if (target_ts > 0) [dr present];
    else if (period > 0) [dr presentAfterMinimumDuration:period]; else [dr present];
#endif
#endif
#if DM_NVN_M3
    [cb commit];
#endif
    (void)frame;
    return 1;
}
