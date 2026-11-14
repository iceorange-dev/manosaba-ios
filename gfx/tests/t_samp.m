// Samplers as the replay builds them (samp_by_id from a registered samp_par): the LOD bias reaches the sampler (a two-level texture sampled at a
// level-0 footprint with bias 1 reads level 1) and min / max reduction filters (a linear sample midway between two texels gives the smaller or
// the larger one, averaging gives the mean). The old code applied neither: bias 1 read level 0, min and max both read the mean.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
#define EXPECT(name, want, got) do { long long w_ = (want), g_ = (got); n++; printf("%-84s expected %-4lld → got %-4lld %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); fail += w_ != g_; } while (0)

static id<MTLComputePipelineState> g_ps;
static uint8_t sample_red(id<MTLTexture> t, int sid, float u, float lodgrad) {   // the red byte of one sample at (u, 0.5) with gradients for that footprint
    pthread_mutex_lock(&g_rmu); id<MTLSamplerState> s = samp_by_id(sid); pthread_mutex_unlock(&g_rmu);
    id<MTLBuffer> out = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
    float par[2] = {u, lodgrad};
    id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:g_ps]; [e setTexture:t atIndex:0]; [e setSamplerState:s atIndex:0]; [e setBuffer:out offset:0 atIndex:0]; [e setBytes:par length:sizeof par atIndex:1];
    [e dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)]; [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
    return (uint8_t)lroundf(((float *)out.contents)[0] * 255.f);
}
static int reg(int sid, int minf, int magf, float bias, int reduction) {
    samp_par p = {0}; p.minf = (uint8_t)minf; p.magf = (uint8_t)magf; p.ws = p.wt = p.wr = NVN_WRAP_MODE_CLAMP_TO_EDGE; p.lod_min = 0; p.lod_max = 1000; p.aniso = 1;
    p.lod_bias = bias; p.reduction = (uint8_t)reduction; mtl_reg_samp(sid, &p); return sid;
}
int main(void) { @autoreleasepool {
    mtl_init(); NSError *e = nil;
    id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void k(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o [[buffer(0)]], constant float2 &p [[buffer(1)]]) {\n"
        "  o[0] = t.sample(s, float2(p.x, 0.5), gradient2d(float2(p.y, 0), float2(0, p.y))); }\n" options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    g_ps = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e]; g_q = [g_dev newCommandQueue];
    if (!g_ps) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    // a 4x4 texture, level 0 red 255, level 1 red 100
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:4 mipmapped:YES];
    td.mipmapLevelCount = 2; id<MTLTexture> mip = [g_dev newTextureWithDescriptor:td];
    uint32_t l0[16], l1[4]; for (int i = 0; i < 16; i++) l0[i] = 0xff0000ffu; for (int i = 0; i < 4; i++) l1[i] = 0xff000064u;
    [mip replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:l0 bytesPerRow:16]; [mip replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:1 withBytes:l1 bytesPerRow:8];
    // NVN min filters: 3 linear within the nearest level, 5 linear between levels (needed for reduction); mag 1 linear
    EXPECT("bias 0, level-0 footprint: level 0", 255, sample_red(mip, reg(10, 3, 1, 0.f, 0), .5f, .25f));
    EXPECT("bias 1, level-0 footprint: level 1", 100, sample_red(mip, reg(11, 3, 1, 1.f, 0), .5f, .25f));
    EXPECT("bias -1, level-1 footprint: level 0", 255, sample_red(mip, reg(12, 3, 1, -1.f, 0), .5f, .5f));
    // a 2x1 texture, red 51 and 204: a linear sample at u = 0.5 weighs both equally
    td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:2 height:1 mipmapped:NO]; id<MTLTexture> two = [g_dev newTextureWithDescriptor:td];
    uint32_t px[2] = {0xff000033u, 0xff0000ccu}; [two replaceRegion:MTLRegionMake2D(0, 0, 2, 1) mipmapLevel:0 withBytes:px bytesPerRow:8];
    int avg = sample_red(two, reg(20, 5, 1, 0.f, NVN_SAMPLER_REDUCTION_AVERAGE), .5f, .5f);
    EXPECT("reduction average: the mean (128 ± 1)", 1, abs(avg - 128) <= 1);
    EXPECT("reduction min: the smaller texel", 51, sample_red(two, reg(21, 5, 1, 0.f, NVN_SAMPLER_REDUCTION_MIN), .5f, .5f));
    EXPECT("reduction max: the larger texel", 204, sample_red(two, reg(22, 5, 1, 0.f, NVN_SAMPLER_REDUCTION_MAX), .5f, .5f));
    // Plan item 195: a sampler registered again over a slot is kept until the submissions before that have run (argument blocks name it by its
    // gpuResourceID only): it waits in the deferred releases, which let it go after them. (Its liveness cannot tell: the device keeps sampler
    // states of its own, so a weak reference stays set even when the slot let go at once.) -DDM_TEST_SAMP_RELEASE_AT_ONCE fails the first check
    id<MTLSamplerState> old;
    reg(30, 3, 1, 0.f, 0); pthread_mutex_lock(&g_rmu); old = samp_by_id(30); pthread_mutex_unlock(&g_rmu);
    reg(30, 5, 0, .5f, 0);
    int held = 0; pthread_mutex_lock(&g_deferred_mu); for (NvDeferredRelease *d in g_deferred) held += d->object == old; pthread_mutex_unlock(&g_deferred_mu);
    EXPECT("195: the replaced sampler waits in the deferred releases", 1, held);
    for (int i = 0; i < 2; i++) @autoreleasepool { mtl_sub_begin(); mtl_wait(mtl_sub_commit(), 5000000000ull); }
    held = 0; pthread_mutex_lock(&g_deferred_mu); for (NvDeferredRelease *d in g_deferred) held += d->object == old; pthread_mutex_unlock(&g_deferred_mu);
    EXPECT("195: let go once the submissions before it have run", 0, held);
    printf("t_samp: %d checks, %d failed\n", n, fail); return fail != 0;
} }
