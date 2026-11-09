// What a GPU stores for a float above float16's range (plan item 310; nxnative PR #1128, MIT, facts: an M1 Pro stores 65504 where an A19 Pro stores
// +inf, and +inf times 0 gives a NaN that `precise::min(NaN, 2048)` turns into 2048: white blobs). fp16_probe draws one pixel into an RGBA16Float
// target from the floats 1e5, -1e5, 65504 and 65520 (a buffer the shader reads, so the compiler folds nothing) and writes the same four through
// `half()` into a buffer: out[0..3] the target's halves, out[4..7] the shader's. 0x7bff is 65504, 0x7c00 +inf (0xfbff / 0xfc00 negative). The
// backend logs it with DM_FP16_PROBE=1 (mtl_init); gfx/tests/t_fp16_probe.m runs it on the Mac. Returns 0 on success.
// Plan item 315d: a second target gets 1.0007, -1.0007 and 1 + 2^-11 (half way between 1 and the next half, 1.000977): rounded to nearest even
// they store 0x3c01, 0xbc01, 0x3c00, toward zero 0x3c00, 0xbc00, 0x3c00; out[8..11] the target's, out[12..15] half()'s.
// f16_edge_count: how many halves of a target sit at ±65504 (where the M5 puts what overflowed), ±inf (where the A19 Pro does), or are NaN
// (texdump.inc counts them in every half-float colour target it dumps)
#pragma once
#import <Metal/Metal.h>
#include <stdint.h>
#include <string.h>
static const char *fp16_name(uint16_t h) {
    switch (h) { case 0x7bff: return "65504"; case 0xfbff: return "-65504"; case 0x7c00: return "+inf"; case 0xfc00: return "-inf"; default: return (h & 0x7c00) == 0x7c00 ? "NaN" : "other"; }
}
static const char *fp16_rounding(uint16_t h) { return h == 0x3c01 ? "to nearest" : h == 0x3c00 ? "toward zero" : "neither"; }   // what 1.0007 became
static void f16_edge_count(const uint16_t *p, size_t n, uint64_t out[3]) {   // out: at ±65504, at ±inf, NaN
    for (size_t i = 0; i < n; i++) {
#ifndef DM_TEST_F16_COUNT_POSITIVE   // negative control of t_fp16_probe only: the sign bit kept, so only positive values count
        uint16_t v = p[i] & 0x7fff;
#else
        uint16_t v = p[i];
#endif
        if (v == 0x7bff) out[0]++; else if (v == 0x7c00) out[1]++; else if (v > 0x7c00 && v <= 0x7fff) out[2]++;
    }
}
static int fp16_probe(id<MTLDevice> dev, uint16_t out[16]) { @autoreleasepool {
    static const char *src =
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct V { float4 p [[position]]; };\n"
        "vertex V vs(uint i [[vertex_id]]) { V o; float2 q = float2((i << 1) & 2, i & 2); o.p = float4(q * 2 - 1, 0, 1); return o; }\n"
        "struct F { float4 a [[color(0)]]; float4 b [[color(1)]]; };\n"
        "fragment F fs(constant float4 *v [[buffer(0)]]) { F o; o.a = v[0]; o.b = v[1]; return o; }\n"
        "kernel void cs(constant float4 *v [[buffer(0)]], device ushort4 *o [[buffer(1)]]) { o[0] = as_type<ushort4>(half4(v[0])); o[1] = as_type<ushort4>(half4(v[1])); }\n";
    MTLCompileOptions *opt = [MTLCompileOptions new]; opt.mathMode = MTLMathModeSafe;
    NSError *e = nil; id<MTLLibrary> lib = [dev newLibraryWithSource:@(src) options:opt error:&e]; if (!lib) return 1;
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
#ifndef DM_TEST_FP16_PROBE_UNORM   // negative control of t_fp16_probe only: an 8-bit target, which cannot hold the values
    MTLPixelFormat fmt = MTLPixelFormatRGBA16Float;
#else
    MTLPixelFormat fmt = MTLPixelFormatRGBA16Unorm;
#endif
    pd.colorAttachments[0].pixelFormat = fmt; pd.colorAttachments[1].pixelFormat = fmt;
    id<MTLRenderPipelineState> rps = [dev newRenderPipelineStateWithDescriptor:pd error:&e]; if (!rps) return 2;
    id<MTLComputePipelineState> cps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"cs"] error:&e]; if (!cps) return 3;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:1 height:1 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [dev newTextureWithDescriptor:td], t2 = [dev newTextureWithDescriptor:td];
    const float v[8] = {1e5f, -1e5f, 65504.0f, 65520.0f, 1.0007f, -1.0007f, 1.00048828125f, 0};
    id<MTLBuffer> in = [dev newBufferWithBytes:v length:sizeof v options:MTLResourceStorageModeShared];
    id<MTLBuffer> rt = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared], cv = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> q = [dev newCommandQueue]; id<MTLCommandBuffer> cb = [q commandBuffer];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = t; rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[1].texture = t2; rp.colorAttachments[1].loadAction = MTLLoadActionClear; rp.colorAttachments[1].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
    [re setRenderPipelineState:rps]; [re setFragmentBuffer:in offset:0 atIndex:0]; [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [re endEncoding];
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:cps]; [ce setBuffer:in offset:0 atIndex:0]; [ce setBuffer:cv offset:0 atIndex:1];
    [ce dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)]; [ce endEncoding];
    id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
    [be copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(1, 1, 1) toBuffer:rt destinationOffset:0 destinationBytesPerRow:8 destinationBytesPerImage:8];
    [be copyFromTexture:t2 sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(1, 1, 1) toBuffer:rt destinationOffset:8 destinationBytesPerRow:8 destinationBytesPerImage:8];
    [be endEncoding]; [cb commit]; [cb waitUntilCompleted];
    if (cb.error) return 4;
    memcpy(out, rt.contents, 8); memcpy(out + 4, cv.contents, 8); memcpy(out + 8, (uint8_t *)rt.contents + 8, 8); memcpy(out + 12, (uint8_t *)cv.contents + 8, 8);
    return 0;
} }
