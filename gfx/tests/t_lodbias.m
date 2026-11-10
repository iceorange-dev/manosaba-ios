// Sampler LOD bias carried by the shader (gfx/nvn/mtl/lod_bias.inc lb_src / lb_variant / lb_rebind, descriptors.inc lb_emu; plan item 318) on the
// GPU, with DM_SAMP_LB_EMU=1 (as on a GPU that ignores the sampler's): a 4x4 texture with mips red 4x4, green 2x2, blue 1x1 and a nearest
// sampler registered with LOD bias 1 through the production path. The sampler is made without its bias (a plain level(0) read gives red); each
// form rewritten reads the mip the bias selects: level(0) → green, with bias 2 → blue (a kernel); in a fragment shader drawing a 4x4 target at
// level of detail 0, no option → bias(1), an offset kept after it, bias(0) → bias(0 + 1), gradient2d × 2: green; a 2D array sample gets bias after
// its layer and compiles. sample_compare on a 4x4 depth texture with mips 0.25 and 0.75 through a less-or-equal sampler, compare value 0.5: plain
// 0 (mip 0), level(0) → level(0 + 1) 1 in a kernel, no option → bias(1) 1 in a fragment shader, and the array form with its layer and level
// compiles. lb_variant builds the kernel's variant from its .metal and .json (t_lodbias.d) for the
// sampler bound to the stage, once (the second call returns it), and none for a sampler without a bias; lb_rebind marks the stage while one is
// bound. With DM_SAMP_LB_EMU=0 the sampler keeps its bias and no shader carries it. No variant (-DDM_TEST_LB_VARIANT_OFF): 1 fails;
// sample_compare left as it was (-DDM_TEST_LB_COMPARE_LEFT): 3 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
#define HDR "#include <metal_stdlib>\nusing namespace metal;\nstruct spvDescriptorSetBuffer0 { texture2d<float> tex [[id(0)]]; sampler texSmplr [[id(1)]]; };\n"
#define KERNEL(opt) HDR "kernel void k(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], device float4 *out [[buffer(1)]])\n" \
    "{ out[0] = spvDescriptorSet0.tex.sample(spvDescriptorSet0.texSmplr, float2(0.5), " opt "); }\n"
#define FRAG(call) HDR "vertex float4 vtx(uint i [[vertex_id]]) { float2 p = float2((i << 1) & 2, i & 2); return float4(p * 2.0 - 1.0, 0, 1); }\n" \
    "fragment float4 f(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], float4 p [[position]])\n" \
    "{ float2 uv = p.xy / 4.0; return " call "; }\n"
static id<MTLCommandQueue> g_q3; static id<MTLTexture> g_t;
static const char *col(const float *c) { return c[0] > 0.5f && c[1] < 0.5f ? "red" : c[1] > 0.5f && c[0] < 0.5f ? "green" : c[2] > 0.5f && c[0] < 0.5f ? "blue" : "other"; }
static id<MTLBuffer> args(id<MTLSamplerState> s) {
    id<MTLBuffer> ab = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]; MTLResourceID ids[2] = {g_t.gpuResourceID, s.gpuResourceID}; memcpy(ab.contents, ids, sizeof ids); return ab;
}
static int runk_fn(id<MTLFunction> fn, id<MTLSamplerState> s, float out[4]) {
    NSError *e = nil; id<MTLComputePipelineState> ps = fn ? [g_dev newComputePipelineStateWithFunction:fn error:&e] : nil;
    if (!ps) { printf("    pipeline: %s\n", e ? e.localizedDescription.UTF8String : "no function"); return 0; }
    id<MTLBuffer> ab = args(s), ob = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_q3 commandBuffer]; id<MTLComputeCommandEncoder> en = [cb computeCommandEncoder];
    [en setComputePipelineState:ps]; [en setBuffer:ab offset:0 atIndex:0]; [en setBuffer:ob offset:0 atIndex:1]; [en useResource:g_t usage:MTLResourceUsageRead];
    [en dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
    memcpy(out, ob.contents, 16); return 1;
}
static int runk(NSString *src, id<MTLSamplerState> s, float out[4]) {
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:src options:nil error:&e];
    if (!lib) { printf("    compile: %s\n", e.localizedDescription.UTF8String); return 0; }
    return runk_fn([lib newFunctionWithName:@"k"], s, out);
}
static int runf(NSString *src, id<MTLSamplerState> s, float out[4]) {   // a 4x4 target drawn whole; pixel (1, 1) read
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:src options:nil error:&e];
    if (!lib) { printf("    compile: %s\n", e.localizedDescription.UTF8String); return 0; }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"vtx"]; pd.fragmentFunction = [lib newFunctionWithName:@"f"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
    id<MTLRenderPipelineState> ps = [g_dev newRenderPipelineStateWithDescriptor:pd error:&e];
    if (!ps) { printf("    pipeline: %s\n", e.localizedDescription.UTF8String); return 0; }
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:4 height:4 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModeShared; id<MTLTexture> rt = [g_dev newTextureWithDescriptor:td];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor]; rp.colorAttachments[0].texture = rt; rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLBuffer> ab = args(s); id<MTLCommandBuffer> cb = [g_q3 commandBuffer]; id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
    [en setRenderPipelineState:ps]; [en setFragmentBuffer:ab offset:0 atIndex:0]; [en useResource:g_t usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
    [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
    float px[16 * 4]; [rt getBytes:px bytesPerRow:16 * 4 fromRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0]; memcpy(out, px + (1 * 4 + 1) * 4, 16); return 1;
}
static NSString *rw(NSString *src, float b, int frag, int *cnt, int *left) { return lb_src(src, @[@"texSmplr"], &b, frag, cnt, left); }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_SAMP_LB_EMU", "1", 1);
    mtl_init(); g_q3 = [g_dev newCommandQueue];
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:4 mipmapped:YES]; td.storageMode = MTLStorageModeShared;
    g_t = [g_dev newTextureWithDescriptor:td]; uint32_t px[16], cl[3] = {0xff0000ff, 0xff00ff00, 0xffff0000};   // red, green, blue (RGBA8 little-endian)
    for (int l = 0; l < 3; l++) { int w = 4 >> l; for (int i = 0; i < w * w; i++) px[i] = cl[l]; [g_t replaceRegion:MTLRegionMake2D(0, 0, w, w) mipmapLevel:l withBytes:px bytesPerRow:w * 4]; }
    samp_par sp = {.minf = 2, .magf = 0, .ws = 0, .wt = 0, .wr = 0, .lod_max = 16, .lod_bias = 1.0f}, none = sp; none.lod_bias = 0;
    mtl_reg_samp(7, &sp); mtl_reg_samp(8, &none);
    pthread_mutex_lock(&g_rmu); id<MTLSamplerState> s = samp_by_id(7); (void)samp_by_id(8); int lb7 = g_sreg_lb[7], lb8 = g_sreg_lb[8]; pthread_mutex_unlock(&g_rmu);
    CHECK(lb7 && !lb8 && g_lb_any, "DM_SAMP_LB_EMU=1: the sampler with bias 1 carried by the shaders, the one without not");
    float o[4] = {0}; int cnt, left;
    CHECK(runk(@KERNEL("level(0.0)"), s, o) && !strcmp(col(o), "red"), "the sampler made without its bias: a plain level(0) read gives mip 0 (%s)", col(o));
    NSString *k1 = rw(@KERNEL("level(0.0)"), 1.0f, 0, &cnt, &left);
    CHECK(cnt == 1 && runk(k1, s, o) && !strcmp(col(o), "green"), "a kernel's level(0) → level((0) + 1): mip 1 (%s, %d call)", col(o), cnt);
    CHECK(runk(rw(@KERNEL("level(0.0)"), 2.0f, 0, &cnt, &left), s, o) && !strcmp(col(o), "blue"), "... with bias 2: mip 2 (%s)", col(o));
    CHECK(runf(@FRAG("spvDescriptorSet0.tex.sample(spvDescriptorSet0.texSmplr, uv)"), s, o) && !strcmp(col(o), "red"), "a fragment shader at level of detail 0, plain: mip 0 (%s)", col(o));
    CHECK(runf(rw(@FRAG("spvDescriptorSet0.tex.sample(spvDescriptorSet0.texSmplr, uv)"), 1.0f, 1, &cnt, &left), s, o) && cnt == 1 && !strcmp(col(o), "green"), "... no option → bias(1): mip 1 (%s)", col(o));
    NSString *fo = rw(@FRAG("spvDescriptorSet0.tex.sample(spvDescriptorSet0.texSmplr, uv, int2(0))"), 1.0f, 1, &cnt, &left);
    CHECK(cnt == 1 && [fo containsString:@"bias(float("] && [fo rangeOfString:@"bias(float("].location < [fo rangeOfString:@"int2(0)"].location && runf(fo, s, o) && !strcmp(col(o), "green"),
          "... with an offset: bias(1) before it, mip 1 (%s)", col(o));
    CHECK(runf(rw(@FRAG("spvDescriptorSet0.tex.sample(spvDescriptorSet0.texSmplr, uv, bias(0.0))"), 1.0f, 1, &cnt, &left), s, o) && cnt == 1 && !strcmp(col(o), "green"), "... bias(0) → bias((0) + 1): mip 1 (%s)", col(o));
    CHECK(runf(rw(@FRAG("spvDescriptorSet0.tex.sample(spvDescriptorSet0.texSmplr, uv, gradient2d(dfdx(uv), dfdy(uv)))"), 1.0f, 1, &cnt, &left), s, o) && cnt == 1 && !strcmp(col(o), "green"),
          "... gradient2d(dx, dy) → both × 2: mip 1 (%s)", col(o));
    NSString *arr = @"#include <metal_stdlib>\nusing namespace metal;\nstruct spvDescriptorSetBuffer0 { texture2d_array<float> arr [[id(0)]]; sampler texSmplr [[id(1)]]; };\n"
        "fragment float4 f(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], float4 p [[position]])\n{ return spvDescriptorSet0.arr.sample(spvDescriptorSet0.texSmplr, p.xy, uint(rint(p.z))); }\n";
    NSString *arw = rw(arr, 1.0f, 1, &cnt, &left); NSError *e = nil;
    CHECK(cnt == 1 && [arw containsString:@"uint(rint(p.z)), bias(float("] && [g_dev newLibraryWithSource:arw options:nil error:&e] != nil, "a 2D array sample: bias after its layer, compiles%s%s", e ? ": " : "", e ? e.localizedDescription.UTF8String : "");
    {   // sample_compare: a depth texture whose mip 0 holds 0.25 and mip 1 0.75, compare value 0.5 through a less-or-equal sampler (0.5 <= texel)
#define DHDR "#include <metal_stdlib>\nusing namespace metal;\nstruct spvDescriptorSetBuffer0 { depth2d<float> tex [[id(0)]]; sampler texSmplr [[id(1)]]; };\n"
#define DKERNEL(opt) DHDR "kernel void k(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], device float4 *out [[buffer(1)]])\n" \
    "{ out[0] = float4(spvDescriptorSet0.tex.sample_compare(spvDescriptorSet0.texSmplr, float2(0.5), 0.5" opt ")); }\n"
#define DFRAG DHDR "vertex float4 vtx(uint i [[vertex_id]]) { float2 p = float2((i << 1) & 2, i & 2); return float4(p * 2.0 - 1.0, 0, 1); }\n" \
    "fragment float4 f(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], float4 p [[position]])\n" \
    "{ float2 uv = p.xy / 4.0; return float4(spvDescriptorSet0.tex.sample_compare(spvDescriptorSet0.texSmplr, uv, 0.5)); }\n"
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:4 height:4 mipmapped:YES];
        dd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; dd.storageMode = MTLStorageModePrivate; id<MTLTexture> dt = [g_dev newTextureWithDescriptor:dd];
        id<MTLCommandBuffer> cb = [g_q3 commandBuffer];
        for (int l = 0; l < 3; l++) { MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor]; rp.depthAttachment.texture = dt; rp.depthAttachment.level = (NSUInteger)l;
            rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.storeAction = MTLStoreActionStore; rp.depthAttachment.clearDepth = l ? 0.75 : 0.25;
            [[cb renderCommandEncoderWithDescriptor:rp] endEncoding]; }
        [cb commit]; [cb waitUntilCompleted];
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest; sd.mipFilter = MTLSamplerMipFilterNearest;
        sd.compareFunction = MTLCompareFunctionLessEqual; sd.supportArgumentBuffers = YES; id<MTLSamplerState> cs = [g_dev newSamplerStateWithDescriptor:sd];
        id<MTLTexture> was = g_t; g_t = dt;
        CHECK(runk(@DKERNEL(", level(0.0)"), cs, o) && o[0] == 0.f, "sample_compare, plain level(0): mip 0's 0.25 fails the compare (%g)", o[0]);
        NSString *ck = rw(@DKERNEL(", level(0.0)"), 1.0f, 0, &cnt, &left);
        CHECK(cnt == 1 && !left && runk(ck, cs, o) && o[0] == 1.f, "... level(0) → level((0) + 1): mip 1's 0.75 passes (%g, %d call) [-DDM_TEST_LB_COMPARE_LEFT: left]", o[0], cnt);
        NSString *cf = rw(@DFRAG, 1.0f, 1, &cnt, &left);
        CHECK(cnt == 1 && [cf containsString:@"uv, 0.5, bias(float("] && runf(cf, cs, o) && o[0] == 1.f, "... a fragment shader's, no option → bias(1) after the compare value: passes (%g)", o[0]);
        g_t = was;
        NSString *ca = @"#include <metal_stdlib>\nusing namespace metal;\nstruct spvDescriptorSetBuffer0 { depth2d_array<float> d [[id(0)]]; sampler texSmplr [[id(1)]]; };\n"
            "fragment float4 f(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], float4 p [[position]])\n"
            "{ return float4(spvDescriptorSet0.d.sample_compare(spvDescriptorSet0.texSmplr, p.xy, uint(rint(p.z)), 0.5) + spvDescriptorSet0.d.sample_compare(spvDescriptorSet0.texSmplr, p.xy, uint(rint(p.z)), 0.5, level(p.w))); }\n";
        NSString *caw = rw(ca, 1.0f, 1, &cnt, &left); NSError *ce = nil;
        CHECK(cnt == 2 && !left && [caw containsString:@"uint(rint(p.z)), 0.5, bias(float("] && [caw containsString:@"0.5, level((p.w) + float("] && [g_dev newLibraryWithSource:caw options:nil error:&ce] != nil,
              "the array forms (Tears of the Kingdom's shadow reads): bias after the compare value, level(l) → level((l) + 1), compiles%s%s", ce ? ": " : "", ce ? ce.localizedDescription.UTF8String : "");
    }
    // lb_variant end to end: the kernel's .metal and .json beside each other (as in the shader cache), the sampler bound to the compute stage
    char cwd[1024]; if (!getcwd(cwd, sizeof cwd)) return 2;
    NSString *dir = [@(cwd) stringByAppendingPathComponent:@"t_lodbias.d"]; [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    [@KERNEL("level(0.0)") writeToFile:[dir stringByAppendingPathComponent:@"00000000000003b8_b.metal"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [@"{\"entry\": \"k\", \"bindings\": [{\"msl_name\": \"tex\", \"arg_id\": 0, \"kind\": \"tex\"}, {\"msl_name\": \"texSmplr\", \"arg_id\": 1, \"kind\": \"samp\"}]}"
        writeToFile:[dir stringByAppendingPathComponent:@"00000000000003b8_b.json"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    g_msl_dir = dir;
    NvShader *sh = [NvShader new]; sh->stage = 5; snprintf(sh->key, sizeof sh->key, "00000000000003b8_b"); sh->rf.nb = 2;
    sh->rf.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .aid = 0}; sh->rf.b[1] = (nvn_rbind){.kind = NVN_RB_SAMPT, .reg = 0, .aid = 1};
    uint64_t h7 = (uint64_t)(7 - g_samp_ns) << 20, h8 = (uint64_t)(8 - g_samp_ns) << 20;
    S.tex[5][0] = h7; NvShader *v = lb_variant(sh, 5);
    CHECK(v != sh && runk_fn(v->fn, s, o) && !strcmp(col(o), "green"), "lb_variant: a variant for the sampler with bias 1 bound, it reads mip 1 (%s) [-DDM_TEST_LB_VARIANT_OFF: the plain shader]", v != sh ? col(o) : "no variant");
    CHECK(lb_variant(sh, 5) == v, "... the same variant at the next call");
    S.tex[5][0] = h8; CHECK(lb_variant(sh, 5) == sh, "the sampler without a bias bound: the shader itself");
    S.lb_bound[1] = 0; lb_rebind(1, &h7, 1); int on = S.lb_bound[1]; S.tex[1][0] = h8; lb_rebind(1, &h8, 1);
    CHECK(on && !S.lb_bound[1], "lb_rebind: the stage marked while the biased sampler is bound, not after");
    // DM_SAMP_LB_EMU=0: the sampler keeps its bias, no shader carries it
    g_lb_force = 0; samp_par sp9 = sp; mtl_reg_samp(9, &sp9);
    pthread_mutex_lock(&g_rmu); id<MTLSamplerState> s9 = samp_by_id(9); int lb9 = g_sreg_lb[9]; pthread_mutex_unlock(&g_rmu);
    int native = runk(@KERNEL("level(0.0)"), s9, o);
    if (!g_lb_probe) CHECK(!lb9 && native && !strcmp(col(o), "green"), "DM_SAMP_LB_EMU=0: not carried, the sampler's own bias applied by this GPU (%s)", col(o));
    else CHECK(!lb9, "DM_SAMP_LB_EMU=0: not carried (this GPU ignores the sampler's bias: its read not checked)");
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
