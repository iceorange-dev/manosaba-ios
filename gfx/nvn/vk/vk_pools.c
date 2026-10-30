// gfx/nvn/vk/vk_pools.c: memory pools at the guest's own addresses (plan item 301e; 301a measured the routes on the K100): each pool's range becomes
// device memory mapped there by VK_EXT_map_memory_placed, with one buffer over all of it, so the GPU reads and writes what the game's CPU sees at the
// same addresses and a GPU address is a CPU address, as on Metal. The memory type is host-visible and coherent, cached when the device has such a
// type (type 6 on the 840: device-local, host-visible, coherent, cached): no flush or invalidation either way. The pool's content is copied into the
// device memory before the mapping takes the range and copied out again when the pool goes (anonymous memory put back there: the game owns the range
// again), after the GPU has finished with it.
// Unity makes a pool for each texture (16-34 MB for a character's picture with its mips): in manosaba's first trial about one is made and one goes at
// each line of text, and copying them both ways (and waiting for the GPU to go idle) made those frames 40-100 ms (plan item 301f). Now a page that
// /proc/self/pagemap shows neither present nor swapped out (never written since it was mapped: the pools there were all such) is not copied in, as
// new device memory reads zero, and a pool going gives its content back by one copy and an mremap. It still waits for the GPU to go idle first:
// freed while another submission ran, a 64 KB pool's range kept only its first 16 KB once that submission was done, in 2 of 18 test runs (batch
// 135's; not seen again in 300 tries, the cause not found), so the memory is freed with the GPU idle, as before.
// Other projects (2026-10-30): nx2android maps an AHardwareBuffer's dma-buf over the pool (gralloc refused large ones, so 256 MB chunks), PKShield an
// exported fd or an mremap of the driver's mapping, d2r-mobile copies what each draw reads; mamcjr's placed heap needed Mesa's driver on the Adreno
// 830, Qualcomm's has it on the 840.
#include "vk.h"
#include "../nvn_pool_find.h"
#include "../core/core.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static vk_pool *g_pools; static int g_npool, g_cap;
static pool_find_t g_pf = {.last = -1};
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_type = UINT32_MAX; static uint64_t g_bytes;
static struct { uint64_t add, add_b, copied_b, del, del_b, wait_ns, copy_ns; } g_ps;   // pools made and gone, bytes, bytes copied in, time (under g_mu)
// New device memory reads zero on the 840 (the kernel's pages: 168 pools of 2-34 MB in manosaba's first trial, every page). It is checked on the first
// pools with unwritten pages before it is relied on; memory that does not read zero has those pages cleared instead
static _Atomic int g_zero_checks = 4, g_zero_bad;
static _Atomic uint64_t g_copied_all;   // bytes copied into pools, all along (t_vk_be)
uint64_t vk_pool_copied(void) { return atomic_load(&g_copied_all); }

// Which pages of [lo, lo + size) have been written: present or swapped out in /proc/self/pagemap (bits 63 and 62); one neither reads zero (never
// written since it was mapped, or discarded). 0 when the map cannot be read: every page counts as written
static int pages_written(uintptr_t lo, uint64_t size, uint8_t **out, size_t *np, size_t *pg) {
    long ps = sysconf(_SC_PAGESIZE); *pg = (size_t)ps; *np = (size_t)(size / (uint64_t)ps); *out = NULL;
    if (ps <= 0 || lo % (uintptr_t)ps || size % (uint64_t)ps) return 0;
    int fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC); if (fd < 0) return 0;
    uint64_t *pm = malloc(*np * 8); uint8_t *w = malloc(*np);
    int ok = pm && w && pread(fd, pm, *np * 8, (off_t)(lo / (uintptr_t)ps * 8)) == (ssize_t)(*np * 8); close(fd);
    if (ok) for (size_t k = 0; k < *np; k++) w[k] = (pm[k] >> 62 & 3) != 0;
#ifdef DM_TEST_VK_POOL_ALL_UNWRITTEN   // negative control of t_vk_be only: every page taken as never written
    if (ok) memset(w, 0, *np);
#endif
    free(pm); if (!ok) { free(w); return 0; }
    *out = w; return 1;
}
// The next run of pages from *k that are written (want 1) or not (want 0): its byte offset and length, 0 at the end
static size_t page_run(const uint8_t *w, size_t np, size_t pg, int want, size_t *k, size_t *off) {
    while (*k < np && w[*k] != want) (*k)++;
    size_t a = *k; while (*k < np && w[*k] == want) (*k)++;
    *off = a * pg; return (*k - a) * pg;
}

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
    // the written pages' content first, then the device memory placed over the range, then that content into it
    uint64_t t0 = core_now_ns(), copied = 0; uint8_t *wr; size_t np, pg, k, off, n;
    int known = pages_written(lo, size, &wr, &np, &pg);
    void *keep = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (keep == MAP_FAILED) VDIE("pool %p: no room to keep its %llu bytes", mem, (unsigned long long)size);
    if (!known) { memcpy(keep, mem, size); copied = size; }
    else for (k = 0; (n = page_run(wr, np, pg, 1, &k, &off));) { memcpy((uint8_t *)keep + off, (const uint8_t *)mem + off, n); copied += n; }
    VkMemoryMapPlacedInfoEXT pl = {VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT, .pPlacedAddress = (void *)lo};
    VkMemoryMapInfoKHR mi = {VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR, &pl, .flags = VK_MEMORY_MAP_PLACED_BIT_EXT, .memory = dm, .offset = 0, .size = VK_WHOLE_SIZE};
    void *at = NULL; r = V.map2(V.dev, &mi, &at);
    if (r != VK_SUCCESS || at != (void *)lo) VDIE("pool %p: the placed mapping failed (%d, at %p)", mem, (int)r, at);
    if (!known) memcpy((void *)lo, keep, size);
    else {
        for (k = 0; (n = page_run(wr, np, pg, 1, &k, &off));) memcpy((uint8_t *)lo + off, (const uint8_t *)keep + off, n);
        int check = atomic_load(&g_zero_checks) > 0, bad = 0, any = 0;
        for (k = 0; (n = page_run(wr, np, pg, 0, &k, &off));) {   // pages never written: zero in new device memory
            any = 1; const uint64_t *q = (const uint64_t *)(lo + off);
            if (check && !atomic_load(&g_zero_bad)) for (size_t w = 0; w < n / 8; w++) if (q[w]) { bad = 1; break; }
            if (bad || atomic_load(&g_zero_bad)) { atomic_store(&g_zero_bad, 1); memset((uint8_t *)lo + off, 0, n); }
        }
        if (check && any && !atomic_load(&g_zero_bad) && atomic_fetch_sub(&g_zero_checks, 1) == 1) VLOG("new device memory reads zero (4 pools checked): pages never written are not copied into pools");
        if (bad) VRED("new device memory does not read zero: pages never written are cleared in pools from now on");
        free(wr);
    }
    munmap(keep, size);
    pthread_mutex_lock(&g_mu);
    if (g_npool == g_cap) { g_cap = g_cap ? 2 * g_cap : 64; g_pools = realloc(g_pools, (size_t)g_cap * sizeof *g_pools); if (!g_pools) VDIE("pool table"); }
    g_pools[g_npool++] = (vk_pool){lo, hi, buf, dm, 1};
    pool_find_reset(&g_pf); g_bytes += size;
    g_ps.add++; g_ps.add_b += size; g_ps.copied_b += copied; g_ps.copy_ns += core_now_ns() - t0; atomic_fetch_add(&g_copied_all, copied);
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
    uint64_t size = p.hi - p.lo, t0 = core_now_ns();
    void *keep = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (keep == MAP_FAILED) VDIE("pool %p: no room to keep its content", mem);
    // The GPU is let finish what it may still do with it (the game finalizes a pool only once its work is done: short), then the memory is unmapped
    // and freed before anonymous memory takes the range: freeing it unmaps the range the driver reserved, and took the anonymous mapping put there
    // first with it (the first try, on the K100: the range was gone after vkFreeMemory). The copy kept is moved there (mremap: no second copy)
    mtl_finish();
    uint64_t t1 = core_now_ns();
    memcpy(keep, mem, size);
    VkMemoryUnmapInfoKHR ui = {VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR, .flags = V.unmap_reserve ? VK_MEMORY_UNMAP_RESERVE_BIT_EXT : 0, .memory = p.mem};
    VK(V.unmap2(V.dev, &ui));
    vkDestroyBuffer(V.dev, p.buf, NULL); vkFreeMemory(V.dev, p.mem, NULL);
    if (mremap(keep, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, (void *)mem) != mem) VDIE("pool %p: the range could not be given back (%s)", mem, strerror(errno));
    uint64_t t2 = core_now_ns();
    pthread_mutex_lock(&g_mu); g_ps.del++; g_ps.del_b += size; g_ps.wait_ns += t1 - t0; g_ps.copy_ns += t2 - t1; pthread_mutex_unlock(&g_mu);
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
void vk_pool_line(uint64_t presents) {   // the stats line's: pools made and gone since the last
    pthread_mutex_lock(&g_mu); __typeof__(g_ps) s = g_ps; memset(&g_ps, 0, sizeof g_ps); pthread_mutex_unlock(&g_mu);
    if (!s.add && !s.del) return;
    VLOG("memory pools over the last %llu presents: %llu made (%.0f MB, %.0f MB of it written and copied in), %llu gone (%.0f MB); %.1f ms waiting for "
         "the GPU, %.1f ms copying", (unsigned long long)presents, (unsigned long long)s.add, s.add_b / 1048576.0, s.copied_b / 1048576.0,
         (unsigned long long)s.del, s.del_b / 1048576.0, s.wait_ns / 1e6, s.copy_ns / 1e6);
}
void mtl_pool_invalidate(const pool_t *p, uint64_t off, uint64_t n) { (void)p; (void)off; (void)n; }   // coherent memory: nothing to do

// The argument arena (the core's uniform snapshots and renamed updates, usnap.h): 64 MB of host-visible coherent memory in rings of submissions,
// *gpu is the offset in *buf (the core adds the same padding to both). An allocation entering a 1 MB block waits for the newest submission that
// used the block on the ring's last lap (a wrap); the rest of the block the last allocation ended in is free whoever used its start (batch 133:
// that wait made each submission's first allocation wait for the one before it to finish on the GPU, its uniform snapshot 4-10 ms in the trial)
enum { ARENA = 64u << 20 };
static struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *cpu; size_t at; uint64_t fence_at[64]; } g_ar;   // fence_at[k]: the newest submission using 1 MB block k
uint64_t vk_arena_size(void) { return ARENA; }
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
    size_t open = g_ar.at & ((1u << 20) - 1) ? g_ar.at >> 20 : SIZE_MAX;   // the block the last allocation ended in (none at a block's start)
    for (size_t k = g_ar.at >> 20; k <= (g_ar.at + n - 1) >> 20; k++) {
#ifndef DM_TEST_VK_ARENA_WAIT_OPEN   // negative control of t_vk_be only: the open block waited for as well (as before batch 133)
        int enter = k != open;
#else
        int enter = 1; (void)open;
#endif
        if (enter && g_ar.fence_at[k] && g_ar.fence_at[k] != vk_cur_fence() && !mtl_wait(g_ar.fence_at[k], 0)) mtl_wait(g_ar.fence_at[k], UINT64_MAX);
        g_ar.fence_at[k] = vk_cur_fence();
    }
    *cpu = g_ar.cpu + g_ar.at; *gpu = g_ar.at; *buf = (void *)(uintptr_t)g_ar.buf; g_ar.at += n;
}
