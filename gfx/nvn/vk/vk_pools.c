// gfx/nvn/vk/vk_pools.c: memory pools at the guest's own addresses (plan item 301e; 301a measured the routes on the K100): each pool's range becomes
// device memory mapped there by VK_EXT_map_memory_placed, with one buffer over all of it, so the GPU reads and writes what the game's CPU sees at the
// same addresses and a GPU address is a CPU address, as on Metal. The memory type is host-visible and coherent, cached when the device has such a
// type (type 6 on the 840: device-local, host-visible, coherent, cached): no flush or invalidation either way. The pool's content is copied into the
// device memory before the mapping takes the range and copied out again when the pool goes (anonymous memory put back there: the game owns the range
// again), after the GPU has finished with it.
// Other projects (2026-10-30): nx2android maps an AHardwareBuffer's dma-buf over the pool (gralloc refused large ones, so 256 MB chunks), PKShield an
// exported fd or an mremap of the driver's mapping, d2r-mobile copies what each draw reads; mamcjr's placed heap needed Mesa's driver on the Adreno
// 830, Qualcomm's has it on the 840.
#include "vk.h"
#include "../nvn_pool_find.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static vk_pool *g_pools; static int g_npool, g_cap;
static pool_find_t g_pf = {.last = -1};
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_type = UINT32_MAX; static uint64_t g_bytes;

static const VkBufferUsageFlags k_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
    VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

void mtl_pool_add(const void *mem, uint64_t size) {
    vk_init();
    uintptr_t lo = (uintptr_t)mem, hi = lo + size;
    if ((lo & 4095) || (size & 4095) || !size) VDIE("pool %p + %#llx is not whole 4 KB pages (NVN pools are)", mem, (unsigned long long)size);
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = k_usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buf; VK(vkCreateBuffer(V.dev, &bi, NULL, &buf));
    VkMemoryRequirements rq; vkGetBufferMemoryRequirements(V.dev, buf, &rq);
    if (g_type == UINT32_MAX || !(rq.memoryTypeBits >> g_type & 1)) {
        VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        g_type = vk_mem_type(rq.memoryTypeBits, want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        if (g_type == UINT32_MAX) g_type = vk_mem_type(rq.memoryTypeBits, want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
        if (g_type == UINT32_MAX) g_type = vk_mem_type(rq.memoryTypeBits, want, 0);
        if (g_type == UINT32_MAX) VDIE("no host-visible coherent memory type for pools (types %#x)", rq.memoryTypeBits);
        VLOG("pools in memory type %u (flags %#x)", g_type, V.mem.memoryTypes[g_type].propertyFlags);
    }
    // the mapping covers the whole allocation: it must be the pool's size, or it would take the guest memory after the pool
    if (rq.size != size) VDIE("pool %p + %#llx: its buffer needs %#llx bytes; the placed mapping would run past the pool", mem, (unsigned long long)size, (unsigned long long)rq.size);
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = size, .memoryTypeIndex = g_type};
    VkDeviceMemory dm; VkResult r = vkAllocateMemory(V.dev, &mai, NULL, &dm);
    if (r != VK_SUCCESS) VDIE("pool %p: %llu MB of device memory refused (%d; %llu MB in pools)", mem, (unsigned long long)(size >> 20), (int)r, (unsigned long long)(g_bytes >> 20));
    VK(vkBindBufferMemory(V.dev, buf, dm, 0));
    // the content first, then the device memory placed over the range, then the content into it
    void *keep = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (keep == MAP_FAILED) VDIE("pool %p: no room to keep its %llu bytes", mem, (unsigned long long)size);
    memcpy(keep, mem, size);
    VkMemoryMapPlacedInfoEXT pl = {VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT, .pPlacedAddress = (void *)lo};
    VkMemoryMapInfoKHR mi = {VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR, &pl, .flags = VK_MEMORY_MAP_PLACED_BIT_EXT, .memory = dm, .offset = 0, .size = VK_WHOLE_SIZE};
    void *at = NULL; r = V.map2(V.dev, &mi, &at);
    if (r != VK_SUCCESS || at != (void *)lo) VDIE("pool %p: the placed mapping failed (%d, at %p)", mem, (int)r, at);
    memcpy((void *)lo, keep, size); munmap(keep, size);
    pthread_mutex_lock(&g_mu);
    if (g_npool == g_cap) { g_cap = g_cap ? 2 * g_cap : 64; g_pools = realloc(g_pools, (size_t)g_cap * sizeof *g_pools); if (!g_pools) VDIE("pool table"); }
    g_pools[g_npool++] = (vk_pool){lo, hi, buf, dm, 1};
    pool_find_reset(&g_pf); g_bytes += size;
    pthread_mutex_unlock(&g_mu);
    static unsigned said; if (said++ < 4 || size >= (64u << 20)) VLOG("pool %p + %llu MB placed (%d pools, %llu MB)", mem, (unsigned long long)(size >> 20), g_npool, (unsigned long long)(g_bytes >> 20));
}
void mtl_pool_del(const void *mem) {
    uintptr_t lo = (uintptr_t)mem;
    pthread_mutex_lock(&g_mu);
    int i = 0; for (; i < g_npool && g_pools[i].lo != lo; i++) ;
    if (i == g_npool) { pthread_mutex_unlock(&g_mu); return; }
    vk_pool p = g_pools[i]; g_pools[i] = g_pools[--g_npool]; pool_find_reset(&g_pf); g_bytes -= p.hi - p.lo;
    pthread_mutex_unlock(&g_mu);
    uint64_t size = p.hi - p.lo;
    void *keep = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (keep == MAP_FAILED) VDIE("pool %p: no room to keep its content", mem);
    // The GPU is let finish what it may still do with it (the game finalizes a pool only once its work is done: short), then the memory is unmapped
    // and freed before anonymous memory takes the range: freeing it unmaps the range the driver reserved, and took the anonymous mapping put there
    // first with it (the first try, on the K100: the range was gone after vkFreeMemory)
    mtl_finish();
    memcpy(keep, mem, size);
    VkMemoryUnmapInfoKHR ui = {VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR, .flags = V.unmap_reserve ? VK_MEMORY_UNMAP_RESERVE_BIT_EXT : 0, .memory = p.mem};
    VK(V.unmap2(V.dev, &ui));
    vkDestroyBuffer(V.dev, p.buf, NULL); vkFreeMemory(V.dev, p.mem, NULL);
    if (mmap((void *)mem, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != mem) VDIE("pool %p: the range could not be given back (%s)", mem, strerror(errno));
    memcpy((void *)mem, keep, size); munmap(keep, size);
}
static int find_locked(uint64_t addr, uint64_t len) { return pool_find(&g_pf, g_pools, sizeof *g_pools, g_npool, addr, len ? len : 1); }
int mtl_pool_has(uint64_t addr, uint64_t len) { pthread_mutex_lock(&g_mu); int i = find_locked(addr, len); pthread_mutex_unlock(&g_mu); return i >= 0; }
int nvn_be_pool_span(uint64_t lo, uint64_t n) { return mtl_pool_has(lo, n); }
int vk_addr(uint64_t addr, uint64_t len, VkBuffer *buf, VkDeviceSize *off) {
    pthread_mutex_lock(&g_mu); int i = find_locked(addr, len);
    if (i >= 0) { *buf = g_pools[i].buf; *off = addr - g_pools[i].lo; }
    pthread_mutex_unlock(&g_mu); return i >= 0;
}
int vk_addr_span(uint64_t addr, VkBuffer *buf, VkDeviceSize *off, uint64_t *left) {
    pthread_mutex_lock(&g_mu); int i = find_locked(addr, 1);
    if (i >= 0) { *buf = g_pools[i].buf; *off = addr - g_pools[i].lo; *left = g_pools[i].hi - addr; }
    pthread_mutex_unlock(&g_mu); return i >= 0;
}
void mtl_pool_invalidate(const pool_t *p, uint64_t off, uint64_t n) { (void)p; (void)off; (void)n; }   // coherent memory: nothing to do

// The argument arena (the core's uniform snapshots and renamed updates, usnap.h): 64 MB of host-visible coherent memory in rings of submissions,
// *gpu is the offset in *buf (the core adds the same padding to both). A block is reused once its submission has completed (a wrap waits for it)
enum { ARENA = 64u << 20 };
static struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *cpu; size_t at; uint64_t fence_at[64]; } g_ar;   // fence_at[k]: the newest submission using 1 MB block k
void nvn_be_arena(size_t n, uint8_t **cpu, uint64_t *gpu, void **buf) {
    if (!g_ar.cpu) {
        vk_init();
        VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = ARENA, .usage = k_usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VK(vkCreateBuffer(V.dev, &bi, NULL, &g_ar.buf)); VkMemoryRequirements rq; vkGetBufferMemoryRequirements(V.dev, g_ar.buf, &rq);
        uint32_t t = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
        if (t == UINT32_MAX) VDIE("no memory type for the argument arena");
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rq.size, .memoryTypeIndex = t};
        VK(vkAllocateMemory(V.dev, &mai, NULL, &g_ar.mem)); VK(vkBindBufferMemory(V.dev, g_ar.buf, g_ar.mem, 0));
        VK(vkMapMemory(V.dev, g_ar.mem, 0, VK_WHOLE_SIZE, 0, (void **)&g_ar.cpu));
    }
    n = (n + 255) & ~(size_t)255;
    if (n > ARENA / 2) VDIE("an arena allocation of %zu bytes", n);
    if (g_ar.at + n > ARENA) g_ar.at = 0;
    for (size_t k = g_ar.at >> 20; k <= (g_ar.at + n - 1) >> 20; k++) {   // blocks a submission still on the GPU uses: wait for it
        if (g_ar.fence_at[k] && g_ar.fence_at[k] != vk_cur_fence() && !mtl_wait(g_ar.fence_at[k], 0)) mtl_wait(g_ar.fence_at[k], UINT64_MAX);
        g_ar.fence_at[k] = vk_cur_fence();
    }
    *cpu = g_ar.cpu + g_ar.at; *gpu = g_ar.at; *buf = (void *)(uintptr_t)g_ar.buf; g_ar.at += n;
}
