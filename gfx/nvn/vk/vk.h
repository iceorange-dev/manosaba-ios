// gfx/nvn/vk/vk.h: the Vulkan backend's own header (plan item 301e), shared by its parts. The backend implements what gfx/nvn/null/null.c lists (the
// interface layer's and the replay core's set: nvn_backend.h, nvn_mtl.h, nvn_obj.h and the core headers' "The backend provides") on Vulkan 1.3 for
// the Android host, in the null backend's place:
//   vk_device.c   the instance, the device and its queue, the features and extensions taken (what the device has of what is wanted)
//   vk_pools.c    memory pools at the guest's own addresses (VK_EXT_map_memory_placed: the pool's pages become device memory, GPU address = CPU
//                 address as on Metal), the address lookup, the argument arena
//   vk_submit.c   submissions: a ring of command buffers, a timeline semaphore whose value is the NVN fence, the completion thread that lands
//                 event writes and counter reports after the GPU has finished, waits and drains
//   vk_tex.c      textures and samplers: NVN formats, images by serial, uploads of CPU-filled storage (block linear deswizzled on the CPU; again
//                 when the CPU writes it after, nvn_dirty.h), views, the texture and sampler pools
//   vk_ops.c      the execution interface's copies and clears
//   vk_shader.c   the game's shaders: the bundle gfx/tools/dmsh_vk.py writes (DM_VK_SHADERS), modules and their reflection (vk_shader.h)
//   vk_bind.c     what a program's shaders bind: uniform, storage and driver banks, textures, images, samplers, pushed or in a set (vk_bind.h)
//   vk_draw.c     draws and dispatches: the pass (dynamic rendering), pipelines, the dynamic state
//   vk_present.c  the swapchain on the app's window and the present of the game's window texture
//   vk_shot.c     screenshots of presented frames (DM_NVN_SHOT, the command channel's shot) as PNGs
//   vk_ptime.c    each pass's GPU time by its targets (DM_VK_PASS_GPU), for the stats line
#pragma once
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include "../nvn_backend.h"
#include "../nvn_mtl.h"
#include "../nvn_obj.h"
#include "../../../host/dm.h"

#define VLOG(...) LOG("[vk] " __VA_ARGS__)
#define VDIE(...) die("[vk] " __VA_ARGS__)
#define VRED(...) do { static int said_; if (!said_++) VLOG("[red] " __VA_ARGS__); } while (0)   // a red line once at this place
#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) VDIE("%s: %d", #x, (int)r_); } while (0)

typedef struct {
    VkInstance inst; VkPhysicalDevice pd; VkDevice dev; VkQueue q; uint32_t qf;
    VkPhysicalDeviceProperties props; VkPhysicalDeviceMemoryProperties mem;
    pthread_mutex_t qmu;                                   // the queue is the game's and the present's: one submit at a time
    // what was taken
    int placed, unmap_reserve, push_ds, custom_border, vi_dyn, eds2, depth_clip_ctl, robust2, bc, android_surface, idx8;
    uint32_t max_push_ds, ts_bits;                       // ts_bits: the queue family's valid timestamp bits (0: none)
    PFN_vkMapMemory2KHR map2; PFN_vkUnmapMemory2KHR unmap2; PFN_vkCmdPushDescriptorSetKHR push;
} vk_ctx;
extern vk_ctx V;
void vk_init(void);                                         // once, on the first use (any thread)
uint32_t vk_mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid);   // UINT32_MAX: none

// vk_pools.c
typedef struct { uintptr_t lo, hi; VkBuffer buf; VkDeviceMemory mem; int placed; } vk_pool;   // lo, hi first (nvn_pool_find.h)
int vk_addr(uint64_t addr, uint64_t len, VkBuffer *buf, VkDeviceSize *off);   // the pool buffer holding [addr, addr + len) and the offset there
int vk_addr_span(uint64_t addr, VkBuffer *buf, VkDeviceSize *off, uint64_t *left);   // ... and the bytes from addr to its pool's end

// vk_submit.c: the submission being recorded
typedef struct { int kind; volatile uint32_t *val; uint32_t mode, v; uint64_t addr; uint32_t type; } vk_post;   // kind 1 event, 2 timestamp, 3 samples passed
VkCommandBuffer vk_cb(void);                    // this submission's command buffer (begun on first use)
uint64_t vk_cur_fence(void);                    // the fence this submission commits with
void vk_post_add(vk_post p);                    // lands at completion, in command order
void vk_after(uint64_t fence, void (*fn)(void *), void *arg);   // fn(arg) on the completion thread once fence is done (already done: at once)
void vk_barrier_all(VkCommandBuffer cb);        // every earlier access before every later one
VkCommandBuffer vk_once_begin(void);            // a command buffer of its own, outside submissions (uploads before the first, read-backs)
void vk_once_end(VkCommandBuffer cb, int wait); // submitted after the work queued so far; wait: until it has completed

// vk_tex.c
typedef struct vk_img vk_img;
struct vk_img {
    uint64_t serial; tex_t t;                    // the texture object as last seen (its storage, its description)
    VkImage img; VkDeviceMemory mem; VkFormat fmt; VkImageAspectFlags aspect;
    uint32_t w, h, d, layers, levels; VkImageType type;
    int gpu_written;                             // rendered into, cleared or copied into by the GPU: its storage no longer tells its content
    int cube;                                    // made cube-compatible (2D, six layers or more, square)
    VkImageUsageFlags usage;                     // (storage only for NVN_TEXTURE_FLAGS_IMAGE textures whose format allows it)
    int uploaded;                                // its storage's content copied in (again when the CPU writes it after stamp)
    uint64_t stamp;                              // the dirty-page stamp of the content last copied in (nvn_dirty.h)
    uint64_t bytes;                              // its memory
};
int vk_fmt(int nvn_format, VkFormat *f, VkComponentMapping *swz);   // 1 exact, 2 a substitute, 0 none (vk_fmt.c)
vk_img *vk_tex(const tex_t *t);                 // the image of a texture object (made on first use; NULL: none can be made)
void vk_tex_ready(vk_img *im, VkCommandBuffer cb);   // before a GPU read: CPU-filled content uploaded (recorded into cb before its use)
VkImageView vk_tex_view(vk_img *im, const view_t *v, int has_v, int attach, int dim);   // a view (cached); attach: 0 sampled, 1 for rendering (one
                                                // level, its layers), 2 as a storage image (one level, no swizzle); dim: the shader's declared
                                                // dimension (dmsh_vk.py dim_of), -1 the texture's own
int vk_tex_by_id(uint32_t id, tex_t *t, view_t *v);   // the texture pool's entry for a texture ID (0: none registered)
int vk_samp_by_id(uint32_t id, samp_par *p);
void vk_tex_upload_now(vk_img *im);             // its storage's content copied in now, ahead of the submission being recorded
typedef struct { uint64_t made, made_bytes, live, live_bytes, up_n, up_bytes, reup_n; } vk_tex_counts;   // images made so far and alive now, uploads
void vk_tex_stats(vk_tex_counts *c);
void vk_stats_line(void);                       // vk_draw.c: draws, pipelines, uploads so far (every 600 presents)
// vk_ptime.c: each pass's GPU time (DM_VK_PASS_GPU=1), summed by its targets: a slot's command buffer begun (vk_submit.c), a pass's begin and end
// (vk_draw.c), the slot's submission done (the completion thread), the sums taken (the stats line's, or a test's)
enum { VK_NSLOT = 8 };                          // vk_submit.c's ring of command buffers
int vk_cur_slot(void);                          // vk_submit.c: the slot being recorded
typedef struct { uint32_t w, h; int32_t cfmt, dfmt; } vk_ptime_key;   // the first colour attachment's size and VkFormat, the depth's (0: none)
typedef struct { vk_ptime_key k; double ns; uint64_t passes, draws; } vk_ptime_row;
void vk_ptime_slot(VkCommandBuffer cb, int slot); void vk_ptime_begin(VkCommandBuffer cb);
void vk_ptime_end(VkCommandBuffer cb, const vk_ptime_key *k, uint32_t draws); void vk_ptime_done(int slot);
int vk_ptime_take(vk_ptime_row *out, int max, uint64_t *untimed);   // the rows summed since the last take (and the passes untimed), then from 0
void vk_ptime_line(uint64_t presents);         // the stats line's part (nothing when off)

// vk_ops.c / vk_draw.c: the render pass
void vk_pass_end_now(void); uint64_t vk_pass_count(void);                     // end the open pass (vk_draw.c) and land the clears still pending
// Why passes end (plan item 301e3, the stats line): the backend's own reason when one is set (vk_pe_push / vk_pe_pop around the end), else
// the op the replay is at: SetRenderTargets of other textures or of the same ones (clears pending), a clear in a continued pass, a copy, a
// dispatch, a barrier, an inline uniform update copied at once; OTHER: outside the replay
enum { VK_PE_RT, VK_PE_RT_SAME, VK_PE_CLEAR, VK_PE_COPY, VK_PE_DISPATCH, VK_PE_BARRIER, VK_PE_UB, VK_PE_UPD, VK_PE_SAMPLES, VK_PE_SUBMIT, VK_PE_OTHER, VK_PE_N };
int vk_pe_push(int why); void vk_pe_pop(int was);   // set why (if none is set yet) / back to what push returned
void vk_pass_ends(uint64_t out[VK_PE_N]);       // the passes ended so far by each
void vk_upd_flush(void);                       // vk_ops.c: the renamed uniform updates into memory (ends the pass)
uint64_t vk_arena_size(void);                   // vk_pools.c
int vk_pass_is_open(void);
int vk_pass_barrier(void);                      // vk_draw.c: a barrier in the open pass taken there (0: none open, or it must end)
void vk_land_clears(void);                      // vk_ops.c: R's pending clears with no pass open

// vk_present.c
void vk_window_gone(void);                      // the app's window is going: the swapchain dropped first (front_android.c, main thread)
void vk_shot_present(const tex_t *t, const int crop[4]);   // vk_shot.c: at each present, the screenshots due
