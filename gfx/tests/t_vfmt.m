// Scaled vertex formats (*_UI2F / *_I2F: the integer converted to its float value) through the production pipeline descriptor (pso_desc): a float
// shader input reads RG8_UI2F 17 / 203 as 17.0 / 203.0, RG8_I2F 0xF0 as -16.0, RGBA16_UI2F 65535 as 65535.0, RGBA16_I2F 0x8000 as -32768.0 (BotW's
// grass reads RG8_UI2F; jiangjifeng's nxnative fork ae9082d checks the same). The old mapping had none of them: the attribute fell back to the
// constant buffer and read 0.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
#define EXPECT(name, want, got) do { double w_ = (want), g_ = (got); n++; printf("%-74s expected %-9g → got %-9g %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); fail += w_ != g_; } while (0)
static NvShader *g_tvs, *g_tfs;
static void fetch(int fmt, const uint8_t *bytes, size_t len, float out[4]) {   // one point whose attribute 2 (stream 1, offset 2) has this format and these bytes
    pso_key k = {0}; k.cfmt[0] = MTLPixelFormatRGBA32Float; k.chmask = 0xf; k.samples = 1;
    k.attr[2] = (va_st){.fmt = (uint32_t)fmt, .offset = 2, .stream = 1}; k.stream[1].stride = 16;
    MTLRenderPipelineDescriptor *pd = pso_desc(&k, g_tvs, g_tfs, g_tfs->fn, 0);
    pd.inputPrimitiveTopology = MTLPrimitiveTopologyClassPoint;
    NSError *e = nil; id<MTLRenderPipelineState> ps = [g_dev newRenderPipelineStateWithDescriptor:pd error:&e];
    out[0] = out[1] = out[2] = out[3] = -999;
    if (!ps) { printf("  pipeline refused: %s\n", e.localizedDescription.UTF8String); return; }
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:1 height:1 mipmapped:NO]; td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [g_dev newTextureWithDescriptor:td];
    uint8_t vb[16] = {0}; memcpy(vb + 2, bytes, len); id<MTLBuffer> b = [g_dev newBufferWithBytes:vb length:16 options:MTLResourceStorageModeShared];
    float zero[4] = {0}; id<MTLBuffer> cz = [g_dev newBufferWithBytes:zero length:16 options:MTLResourceStorageModeShared];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor new]; rp.colorAttachments[0].texture = t; rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
    [en setRenderPipelineState:ps]; [en setVertexBuffer:b offset:0 atIndex:16 + 1]; [en setVertexBuffer:cz offset:0 atIndex:30];
    [en drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:1]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
    [t getBytes:out bytesPerRow:16 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
}
int main(void) { @autoreleasepool {
    mtl_init(); g_q = [g_dev newCommandQueue]; NSError *e = nil;
    id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nstruct I { float4 a [[attribute(0)]]; }; struct V { float4 p [[position]]; float4 a; float ps [[point_size]]; };\n"
        "vertex V vs(I i [[stage_in]]) { V o; o.p = float4(0, 0, 0, 1); o.a = i.a; o.ps = 1; return o; }\nfragment float4 fs(V v [[stage_in]]) { return v.a; }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    g_tvs = [NvShader new]; g_tvs->fn = [l newFunctionWithName:@"vs"]; g_tvs->rf.nv = 1; g_tvs->rf.v[0].loc = 0; g_tvs->rf.v[0].nvn = 2; g_tvs->rf.v[0].ct = NVN_CT_FLOAT;
    g_tfs = [NvShader new]; g_tfs->stage = 1; g_tfs->fn = [l newFunctionWithName:@"fs"];
    float v[4];
    static const uint8_t rg8u[] = {17, 203}; fetch(NVN_FORMAT_RG8_UI2F, rg8u, 2, v);
    EXPECT("RG8_UI2F 17: 17.0", 17, v[0]); EXPECT("RG8_UI2F 203: 203.0", 203, v[1]); EXPECT("RG8_UI2F: z 0", 0, v[2]); EXPECT("RG8_UI2F: w 1", 1, v[3]);
    static const uint8_t rg8s[] = {17, 0xF0}; fetch(NVN_FORMAT_RG8_I2F, rg8s, 2, v);
    EXPECT("RG8_I2F 0xF0: -16.0", -16, v[1]);
    static const uint8_t rgba16u[] = {0xFF, 0xFF, 0x10, 0x27, 0, 0, 1, 0}; fetch(NVN_FORMAT_RGBA16_UI2F, rgba16u, 8, v);
    EXPECT("RGBA16_UI2F 65535: 65535.0", 65535, v[0]); EXPECT("RGBA16_UI2F 10000: 10000.0", 10000, v[1]); EXPECT("RGBA16_UI2F 1: 1.0", 1, v[3]);
    static const uint8_t rgba16s[] = {0x00, 0x80, 0xFF, 0x7F, 0xFF, 0xFF, 0, 0}; fetch(NVN_FORMAT_RGBA16_I2F, rgba16s, 8, v);
    EXPECT("RGBA16_I2F 0x8000: -32768.0", -32768, v[0]); EXPECT("RGBA16_I2F 0x7FFF: 32767.0", 32767, v[1]); EXPECT("RGBA16_I2F 0xFFFF: -1.0", -1, v[2]);
    printf("t_vfmt: %d checks, %d failed\n", n, fail); return fail != 0;
} }
