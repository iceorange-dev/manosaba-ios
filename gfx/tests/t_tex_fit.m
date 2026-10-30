// Unit tests for the compatible texture views of tex_fit (nvn_tex_fit.h): which pairs get a view and how many slices, worked out by hand; then on
// the GPU, a kernel that declares the shader's type reads the view and must see the right slice: 2D array (3 layers) as 2D reads layer 0, 2D as
// a 2D array reads it, 2D array (8 layers) as a cube reads face 3 = layer 3, 2D array (13 layers) as a cube array reads cube 1 face 2 = layer 8,
// and a 2D array view with a swizzle (as tex_by_id builds them) keeps the swizzle through the compatible view; a view of a view is made from the
// texture under it, at its slice (-DDM_TEST_FIT_FROM_VIEW, from the view as before: 1 fails). Run with MTL_DEBUG_LAYER=1.
#import "../nvn/nvn_tex_fit.h"
#include <stdio.h>

static int fail, total;
static void t(const char *name, int ok) { total++; fail += !ok; printf("%-78s %s\n", name, ok ? "✓" : "✗"); }
static id<MTLDevice> dev; static id<MTLCommandQueue> q; static id<MTLLibrary> lib;

// 4×4 RGBA8 texture of the given type; slice s is filled with (s * 20 + 10, 100, 200, 255)
static id<MTLTexture> make(MTLTextureType type, NSUInteger slices) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.textureType = type; d.pixelFormat = MTLPixelFormatRGBA8Unorm; d.width = d.height = 4; d.arrayLength = slices; d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> x = [dev newTextureWithDescriptor:d];
    for (NSUInteger s = 0; s < slices; s++) {
        uint8_t px[4 * 4 * 4]; for (int i = 0; i < 16; i++) { px[4 * i] = (uint8_t)(s * 20 + 10); px[4 * i + 1] = 100; px[4 * i + 2] = 200; px[4 * i + 3] = 255; }
        [x replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 slice:s withBytes:px bytesPerRow:16 bytesPerImage:64];
    }
    return x;
}
// Runs kernel k on texture x, returns the four channels it read, in 0–255
static void run(NSString *k, id<MTLTexture> x, int out[4]) {
    id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:k] error:nil];
    id<MTLBuffer> b = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:ps]; [e setTexture:x atIndex:0]; [e setBuffer:b offset:0 atIndex:0];
    [e dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)]; [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
    const float *f = b.contents; for (int i = 0; i < 4; i++) out[i] = (int)(f[i] * 255.0f + 0.5f);
}
int main(void) { @autoreleasepool {
    t("2D array → 2D: one slice", nvn_tex_fit_slices(MTLTextureType2D, MTLTextureType2DArray, 3) == 1);
    t("2D → 2D array: one slice", nvn_tex_fit_slices(MTLTextureType2DArray, MTLTextureType2D, 1) == 1);
    t("2D array of 8 → cube: six slices", nvn_tex_fit_slices(MTLTextureTypeCube, MTLTextureType2DArray, 8) == 6);
    t("2D array of 13 → cube array: twelve slices (two whole cubes)", nvn_tex_fit_slices(MTLTextureTypeCubeArray, MTLTextureType2DArray, 13) == 12);
    t("neg: 2D array of 5 → cube (fewer than six)", nvn_tex_fit_slices(MTLTextureTypeCube, MTLTextureType2DArray, 5) == 0);
    t("neg: cube → 2D (not a supported pair)", nvn_tex_fit_slices(MTLTextureType2D, MTLTextureTypeCube, 6) == 0);
    t("neg: 2D → 2D multisample", nvn_tex_fit_slices(MTLTextureType2DMultisample, MTLTextureType2D, 1) == 0);

    dev = MTLCreateSystemDefaultDevice(); q = [dev newCommandQueue];
    NSError *err = nil;
    lib = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void k2d(texture2d<float> t [[texture(0)]], device float4 *o [[buffer(0)]]) { o[0] = t.read(uint2(1, 1)); }\n"
        "kernel void k2da(texture2d_array<float> t [[texture(0)]], device float4 *o [[buffer(0)]]) { o[0] = t.read(uint2(1, 1), 0); }\n"
        "kernel void kcube(texturecube<float> t [[texture(0)]], device float4 *o [[buffer(0)]]) { o[0] = t.read(uint2(1, 1), 3); }\n"
        "kernel void kcubea(texturecube_array<float> t [[texture(0)]], device float4 *o [[buffer(0)]]) { o[0] = t.read(uint2(1, 1), 2, 1); }\n"
        options:nil error:&err];
    if (!lib) { printf("kernel compile failed: %s\n", err.localizedDescription.UTF8String); return 1; }
    int v[4];
    id<MTLTexture> a3 = make(MTLTextureType2DArray, 3), v1 = nvn_tex_fit_view(a3, MTLTextureType2D, 1);
    run(@"k2d", v1, v); t("GPU: 2D array as 2D reads layer 0 (10)", v1 && v[0] == 10 && v[1] == 100);
    id<MTLTexture> t2 = make(MTLTextureType2D, 1), v2 = nvn_tex_fit_view(t2, MTLTextureType2DArray, 1);
    run(@"k2da", v2, v); t("GPU: 2D as a 2D array reads its one slice (10)", v2 && v[0] == 10);
    id<MTLTexture> a8 = make(MTLTextureType2DArray, 8), v3 = nvn_tex_fit_view(a8, MTLTextureTypeCube, nvn_tex_fit_slices(MTLTextureTypeCube, MTLTextureType2DArray, 8));
    run(@"kcube", v3, v); t("GPU: 2D array of 8 as a cube, face 3 = layer 3 (70)", v3 && v[0] == 70);
    id<MTLTexture> a13 = make(MTLTextureType2DArray, 13), v4 = nvn_tex_fit_view(a13, MTLTextureTypeCubeArray, nvn_tex_fit_slices(MTLTextureTypeCubeArray, MTLTextureType2DArray, 13));
    run(@"kcubea", v4, v); t("GPU: 2D array of 13 as a cube array, cube 1 face 2 = layer 8 (170)", v4 && v[0] == 170);
    // A swizzled view (blue and red swapped), then the compatible 2D view of it: the swizzle must survive
    id<MTLTexture> sw = [a3 newTextureViewWithPixelFormat:a3.pixelFormat textureType:MTLTextureType2DArray levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 3)
                                                 swizzle:MTLTextureSwizzleChannelsMake(MTLTextureSwizzleBlue, MTLTextureSwizzleGreen, MTLTextureSwizzleRed, MTLTextureSwizzleAlpha)];
    id<MTLTexture> v5 = nvn_tex_fit_view(sw, MTLTextureType2D, 1);
    run(@"k2d", v5, v); t("GPU: a swizzled 2D array as 2D keeps the swizzle (r = 200, b = 10)", v5 && v[0] == 200 && v[2] == 10);
    // A view of slices 1-2, then the compatible 2D view of it: made from the texture under it at slice 1 (plan item 296)
    id<MTLTexture> sub = [a3 newTextureViewWithPixelFormat:a3.pixelFormat textureType:MTLTextureType2DArray levels:NSMakeRange(0, 1) slices:NSMakeRange(1, 2)];
    id<MTLTexture> v6 = nvn_tex_fit_view(sub, MTLTextureType2D, 1);
    run(@"k2d", v6, v); t("GPU: a view of slices 1-2 as 2D reads slice 1 (30)", v6 && v[0] == 30);
    t("... the view made from the texture under it, at slice 1 (not a view of a view)", v6.parentTexture == a3 && v6.parentRelativeSlice == 1);
    printf("== %d checks, %d failed ==\n", total, fail);
    return fail != 0;
} }
