// MetalFX upscaling of the presented picture (fx_apply + present_encode_m4): real Metal, no window, no game. Checks the result of upscaling the frame
// committed just before on the same queue, the output size and colors, and the bilinear fallbacks. It does not prove the ordering (fx_commit_sync +
// the fence): on an M5 the result was right even with fx_commit_sync left out, also with 40 serialized 4K clears as the frame (render passes ran in
// order there); the ordering rests on the documented signalEvent / waitForEvent of MTL4CommandQueue and on MetalFX's measured fence use (fx_apply)
#include "../nvn/nvn_mtl.m"
#include "../nvn/mtl/present.m"   // the presentation unit's statics, in this translation unit too
#include "../nvn/hle_nvn.c"
const dm_game_profile dm_game = { .id = "test", .nvn_api_major = 55, .nvn_api_minor = 15, .nvn_c2 = {0x168, 128, 0x568, 128, 0x120, 8}, .nvn_depth_mode = 1 };   // hle_nvn.c reports the NVN API version and sets the c[2] layout from the profile
uint64_t g_t0_ns;   // process start time from host main.c (used by hle_nvn's per-frame @t_s); unit test stub
void dm_touch_screen(int w, int h) { (void)w; (void)h; }   // touch coordinate space from host hle_touch.c (called by hle_nvn WindowSetCrop); unit test stub
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }

static int failed, cases;
#define EXPECT(n, w, v) do { long a_ = (w), b_ = (v); cases++; printf("%s expected %ld → got %ld %s\n", n, a_, b_, a_ == b_ ? "✓" : "✗"); failed += a_ != b_; } while (0)

static id<MTLTexture> tex(MTLPixelFormat f, NSUInteger w, NSUInteger h, MTLTextureUsage u) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO];
    d.storageMode = MTLStorageModeShared; d.usage = u;
    id<MTLTexture> t = [g_dev newTextureWithDescriptor:d]; res_add(t); res_commit();
    return t;
}
#if DM_NVN_M3
static CBUF cb_begin(void) { return [g_q commandBuffer]; }
static void cb_commit(CBUF cb, int fx_used) { (void)fx_used; [cb commit]; }
static void q_idle(void) { id<MTLCommandBuffer> cb = [g_q commandBuffer]; [cb commit]; [cb waitUntilCompleted]; }
#else
static id<MTL4CommandAllocator> g_ta[8]; static CBUF g_tcb[8]; static int g_tk; static id<MTLSharedEvent> g_tev; static uint64_t g_tev_n;
static CBUF cb_begin(void) {
    int k = g_tk++ % 8;
    if (!g_ta[k]) { g_ta[k] = [g_dev newCommandAllocator]; g_tcb[k] = [g_dev newCommandBuffer]; }
    [g_ta[k] reset]; [g_tcb[k] beginCommandBufferWithAllocator:g_ta[k]];
    return g_tcb[k];
}
static void cb_commit(CBUF cb, int fx_used) { [cb endCommandBuffer]; if (fx_used) fx_commit_sync(); CBUF a[] = {cb}; [g_q4 commit:a count:1]; }
static void q_idle(void) { if (!g_tev) g_tev = [g_dev newSharedEvent]; [g_q4 signalEvent:g_tev value:++g_tev_n]; [g_tev waitUntilSignaledValue:g_tev_n timeoutMS:10000]; }
#endif
// The "game frame": a render pass that only clears the picture to a color, committed on its own (as the game's submission is)
static void game_clear(id<MTLTexture> t, double r, double g, double b) {
    CBUF cb = cb_begin(); RPD *rp = [RPD new];
    rp.colorAttachments[0].texture = t; rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, 1);
    RENC e = [cb renderCommandEncoderWithDescriptor:rp]; [e endEncoding];
    cb_commit(cb, 0);
}
// One present of src (crop) into dst through fx_apply; returns whether MetalFX was used
static int present(id<MTLTexture> src, const int crop[4], id<MTLTexture> dst) {
    CBUF cb = cb_begin();
    int fc[4]; id<MTLFence> w; id<MTLTexture> fx = fx_apply(cb, src, crop, dst, fc, &w);
    int ok = present_encode_m4(cb, fx ?: src, dst, fx ? fc : crop, 0, w);
    cb_commit(cb, fx != nil);
    q_idle();
    if (!ok) { failed++; printf("present encode failed ✗\n"); }
    return fx != nil;
}
static void px(id<MTLTexture> t, NSUInteger x, NSUInteger y, uint8_t o[4]) { [t getBytes:o bytesPerRow:4 fromRegion:MTLRegionMake2D(x, y, 1, 1) mipmapLevel:0]; }
static int near(const uint8_t a[4], int b0, int b1, int b2) { return abs(a[0] - b0) <= 3 && abs(a[1] - b1) <= 3 && abs(a[2] - b2) <= 3; }   // dst is BGRA: b0 = B

int main(void) { @autoreleasepool {
    mtl_init();
    mtl_upscale_set(1);
    // The game's swapchain format (RGBA8 sRGB, presented through its non-sRGB view) onto the layer's BGRA8
    id<MTLTexture> src = tex(MTLPixelFormatRGBA8Unorm_sRGB, 640, 360, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
    id<MTLTexture> dst = tex(MTLPixelFormatBGRA8Unorm, 1280, 720, MTLTextureUsageRenderTarget);
    int crop[4] = {0, 0, 640, 360};

    // 1) The frame cleared just before is the one upscaled, 24 times with alternating colors
    int stale = 0, used = 0;
    for (int i = 0; i < 24; i++) {
        int red = i & 1;
        game_clear(src, red, red ? 0 : 1, 0);
        used += present(src, crop, dst);
        uint8_t o[4]; px(dst, 640, 360, o);
        stale += !(red ? near(o, 0, 0, 255) : near(o, 0, 255, 0));
    }
    EXPECT("MetalFX used for every 2x present", 24, used);
    EXPECT("upscaled frames that are not the frame cleared just before", 0, stale);
    EXPECT("MetalFX output width = on-screen width", 1280, (long)g_fx.out.width);
    EXPECT("MetalFX output height = on-screen height", 720, (long)g_fx.out.height);

    // 2) Geometry: four quadrants (written by the CPU, the GPU idle), centers of each quadrant in the 2x output
    q_idle();
    uint8_t *q = malloc(640 * 360 * 4);
    static const uint8_t col[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}};
    for (int y = 0; y < 360; y++) for (int x = 0; x < 640; x++) memcpy(q + 4 * (y * 640 + x), col[(x >= 320) + 2 * (y >= 180)], 4);
    [src replaceRegion:MTLRegionMake2D(0, 0, 640, 360) mipmapLevel:0 withBytes:q bytesPerRow:640 * 4]; free(q);
    present(src, crop, dst);
    uint8_t o[4]; int bad = 0;
    px(dst, 320, 180, o); bad += !near(o, 0, 0, 255);
    px(dst, 960, 180, o); bad += !near(o, 0, 255, 0);
    px(dst, 320, 540, o); bad += !near(o, 255, 0, 0);
    px(dst, 960, 540, o); bad += !near(o, 0, 255, 255);
    EXPECT("quadrant centers keep their colors", 0, bad);

    // 3) A smaller crop of the same texture: the scaler is rebuilt for the new input size
    int crop2[4] = {0, 0, 320, 180};
    EXPECT("320x180 crop upscaled by MetalFX", 1, present(src, crop2, dst));
    EXPECT("the scaler's input is the whole texture (the crop is its content size)", 640, (long)g_fx.iw);
    px(dst, 640, 360, o); EXPECT("the whole 320x180 red quadrant fills the screen", 1, near(o, 0, 0, 255));
    id<MTLTexture> mid = tex(MTLPixelFormatBGRA8Unorm, 480, 270, MTLTextureUsageRenderTarget);
    EXPECT("320x180 crop shown at 480x270 (smaller than the 640x360 texture): MetalFX", 1, present(src, crop2, mid));
    px(mid, 240, 135, o); EXPECT("still all red", 1, near(o, 0, 0, 255));

    // 4) Bilinear fallbacks
    int off[4] = {8, 8, 320, 180};
    EXPECT("crop not at the origin: bilinear", 0, present(src, off, dst));
    id<MTLTexture> small = tex(MTLPixelFormatBGRA8Unorm, 320, 180, MTLTextureUsageRenderTarget);
    EXPECT("not shown larger than rendered: bilinear", 0, present(src, crop, small));
    mtl_upscale_set(0);
    EXPECT("switched off: bilinear", 0, present(src, crop, dst));
    mtl_upscale_set(1);
    EXPECT("switched on again: MetalFX", 1, present(src, crop, dst));
    EXPECT("nothing reported red", 0, (long)g_fx.broken);

    printf("== %d checks, %d failed ==\n", cases, failed);
    return failed != 0;
} }
