// Memory pool range checks and the GPU memory budget (nvn_mtl.m pool_range_ok / gpu_budget_exceeded; MacNativeGames, MIT): a pool whose range has an
// unmapped hole, pages that cannot be read, read-only pages the dirty tracker did not protect, or more than POOL_BUFFER_MAX bytes gets no zero-copy
// buffer; read-only pages the dirty tracker protected are fine. The budget compares the GPU allocation with DM_NVN_GPU_BUDGET_MB. Plan item 279:
// over the budget, a retired buffer a running submission holds is released once it completes and the allocation is back under (gpu_budget_settle);
// with nothing running it gives up after 20 ms of quiet; a zero-copy buffer Metal refuses three times is made on the fourth attempt.
// Stopping at once (-DDM_TEST_BUDGET_NO_WAIT): 3 fail. No retry (-DDM_TEST_POOL_NO_RETRY): the run stops at the refused buffer.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-84s expected %-6lld → got %-6lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
static uint8_t *pages(size_t n) { void *p = mmap(NULL, n * 16384, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); if (p == MAP_FAILED) abort(); memset(p, 1, n * 16384); return p; }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_GPU_BUDGET_MB", "1", 1); mtl_init();
    uint64_t r0 = g_pool_refused;
    uint8_t *ok = pages(4); mtl_pool_add(ok, 4 * 16384);
    EXPECT("an ordinary read-write range: registered", 1, mtl_pool_has((uint64_t)(uintptr_t)ok, 64));
    uint8_t *hole = pages(3); munmap(hole + 16384, 16384); mtl_pool_add(hole, 3 * 16384);
    EXPECT("a range with an unmapped page in it: refused", 0, mtl_pool_has((uint64_t)(uintptr_t)hole, 64));
    uint8_t *ro = pages(2); mprotect(ro, 2 * 16384, PROT_READ); mtl_pool_add(ro, 2 * 16384);
    EXPECT("read-only pages nothing tracks: refused", 0, mtl_pool_has((uint64_t)(uintptr_t)ro, 64));
    uint8_t *none = pages(2); mprotect(none + 16384, 16384, PROT_NONE); mtl_pool_add(none, 2 * 16384);
    EXPECT("a page without access: refused", 0, mtl_pool_has((uint64_t)(uintptr_t)none, 64));
    uint8_t *tr = pages(4); nvn_dirty_add_range(tr, 4 * 16384); nvn_dirty_protect(tr + 16384, 2 * 16384); mtl_pool_add(tr + 16384, 2 * 16384);
    EXPECT("read-only pages the dirty tracker protected: registered", 1, mtl_pool_has((uint64_t)(uintptr_t)(tr + 16384), 64));
    size_t big = (POOL_BUFFER_MAX + (64u << 20)); void *bg = mmap(NULL, big, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); if (bg == MAP_FAILED) abort();
    mtl_pool_add(bg, big);
    EXPECT("a range above POOL_BUFFER_MAX: refused", 0, mtl_pool_has((uint64_t)(uintptr_t)bg, 64));
    EXPECT("four pools refused in all", 4, (long long)(g_pool_refused - r0));
    id<MTLBuffer> eight = [g_dev newBufferWithLength:8u << 20 options:MTLResourceStorageModePrivate]; (void)eight;   // something allocated on the GPU
    uint64_t cur, budget; int over = gpu_budget_exceeded(&cur, &budget);
    EXPECT("budget 1 MB (DM_NVN_GPU_BUDGET_MB): the allocation is above it", 1, over && budget == (1u << 20) && cur > budget);
    // 279: a retired 64 MB buffer that a running submission holds, completing 40 ms from now
    uint64_t base = g_dev.currentAllocatedSize, bud = base + (16u << 20), f = g_fence + 1;
    @autoreleasepool { id<MTLBuffer> big = [g_dev newBufferWithLength:64u << 20 options:MTLResourceStorageModePrivate]; defer_obj_release(big); }
    uint64_t with = g_dev.currentAllocatedSize; atomic_store(&g_inflight, 1);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 40 * NSEC_PER_MSEC), dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
        pthread_mutex_lock(&g_mu); if (g_done < f) g_done = f; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_mu); atomic_store(&g_inflight, 0); });
    double ms; int back = gpu_budget_settle(bud, &cur, &ms);
    EXPECT("over the budget by a retired buffer a running submission holds: under it once that completes [-DDM_TEST_BUDGET_NO_WAIT]", 1, with > bud && back && cur <= bud);
    EXPECT("... after waiting for it (30 ms to 2 s)", 1, ms >= 30 && ms < 2000);
    back = gpu_budget_settle(1, &cur, &ms);
    EXPECT("over the budget with nothing running: still over, given up after 20 ms of quiet (not 2 s)", 1, !back && ms >= 20 && ms < 500);
    g_test_nocopy_fail = 3; uint8_t *rf = pages(2); mtl_pool_add(rf, 2 * 16384);
    EXPECT("a zero-copy buffer refused three times: the pool registered on the fourth attempt [-DDM_TEST_POOL_NO_RETRY]", 1, mtl_pool_has((uint64_t)(uintptr_t)rf, 64) && !g_test_nocopy_fail);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
