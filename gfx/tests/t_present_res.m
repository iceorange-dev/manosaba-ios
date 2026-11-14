// The present's command buffer is committed with the residency set committed (nvn_mtl.m present_now): the source texture of the first present is
// made by present_now itself (tex_mtl → res_add), and the set was committed only by the next submission, after the present had run (MacNativeGames
// PR #30: a GPU page fault in 4 of 14 boots). A real present into an offscreen layer; the set must hold no uncommitted change once present_now has
// returned, and the GPU must finish it without an error. Without the commit (-DDM_TEST_PRESENT_NO_RESCOMMIT) the first check fails.
#include "../nvn/nvn_mtl.m"
#include "../nvn/mtl/present.m"   // the presentation unit's statics, in this translation unit too
static CAMetalLayer *g_layer;
void *front_metal_layer(void) { return (__bridge void *)g_layer; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-78s expected %-4lld → got %-4lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    g_layer = [CAMetalLayer layer]; g_layer.device = g_dev; g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm; g_layer.drawableSize = CGSizeMake(64, 64);
    uint8_t *mem; if (posix_memalign((void **)&mem, getpagesize(), 1 << 16)) abort();
    for (int i = 0; i < 1 << 16; i++) mem[i] = (uint8_t)i;
    mtl_pool_add(mem, 1 << 16);
    tex_t t = {.serial = 4242}; t.d.width = t.d.height = 16; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.mem = mem; mtl_tex_live(&t);
    res_commit();
    EXPECT("before: no uncommitted residency change", 0, g_res_dirty);
    EXPECT("the source texture does not exist yet", 1, serial_get(t.serial) == nil);
    int ok = present_now(&t, 0, 1, 1, NULL, nil, 0);
    EXPECT("present_now presented", 1, ok);
    EXPECT("the source texture was made by the present", 1, serial_get(t.serial) != nil);
    EXPECT("after: the residency set committed before the commit (no change left)", 0, g_res_dirty);
    pthread_mutex_lock(&g_pq_mu); uint64_t t0 = bp_now();
    while (g_pq_done < 1 && bp_now() - t0 < 5000000000ull) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 10000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; } pthread_cond_timedwait(&g_pq_cv, &g_pq_mu, &ts); }
    uint64_t done = g_pq_done; pthread_mutex_unlock(&g_pq_mu);
    EXPECT("the GPU finished the present", 1, (long long)done);
    printf("PRESENT_RES %s %d/%d\n", fail ? "FAIL" : "OK", count - fail, count);
    return fail != 0;
} }
