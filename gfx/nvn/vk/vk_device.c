// gfx/nvn/vk/vk_device.c: the Vulkan instance and device (plan item 301e). Vulkan 1.3 (the Adreno 840 has 1.4.295); one queue family that draws,
// computes and copies; of the features and extensions the backend uses each is taken when the device has it, and what is missing is logged once.
// Needed, or the backend stops: dynamic rendering, synchronization2 and timeline semaphores (core in 1.3), VK_EXT_map_memory_placed for pools at the
// guest's addresses (the K100's driver has it in an app: plan item 301a).
#include "vk.h"
#include <stdlib.h>
#include <string.h>

vk_ctx V;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

uint32_t vk_mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid) {
    for (uint32_t i = 0; i < V.mem.memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = V.mem.memoryTypes[i].propertyFlags;
        if ((bits >> i & 1) && (f & want) == want && !(f & avoid)) return i;
    }
    return UINT32_MAX;
}
static int has_ext(const VkExtensionProperties *e, uint32_t n, const char *name) { for (uint32_t i = 0; i < n; i++) if (!strcmp(e[i].extensionName, name)) return 1; return 0; }

static void init(void) {
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "dm", .apiVersion = VK_API_VERSION_1_3};
    const char *iext[] = {VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_android_surface"};
    uint32_t nie = 0; vkEnumerateInstanceExtensionProperties(NULL, &nie, NULL);
    VkExtensionProperties *ie = calloc(nie ? nie : 1, sizeof *ie); vkEnumerateInstanceExtensionProperties(NULL, &nie, ie);
    V.android_surface = has_ext(ie, nie, iext[0]) && has_ext(ie, nie, iext[1]); free(ie);
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai, .enabledExtensionCount = V.android_surface ? 2 : 0, .ppEnabledExtensionNames = iext};
    VK(vkCreateInstance(&ici, NULL, &V.inst));
    uint32_t npd = 1; VkPhysicalDevice pd[1];
    if (vkEnumeratePhysicalDevices(V.inst, &npd, pd) < 0 || !npd) VDIE("no Vulkan device");
    V.pd = pd[0]; vkGetPhysicalDeviceProperties(V.pd, &V.props); vkGetPhysicalDeviceMemoryProperties(V.pd, &V.mem);
    if (V.props.apiVersion < VK_API_VERSION_1_3) VDIE("%s has Vulkan %u.%u; 1.3 is needed", V.props.deviceName, VK_API_VERSION_MAJOR(V.props.apiVersion), VK_API_VERSION_MINOR(V.props.apiVersion));
    uint32_t nqf = 0; vkGetPhysicalDeviceQueueFamilyProperties(V.pd, &nqf, NULL);
    VkQueueFamilyProperties *qfp = calloc(nqf, sizeof *qfp); vkGetPhysicalDeviceQueueFamilyProperties(V.pd, &nqf, qfp);
    V.qf = UINT32_MAX;
    for (uint32_t i = 0; i < nqf && V.qf == UINT32_MAX; i++) if ((qfp[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) V.qf = i;
    if (V.qf != UINT32_MAX) V.ts_bits = qfp[V.qf].timestampValidBits;
    free(qfp); if (V.qf == UINT32_MAX) VDIE("no queue family draws and computes");
    uint32_t nde = 0; vkEnumerateDeviceExtensionProperties(V.pd, NULL, &nde, NULL);
    VkExtensionProperties *de = calloc(nde ? nde : 1, sizeof *de); vkEnumerateDeviceExtensionProperties(V.pd, NULL, &nde, de);

    // What the device has
    VkPhysicalDeviceIndexTypeUint8Features idx8 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES};
    VkPhysicalDeviceMapMemoryPlacedFeaturesEXT placed = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT, &idx8};
    VkPhysicalDeviceCustomBorderColorFeaturesEXT border = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT, &placed};
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vi = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT, &border};
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT eds2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT, &vi};
    VkPhysicalDeviceDepthClipControlFeaturesEXT dcc = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT, &eds2};
    VkPhysicalDeviceRobustness2FeaturesEXT rb2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, &dcc};
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &rb2};
    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13};
    VkPhysicalDeviceVulkan11Features f11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &f12};
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f11};
    vkGetPhysicalDeviceFeatures2(V.pd, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2 || !f12.timelineSemaphore) VDIE("dynamic rendering, synchronization2 and timeline semaphores are needed");
    // What is taken: the core features as the device has them (robust buffer access left off: it costs, and the replay keeps bindings in range),
    // each extension with its feature struct when the device has both
    f2.features.robustBufferAccess = VK_FALSE;
    V.robust2 = has_ext(de, nde, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME) && rb2.nullDescriptor;
    V.depth_clip_ctl = has_ext(de, nde, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME) && dcc.depthClipControl;
    V.eds2 = has_ext(de, nde, VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME) && eds2.extendedDynamicState2;
    V.vi_dyn = has_ext(de, nde, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME) && vi.vertexInputDynamicState;
    V.custom_border = has_ext(de, nde, VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME) && border.customBorderColors;
    int map2 = has_ext(de, nde, VK_KHR_MAP_MEMORY_2_EXTENSION_NAME);
    V.placed = map2 && has_ext(de, nde, VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME) && placed.memoryMapPlaced;
    V.unmap_reserve = V.placed && placed.memoryUnmapReserve;
    V.push_ds = has_ext(de, nde, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    const char *idx8_ext = has_ext(de, nde, VK_KHR_INDEX_TYPE_UINT8_EXTENSION_NAME) ? VK_KHR_INDEX_TYPE_UINT8_EXTENSION_NAME :
                           has_ext(de, nde, VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME) ? VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME : NULL;
    V.idx8 = idx8_ext && idx8.indexTypeUint8;
    free(de);
    if (!V.vi_dyn) VDIE("VK_EXT_vertex_input_dynamic_state is needed (the vertex input is set at each draw; %s does not have it)", V.props.deviceName);
    if (!V.placed) VDIE("VK_EXT_map_memory_placed is needed for pools at the guest's addresses (%s does not have it)", V.props.deviceName);
    const char *dext[16]; uint32_t ndx = 0;
    dext[ndx++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME; dext[ndx++] = VK_KHR_MAP_MEMORY_2_EXTENSION_NAME; dext[ndx++] = VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME;
    if (V.robust2) dext[ndx++] = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
    if (V.depth_clip_ctl) dext[ndx++] = VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME;
    if (V.eds2) dext[ndx++] = VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME;
    if (V.vi_dyn) dext[ndx++] = VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME;
    if (V.custom_border) dext[ndx++] = VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME;
    if (V.push_ds) dext[ndx++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    if (V.idx8) dext[ndx++] = idx8_ext;
    rb2.robustBufferAccess2 = rb2.robustImageAccess2 = VK_FALSE; placed.memoryMapRangePlaced = VK_FALSE;
    {   // the chain: features2 → 1.1 → 1.2 → 1.3 → the taken extensions' structs
        VkBaseOutStructure *ext[7] = {(void *)&rb2, (void *)&dcc, (void *)&eds2, (void *)&vi, (void *)&border, (void *)&placed, (void *)&idx8};
        int on[7] = {V.robust2, V.depth_clip_ctl, V.eds2, V.vi_dyn, V.custom_border, 1, V.idx8};
        void *head = NULL; for (int i = 0; i < 7; i++) if (on[i]) { ext[i]->pNext = head; head = ext[i]; }
        f13.pNext = head;
    }
    V.bc = f2.features.textureCompressionBC;
    float prio = 1;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = V.qf, .queueCount = 1, .pQueuePriorities = &prio};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci, .enabledExtensionCount = ndx, .ppEnabledExtensionNames = dext};
    VK(vkCreateDevice(V.pd, &dci, NULL, &V.dev));
    vkGetDeviceQueue(V.dev, V.qf, 0, &V.q);
    pthread_mutex_init(&V.qmu, NULL);
    V.map2 = (PFN_vkMapMemory2KHR)vkGetDeviceProcAddr(V.dev, "vkMapMemory2KHR");
    V.unmap2 = (PFN_vkUnmapMemory2KHR)vkGetDeviceProcAddr(V.dev, "vkUnmapMemory2KHR");
    if (V.push_ds) V.push = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(V.dev, "vkCmdPushDescriptorSetKHR");
    if (!V.map2 || !V.unmap2) VDIE("vkMapMemory2KHR / vkUnmapMemory2KHR missing");
    if (V.push_ds) {
        VkPhysicalDevicePushDescriptorPropertiesKHR pp = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 p2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &pp}; vkGetPhysicalDeviceProperties2(V.pd, &p2); V.max_push_ds = pp.maxPushDescriptors;
    }
    VLOG("%s, Vulkan %u.%u.%u, driver %#x; queue family %u; placed pools (unmap reserving %d), push descriptors %d (%u), BC %d, custom border colours %d, "
         "dynamic vertex input %d, extended dynamic state 2 %d, depth clip control %d, null descriptors %d, 8-bit indices %d", V.props.deviceName,
         VK_API_VERSION_MAJOR(V.props.apiVersion), VK_API_VERSION_MINOR(V.props.apiVersion), VK_API_VERSION_PATCH(V.props.apiVersion), V.props.driverVersion,
         V.qf, V.unmap_reserve, V.push_ds, V.max_push_ds, V.bc, V.custom_border, V.vi_dyn, V.eds2, V.depth_clip_ctl, V.robust2, V.idx8);
}
void vk_init(void) { pthread_once(&g_once, init); }
