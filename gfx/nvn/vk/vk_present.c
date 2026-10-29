// gfx/nvn/vk/vk_present.c: the present (plan item 301e). The game's window texture is blitted to a swapchain on the app's window (front_android.c
// keeps it), letterboxed to the texture's crop at its aspect ratio over black, linear filtering; the swapchain is in the window texture's sRGB-ness
// (an sRGB texture into a UNORM swapchain would be blitted as decoded values: too dark; nx2android picks it the same way), FIFO, the transform left
// to the compositor (identity: the app is landscape on a portrait panel). It is made on the first present and again when the window changes or the
// swapchain is out of date; the window going (the app in the background) drops it first, from the main thread. Presents run on the caller's thread
// and are queued behind the frame's submission (queue order); the next present waits for the one before it to finish on the GPU (one in flight).
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include "vk.h"
#include <android/native_window.h>
#include <stdlib.h>
#include <string.h>

ANativeWindow *front_android_window(void);   // android/front_android.c (NULL: no window now)
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    ANativeWindow *win; VkSurfaceKHR surf; VkSwapchainKHR sc; VkFormat fmt; VkExtent2D ext; uint32_t n; VkImage img[8];
    VkSemaphore acq[4], rel[8]; VkFence fence[4]; VkCommandPool pool; VkCommandBuffer cb[4]; uint32_t at; int srgb;
} P;
static uint64_t g_presents, g_drops;

static void idle(void) { pthread_mutex_lock(&V.qmu); vkDeviceWaitIdle(V.dev); pthread_mutex_unlock(&V.qmu); }   // (every queue's host access held: the queue lock)
static void drop_locked(void) {
    if (!V.dev || (!P.sc && !P.surf)) return;
    idle();
    if (P.sc) vkDestroySwapchainKHR(V.dev, P.sc, NULL);
    if (P.surf) vkDestroySurfaceKHR(V.inst, P.surf, NULL);
    P.sc = VK_NULL_HANDLE; P.surf = VK_NULL_HANDLE; P.win = NULL;
}
void vk_window_gone(void) { pthread_mutex_lock(&g_mu); drop_locked(); pthread_mutex_unlock(&g_mu); }   // front_android.c, before the window is released
static int make_locked(ANativeWindow *w, int srgb) {
    if (!V.android_surface) { VRED("no VK_KHR_android_surface: nothing is shown"); return 0; }
    if (!P.pool) {
        VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = V.qf};
        VK(vkCreateCommandPool(V.dev, &pci, NULL, &P.pool));
        VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = P.pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 4};
        VK(vkAllocateCommandBuffers(V.dev, &ai, P.cb));
        VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        for (int i = 0; i < 4; i++) { VK(vkCreateSemaphore(V.dev, &si, NULL, &P.acq[i])); VK(vkCreateFence(V.dev, &fi, NULL, &P.fence[i])); }
        for (int i = 0; i < 8; i++) VK(vkCreateSemaphore(V.dev, &si, NULL, &P.rel[i]));
    }
    VkSwapchainKHR old = P.sc;
    if (P.win != w) { if (old) { idle(); vkDestroySwapchainKHR(V.dev, old, NULL); old = VK_NULL_HANDLE; P.sc = VK_NULL_HANDLE; }
        if (P.surf) vkDestroySurfaceKHR(V.inst, P.surf, NULL); P.surf = VK_NULL_HANDLE;
        VkAndroidSurfaceCreateInfoKHR ci = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = w};
        VkResult r = vkCreateAndroidSurfaceKHR(V.inst, &ci, NULL, &P.surf);
        if (r != VK_SUCCESS) { VLOG("[red] the window's surface: %d", (int)r); P.surf = VK_NULL_HANDLE; return 0; }
        P.win = w; }
    VkSurfaceCapabilitiesKHR cap; VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(V.pd, P.surf, &cap));
    VkBool32 ok = 0; vkGetPhysicalDeviceSurfaceSupportKHR(V.pd, V.qf, P.surf, &ok); if (!ok) { VRED("the queue cannot present to the window"); return 0; }
    VkFormat want = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, got = VK_FORMAT_UNDEFINED;
    uint32_t nf = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(V.pd, P.surf, &nf, NULL); VkSurfaceFormatKHR sf[32]; if (nf > 32) nf = 32; vkGetPhysicalDeviceSurfaceFormatsKHR(V.pd, P.surf, &nf, sf);
    for (uint32_t i = 0; i < nf; i++) if (sf[i].format == want) got = want;
    if (!got) got = nf ? sf[0].format : VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D ext = cap.currentExtent.width != UINT32_MAX ? cap.currentExtent : (VkExtent2D){(uint32_t)ANativeWindow_getWidth(w), (uint32_t)ANativeWindow_getHeight(w)};
    uint32_t n = cap.minImageCount < 3 ? 3 : cap.minImageCount; if (cap.maxImageCount && n > cap.maxImageCount) n = cap.maxImageCount;
    VkSwapchainCreateInfoKHR ci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = P.surf, .minImageCount = n, .imageFormat = got, .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = ext, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = (cap.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : cap.currentTransform,
        .compositeAlpha = (cap.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE, .oldSwapchain = old};
    VkSwapchainKHR sc; VkResult r = vkCreateSwapchainKHR(V.dev, &ci, NULL, &sc);
    if (old) { idle(); vkDestroySwapchainKHR(V.dev, old, NULL); }
    P.sc = VK_NULL_HANDLE;
    if (r != VK_SUCCESS) { VLOG("[red] swapchain %ux%u format %d: %d", ext.width, ext.height, (int)got, (int)r); return 0; }
    P.sc = sc; P.fmt = got; P.ext = ext; P.srgb = srgb;
    P.n = 8; VK(vkGetSwapchainImagesKHR(V.dev, sc, &P.n, P.img));
    VLOG("swapchain %ux%u, %u images, format %d (%s, the window texture %s), transform %#x (the window's %#x)", ext.width, ext.height, P.n, (int)got,
         got == want ? "as wanted" : "[red] not the one wanted", srgb ? "sRGB" : "linear", ci.preTransform, cap.currentTransform);
    return 1;
}
static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, .oldLayout = from, .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &di);
}
uint64_t mtl_present(const tex_t *t, double period_s, const int crop[4]) {
    (void)period_s; mtl_init();
    static uint64_t calls; if (!(++calls % 600)) vk_stats_line();
    vk_img *im = vk_tex(t); if (!im) { g_drops++; return 0; }
    ANativeWindow *w = front_android_window();
    pthread_mutex_lock(&g_mu);
    if (!w) { drop_locked(); pthread_mutex_unlock(&g_mu); g_drops++; return 0; }
    int srgb = im->fmt == VK_FORMAT_R8G8B8A8_SRGB || im->fmt == VK_FORMAT_B8G8R8A8_SRGB;
    if ((!P.sc || P.win != w || P.srgb != srgb) && !make_locked(w, srgb)) { pthread_mutex_unlock(&g_mu); g_drops++; return 0; }
    uint32_t k = P.at++ % 4;
    VK(vkWaitForFences(V.dev, 1, &P.fence[k], VK_TRUE, UINT64_MAX));
    uint32_t idx; VkResult r = vkAcquireNextImageKHR(V.dev, P.sc, UINT64_MAX, P.acq[k], VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { make_locked(w, srgb); pthread_mutex_unlock(&g_mu); g_drops++; return 0; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { VLOG("[red] acquiring a swapchain image: %d", (int)r); pthread_mutex_unlock(&g_mu); g_drops++; return 0; }
    VK(vkResetFences(V.dev, 1, &P.fence[k]));
    VkCommandBuffer cb = P.cb[k]; VK(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}; VK(vkBeginCommandBuffer(cb, &bi));
    VkImage dst = P.img[idx];
    barrier(cb, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue black = {{0, 0, 0, 1}}; VkImageSubresourceRange rg = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &rg);
    barrier(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    // the crop (x, y, w, h; 0 w or h: the whole texture), fitted into the swapchain at its aspect ratio (within 1%: the whole swapchain)
    int cx = crop ? crop[0] : 0, cy = crop ? crop[1] : 0, cw = crop && crop[2] > 0 ? crop[2] : (int)im->w, ch = crop && crop[3] > 0 ? crop[3] : (int)im->h;
    double sa = (double)cw / ch, da = (double)P.ext.width / P.ext.height; int ow = (int)P.ext.width, oh = (int)P.ext.height;
    if (sa / da > 1.01) oh = (int)(P.ext.width / sa); else if (da / sa > 1.01) ow = (int)(P.ext.height * sa);
    int ox = ((int)P.ext.width - ow) / 2, oy = ((int)P.ext.height - oh) / 2;
    VkImageBlit b = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{cx, cy, 0}, {cx + cw, cy + ch, 1}}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{ox, oy, 0}, {ox + ow, oy + oh, 1}}};
    vkCmdBlitImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
    barrier(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    VK(vkEndCommandBuffer(cb));
    VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = P.acq[k], .stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT};
    VkSemaphoreSubmitInfo sig = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = P.rel[idx], .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    VkCommandBufferSubmitInfo cbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cb};
    VkSubmitInfo2 si = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .waitSemaphoreInfoCount = 1, .pWaitSemaphoreInfos = &wait, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbi,
                        .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &sig};
    VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &P.rel[idx], .swapchainCount = 1, .pSwapchains = &P.sc, .pImageIndices = &idx};
    pthread_mutex_lock(&V.qmu);
    r = vkQueueSubmit2(V.q, 1, &si, P.fence[k]);
    VkResult pr = r == VK_SUCCESS ? vkQueuePresentKHR(V.q, &pi) : r;
    pthread_mutex_unlock(&V.qmu);
    if (r != VK_SUCCESS) VDIE("the present's submission: %d", (int)r);
    // suboptimal is what Android answers while the compositor rotates the identity transform onto the panel (pre-rotation: 301f): kept
    if (pr == VK_ERROR_OUT_OF_DATE_KHR) make_locked(w, srgb);
    else if (pr == VK_SUBOPTIMAL_KHR) { static int said; if (!said++) VLOG("present: suboptimal (the compositor rotates the image): the swapchain kept"); }
    else if (pr != VK_SUCCESS) VLOG("[red] present: %d", (int)pr);
    uint64_t n = ++g_presents;
    pthread_mutex_unlock(&g_mu);
    if (n == 1) VLOG("first present: the window texture %ux%u (format %d), crop %d,%d %dx%d → %dx%d at %d,%d", im->w, im->h, (int)im->fmt, cx, cy, cw, ch, ow, oh, ox, oy);
    return n;
}
void mtl_present_wait(uint64_t ticket) { (void)ticket; }
void mtl_present_flush(void) {}
double mtl_presented_time(uint64_t n) { (void)n; return 0; }
uint32_t mtl_present_qos(void) { return 0; }
int mtl_has_window(void) { return front_android_window() != NULL; }
int mtl_display_hz(void) { return front_android_window() ? 60 : 0; }   // (the panel's own rate and frame pacing: 301f)
int mtl_fps_target(void) { const char *e = getenv("DM_FPS"); return e && atoi(e) == 30 ? 30 : 60; }
int mtl_link_active(void) { return 0; }
int mtl_link_wait_tick(uint64_t timeout_ns) { (void)timeout_ns; return 0; }
void mtl_link_set_fifo(int on) { (void)on; }
void mtl_link_set_interval(int n) { (void)n; }
void mtl_link_set_rate(int hz) { (void)hz; }
void vk_present_stats(uint64_t *n, uint64_t *drops) { *n = g_presents; *drops = g_drops; }
void mtl_drain(void) { if (!V.dev) return; mtl_finish(); pthread_mutex_lock(&V.qmu); vkQueueWaitIdle(V.q); pthread_mutex_unlock(&V.qmu); }
