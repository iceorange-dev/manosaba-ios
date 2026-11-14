// NVN host-side object layout and command record format: shared by hle_nvn.c (interface layer, writes) and nvn_mtl.m (Metal replay layer, reads).
// Objects are stored in place in the NVN object memory allocated by the game, with size not exceeding the reserved size in nvn.h (FITS static assertion).
// State objects (Blend / Color / ... 4–0x18 bytes) are small structs the game copies by value; their layout is packed in place with bit-fields.
#pragma once
#include <stdint.h>
#include <string.h>
#include "nvn.h"
#include "nvn_tex.h"

#define MAGIC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define FITS(T, H) _Static_assert(sizeof(T) <= sizeof(H), #T " exceeds the reserved size of " #H)
// nvn.h's sizes come from an older NVN and are not upper bounds for this SDK (NVNtexture is 0x80, NVNtextureView at most 0x10, both smaller than
// nvn.h). Upper bounds read from manosaba 2.0.4b's Unity code — the room it leaves for each object: the next stack local or struct field after it,
// or its allocation size — which every host struct here stays within:
//   TextureView 0x10, Texture 0x80, TextureBuilder 0x80, Buffer 0x30, BufferBuilder 0x40, MemoryPool 0x100, MemoryPoolBuilder 0x40,
//   TexturePool / SamplerPool 0x20, Sampler 0x40, SamplerBuilder 0x40, Program 0x1a0, CommandBuffer 0xa0, Sync 0x48, Queue 0x2000,
//   QueueBuilder 0x80, Window 0x180, WindowBuilder 0x40, DeviceBuilder 0x40, BlendState 8, ColorState 4, ChannelMaskState 4,
//   MultisampleState 0x1c, PolygonState 4, VertexAttribState 4, VertexStreamState 0x10.
// A host struct must not grow past these: the game keeps other data right after the object.

typedef struct { uint32_t flags; } dev_builder_t;                       FITS(dev_builder_t, NVNdeviceBuilder);
typedef struct { uint32_t magic, flags, origin, depth_mode; void *dbg_cb, *dbg_data; } dev_t_;  FITS(dev_t_, NVNdevice);

typedef struct { NVNdevice *dev; uint32_t flags; uint64_t cmd_sz, comp_sz, ctl_sz, flush; void *mem; uint64_t mem_sz; } queue_builder_t;
FITS(queue_builder_t, NVNqueueBuilder);
typedef struct { uint32_t magic; NVNdevice *dev; uint64_t submits; } queue_t;                 FITS(queue_t, NVNqueue);

typedef struct { NVNdevice *dev; void *mem; uint64_t size; uint32_t flags; } pool_builder_t;  FITS(pool_builder_t, NVNmemoryPoolBuilder);
typedef struct { uint32_t magic, flags; uint8_t *mem; uint64_t size; } pool_t;                FITS(pool_t, NVNmemoryPool);

// Buffer: GPU address = memory of its pool + offset (pool memory is the CPU address); serial serves as the debug ID
typedef struct { NVNdevice *dev; pool_t *pool; int64_t off; uint64_t size; } buf_builder_t;  FITS(buf_builder_t, NVNbufferBuilder);
typedef struct { uint32_t magic; pool_t *pool; int64_t off; uint64_t size; uint8_t *mem; uint64_t serial; } buf_t;  FITS(buf_t, NVNbuffer);

typedef struct { const pool_t *pool; int64_t off; int32_t n, base; } dpool_t;                 // Texture / sampler descriptor pool; base: its IDs' start in the replay's tables (plan item 64e)
FITS(dpool_t, NVNtexturePool); FITS(dpool_t, NVNsamplerPool);

typedef struct {
    NVNdevice *dev; nvn_texdesc d; pool_t *pool; int64_t off;
    uint8_t has_layout, layout[8];
} tex_builder_t;                                                                             FITS(tex_builder_t, NVNtextureBuilder);
// The texture object must fit in 0x80 bytes: this SDK's NVNtexture is really 0x80 (Unity's swapchain texture array has a 0x80 stride; Unity copies texture objects by value, only 0x80 bytes,
// followed directly by its own fields); the 0xC0 in nvn.h (DWARF reconstruction, NVN 53) is too large. The serial must lie within the first 0x80 bytes so a copied object carries the same serial (= the same texture).
// Of the layout only the two values the replay layer needs are stored (total storage / bytes per layer, u32 is enough); the rest is recomputed from d (nvn_tex_layout).
typedef struct {
    uint32_t magic; nvn_texdesc d; uint32_t size, layer_size; uint8_t has_layout, layout[8];
    uint8_t *mem; pool_t *pool; int64_t off;
    uint64_t serial;                     // Initialization serial: the replay layer uses it to find the MTLTexture in the host table (the game may copy the object by value, so object pointers cannot be stored)
} tex_t;                                                                                     FITS(tex_t, NVNtexture);
_Static_assert(sizeof(tex_t) <= 0x80, "tex_t exceeds this SDK's actual NVNtexture size 0x80");
// Two texture objects on the same storage (plan item 119; bayonetta3-mobile compares storage ranges): the same serial, or overlapping memory (a
// texture initialized over a live one's storage gets its own serial but the same texture, nvn_mtl.m g_stor_shared)
// A primitive that has a front and a back face (plan item 120): polygons only, as the SDK says of culling; points and lines are drawn whatever the
// cull face, FRONT_AND_BACK included (both draw paths used to drop every primitive under it). Patches are taken as triangles (their domain is
// not known here). Checked by gfx/tests/t_cull_prim.c
static inline int nvn_prim_has_face(int prim) {
#ifndef DM_TEST_CULL_ALL_PRIMS   // negative control of t_cull_prim only: every primitive, as before
    return (prim >= NVN_DRAW_PRIMITIVE_TRIANGLES && prim <= NVN_DRAW_PRIMITIVE_POLYGON) || prim == NVN_DRAW_PRIMITIVE_TRIANGLES_ADJACENCY ||
           prim == NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP_ADJACENCY || prim == NVN_DRAW_PRIMITIVE_PATCHES;
#else
    (void)prim; return 1;
#endif
}
// The size of a render pass without attachments (plan item 121; bayonetta3-mobile test_attachmentless.m: a 4x3 viewport gives 12 fragments): the
// scissor's extent when one is set (it bounds every fragment; Tears of the Kingdom's case, a 1x1 scissor), else the viewport's (it was 1x1 then),
// at least 1x1, at most 16384. Checked by gfx/tests/t_attless.c
static inline void nvn_attless_size(int has_sc, int sx, int sy, int sw, int sh, const float vp[4], unsigned *w, unsigned *h) {
    long x = 1, y = 1;
    if (has_sc) { x = (long)sx + sw; y = (long)sy + sh; }
#ifndef DM_TEST_ATTLESS_SCISSOR_ONLY   // negative control of t_attless only: 1x1 without a scissor, as before
    else if (vp && vp[2] > 0 && vp[3] > 0) { x = (long)__builtin_ceilf(vp[0] + vp[2]); y = (long)__builtin_ceilf(vp[1] + vp[3]); }
#endif
    *w = (unsigned)(x < 1 ? 1 : x > 16384 ? 16384 : x); *h = (unsigned)(y < 1 ? 1 : y > 16384 ? 16384 : y);
}
static inline int tex_same_storage(const tex_t *a, const tex_t *b) {
    return a->serial == b->serial || (a->mem && b->mem && a->mem < b->mem + b->size && b->mem < a->mem + a->size);
}
// NVNtextureView is at most 0x10 bytes in this SDK: Unity keeps two views 0x10 apart on its stack (main+0x9c35ec / +0x9c361c), and a view right before a
// staging NVNbuffer (main+0x9c7f40: view at sp+0x20, buffer at sp+0x30). The earlier 0x1c-byte view_t overran the next local on every SetDefaults
// (that NVNbuffer lost its memory pool pointer's low half; Unity then crashed releasing the upload memory of the first video frame).
// Packed to 10 bytes (the size proven safe in both games): levels and targets fit 4 bits (at most 15 levels, targets 0..10), layers and formats 16 bits,
// the swizzle 4 × 3 bits (NVNtextureSwizzle 0..5) in what is left.
typedef struct {
    uint8_t set_levels : 1, set_layers : 1, set_format : 1, set_target : 1, set_swz : 1, swz_a : 3;
    uint8_t base_level : 4, nlevels : 4;
    uint8_t target : 4, swz_r : 3;
    uint8_t swz_g : 3, swz_b : 3;
    uint16_t min_layer, nlayers, format;
} view_t;
_Static_assert(sizeof(view_t) == 10, "view_t must stay 10 bytes (see above)");
FITS(view_t, NVNtextureView);

// Sampler parameters (one copy shared by builder and object)
typedef struct { uint8_t minf, magf, ws, wt, wr, cmp_mode, cmp_func, reduction; float lod_min, lod_max, lod_bias, aniso, border[4]; } samp_par;   // Enum values are all < 256
typedef struct { NVNdevice *dev; samp_par p; } samp_builder_t;                                FITS(samp_builder_t, NVNsamplerBuilder);
typedef struct { uint32_t magic; samp_par p; } samp_t;                                        FITS(samp_t, NVNsampler);

// key[i]: first 8 bytes of the sha256 of stage i's control section, computed at SetShaders -- NVN only requires the control section to be valid during the SetShaders call; Unity frees / reuses that memory right after,
// so reading it later at draw time in the replay layer reads other data (D2R's control sections stay in its shader package, which is why this used to happen to work)
// (Nothing more fits here: a game may allocate NVNprogram smaller than the header's 0xC0, lordran-native 94a6307 saw it in Mario, and a larger prog_t
// writes into the next object. The functional keys of plan item 169 live in a side table by serial: nvn_mtl.m mtl_prog_fkeys)
typedef struct { uint32_t magic, nstage; uint64_t serial; uint8_t key[6][8]; uint8_t kstage[6]; } prog_t;     FITS(prog_t, NVNprogram);   // serial: table key for the replay layer's resolved result
void mtl_prog_fkeys(uint64_t serial, const uint8_t fk[6][8]);   // nvn_mtl.m: a program's functional keys (all zero: none), until mtl_prog_release

typedef struct { uint32_t magic, fenced; uint64_t fence; } sync_t;                            FITS(sync_t, NVNsync);
// A sync's fence is published value first, then the flag (release); a waiter on another thread reads the flag (acquire), then the value. Flag first,
// a waiter between the two stores saw a fenced sync with fence 0 and reported ALREADY_SIGNALED early (GirlNet f2faa5f; unit test tests/t_sync_order.c)
static inline void sync_fence_store(sync_t *s, uint64_t fence) {
#ifndef DM_TEST_SYNC_FLAG_FIRST   // negative control of t_sync_order only: the old order
    __atomic_store_n(&s->fence, fence, __ATOMIC_RELAXED); __atomic_store_n(&s->fenced, 1, __ATOMIC_RELEASE);
#else
    s->fenced = 1; __atomic_signal_fence(__ATOMIC_SEQ_CST); s->fence = fence;
#endif
}
static inline int sync_fence_load(const sync_t *s, uint64_t *fence) {   // 0: never fenced
    if (!__atomic_load_n(&s->fenced, __ATOMIC_ACQUIRE)) return 0;
    *fence = __atomic_load_n(&s->fence, __ATOMIC_RELAXED); return 1;
}
// With a translation thread (plan item 61, DM_NVN_XLAT=1) a sync can be fenced to a submission still in its queue: the fence is then that submission's
// number with SYNC_TOKEN set (one 64-bit value, so a waiter never pairs a token's flag with a Metal fence stored meanwhile by the replay); a wait
// first waits for the submission's translation, then for the Metal fence it ended with (hle_nvn.c x_token_fence). Metal fences never reach bit 63
#define SYNC_TOKEN (1ull << 63)
static inline void sync_token_store(sync_t *s, uint64_t sub) { sync_fence_store(s, sub | SYNC_TOKEN); }
// The submit call's view of a submission for the translation thread (gfx/nvn/core/capture.c): taken on the game's thread, installed by the replay, freed after its commit
typedef struct mtl_cap mtl_cap;
mtl_cap *mtl_sub_capture(const uint64_t *h, int n, uint64_t token);
void mtl_sub_install(mtl_cap *c);
void mtl_cap_free(mtl_cap *c);
typedef struct { const pool_t *pool; int64_t off; } ev_builder_t;                             FITS(ev_builder_t, NVNeventBuilder);
typedef struct { uint32_t magic; volatile uint32_t *val; } ev_t;                              FITS(ev_t, NVNevent);

typedef struct { NVNdevice *dev; void *native; int32_t ntex, interval; NVNtexture *tex[4]; int32_t nactive; } win_builder_t;   // nactive: how many leading textures are cycled through (0 = all)
FITS(win_builder_t, NVNwindowBuilder);
// avail[i]: time at which swapchain image i can be acquired again (ns, monotonic clock) = its display time + one present period; pace: target time of this window's present pacer
typedef struct { uint32_t magic; void *native; int32_t ntex, interval, next, crop[4]; NVNtexture *tex[4]; uint64_t presents, avail[4], pace, ticket[4]; int32_t nactive; uint64_t rfence[4]; } win_t;   // ticket[i]: ticket of the most recent present of image i; rfence[i]: the newest submission's fence then (the frame drawn into it)
FITS(win_t, NVNwindow);

// Command buffer: commands are written into the command memory given by the game (AddCommandMemory), handle = segment start address; when it runs out, the game is called back for more memory
typedef struct {
    uint32_t magic, recording;
    uint8_t *cur;                        // Write pointer of the current command memory segment
    uint64_t guest_flags;                // +0x10 belongs to the game: Tears of the Kingdom's NVN 55 inline wrappers (main 0xba70 and 664 more) store a byte here before every call,
                                         // also with their inline path off (it is while the SDK's nvnPrivate0, a .bss word, stays 0, as here). It was end's low byte: segments ended up to 255 bytes early
    uint8_t *end;                        // End of the current command memory segment
    uint8_t *begin;                      // BEGIN record of this recording
    PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC cb; void *cb_data;
    NVNdevice *dev;
    uint64_t used;                       // Record bytes written since BeginRecording, over all segments, and the JUMPs linking them (GetCommandMemoryUsed)
    uint8_t cb_off;                      // The game turned the out-of-command-memory callback off (SetCommandMemoryCallbackEnabled)
} cmd_t;                                                                                     FITS(cmd_t, NVNcommandBuffer);
_Static_assert(__builtin_offsetof(cmd_t, guest_flags) == 0x10, "the game writes cmd_t + 0x10");

// ---------- State objects (defaults follow the NVN / GL defaults) ----------
typedef struct { uint32_t blend_en : 8, logic_op : 4, alpha_func : 4; } color_st;            FITS(color_st, NVNcolorState);
typedef struct { uint32_t mask; } chmask_st;            /* 4 bits per target: r=1 g=2 b=4 a=8, bit set = write */ FITS(chmask_st, NVNchannelMaskState);
typedef struct { uint64_t target : 3, src_rgb : 7, dst_rgb : 7, src_a : 7, dst_a : 7, eq_rgb : 3, eq_a : 3; } blend_st;
FITS(blend_st, NVNblendState);
typedef struct { uint8_t enable, samples, a2c, a2c_dither, raster_samples, cov_mod, cov2color_en, cov2color_out, sl_en, sl_grid; } ms_st;
FITS(ms_st, NVNmultisampleState);
typedef struct { uint32_t cull : 2, front : 1, mode : 2, offset_en : 3; } poly_st;           FITS(poly_st, NVNpolygonState);
typedef struct {
    uint64_t dt : 1, dw : 1, df : 4, st : 1, sf_f : 4, sf_b : 4,
             fail_f : 4, zfail_f : 4, zpass_f : 4, fail_b : 4, zfail_b : 4, zpass_b : 4;
} ds_st;                                                                                     FITS(ds_st, NVNdepthStencilState);
typedef struct { uint32_t fmt : 8, offset : 19, stream : 5; } va_st;                         FITS(va_st, NVNvertexAttribState);
typedef struct { uint32_t stride, divisor; } vs_st;                                          FITS(vs_st, NVNvertexStreamState);

// ---------- Command records: {op, size (including header, aligned to 8)} + payload ----------
typedef struct { uint32_t op, size; } op_hdr;
enum {
    OP_END, OP_JUMP, OP_BEGIN, OP_CALL,
    OP_SIGNAL_EVENT, OP_REPORT_COUNTER, OP_COPY_B2B, OP_UPDATE_UB,                 // CPU-visible side effects
    OP_SET_RT, OP_CLEAR_COLOR, OP_CLEAR_DS, OP_VIEWPORTS, OP_SCISSOR, OP_DEPTH_RANGE,
    OP_BIND_PROGRAM, OP_BIND_STATE, OP_BIND_VATTR, OP_BIND_VSTREAM, OP_BIND_VBUF,
    OP_BIND_UBO, OP_BIND_SSBO, OP_BIND_TEX, OP_BIND_SAMP,
    OP_STENCIL, OP_POLY_OFFSET, OP_DRAW, OP_DRAW_IDX, OP_DISPATCH, OP_BARRIER,
    OP_SET_TEX_POOL, OP_SET_SAMP_POOL, OP_COPY_B2T, OP_COPY_STRIDE, OP_BLEND_COLOR, OP_COPY_T2B, OP_BIND_IMG, OP_COPY_T2T, OP_RESET_COUNTER, OP_CLEAR_TEX, OP_DEPTH_CLAMP, OP_COPY_IMAGE_STRIDE, OP_RENDER_ENABLE, OP_PRIM_RESTART, OP_FENCE_SYNC, OP_DEPTH_BOUNDS, OP_RASTER_DISCARD, OP_COUNT_
};
// Payload (immediately follows op_hdr)
typedef struct { int32_t n; const tex_t *color[8]; view_t cview[8]; uint8_t has_cview[8]; const tex_t *depth; view_t dview; uint8_t has_dview; } p_set_rt;
typedef struct { int32_t index, mask; float rgba[4]; } p_clear_color;
typedef struct { int32_t mode; uint64_t counters; } p_render_enable;   // mode 0 off, 1 on, 2 render if the two counters are equal, 3 if not equal (SetRenderEnable / Conditional)
typedef struct { float depth; int32_t depth_mask, stencil, stencil_mask; } p_clear_ds;
typedef struct { int32_t first, n; float v[16][6]; } p_viewports;          // x, y, w, h, near, far (near and far are overridden by DEPTH_RANGE)
typedef struct { int32_t x, y, w, h; } p_scissor;
typedef struct { float n, f; } p_depth_range;
typedef struct { const prog_t *prog; int32_t stages; } p_bind_program;
typedef struct { int32_t kind; uint8_t bytes[0x18]; } p_bind_state;      // kind: 0 blend 1 color 2 chmask 3 ms 4 poly 5 ds 6 alpha reference
typedef struct { int32_t first, n; uint32_t st[16]; } p_bind_vattr;
typedef struct { int32_t first, n; vs_st st[16]; } p_bind_vstream;
typedef struct { int32_t first, n; NVNbufferRange r[16]; } p_bind_vbuf;
typedef struct { int32_t stage, index; uint64_t addr, size; } p_bind_buf;  // UBO / SSBO
// defer / src: a deferred binding (BindTexturesDeferred) records where the handles are; replay reads them from there once a submission (nvn_mtl.m
// bh_fresh: defer becomes the submission's generation)
typedef struct { int32_t stage, first, n, defer; uint64_t src; uint64_t h[32]; } p_bind_handles; // Separate texture / sampler handles
typedef struct { int32_t face, value_mask, mask, ref, which; } p_stencil;  // which: 0 value mask 1 write mask 2 reference value
typedef struct { float factor, units, clamp; } p_poly_offset;
typedef struct { int32_t prim, first, count, instances, base_instance; } p_draw;   // instances 0 is read as 1
typedef struct { int32_t prim, itype, count, base_vertex, base_instance, instances; uint64_t ibuf; } p_draw_idx;
typedef struct { int32_t x, y, z; uint64_t caller; } p_dispatch;   // caller: return address of the game call site (to locate the dispatcher)
typedef struct { const dpool_t *pool; } p_set_pool;
typedef struct { uint64_t src; const tex_t *tex; view_t view; uint8_t has_view; NVNcopyRegion r; int32_t flags; } p_copy_b2t;
typedef struct { const tex_t *src, *dst; view_t sv, dv; uint8_t has_sv, has_dv; NVNcopyRegion sr, dr; int32_t flags; } p_copy_t2t;   // Reused for T2B: src stores the destination buffer address
typedef struct { const tex_t *tex; view_t view; uint8_t has_view, is_uint; NVNcopyRegion r; int32_t mask; union { float f[4]; uint32_t u[4]; } c; } p_clear_tex;
typedef struct { int64_t stride; } p_copy_stride;
typedef struct { float rgba[4]; } p_blend_color;
typedef struct { int32_t enable; } p_depth_clamp;
typedef struct { int32_t enable; float lo, hi; } p_depth_bounds;   // SetDepthBounds: stored depths outside [lo, hi] are discarded (plan item 64c)
typedef struct { int32_t enable; uint32_t index; } p_prim_restart;   // SetPrimitiveRestart   // 1 = depth clamp (fragments beyond the near / far planes are kept at the boundary depth, Metal DepthClipModeClamp)

// Entry point of the Metal replay layer (nvn_mtl.m): replays a set of command handles into the current submission
void nvn_exec(const uint8_t *handle);   // Replay a command list (gfx/nvn/core/exec.c)
void nvn_exec_rest(const uint64_t *handles, int n);   // Tells which handles in this submission still follow the current handle (used by the pass lookahead across handles)
// Present: draw the swapchain texture to the front-end CAMetalLayer (only counts when there is no window)
uint64_t mtl_present(const tex_t *t, double period_s, const int crop[4]);   // Enqueue a present and return the ticket immediately; period_s: minimum time this image is displayed (= present period, 0 = unlimited)
void mtl_present_wait(uint64_t ticket);                    // Wait until the present with this ticket has been processed (blit committed to the GPU queue)
void mtl_present_flush(void);                              // Wait until all enqueued presents have been processed
void mtl_present_test_stall(uint64_t ns);                  // Unit test: extra pause in ns before each present on the present thread
int mtl_display_hz(void);                                   // Maximum refresh rate of the screen (Hz) when there is a window, 0 without a window
void mtl_set_tex_pool_n(int n);                            // Texture descriptor pool capacity: texture IDs outside the pool are treated as 'empty slots' (read 0)
// Timing instrumentation (shared by Mac / iOS): submissions are tagged with the frame number; GPU time = sum of GPUEnd−GPUStart over all of that frame's command buffers, readable only after all complete (otherwise −1)
void mtl_set_frame(uint64_t f);
void mtl_ub_present(double iv_ms);   // Same-process alternating measurement (DM_NVN_UB): the present thread reports the actual present interval every frame
double mtl_gpu_ms(uint64_t f);       // the union of the frame's command-buffer spans
double mtl_gpu_sum_ms(uint64_t f);   // their plain sum (the GPU time reported before 2026-10-19)
void mtl_sub_index(const uint64_t *h, int n);   // Start of submission: build the store lookahead index in replay order (nvn_mtl.m)
void mtl_sub_snapshot(const uint64_t *h, int n);   // Start of submission: copy the uniform ranges it binds as they are now (nvn_mtl.m, DM_NVN_UBO_SNAPSHOT)
void mtl_bh_deferred_seen(void);   // A deferred binding was recorded: submissions read deferred handles at their start even without uniform snapshots (nvn_mtl.m)
void xv_test_counts(int *dc, int *viol);
int mtl_gpu_span(uint64_t f, uint64_t *t0, uint64_t *t1);   // The frame's earliest GPUStart / latest GPUEnd (mach absolute time, ns); returns 0 if not all complete or there is no GPU time
extern int g_gpu_span_host;          // those times are on the host's clock (now_ns): the Metal backend's are; the Vulkan backend's are on the GPU's own clock (core/frame.h)
void mtl_stats(uint64_t *passes, uint64_t *draws);
void mtl_args_stats(uint64_t *enc, uint64_t *reuse);
void mtl_use_stats(uint64_t *call, uint64_t *skip);
uint64_t mtl_ds_omit(void);   // Cumulative number of passes with no depth attachment because the lookahead proved depth-stencil unused
uint64_t mtl_drawable_ns_take(void);   // Take and zero: cumulative ns spent waiting for nextDrawable in present   // Cumulative useResource calls actually issued / skipped by same-encoder dedup   // Cumulative argument buffer new-encode / reuse counts
void mtl_bw(uint64_t *load, uint64_t *store, uint64_t *full_rw_passes);   // Cumulative attachment load / store bytes, cumulative number of full load-store passes
void mtl_clear_only_stats(uint64_t *n, uint64_t *bytes);   // Cumulative passes opened only to land clears and the bytes they stored (plan item 187)
uint64_t mtl_udc_saved_bytes(void);                                      // Write-back bytes actually saved by DM_NVN_UNWRITTEN_DC
int mtl_has_window(void);
int mtl_warm_wait(int timeout_ms);
int mtl_link_active(void);
int mtl_link_wait_tick(uint64_t timeout_ns);
double mtl_presented_time(uint64_t n);
   // Actual presentation time (seconds) of the n-th present (counted from 1); 0 = not ready, −1 = dropped
// Texture release (at Finalize)
void mtl_tex_release(tex_t *t);
void mtl_tex_live(const tex_t *t);   // nvnTextureInitialize: the texture now living at its storage (nvn_mtl.m)
const tex_t *mtl_tex_snap(const tex_t *t);   // Host copy of a texture object (by serial) for commands to record instead of the game's object
uint64_t mtl_tex_snap_count(void);
void mtl_pkg_stats(uint64_t *uploads, uint64_t *reuploads);
uint64_t mtl_pkg_miss(void);
uint64_t mtl_upload_ns_take(void);   // Take and zero: time spent uploading packaged textures on the submit thread since last time   // DM_NVN_DIRTYCHECK diagnostic: number of missed detections   // Packaged textures: number of first uploads, number of re-uploads due to pool content changes
uint64_t mtl_upload_gpu_ns_take(void);   // Take and zero: GPU execution time of upload command buffers (sum of GPUEndTime − GPUStartTime)
void mtl_create_stats(uint64_t *lib, uint64_t *pso);   // Cumulative: number of shader libraries loaded synchronously on the submit thread, number of render + compute pipelines created synchronously (those taken from warm-up do not count)
// CPU texture read: starting at level / layer0, written into dst with row / img byte strides
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *dst, size_t row, size_t img);
// CPU texture write: write from src (row / img byte strides) into the region starting at level / layer0
void mtl_write_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, const uint8_t *src, size_t row, size_t img);
void mtl_flush_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r);   // FlushTexels: a CPU write of the region's storage
// Descriptor pool registration (texture ID / sampler ID → object snapshot), program release
void mtl_reg_tex(int id, const tex_t *t, const view_t *v);
void mtl_reg_samp(int id, const samp_par *p);
void mtl_prog_release(prog_t *p);
// New serial (> 0, globally increasing)
uint64_t nvn_new_serial(void);
