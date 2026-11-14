// gfx/nvn/mtl/enc.h: the encoder layer for the Metal backend's translation units (plan item 264; included by mtl/mtl.h): the submission's command
// buffer and its copy encoder, the command buffers made outside the replay, the render pass's end, and the copy calls, which are the same on
// Metal 4 and Metal 3 (a Metal 4 copy encoder is a compute encoder: BENC). The render and compute wrappers, with the render state filter and the
// encode stage's packets, stay with the draw's path in mtl/encoders.inc
#pragma once
extern id g_cb;                     // the submission's command buffer (mtl/encoders.inc)
extern BENC g_blit;                 // its open copy encoder; nil when none is open
BENC blit(void);                    // the copy encoder, opened when none is: the render pass ended, logged uniform updates written first
id<MTLCommandBuffer> q_cb(void);    // a command buffer outside the replay (a readback, a diagnostic, a present): on the submit thread its uploads are flushed and waited for first
void rp_end(void);                  // the open render pass ended (mtl/renderpass.inc)
// The end of an encoder. Metal 4 (conservative barriers, first version): a producer barrier at the end (this encoder and earlier → later encoders);
// the older API has no explicit barriers: dependencies come from useResource declarations plus the system's automatic tracking
#if DM_NVN_M3
static inline void m4_end(id e) { [e endEncoding]; }
#else
static inline void m4_end(id e) {
    if ([e conformsToProtocol:@protocol(MTL4CommandEncoder)]) [(id<MTL4CommandEncoder>)e barrierAfterStages:MTLStageAll beforeQueueStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [e endEncoding];
}
#endif
// A copy encoder on a command buffer: Metal 4's own command buffers have a compute encoder for it, the others (q_cb) a blit encoder. On the
// submission's command buffer enc_b_begin (mtl/encoders.inc) brings the render state up to date first
#if DM_NVN_M3
static inline id enc_b_open(id cb) { return [cb blitCommandEncoder]; }
#else
static inline id enc_b_open(id cb) { return [cb respondsToSelector:@selector(beginCommandBufferWithAllocator:)] ? (id)[cb computeCommandEncoder] : (id)[cb blitCommandEncoder]; }
#endif
static inline void enc_b_end(id e) { m4_end(e); }
static inline void enc_b_copy_buffer(id e, id<MTLBuffer> s, NSUInteger so, id<MTLBuffer> d, NSUInteger doff, NSUInteger n) { [e copyFromBuffer:s sourceOffset:so toBuffer:d destinationOffset:doff size:n]; }
static inline void enc_b_copy_buffer_texture(id e, id<MTLBuffer> s, NSUInteger so, NSUInteger row, NSUInteger img, MTLSize z, id<MTLTexture> d, NSUInteger sl, NSUInteger lv, MTLOrigin o) { [e copyFromBuffer:s sourceOffset:so sourceBytesPerRow:row sourceBytesPerImage:img sourceSize:z toTexture:d destinationSlice:sl destinationLevel:lv destinationOrigin:o]; }
static inline void enc_b_copy_texture_buffer(id e, id<MTLTexture> s, NSUInteger sl, NSUInteger lv, MTLOrigin o, MTLSize z, id<MTLBuffer> d, NSUInteger doff, NSUInteger row, NSUInteger img) { [e copyFromTexture:s sourceSlice:sl sourceLevel:lv sourceOrigin:o sourceSize:z toBuffer:d destinationOffset:doff destinationBytesPerRow:row destinationBytesPerImage:img]; }
static inline void enc_b_copy_texture(id e, id<MTLTexture> s, NSUInteger sl, NSUInteger lv, MTLOrigin so, MTLSize z, id<MTLTexture> d, NSUInteger dl, NSUInteger dv, MTLOrigin doo) { [e copyFromTexture:s sourceSlice:sl sourceLevel:lv sourceOrigin:so sourceSize:z toTexture:d destinationSlice:dl destinationLevel:dv destinationOrigin:doo]; }
