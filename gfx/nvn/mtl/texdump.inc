// gfx/nvn/mtl/texdump.inc, part of nvn_mtl.m (included there in order, one translation unit): Texture dumps and snapshots (DM_NVN_TEXDUMP, DM_NVN_TEXSNAP), presented times
// ---------- DM_NVN_TEXDUMP=frame number:path (%d in the path is replaced with "index*100+mip level"): save each color render target used by that frame as an image ----------
// Diagnostic: before the first render pass of the next frame is opened, wait for the GPU to finish, then draw each one (the mip level used by that frame) by full-screen sampling into RGBA8 and save as PNG (float formats are clamped to [0,1])
static long g_td_frame = -2; static const char *g_td_path; static NSMutableArray *g_td_list; static int g_td_done;
static void td_dump(void);
void mtl_texdump_next(const char *path) {   // the command channel's "texdump <path>" (host/front/cmdchan.c): the next frame's colour targets, as DM_NVN_TEXDUMP
    static char p[4][1024]; static int k; snprintf(p[k], sizeof p[k], "%s", path);
    if (!g_td_list) g_td_list = [NSMutableArray array];
    g_td_path = p[k]; k = (k + 1) & 3; g_td_done = 0;
    long f = (long)atomic_load(&g_cur_frame) + 1; __atomic_store_n(&g_td_frame, f, __ATOMIC_RELEASE);
    MLOG("texdump of frame %ld to %s (command channel)", f, path);
}
void td_note(RPD *pd, NSUInteger nc) {
    if (g_td_frame == -2) { const char *e = getenv("DM_NVN_TEXDUMP"), *c = e ? strchr(e, ':') : NULL; g_td_frame = c ? atol(e) : -1; g_td_path = c ? c + 1 : NULL; g_td_list = [NSMutableArray array]; }
    if (g_td_frame < 0 || g_td_done) return;
    long f = (long)atomic_load(&g_cur_frame);
    if (f > g_td_frame) { td_dump(); return; }
    if (f != g_td_frame) return;
    for (NSUInteger i = 0; i < nc && i < 8; i++) {
        id<MTLTexture> t = pd.colorAttachments[i].texture; if (!t) continue;
        uint64_t ser = i < (NSUInteger)R.rt.n && R.rt.color[i] ? R.rt.color[i]->serial : 0;
        NSUInteger lv = pd.colorAttachments[i].level;
        BOOL seen = NO; for (NSArray *e in g_td_list) if ([e[1] unsignedLongLongValue] == ser && [e[2] unsignedIntegerValue] == lv) seen = YES;
        if (!seen) [g_td_list addObject:@[t, @(ser), @(lv)]];
    }
}
static void td_dump(void) {
    g_td_done = 1;
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    mtl_finish_gpu();
    static id<MTLRenderPipelineState> ps; static id<MTLSamplerState> sp;
    if (!ps) {
        static const char *src_msl =
            "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct V { float4 p [[position]]; float2 uv; };\n"
            "vertex V vs(uint id [[vertex_id]]) { float2 uv = float2((id << 1) & 2, id & 2); V o; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o; }\n"
            "fragment float4 fs(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) { float4 c = t.sample(s, in.uv); return float4(c.rgb, 1); }\n";
        NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(src_msl) options:nil error:&e];
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [lib newFunctionWithName:@"vs"]; d.fragmentFunction = [lib newFunctionWithName:@"fs"];
        d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        ps = [g_dev newRenderPipelineStateWithDescriptor:d error:&e];
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sp = [g_dev newSamplerStateWithDescriptor:sd];
        if (!ps) { MLOG("[red] TEXDUMP pipeline failed %s", e.localizedDescription.UTF8String); return; }
    }
    MLOG("[texdump] frame %ld, %lu color targets in total", g_td_frame, (unsigned long)g_td_list.count);
    // DM_NVN_TEXRAW=index,...: save raw bytes by texture index (including 3D; file: %d in the path is replaced with the index, suffix .raw, first-line text header: width height depth format bytes-per-row)
    for (const char *q = getenv("DM_NVN_TEXRAW"); q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) {
        uint64_t ser = (uint64_t)atoll(q); id o = serial_get(ser);
        id<MTLTexture> t = [o isKindOfClass:[NvPkg class]] ? ((NvPkg *)o)->tex : o;
        if (!t) { MLOG("[texraw] ser=%llu not in the serial table", ser); continue; }
        int bw, bh; NSUInteger w = t.width, h = t.height, dz = t.depth;
        size_t bpp = t.pixelFormat == MTLPixelFormatRGBA16Float ? 8 : t.pixelFormat == MTLPixelFormatRGBA32Float ? 16 : 4; (void)bw; (void)bh;
        size_t row = w * bpp, img = row * h;
        if (t.pixelFormat >= MTLPixelFormatBC1_RGBA && t.pixelFormat <= MTLPixelFormatBC7_RGBAUnorm_sRGB) {   // BC: rows of 4×4 blocks, 8 bytes (BC1 / BC4) or 16 bytes each
            size_t blk = t.pixelFormat <= MTLPixelFormatBC1_RGBA_sRGB || t.pixelFormat == MTLPixelFormatBC4_RUnorm || t.pixelFormat == MTLPixelFormatBC4_RSnorm ? 8 : 16;
            row = (w + 3) / 4 * blk; img = row * ((h + 3) / 4);
        }
        id<MTLBuffer> buf = [g_dev newBufferWithLength:img * dz options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
        [b copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, dz) toBuffer:buf destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:img];
        [b endEncoding]; [cb commit]; [cb waitUntilCompleted];
        char path[1024]; snprintf(path, sizeof path, g_td_path, (int)ser); strlcat(path, ".raw", sizeof path);
        FILE *f = fopen(path, "wb"); if (f) { fprintf(f, "%lu %lu %lu %lu %zu\n", (unsigned long)w, (unsigned long)h, (unsigned long)dz, (unsigned long)t.pixelFormat, row); fwrite(buf.contents, 1, img * dz, f); fclose(f); }
        MLOG("[texraw] ser=%llu %lux%lux%lu format %lu → %s", ser, (unsigned long)w, (unsigned long)h, (unsigned long)dz, (unsigned long)t.pixelFormat, path);
    }
    for (NSArray *e in g_td_list) {
        id<MTLTexture> t = e[0]; uint64_t ser = [e[1] unsignedLongLongValue]; NSUInteger lv = [e[2] unsignedIntegerValue];
        if (t.textureType != MTLTextureType2D) { MLOG("[texdump] ser=%llu type %lu skipped", ser, (unsigned long)t.textureType); continue; }
        if (getenv("DM_NVN_SNAPRAW")) {   // Also save the raw bytes (converting an HDR target to an 8-bit image truncates; first-line text header: width height format bytes-per-row)
            NSUInteger rw = MAX(t.width >> lv, 1u), rh = MAX(t.height >> lv, 1u);
            size_t bpp = t.pixelFormat == MTLPixelFormatRGBA16Float ? 8 : t.pixelFormat == MTLPixelFormatRGBA32Float ? 16 : t.pixelFormat == MTLPixelFormatR16Float || t.pixelFormat == MTLPixelFormatR16Unorm ? 2 : 4;
            id<MTLBuffer> rb = [g_dev newBufferWithLength:rw * rh * bpp options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> rc = q_cb(); id<MTLBlitCommandEncoder> bb = [rc blitCommandEncoder];
            [bb copyFromTexture:t sourceSlice:0 sourceLevel:lv sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(rw, rh, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:rw * bpp destinationBytesPerImage:rw * rh * bpp];
            [bb endEncoding]; [rc commit]; [rc waitUntilCompleted];
            char rp_[1024]; snprintf(rp_, sizeof rp_, g_td_path, (int)(ser * 100 + lv)); strlcat(rp_, ".raw", sizeof rp_);
            FILE *f = fopen(rp_, "wb"); if (f) { fprintf(f, "%lu %lu %lu %zu\n", (unsigned long)rw, (unsigned long)rh, (unsigned long)t.pixelFormat, rw * bpp); fwrite(rb.contents, 1, rw * rh * bpp, f); fclose(f); }
        }
        id<MTLTexture> v = [t newTextureViewWithPixelFormat:t.pixelFormat textureType:MTLTextureType2D levels:NSMakeRange(lv, 1) slices:NSMakeRange(0, 1)];
        NSUInteger w = MAX(t.width >> lv, 1u), h = MAX(t.height >> lv, 1u);
        MTLTextureDescriptor *od = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
        od.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; od.storageMode = MTLStorageModePrivate;
        id<MTLTexture> o = [g_dev newTextureWithDescriptor:od];
        id<MTLCommandBuffer> cb = q_cb();
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = o; rp.colorAttachments[0].loadAction = MTLLoadActionDontCare; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
        [en setRenderPipelineState:ps]; [en setFragmentTexture:v atIndex:0]; [en setFragmentSamplerState:sp atIndex:0];
        [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
        char path[1024]; snprintf(path, sizeof path, g_td_path, (int)(ser * 100 + lv));
        MLOG("[texdump] ser=%llu lv=%lu format %lu %lux%lu", ser, (unsigned long)lv, (unsigned long)t.pixelFormat, (unsigned long)w, (unsigned long)h);
        shot(o, path, 0, NULL);
    }
    [g_td_list removeAllObjects];
}
// DM_NVN_TEXSNAP=item,... (together with the frame number and path of DM_NVN_TEXDUMP): as soon as the selected render passes of that frame end, copy
// their color attachments and save them with the end-of-frame dump (the same target is often cleared and reused within a frame, so the end-of-frame
// state cannot show what intermediate passes wrote). Items: an in-frame pass index, a range "a-b", or "@WxH" for every pass with a color attachment
// of that size; at most 96 copies. The copy is encoded on the replay's own blit encoder, so it runs right after the pass on the GPU (a command buffer
// of its own, committed at once, ran before the replay's and copied old contents). The pass dump prints "S <pass> snapshot index <i>" for each one.
// File name index = (in-frame index * 1000 + serial) * 100 + mip level
static int g_ts_idx[64], g_ts_lo[16], g_ts_hi[16], g_ts_w[8], g_ts_h[8], g_ts_n = -1, g_ts_nr, g_ts_nsz, g_ts_on, g_ts_copies; static uint64_t g_ts_first, g_ts_curf = ~0ull;
static __strong id<MTLTexture> g_ts_att[8]; static uint64_t g_ts_ser[8]; static NSUInteger g_ts_lv[8], g_ts_sl[8]; static int g_ts_na;
void ts_note(RPD *pd, NSUInteger nc) {
    if (g_ts_n < 0) { g_ts_n = 0; const char *e = getenv("DM_NVN_TEXSNAP");
        for (const char *q = e; q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) {
            int a1, a2;
            if (*q == '@') { if (g_ts_nsz < 8 && sscanf(q + 1, "%dx%d", &a1, &a2) == 2) { g_ts_w[g_ts_nsz] = a1; g_ts_h[g_ts_nsz++] = a2; } }
            else if (sscanf(q, "%d-%d", &a1, &a2) == 2) { if (g_ts_nr < 16) { g_ts_lo[g_ts_nr] = a1; g_ts_hi[g_ts_nr++] = a2; } }
            else if (g_ts_n < 64) g_ts_idx[g_ts_n++] = atoi(q);
        } }
    g_ts_on = 0; if (!(g_ts_n || g_ts_nr || g_ts_nsz) || g_td_frame < 0 || g_td_done) return;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_ts_curf) { g_ts_curf = f; g_ts_first = g_stat_pass; }
    if ((long)f != g_td_frame || g_ts_copies >= 96) return;
    int idx = (int)(g_stat_pass - g_ts_first), hit = 0;
    for (int i = 0; i < g_ts_n; i++) hit |= g_ts_idx[i] == idx;
    for (int i = 0; i < g_ts_nr; i++) hit |= idx >= g_ts_lo[i] && idx <= g_ts_hi[i];
    for (NSUInteger i = 0; i < nc && i < 8 && !hit; i++) { id<MTLTexture> t = pd.colorAttachments[i].texture; if (!t) continue;
        for (int k = 0; k < g_ts_nsz; k++) hit |= (int)MAX(t.width >> pd.colorAttachments[i].level, 1u) == g_ts_w[k] && (int)MAX(t.height >> pd.colorAttachments[i].level, 1u) == g_ts_h[k]; }
    g_ts_na = 0; if (!hit) return;
    g_ts_on = idx + 1;
    PD("S %llu snapshot index %d", g_stat_pass + 1, idx);
    for (NSUInteger i = 0; i < nc && i < 8; i++) { id<MTLTexture> t = pd.colorAttachments[i].texture; if (!t) continue;
        g_ts_att[g_ts_na] = t; g_ts_ser[g_ts_na] = R.rt.color[i] ? R.rt.color[i]->serial : 0; g_ts_lv[g_ts_na] = pd.colorAttachments[i].level; g_ts_sl[g_ts_na] = pd.colorAttachments[i].slice; g_ts_na++; }
}
void ts_after(void) {
    if (!g_ts_on) return;
    for (int i = 0; i < g_ts_na && g_ts_copies < 96; i++) {
        id<MTLTexture> t = g_ts_att[i]; if (t.textureType != MTLTextureType2D && t.textureType != MTLTextureType2DArray) continue;
        NSUInteger lv = g_ts_lv[i], w = MAX(t.width >> lv, 1u), h = MAX(t.height >> lv, 1u);
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.pixelFormat width:w height:h mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead; d.storageMode = MTLStorageModePrivate;
        id<MTLTexture> c = [g_dev newTextureWithDescriptor:d]; if (!c) continue;
        res_add(c);
        enc_b_copy_texture(blit(), t, g_ts_sl[i], lv, MTLOriginMake(0, 0, 0), MTLSizeMake(w, h, 1), c, 0, 0, MTLOriginMake(0, 0, 0));
        [g_td_list addObject:@[c, @((uint64_t)(g_ts_on - 1) * 1000 + g_ts_ser[i]), @0]];
        g_ts_copies++;
    }
    g_ts_on = 0;
}
// Ring of actual presentation times: present sequence number -> presentedTime (seconds; -1 = dropped by the system, not presented)
#define PRING 1024
typedef struct { uint64_t n; double t; } pres_ent;
static pres_ent g_pres[PRING];
// Get the actual presentation time of the nth present (returns 0 if not ready, -1 if dropped)
double mtl_presented_time(uint64_t n) { pres_ent e = g_pres[n % PRING]; return e.n == n ? e.t : 0; }
static _Atomic uint64_t g_drawable_ns;   // Accumulated wall clock waiting for nextDrawable in present (timing instrumentation)
uint64_t mtl_drawable_ns_take(void) { return atomic_exchange(&g_drawable_ns, 0); }
// Maximum refresh rate (Hz) of the screen when there is a window; returns 0 without a window. Mac uses CoreGraphics display modes (thread-safe C interface; the present configuration is read on the game thread)
int mtl_display_hz(void) {
    if (!front_metal_layer()) return 0;
#if TARGET_OS_OSX
    CGDisplayModeRef m = CGDisplayCopyDisplayMode(CGMainDisplayID());
    double hz = m ? CGDisplayModeGetRefreshRate(m) : 0; if (m) CGDisplayModeRelease(m);
    return (int)(hz + 0.5);
#else
    int front_ios_screen_hz(void);   // ios/Sources/front_ios.m: the screen the game's window is on (the iPhone Duo has two; plan item 302), kept there on the main thread
    return front_ios_screen_hz();
#endif
}
