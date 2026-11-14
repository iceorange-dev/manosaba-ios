// DM_PRESENT_LUMA's comparison (nvn_mtl.m pl_encode / pl_mean / pl_dark / pl_write, plan item 145) on the GPU through a production submission: a
// 64x36 guest image whose left half is 200 and right half 40, shown cropped to the left half, against a 32x18 output of 40: the source reads
// 200 (the crop, not the whole image), the output 40, flagged dark (under 35 %); an output of 200 is not; a dark scene (10 against 2) is not
// flagged (a source under 16); a 16-bit float image is not compared; each present writes its line and the red line comes once. The whole guest
// image read (-DDM_TEST_PLUMA_NO_CROP): 1 fails
#include "../nvn/nvn_mtl.m"
#include "../nvn/mtl/present.m"   // the presentation unit's statics, in this translation unit too
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static id<MTLTexture> tex(MTLPixelFormat f, NSUInteger w, NSUInteger h, uint8_t left, uint8_t right) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO];
    d.storageMode = MTLStorageModeShared; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    id<MTLTexture> t = [g_dev newTextureWithDescriptor:d]; res_add(t); res_commit();
    if (f == MTLPixelFormatRGBA16Float) return t;
    uint8_t *p = malloc(w * h * 4);
    for (NSUInteger y = 0; y < h; y++) for (NSUInteger x = 0; x < w; x++) { uint8_t v = x < w / 2 ? left : right; uint8_t *q = p + 4 * (y * w + x); q[0] = q[1] = q[2] = v; q[3] = 255; }
    [t replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:p bytesPerRow:w * 4]; free(p);
    return t;
}
static void run(id<MTLTexture> src, const int crop[4], id<MTLTexture> out, double *ms, double *mo, size_t *ws) {
    id<MTLBuffer> bs = nil, bo = nil; size_t wo = 0;
    mtl_sub_begin(); pl_encode(g_cb, src, crop, out, 0, &bs, &bo, ws, &wo); mtl_wait(mtl_sub_commit(), 5000000000ull);
    *ms = *ws ? pl_mean(bs.contents, *ws, PL_ROWS) : -1; *mo = wo ? pl_mean(bo.contents, wo, PL_ROWS) : -1;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[512]; snprintf(path, sizeof path, "%s/t_pluma_%d.tsv", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid());
    setenv("DM_PRESENT_LUMA", path, 1); mtl_init();
    if (!pl_on()) { printf("DM_PRESENT_LUMA not on\n"); return 2; }
    id<MTLTexture> src = tex(MTLPixelFormatRGBA8Unorm, 64, 36, 200, 40), dark = tex(MTLPixelFormatBGRA8Unorm, 32, 18, 40, 40), bright = tex(MTLPixelFormatBGRA8Unorm, 32, 18, 200, 200);
    id<MTLTexture> dim = tex(MTLPixelFormatRGBA8Unorm, 64, 36, 10, 10), dimmer = tex(MTLPixelFormatBGRA8Unorm, 32, 18, 2, 2), half = tex(MTLPixelFormatRGBA16Float, 64, 36, 0, 0);
    const int crop[4] = {0, 0, 32, 36};
    double s, o; size_t ws;
    run(src, crop, dark, &s, &o, &ws);
    CHECK(fabs(s - 200) < 0.5 && fabs(o - 40) < 0.5 && pl_dark(s, o), "cropped to the bright half against a dark output: source %.1f, output %.1f, dark", s, o);
    pl_write(1, s, o);
    run(src, crop, bright, &s, &o, &ws);
    CHECK(!pl_dark(s, o), "... against an output as bright: not dark (%.1f / %.1f)", s, o);
    pl_write(2, s, o);
    run(dim, NULL, dimmer, &s, &o, &ws);
    CHECK(!pl_dark(s, o) && fabs(s - 10) < 0.5, "a dark scene (%.1f against %.1f): not flagged", s, o);
    pl_write(3, s, o);
    run(half, NULL, dark, &s, &o, &ws);
    CHECK(ws == 0, "a 16-bit float guest image: not compared");
    int lines = 0; char l[256]; FILE *f = fopen(path, "r"); while (f && fgets(l, sizeof l, f)) lines++; if (f) fclose(f);
    CHECK(lines == 4 && g_pl_dark == 1, "a line per present after the header (%d lines), dark counted once (%llu)", lines, (unsigned long long)g_pl_dark);
    unlink(path);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
