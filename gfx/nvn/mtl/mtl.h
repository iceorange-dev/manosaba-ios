// gfx/nvn/mtl/mtl.h: the Metal backend's private header (plan item 264): the includes, the Metal 4 / Metal 3 macros and the declarations its
// translation units share. nvn_mtl.m is one translation unit with the parts in mtl/*.inc; the parts in mtl/*.m are translation units of their
// own, each ending with `#pragma clang attribute pop` for the push at the end of this header
#pragma once
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CAMetalDisplayLink.h>
#include <TargetConditionals.h>
#if TARGET_OS_SIMULATOR   // the iOS Simulator SDK has no MetalFX (Xcode 27; plan item 114): no scaler is ever made, the callers keep their own paths
#define DM_METALFX 0
#define FX_SCALER id
#else
#define DM_METALFX 1
#define FX_SCALER id<MTLFXSpatialScalerBase>
#import <MetalFX/MetalFX.h>
#endif
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <dlfcn.h>
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <setjmp.h>
#include "../nvn_mtl.h"
// Compile-time switch DM_NVN_M3=1: the graphics backend uses the older API (ordinary command queue / command buffers / render, compute and copy encoders, buffers bound per slot, each resource referenced by an argument block declared individually),
// target macOS 15 / iOS 18. Default 0 = Metal 4 (macOS 26 / iOS 26). Both branches share the same replay logic, and every divergence is inside #if DM_NVN_M3
#ifndef DM_NVN_M3
#define DM_NVN_M3 0
#endif
#if DM_NVN_M3
#define RENC id<MTLRenderCommandEncoder>
#define CENC id<MTLComputeCommandEncoder>
#define BENC id<MTLBlitCommandEncoder>
#define CBUF id<MTLCommandBuffer>
#define RPD MTLRenderPassDescriptor
#define FB_ON(o, cb) cb
#define M4_ONLY(...)
#define M3_PRESENT(cb, dr) [cb presentDrawable:dr]
#define FB_SEL addCompletedHandler
#define FB_ARG id<MTLCommandBuffer>
#else
#define RENC id<MTL4RenderCommandEncoder>
#define CENC id<MTL4ComputeCommandEncoder>
#define BENC id<MTL4ComputeCommandEncoder>
#define CBUF id<MTL4CommandBuffer>
#define RPD MTL4RenderPassDescriptor
#define FB_ON(o, cb) o
#define M4_ONLY(...) __VA_ARGS__
#define M3_PRESENT(cb, dr) [dr present]
#define FB_SEL addFeedbackHandler
#define FB_ARG id<MTL4CommitFeedback>
#endif
#include "../nvn_obj.h"
#include "../../tex/nvn_fmt_mtl.h"
#include "../../tex/nvn_deswizzle.h"
#include "../../tex/nvn_deswizzle_gpu.h"
#include "../../tex/nvn_bcdec.h"
#include "../../tex/nvn_bc1astc.h"
#include "../nvn_trace.h"
#include "../nvn_dirty.h"
#include "../nvn_refl.h"
#include "../nvn_cover.h"
#include "../nvn_ts.h"
#include <mach/mach_time.h>
#include "../nvn_copy_pitch.h"
#include "../nvn_pool_find.h"
#include "../nvn_tex_fit.h"
#include "../nvn_depth_clear.h"
#include "../dmsh_chain.h"   // DMSH_CHAIN: the translation chain version of the B shader library expected by this source tree
#include "../nvn_hmap.h"
#include "../nvn_pace.h"
#include "../core/core.h"   // the replay's decisions in plain C (plan item 264): MLOG / MDIE / RED_ONCE, and the modules below
#include "../core/rt.h"
#include "../core/prog.h"
#include "../core/store_index.h"
#include "../core/usnap.h"
#include "../core/tbdr.h"
#include "../core/pend.h"
#include "../core/frame.h"
#include "../core/live.h"
#include "../core/capture.h"
#include "../core/guard.h"
#include "../nvn_quad_strip.h"
#include "../core/upd.h"
#include "../core/state.h"
#include "../core/pass.h"
#include "../core/defer.h"
#include "../core/fuse.h"
#include "../core/exec.h"
#include "../core/cond.h"
#include "../nvn_backend.h"
#include "../../../host/dm.h"
#import <CommonCrypto/CommonDigest.h>
#include <stdarg.h>
#include <mach-o/dyld.h>
#include <libgen.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <errno.h>
#include <execinfo.h>
#include <os/lock.h>
// Function parameters in this file are never retained again in the callee (the caller / global tables already hold them, and lifetime is guaranteed by defer_obj_release): saves a retain / release pair per call on the hot path
#pragma clang attribute push(__attribute__((objc_externally_retained)), apply_to = function)

// ---- Shared between the backend's translation units: defined once, declared here ----
extern id<MTLDevice> g_dev;                          // nvn_mtl.m
// GPU keep-alive (mtl/keepalive.m)
extern _Atomic int g_inflight;                       // the game's command buffers committed and not completed
extern dispatch_semaphore_t g_ka_idle;               // signalled when g_inflight drops to 0
extern _Atomic uint64_t g_last_sub_ns, g_ka_n, g_ka_us;   // the last submission's time; spin kernels run and their GPU microseconds
void keepalive_start(void);                          // on the first submission (when on): the spinning thread
// Textures (mtl/textures.inc): base and sRGB formats a view may switch between (present_pipe.m reads it too)
typedef struct { MTLPixelFormat a, b; } pfv_pair;
static const pfv_pair g_pfv_srgb[] = {
    {MTLPixelFormatRGBA8Unorm, MTLPixelFormatRGBA8Unorm_sRGB},
    {MTLPixelFormatBGRA8Unorm, MTLPixelFormatBGRA8Unorm_sRGB},
    {MTLPixelFormatBC1_RGBA, MTLPixelFormatBC1_RGBA_sRGB},
    {MTLPixelFormatBC2_RGBA, MTLPixelFormatBC2_RGBA_sRGB},
    {MTLPixelFormatBC3_RGBA, MTLPixelFormatBC3_RGBA_sRGB},
    {MTLPixelFormatBC7_RGBAUnorm, MTLPixelFormatBC7_RGBAUnorm_sRGB},
};
// Commits (mtl/encoders.inc): blocks run at the submission's commit
extern NSMutableArray<dispatch_block_t> *g_commit_blocks;
void lay_report(uint64_t f);                         // layered passes, every 300 frames (mtl/u8_layers.inc)
void pass_dims(RPD *pd, NSUInteger nc, NSUInteger *w, NSUInteger *h);   // a pass descriptor's target size (mtl/passes.inc)
extern id<MTLRenderPipelineState> g_blit_pso; extern MTLPixelFormat g_blit_fmt; extern id<MTLSamplerState> g_blit_samp;   // the present blit (mtl/shot.inc)
// Frame timing and DM_NVN_GPUWATCH (mtl/frame_timing.m)
extern long g_gw_a;                                  // DM_NVN_GPUWATCH's first frame (-1: off, -2: not read yet)
void gw_note_(const p_bind_buf *b, int ssbo);
static inline __attribute__((always_inline)) void gw_note(const p_bind_buf *b, int ssbo) { if (g_gw_a != -1) gw_note_(b, ssbo); }   // a binding the GPU reads (off: one compare a binding)
void gw_commit(void);                                // at the commit: the watched ranges copied, compared once the GPU completes
// Pass GPU times, DM_NVN_PASSGPU (mtl/passgpu.m)
void pg_render(RPD *pd, NSUInteger nc); void pg_render_begin(id e); void pg_render_end(id e); int pg_dispatch_begin(CENC ce); void pg_dispatch_end(CENC ce, int slot);
void pg_sub_begin(uint64_t fence); void pg_commit(id fb); MTLComputePassDescriptor *pg_compute_desc(void);   // a submission's start, its commit; a dispatch's own encoder (Metal 3)
extern uint64_t g_pgt_from;                          // DM_NVN_PASSGPU_FROM (t_passgpu sets it)
// The present pipeline (mtl/present_pipe.m)
extern _Atomic uint64_t g_present_stall_ns;          // unit tests: an extra pause before each present
extern CAMetalLayer *g_present_layer_resident;
void present_pipeline(MTLPixelFormat format); id<MTLTexture> present_src(id<MTLTexture> src); MTLViewport present_vp(double dw, double dh, double cw, double ch);
int present_encode(id<MTLCommandBuffer> cb, id<MTLTexture> src, id<MTLTexture> dst, const int crop[4]);
void mtl_present_stretch(int on);                    // the wide picture (plan item 154)
