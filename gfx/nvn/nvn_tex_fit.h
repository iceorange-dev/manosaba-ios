// The compatible view tex_fit (nvn_mtl.m) binds when a shader declares another texture type than the bound texture has (unit test
// tests/t_tex_fit.m): the number of slices of a view of type want over a texture of type have with `layers` array slices, or 0 when there is
// none. Supported, as in nxnative 42a50ef: 2D array ↔ 2D (one slice: a non-array Maxwell sample of a 2D array reads layer 0), 2D array → cube
// (the first six slices) and → cube array (whole cubes). Objective-C only (MTLTextureType).
#pragma once
#import <Metal/Metal.h>

static inline NSUInteger nvn_tex_fit_slices(MTLTextureType want, MTLTextureType have, NSUInteger layers) {
    if (want == MTLTextureType2D && have == MTLTextureType2DArray) return 1;
    if (want == MTLTextureType2DArray && have == MTLTextureType2D) return 1;
    if (want == MTLTextureTypeCube && have == MTLTextureType2DArray && layers >= 6) return 6;
    if (want == MTLTextureTypeCubeArray && have == MTLTextureType2DArray && layers >= 6) return layers / 6 * 6;
    return 0;
}
// The view itself: the first ns slices of t, all its levels, its format and swizzle, as type want. Made from the texture under t when t is a view
// (the view cache's), its levels and slices offset by t's place there: Metal's shader validation reported a view of a view as not resident
// (seedds/nxnative d9c2d5e, MIT: a 64x64 cube read through a 2D-array view; plan item 296)
static inline id<MTLTexture> nvn_tex_fit_view(id<MTLTexture> t, MTLTextureType want, NSUInteger ns) {
    id<MTLTexture> root = t; NSUInteger l0 = 0, s0 = 0;
#ifndef DM_TEST_FIT_FROM_VIEW   // negative control of t_tex_fit only: made from t, as before
    while (root.parentTexture) { l0 += root.parentRelativeLevel; s0 += root.parentRelativeSlice; root = root.parentTexture; }
#endif
    return [root newTextureViewWithPixelFormat:t.pixelFormat textureType:want levels:NSMakeRange(l0, t.mipmapLevelCount) slices:NSMakeRange(s0, ns) swizzle:t.swizzle];
}
