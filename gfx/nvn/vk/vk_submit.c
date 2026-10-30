// gfx/nvn/vk/vk_submit.c: submissions (plan item 301e). A submission records into one command buffer of a ring of NSLOT; mtl_sub_commit submits it
// signalling a timeline semaphore with the submission's fence (1, 2, ...), which is the NVN fence: what the CPU sees of the GPU's work happens only once
// the semaphore has reached it, as with the Metal backend's completion handler. A completion thread waits for each committed fence in order and then
// lands that submission's event writes and counter reports in command order, runs what was asked for after it (vk_after) and moves the done fence;
// waits (mtl_wait) wait for the done fence. A slot is reused once its last submission is done. Counter reports: the counter half 0 at once and the
// timestamp half from the guest clock at completion, as the null backend; samples passed a large constant until occlusion queries (301e3; nx2android
// reports 2^20 the same way). Work outside a submission (uploads before the first, read-backs) has its own command buffers (vk_once_*). Each
// submission's command buffer writes a GPU timestamp at its start and its end; at completion the span goes to the core's GPU time of the frame
// it was committed in (gpu_tag_begin / gpu_tag_done: the fps line's GPU time; the spans are on the GPU's clock, not the CPU's).
#include "vk.h"
#include "../core/frame.h"
#include "../core/pend.h"
#include "../core/upd.h"
#include "../core/usnap.h"
#include <stdlib.h>
#include <string.h>

enum { NSLOT = VK_NSLOT };
typedef struct { void (*fn)(void *); void *arg; uint64_t fence; } after_t;
typedef struct {
    VkCommandPool pool; VkCommandBuffer cb; int begun;
    uint64_t fence;                                  // the submission it last held
    vk_post *post; int npost, postcap;
    uint64_t gpu_f; int timed;                       // the frame it was committed in, its timestamps written
} slot_t;
static slot_t g_slot[NSLOT];
static slot_t *g_cur;
static VkSemaphore g_sem;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static uint64_t g_fence, g_done;                    // the newest committed and the newest completed (under g_mu)
static after_t *g_after; static int g_nafter, g_aftercap;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static VkQueryPool g_qp; static double g_ts_s; static uint64_t g_ts_mask;   // two timestamps a slot; seconds a tick

static uint64_t gpu_ticks(void) { return guest_ns() * 12 / 625; }   // the guest's 19.2 MHz counter
static void land(const vk_post *o) {
    if (o->kind == 1) { if (o->mode == 1) *o->val += o->v; else *o->val = o->v; }
    else if (o->kind == 3) { uint64_t rec[2] = {1u << 20, gpu_ticks()}; memcpy((void *)(uintptr_t)o->addr, rec, sizeof rec); }
    else { uint64_t t = gpu_ticks(); memcpy((uint8_t *)(uintptr_t)o->addr + 8, &t, 8); }
}
static void *completion(void *unused) {
    (void)unused; dm_setname_self("dm vk done");
    for (uint64_t next = 1;; next++) {
        pthread_mutex_lock(&g_mu); while (g_fence < next) pthread_cond_wait(&g_cv, &g_mu); pthread_mutex_unlock(&g_mu);
        VkSemaphoreWaitInfo wi = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &g_sem, .pValues = &next};
        VkResult r = vkWaitSemaphores(V.dev, &wi, UINT64_MAX);
        if (r != VK_SUCCESS) VDIE("waiting for submission %llu: %d (device lost?)", (unsigned long long)next, (int)r);
        slot_t *s = &g_slot[next % NSLOT];
#ifndef DM_TEST_VK_TS_NONE   // negative control of t_vk_be only: no GPU time handed on
        if (s->timed) {
            uint64_t t[2] = {0, 0}; uint32_t q = 2 * (uint32_t)(s - g_slot);
            if (vkGetQueryPoolResults(V.dev, g_qp, q, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) t[0] = t[1] = 0;
            gpu_tag_done(s->gpu_f, (double)(t[0] & g_ts_mask) * g_ts_s, (double)(t[1] & g_ts_mask) * g_ts_s);
        }
#endif
        s->timed = 0; vk_ptime_done((int)(s - g_slot));
        for (int i = 0; i < s->npost; i++) land(&s->post[i]);
        s->npost = 0;
        pthread_mutex_lock(&g_mu);
        g_done = next;
        after_t run[64]; int nrun = 0;
        for (int i = 0; i < g_nafter && nrun < 64;) if (g_after[i].fence <= next) { run[nrun++] = g_after[i]; g_after[i] = g_after[--g_nafter]; } else i++;
        pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_mu);
        for (int i = 0; i < nrun; i++) run[i].fn(run[i].arg);
    }
    return NULL;
}
static void init(void) {
    vk_init();
    VkSemaphoreTypeCreateInfo st = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0};
    VkSemaphoreCreateInfo sci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &st};
    VK(vkCreateSemaphore(V.dev, &sci, NULL, &g_sem));
    for (int i = 0; i < NSLOT; i++) {
        VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = V.qf};
        VK(vkCreateCommandPool(V.dev, &pci, NULL, &g_slot[i].pool));
        VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = g_slot[i].pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
        VK(vkAllocateCommandBuffers(V.dev, &ai, &g_slot[i].cb));
    }
    if (V.ts_bits && V.props.limits.timestampPeriod > 0) {
        VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 * NSLOT};
        VK(vkCreateQueryPool(V.dev, &qi, NULL, &g_qp));
        g_ts_s = V.props.limits.timestampPeriod * 1e-9; g_ts_mask = V.ts_bits >= 64 ? UINT64_MAX : (1ull << V.ts_bits) - 1;
    } else VLOG("[red] the queue has no timestamps: no GPU time");
    pthread_t th; if (pthread_create(&th, NULL, completion, NULL)) VDIE("cannot start the completion thread"); pthread_detach(th);
}

void mtl_init(void) { pthread_once(&g_once, init); }
void mtl_sub_begin(void) {
    mtl_init();
    pthread_mutex_lock(&g_mu); uint64_t f = g_fence + 1; slot_t *s = &g_slot[f % NSLOT];
    while (s->fence > g_done) pthread_cond_wait(&g_cv, &g_mu);   // its last submission done
    uint64_t done = g_done; pthread_mutex_unlock(&g_mu);
    VK(vkResetCommandPool(V.dev, s->pool, 0));
    s->begun = 0; s->npost = 0; g_cur = s;
#ifndef DM_TEST_VK_PEND_KEPT   // negative control of t_vk_be only: the pending writes never retired (the table fills, then overflows for good)
    pend_begin(done);   // the writes of completed submissions are in memory now (core/pend.h)
#else
    (void)done;
#endif
}
VkCommandBuffer vk_cb(void) {
    if (!g_cur) VDIE("a command recorded outside a submission");
    if (!g_cur->begun) {
        VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        VK(vkBeginCommandBuffer(g_cur->cb, &bi)); g_cur->begun = 1;
        if (g_qp) { uint32_t q = 2 * (uint32_t)(g_cur - g_slot); vkCmdResetQueryPool(g_cur->cb, g_qp, q, 2); vkCmdWriteTimestamp2(g_cur->cb, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, g_qp, q); }
        vk_ptime_slot(g_cur->cb, (int)(g_cur - g_slot));
        vk_barrier_all(g_cur->cb);   // after whatever ran before (uploads of their own, the previous submission)
    }
    return g_cur->cb;
}
int vk_cur_slot(void) { return g_cur ? (int)(g_cur - g_slot) : 0; }
uint64_t vk_cur_fence(void) { pthread_mutex_lock(&g_mu); uint64_t f = g_fence + 1; pthread_mutex_unlock(&g_mu); return f; }
void vk_post_add(vk_post p) {
    slot_t *s = g_cur; if (!s) VDIE("a completion effect outside a submission");
#ifdef DM_TEST_VK_EVENT_EARLY   // negative control of android/tests/t_vk_be.c only: landed when recorded
    land(&p); return;
#endif
    if (s->npost == s->postcap) { s->postcap = s->postcap ? 2 * s->postcap : 64; s->post = realloc(s->post, (size_t)s->postcap * sizeof *s->post); if (!s->post) VDIE("posts"); }
    s->post[s->npost++] = p;
}
uint64_t mtl_sub_commit(void) {
    slot_t *s = g_cur; if (!s) VDIE("commit without a submission");
    int pe = vk_pe_push(VK_PE_SUBMIT);
    vk_upd_flush(); upd_sub_end();   // renamed uniform updates reach memory at the end of their submission
    vk_pass_end_now(); vk_pe_pop(pe);
    if (s->begun && g_qp) {   // (an empty submission has no GPU time)
        vkCmdWriteTimestamp2(s->cb, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, g_qp, 2 * (uint32_t)(s - g_slot) + 1);
        s->gpu_f = atomic_load(&g_cur_frame); gpu_tag_begin(s->gpu_f); s->timed = 1;
    }
    if (s->begun) VK(vkEndCommandBuffer(s->cb));
    pthread_mutex_lock(&g_mu); uint64_t f = g_fence + 1; pthread_mutex_unlock(&g_mu);
    VkCommandBufferSubmitInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = s->cb};
    VkSemaphoreSubmitInfo ssi = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = g_sem, .value = f, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    VkSubmitInfo2 si = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = s->begun ? 1 : 0, .pCommandBufferInfos = &cbi, .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &ssi};
    pthread_mutex_lock(&V.qmu); VkResult r = vkQueueSubmit2(V.q, 1, &si, VK_NULL_HANDLE); pthread_mutex_unlock(&V.qmu);
    if (r != VK_SUCCESS) VDIE("submission %llu refused: %d", (unsigned long long)f, (int)r);
    s->fence = f; g_cur = NULL;
    pthread_mutex_lock(&g_mu); g_fence = f; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_mu);
    return f;
}
// The interface layer's frame number at each present (the Metal backend's mtl_set_frame): the frame a commit's GPU time counts in, and the core's
// lines every 300 frames (uniform snapshots, renamed updates)
void mtl_set_frame(uint64_t f) { atomic_store(&g_cur_frame, f); us_report(f); ren_report(f); }
uint64_t nvn_be_sub_fence(void) { return vk_cur_fence(); }
uint64_t mtl_done_fence(void) { pthread_mutex_lock(&g_mu); uint64_t d = g_done; pthread_mutex_unlock(&g_mu); return d; }
uint64_t mtl_last_fence(void) { pthread_mutex_lock(&g_mu); uint64_t f = g_fence; pthread_mutex_unlock(&g_mu); return f; }
uint64_t nvn_be_done_fence(void) { return mtl_done_fence(); }
int mtl_wait(uint64_t fence, uint64_t timeout_ns) {
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl);
    uint64_t ns = (uint64_t)dl.tv_nsec + (timeout_ns > 3600000000000ull ? 3600000000000ull : timeout_ns);
    dl.tv_sec += (time_t)(ns / 1000000000ull); dl.tv_nsec = (long)(ns % 1000000000ull);
    pthread_mutex_lock(&g_mu);
    while (g_done < fence) if (pthread_cond_timedwait(&g_cv, &g_mu, &dl)) break;   // (a fence not committed yet is waited for as on Metal)
    int ok = g_done >= fence; pthread_mutex_unlock(&g_mu);
    return ok;
}
void mtl_finish(void) { uint64_t f = mtl_last_fence(); if (f) mtl_wait(f, UINT64_MAX); }   // (nothing committed yet: nothing to wait for)
void mtl_wb_fence_done(void) {}
void vk_after(uint64_t fence, void (*fn)(void *), void *arg) {
    pthread_mutex_lock(&g_mu);
    if (fence <= g_done) { pthread_mutex_unlock(&g_mu); fn(arg); return; }
    if (g_nafter == g_aftercap) { g_aftercap = g_aftercap ? 2 * g_aftercap : 64; g_after = realloc(g_after, (size_t)g_aftercap * sizeof *g_after); if (!g_after) VDIE("after list"); }
    g_after[g_nafter++] = (after_t){fn, arg, fence};
    pthread_mutex_unlock(&g_mu);
}
void vk_barrier_all(VkCommandBuffer cb) {
    VkMemoryBarrier2 mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(cb, &di);
}

// Work of its own outside submissions: one command pool under a lock, a fence per command buffer when it is waited for
static pthread_mutex_t g_once_mu = PTHREAD_MUTEX_INITIALIZER; static VkCommandPool g_once_pool;
VkCommandBuffer vk_once_begin(void) {
    mtl_init();
    pthread_mutex_lock(&g_once_mu);
    if (!g_once_pool) { VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = V.qf};
        VK(vkCreateCommandPool(V.dev, &pci, NULL, &g_once_pool)); }
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = g_once_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cb; VK(vkAllocateCommandBuffers(V.dev, &ai, &cb));
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK(vkBeginCommandBuffer(cb, &bi)); vk_barrier_all(cb);
    return cb;   // (g_once_mu held until vk_once_end)
}
typedef struct { VkCommandBuffer cb; } once_t;
static void once_free(void *a) { once_t *o = a; pthread_mutex_lock(&g_once_mu); vkFreeCommandBuffers(V.dev, g_once_pool, 1, &o->cb); pthread_mutex_unlock(&g_once_mu); free(o); }
void vk_once_end(VkCommandBuffer cb, int wait) {
    vk_barrier_all(cb); VK(vkEndCommandBuffer(cb));
    VkFence f = VK_NULL_HANDLE;
    if (wait) { VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VK(vkCreateFence(V.dev, &fci, NULL, &f)); }
    VkCommandBufferSubmitInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cb};
    VkSubmitInfo2 si = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbi};
    pthread_mutex_lock(&V.qmu); VkResult r = vkQueueSubmit2(V.q, 1, &si, f); pthread_mutex_unlock(&V.qmu);
    pthread_mutex_unlock(&g_once_mu);
    if (r != VK_SUCCESS) VDIE("a command buffer of its own refused: %d", (int)r);
    if (wait) { VK(vkWaitForFences(V.dev, 1, &f, VK_TRUE, UINT64_MAX)); vkDestroyFence(V.dev, f, NULL);
                pthread_mutex_lock(&g_once_mu); vkFreeCommandBuffers(V.dev, g_once_pool, 1, &cb); pthread_mutex_unlock(&g_once_mu); return; }
    // not waited for: freed once the next submission after it is done (queue order)
    once_t *o = malloc(sizeof *o); o->cb = cb; vk_after(mtl_last_fence() + 1, once_free, o);
}

// The execution interface's completion effects (nvn_backend.h)
void nvn_be_signal_event(volatile uint32_t *val, uint32_t mode, uint32_t v) { vk_post_add((vk_post){1, val, mode, v, 0, 0}); }
void nvn_be_report_counter(const uint8_t *p, const void *a) {
    (void)p; uint64_t t, ad; memcpy(&t, a, 8); memcpy(&ad, (const uint8_t *)a + 8, 8);
    if (!ad) return;
    if (t == NVN_COUNTER_TYPE_SAMPLES_PASSED) { vk_post_add((vk_post){3, NULL, 0, 0, ad, (uint32_t)t}); return; }
    uint64_t z = 0; memcpy((void *)(uintptr_t)ad, &z, sizeof z); vk_post_add((vk_post){2, NULL, 0, 0, ad, (uint32_t)t});
}
void nvn_be_reset_counter(const uint8_t *p, const void *a) { (void)p; (void)a; }
