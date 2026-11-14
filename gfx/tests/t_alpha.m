// The alpha test (nvn_mtl.m alpha_on / alpha_fn / pso_key_fill / pso_desc, gfx/tools/dmsh_build.py alpha_test), on the GPU through the replay's own
// pipeline builder: a fragment shader written by t_alpha_src.py (colour 0 red with the alpha from buffer 1, colour 1 green) draws a 1x1 pixel into
// two RGBA8 targets after ColorStateSetAlphaTest (S.color.alpha_func) and SetAlphaRef (bind state 6). With colour 0's alpha exported, each of the
// 8 NVN comparisons against reference 0.5 keeps or discards alpha 0, 0.5 and 1 as the comparison says (both targets written or both left at the
// clear colour), also on the early return; the reference changing 0.75 → 0.25 → 0.75 is honoured each time (one specialization per reference);
// ALWAYS uses the function shader_load specialized without a test. A shader that exports no alpha is discarded only by NEVER. A NaN reference draws
// only with NOTEQUAL and ALWAYS (bayonetta3-mobile's case, plan item 128); taken as 0 (-DDM_TEST_ALPHA_NAN_ZERO) 1 fails. Usage: t_alpha <MSL with alpha> <MSL without>
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

static id<MTLTexture> g_rt[2];
// 1 = drawn (red in target 0 and green in target 1), 0 = discarded (both still the blue clear colour), -1 = anything else; *plain: the pipeline used fs->fn
static int draw1(NvShader *vs, NvShader *fs, unsigned op, float ref, float alpha, uint32_t early, int *plain) {
    state_reset(); S.color.alpha_func = op;
    p_bind_state a = {.kind = 6}; memcpy(a.bytes, &ref, sizeof ref); state_op(OP_BIND_STATE, &a);
    uint32_t cfmt[8] = {MTLPixelFormatRGBA8Unorm, MTLPixelFormatRGBA8Unorm};
    pso_key k; pso_key_fill(&k, vs, fs, cfmt, 0, 0, 1, 0);
    MTLRenderPipelineDescriptor *d = pso_desc(&k, vs, fs, nil, 0);
    if (plain) *plain = d.fragmentFunction == fs->fn;
    NSError *e = nil; id<MTLRenderPipelineState> ps = [g_dev newRenderPipelineStateWithDescriptor:d error:&e];
    if (!ps) { printf("pipeline: %s\n", e.localizedDescription.UTF8String); return -1; }
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    for (int i = 0; i < 2; i++) { rp.colorAttachments[i].texture = g_rt[i]; rp.colorAttachments[i].loadAction = MTLLoadActionClear;
                                  rp.colorAttachments[i].storeAction = MTLStoreActionStore; rp.colorAttachments[i].clearColor = MTLClearColorMake(0, 0, 1, 1); }
    id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:ps]; [enc setFragmentBytes:&alpha length:sizeof alpha atIndex:1]; [enc setFragmentBytes:&early length:sizeof early atIndex:2];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
    uint8_t px[2][4]; for (int i = 0; i < 2; i++) [g_rt[i] getBytes:px[i] bytesPerRow:4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
    if (px[0][0] == 255 && px[0][2] == 0 && px[1][1] == 255 && px[1][2] == 0) return 1;
    if (px[0][0] == 0 && px[0][2] == 255 && px[1][1] == 0 && px[1][2] == 255) return 0;
    return -1;
}
static void shaders(const char *path, NvShader **vs, NvShader **fs) {
    NSError *e = nil; NSString *src = [NSString stringWithContentsOfFile:@(path) encoding:NSUTF8StringEncoding error:&e];
    MTLCompileOptions *o = [MTLCompileOptions new]; o.mathMode = MTLMathModeSafe;
    id<MTLLibrary> lib = src ? [g_dev newLibraryWithSource:src options:o error:&e] : nil;
    if (!lib) { printf("%s: %s\n", path, e.localizedDescription.UTF8String); exit(1); }
    *vs = [NvShader new]; *fs = [NvShader new]; (*vs)->fn = [lib newFunctionWithName:@"vertex0"];
    (*fs)->alib = lib; (*fs)->fn = alpha_plain(lib, @"main0"); (*fs)->stage = 1; strlcpy((*fs)->key, "t_alpha", sizeof (*fs)->key);   // as shader_load does for a reflection with "alpha_test"
}
int main(int argc, char **argv) { @autoreleasepool {
    if (argc != 3) { printf("usage: t_alpha <MSL with alpha> <MSL without>\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); g_q = [g_dev newCommandQueue];
    for (int i = 0; i < 2; i++) { MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
                                  td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModeShared; g_rt[i] = [g_dev newTextureWithDescriptor:td]; }
    NvShader *vs, *fs, *vs0, *fs0; shaders(argv[1], &vs, &fs); shaders(argv[2], &vs0, &fs0);
    // rows NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS; columns alpha 0, 0.5, 1 against reference 0.5
    static const int want[8][3] = {{0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1}};
    static const char *const name[8] = {"NEVER", "LESS", "EQUAL", "LEQUAL", "GREATER", "NOTEQUAL", "GEQUAL", "ALWAYS"};
    for (unsigned op = 1; op <= 8; op++) {
        int got[3], early[3], ok = 1;
        for (int i = 0; i < 3; i++) { got[i] = draw1(vs, fs, op, 0.5f, i * 0.5f, 0, NULL); early[i] = draw1(vs, fs, op, 0.5f, i * 0.5f, 1, NULL); ok &= got[i] == want[op - 1][i] && early[i] == got[i]; }
        CHECK(ok, "%-8s ref 0.5: alpha 0 / 0.5 / 1 drawn %d %d %d (want %d %d %d), the same on the early return", name[op - 1], got[0], got[1], got[2], want[op - 1][0], want[op - 1][1], want[op - 1][2]);
    }
    int r1 = draw1(vs, fs, NVN_ALPHA_FUNC_GREATER, 0.75f, 0.5f, 0, NULL), r2 = draw1(vs, fs, NVN_ALPHA_FUNC_GREATER, 0.25f, 0.5f, 0, NULL), r3 = draw1(vs, fs, NVN_ALPHA_FUNC_GREATER, 0.75f, 0.5f, 0, NULL);
    CHECK(r1 == 0 && r2 == 1 && r3 == 0, "GREATER, alpha 0.5, reference 0.75 → 0.25 → 0.75: drawn %d %d %d (want 0 1 0)", r1, r2, r3);
    CHECK(fs->afn.count == 9, "one specialization per comparison and reference: %lu (want 9: 7 comparisons at 0.5, GREATER at 0.75 and 0.25)", (unsigned long)fs->afn.count);
    {   // A NaN reference (bayonetta3-mobile tests/test_alpha.m's case, plan item 128): every comparison with NaN is false but NOTEQUAL; ALWAYS draws
        int ok = 1; char got[9] = {0};
        for (unsigned op = 1; op <= 8; op++) { int d = draw1(vs, fs, op, NAN, 0.5f, 0, NULL); got[op - 1] = d == 1 ? '1' : d == 0 ? '0' : '?'; ok &= d == (op == NVN_ALPHA_FUNC_NOTEQUAL || op == NVN_ALPHA_FUNC_ALWAYS); }
        CHECK(ok, "reference NaN, alpha 0.5: drawn by NOTEQUAL and ALWAYS only (NEVER .. ALWAYS: %s, want 00000101)", got); }
    int plain = 0, a = draw1(vs, fs, NVN_ALPHA_FUNC_ALWAYS, 0.5f, 0.0f, 0, &plain);
    CHECK(a == 1 && plain, "ALWAYS draws with the function specialized at load (no test)");
    int ok0 = 1; for (unsigned op = 1; op <= 8; op++) for (int i = 0; i < 3; i++) ok0 &= draw1(vs0, fs0, op, 0.5f, i * 0.5f, 0, NULL) == (op != NVN_ALPHA_FUNC_NEVER);
    CHECK(ok0, "a shader exporting no alpha: discarded by NEVER only");
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
