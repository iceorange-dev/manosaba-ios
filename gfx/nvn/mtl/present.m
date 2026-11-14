// gfx/nvn/mtl/present.m: the presentation layer of the Metal backend, a translation unit of its own (plan item 264): what is shown and when.
// Its parts, included below in order: mtl/shot.inc (screenshots), mtl/present.inc (MetalFX upscaling of the presented picture, live shots, the
// luma check, present_now) and mtl/present_thread.inc (the present queue and its thread, the display link). It asks nvn_mtl.m for the texture a
// present shows (present_tex) and commits to its queue; nvn_mtl.m asks it to drop queued frames before the background and whether upscaling is
// on. The declarations both ways are in mtl/mtl.h. A unit test of these parts includes nvn_mtl.m and then this file, as one translation unit
#include "mtl.h"
#ifdef NVN_MTL_M_ENDED   // such a test: nvn_mtl.m has popped the attribute push of mtl.h already, and the end of this file pops one
#pragma clang attribute push(__attribute__((objc_externally_retained)), apply_to = function)
#endif
#include "../../../host/front.h"   // front_metal_layer

// ---- The presents' own objects (made in present_init) and the present queue's counters (under g_pq_mu) ----
static _Atomic uint32_t g_present_qos;
#if !DM_NVN_M3
static id<MTL4CommandAllocator> g_present_alloc4[3];   // a present uses slot ticket % 3
static CBUF g_present_cb[3];
static id<MTL4ArgumentTable> g_present_args;
#endif
static id<MTLBuffer> g_present_uv[3];
static uint64_t g_present_alloc_fence[3];
static pthread_mutex_t g_pq_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pq_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_link_cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_link_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_link_tick_ns, g_link_tick_n;
static uint64_t g_link_dropped;
static int g_pq_inflight;   // Under g_pq_mu: number of presents committed to the GPU whose completion callback has not arrived yet
int g_dm_present_link;   // Present pacing mode (runtime value of DM_PRESENT_LINK, modified directly during same-process alternating measurement): 0 = acquire the drawable itself; 1 = display link at 60 Hz; 2 = display link at 120 Hz, the game presents a frame every other display-link tick
static int g_latency_set = -1;   // DM_PRESENT_LATENCY (nvn_pace.h nvn_latency_setting): n, 0 = auto (2 at a 30 Hz link), -1 = unset (1)
static uint64_t g_pq_done;
static uint64_t g_pq_read;
void present_init(void) {
    const char *link = getenv("DM_PRESENT_LINK");
    g_dm_present_link = link ? atoi(link) : 1; if (g_dm_present_link < 0 || g_dm_present_link > 2) g_dm_present_link = 1;   // Default 1 (real-device same-process alternation: when load exceeds one display-link period, long frames 15% → 0 and frame intervals even out; no difference from 0 at a steady 60); DM_PRESENT_LINK=0 turns it off   // 1 = present on every tick of a 60 Hz display link; 2 = 120 Hz display link, the game presents a frame every other display-link tick (when behind, it can present on the next 8.33 ms display-link tick)
    g_latency_set = nvn_latency_setting(getenv("DM_PRESENT_LATENCY"));
    if (!g_latency_set) MLOG("display link frame latency: auto (2 while the link ticks at 30 Hz, 1 otherwise; DM_PRESENT_LATENCY)");
#if !DM_NVN_M3
    for (int i = 0; i < 3; i++) {
        MTL4CommandAllocatorDescriptor *pad = [MTL4CommandAllocatorDescriptor new]; NSError *ae = nil;
        pad.label = [NSString stringWithFormat:@"nvn-mtl present allocator %d", i];
        g_present_alloc4[i] = [g_dev newCommandAllocatorWithDescriptor:pad error:&ae];
        if (!g_present_alloc4[i]) MDIE("failed to create present MTL4CommandAllocator[%d]: %s", i, ae.localizedDescription.UTF8String);
        g_present_cb[i] = [g_dev newCommandBuffer];
        if (!g_present_cb[i]) MDIE("failed to create present MTL4CommandBuffer[%d]", i);
    }
    MTL4ArgumentTableDescriptor *pad = [MTL4ArgumentTableDescriptor new]; NSError *ate = nil;
    pad.maxTextureBindCount = 1; pad.maxSamplerStateBindCount = 1; pad.maxBufferBindCount = 1;
    g_present_args = [g_dev newArgumentTableWithDescriptor:pad error:&ate];
    if (!g_present_args) MDIE("failed to create present argument table: %s", ate.localizedDescription.UTF8String);
#endif
    for (int i = 0; i < 3; i++) {
        g_present_uv[i] = [g_dev newBufferWithLength:sizeof(float) * 8 options:MTLResourceStorageModeShared];
        if (!g_present_uv[i]) MDIE("failed to create present UV buffer[%d]", i);
        res_add(g_present_uv[i]);
    }
}
uint64_t present_alloc_bytes(void) {
    uint64_t n = 0;
#if !DM_NVN_M3
    for (int i = 0; i < 3; i++) n += g_present_alloc4[i].allocatedSize;
#endif
    return n;
}
#include "shot.inc"
#include "present.inc"
#include "present_thread.inc"
#pragma clang attribute pop
