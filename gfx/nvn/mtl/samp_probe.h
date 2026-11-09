// Whether this GPU applies a sampler's LOD bias and min / max reduction (plan item 318; S2Arm's probe and finding, Apache-2.0, written anew): both
// came to MTLSamplerDescriptor with OS 26, and descriptors.inc sets them on any GPU from then on, but S2Arm read the reduction ignored (sampled as
// plain linear) on an M4 Pro and an M3 and the LOD bias ignored on the M3 (Apple's feature tables give both from Apple10, the M4 Pro applied the
// bias). Tears of the Kingdom biases 861 samplers. samp_probe reads one compute dispatch: a 4x4 R32Float texture with mip 0 at 0 and mip 1 at 1
// sampled at level(0) through a nearest sampler with LOD bias 1 (1 = applied) and with none (0: the control), mip 1 read at level(1) (1: the
// texture is right), and a 2x1 one holding 0 and 1 sampled between them through linear samplers with min and max reduction (0 and 1 = applied,
// 0.5 = plain linear). out: bias, min, max applied (1 / 0), then the control's and level 1's reads as 0 / 1. Returns 0 on success, -1 before OS 26
#pragma once
#import <Metal/Metal.h>
static int samp_probe(id<MTLDevice> dev, int out[5]) { @autoreleasepool {
    if (@available(macOS 26.0, iOS 26.0, *)) {
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
            "kernel void k(texture2d<float> t [[texture(0)]], texture2d<float> u [[texture(1)]], sampler sb [[sampler(0)]], sampler s0 [[sampler(1)]], sampler smin [[sampler(2)]],"
            " sampler smax [[sampler(3)]], device float *o [[buffer(0)]]) {"
            " o[0] = t.sample(sb, float2(0.5), level(0.0)).r; o[1] = t.sample(s0, float2(0.5), level(0.0)).r; o[2] = t.sample(s0, float2(0.5), level(1.0)).r;"
            " o[3] = u.sample(smin, float2(0.5, 0.5), level(0.0)).r; o[4] = u.sample(smax, float2(0.5, 0.5), level(0.0)).r; }\n" options:nil error:&e];
        id<MTLComputePipelineState> ps = lib ? [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e] : nil;
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:4 height:4 mipmapped:YES]; td.storageMode = MTLStorageModeShared;
        MTLTextureDescriptor *ud = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:2 height:1 mipmapped:NO]; ud.storageMode = MTLStorageModeShared;
        id<MTLTexture> t = [dev newTextureWithDescriptor:td], u = [dev newTextureWithDescriptor:ud];
        float z[16] = {0}, one[4] = {1, 1, 1, 1}, two[2] = {0, 1};
        MTLSamplerDescriptor *b = [MTLSamplerDescriptor new]; b.minFilter = b.magFilter = MTLSamplerMinMagFilterNearest; b.mipFilter = MTLSamplerMipFilterNearest;
#ifndef DM_TEST_SAMP_PROBE_NO_BIAS   // negative control of t_samp_probe only: the probe's sampler without its bias
        b.lodBias = 1.0f;
#endif
        MTLSamplerDescriptor *c = [MTLSamplerDescriptor new]; c.minFilter = c.magFilter = MTLSamplerMinMagFilterNearest; c.mipFilter = MTLSamplerMipFilterNearest;
        MTLSamplerDescriptor *mn = [MTLSamplerDescriptor new]; mn.minFilter = mn.magFilter = MTLSamplerMinMagFilterLinear; mn.reductionMode = MTLSamplerReductionModeMinimum;
        MTLSamplerDescriptor *mx = [MTLSamplerDescriptor new]; mx.minFilter = mx.magFilter = MTLSamplerMinMagFilterLinear; mx.reductionMode = MTLSamplerReductionModeMaximum;
        id<MTLSamplerState> sb = [dev newSamplerStateWithDescriptor:b], s0 = [dev newSamplerStateWithDescriptor:c], smin = [dev newSamplerStateWithDescriptor:mn], smax = [dev newSamplerStateWithDescriptor:mx];
        id<MTLBuffer> ob = [dev newBufferWithLength:5 * sizeof(float) options:MTLResourceStorageModeShared]; id<MTLCommandQueue> q = [dev newCommandQueue];
        if (!ps || !t || !u || !sb || !s0 || !smin || !smax || !ob || !q) return 1;
        [t replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:z bytesPerRow:16]; [t replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:1 withBytes:one bytesPerRow:8];
        [u replaceRegion:MTLRegionMake2D(0, 0, 2, 1) mipmapLevel:0 withBytes:two bytesPerRow:8];
        id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> en = [cb computeCommandEncoder];
        [en setComputePipelineState:ps]; [en setTexture:t atIndex:0]; [en setTexture:u atIndex:1];
        [en setSamplerState:sb atIndex:0]; [en setSamplerState:s0 atIndex:1]; [en setSamplerState:smin atIndex:2]; [en setSamplerState:smax atIndex:3]; [en setBuffer:ob offset:0 atIndex:0];
        [en dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
        if (cb.error) return 2;
        const float *r = (const float *)ob.contents;
        out[0] = r[0] == 1.0f; out[1] = r[3] < 0.25f; out[2] = r[4] > 0.75f; out[3] = r[1] == 1.0f; out[4] = r[2] == 1.0f;
        return 0;
    }
    return -1;
} }
