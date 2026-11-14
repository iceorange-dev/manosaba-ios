// Vertex fetch on the GPU (nvn_mtl.m vfmt / vfmt_for_float_input, the formats pso_desc gives Metal; plan item 128): a vertex shader passes its
// attribute's bits to the fragment shader, which writes them to an RGBA32Uint target. dr3-mobile tests/test_vertex_fetch.m's vectors (MIT, as data):
// NVN RGBA8 (normalized) {0, 64, 128, 255} reads {0, 0x3e808081, 0x3f008081, 0x3f800000}; RGBA32F keeps NaN payloads and -0 bit for bit. Ours too:
// RGBA32UI / RGBA32I meeting a float input are fetched as Float4, so the shader sees the integers' bits (Metal refuses a 32-bit integer format for a
// float input); RGBA8UI into a uint input (the main0_vi variant's case) reads the integers. The integer format kept for a float input
// (-DDM_TEST_VFETCH_NO_FLOATIN): Metal refuses the pipeline, 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
// the attribute's 4 words as the vertex shader sees them (type: "float" or "uint"); 0 if the pipeline is refused
static int fetch(int nvn_fmt, int float_input, const void *data, size_t bytes, uint32_t out[4]) {
    NSString *ty = float_input ? @"float" : @"uint";
    NSString *src = [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\nstruct VI { %@4 a [[attribute(0)]]; };\n"
        "struct VO { float4 pos [[position]]; uint4 bits [[user(locn0), flat]]; };\n"
        "vertex VO v(VI in [[stage_in]], uint id [[vertex_id]]) { VO o; float2 p[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)}; o.pos = float4(p[id], 0, 1); o.bits = as_type<uint4>(in.a); return o; }\n"
        "fragment uint4 f(VO in [[stage_in]]) { return in.bits; }\n", ty];
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:src options:nil error:&e]; if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 0; }
    MTLVertexFormat vf = vfmt(nvn_fmt); if (float_input && vfmt_is_int(vf) && !vfmt_scaled(nvn_fmt)) vf = vfmt_for_float_input(vf);   // as pso_desc without a variant
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"v"]; pd.fragmentFunction = [lib newFunctionWithName:@"f"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Uint;
    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor]; vd.attributes[0].format = vf; vd.attributes[0].bufferIndex = 1;
    vd.layouts[1].stride = bytes; vd.layouts[1].stepFunction = MTLVertexStepFunctionConstant; vd.layouts[1].stepRate = 0; pd.vertexDescriptor = vd;
    id<MTLRenderPipelineState> ps = [g_dev newRenderPipelineStateWithDescriptor:pd error:&e]; if (!ps) return 0;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Uint width:1 height:1 mipmapped:NO];
    td.storageMode = MTLStorageModeShared; td.usage = MTLTextureUsageRenderTarget; id<MTLTexture> tex = [g_dev newTextureWithDescriptor:td];
    id<MTLBuffer> in = [g_dev newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_q commandBuffer]; MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = tex; rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp]; [en setRenderPipelineState:ps]; [en setVertexBuffer:in offset:0 atIndex:1];
    [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
    [tex getBytes:out bytesPerRow:16 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
    return 1;
}
static int eq(const uint32_t a[4], const uint32_t b[4]) { return !memcmp(a, b, 16); }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); g_q = [g_dev newCommandQueue]; uint32_t got[4];
    const uint8_t u8[4] = {0, 64, 128, 255}; const uint32_t w8[4] = {0, 0x3e808081, 0x3f008081, 0x3f800000};
    CHECK(fetch(0x25, 1, u8, 4, got) && eq(got, w8), "RGBA8 normalized {0, 64, 128, 255}: %08x %08x %08x %08x (dr3-mobile's)", got[0], got[1], got[2], got[3]);
    const uint32_t f32[4] = {0x7fc12345, 0xffc00042, 0x3f800000, 0x80000000};
    CHECK(fetch(0x2E, 1, f32, 16, got) && eq(got, f32), "RGBA32F: NaN payloads and -0 kept bit for bit (%08x %08x %08x %08x)", got[0], got[1], got[2], got[3]);
    const uint32_t ui[4] = {1, 0x80000000, 0xffffffff, 42};
    CHECK(fetch(0x2F, 1, ui, 16, got) && eq(got, ui), "RGBA32UI into a float input: the integers' bits (%08x %08x %08x %08x)", got[0], got[1], got[2], got[3]);
    const int32_t si[4] = {-1, -32768, 7, INT32_MIN};
    CHECK(fetch(0x30, 1, si, 16, got) && eq(got, (const uint32_t *)si), "RGBA32I into a float input: the integers' bits");
    const uint32_t w8u[4] = {0, 64, 128, 255};
    CHECK(fetch(0x27, 0, u8, 4, got) && eq(got, w8u), "RGBA8UI into a uint input (main0_vi): 0 64 128 255 (%u %u %u %u)", got[0], got[1], got[2], got[3]);
    printf("VFETCH %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
} }
