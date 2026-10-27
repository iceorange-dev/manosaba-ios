// The residency set's add and remove in their own autorelease pool (nvn_mtl.m res_add / res_remove, plan item 282; nxnative 7d368c4, MIT): on a
// thread with no pool of its own, as a guest thread, a texture added, removed and committed out of the set, and no longer referenced, is destroyed
// while the thread still runs (its implicit pool drains only when it exits); one never in the set as well.
// Without their pools (-DDM_TEST_RES_NO_POOL): 1 fails.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-100s expected %-4lld → got %-4lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)

static id<MTLTexture> g_give;                     // handed to the thread, which takes the only strong reference
static int g_add;                                 // the thread puts it in the set first
static dispatch_semaphore_t g_done_sem, g_go_sem;
static void *guest(void *arg) {                   // no @autoreleasepool here: a guest thread's implicit pool
    (void)arg;
    id<MTLTexture> t = g_give; g_give = nil;
    if (g_add) { res_add(t); res_commit(); }
    res_remove(t); res_commit();
    t = nil;
    dispatch_semaphore_signal(g_done_sem);
    dispatch_semaphore_wait(g_go_sem, DISPATCH_TIME_FOREVER);   // alive until the check is done
    return NULL;
}
static int destroyed_while_running(int add) {
    __weak id<MTLTexture> w;
    @autoreleasepool {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO];
        d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> t = [g_dev newTextureWithDescriptor:d]; w = t; g_give = t;
    }
    g_add = add; g_done_sem = dispatch_semaphore_create(0); g_go_sem = dispatch_semaphore_create(0);
    pthread_t th; pthread_create(&th, NULL, guest, NULL);
    dispatch_semaphore_wait(g_done_sem, DISPATCH_TIME_FOREVER);
    int gone; @autoreleasepool { gone = w == nil; }
    dispatch_semaphore_signal(g_go_sem); pthread_join(th, NULL);
    return gone;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    EXPECT("added, removed and committed on a thread with no pool: destroyed while the thread runs [-DDM_TEST_RES_NO_POOL]", 1, destroyed_while_running(1));
    EXPECT("removed (never in the set) on such a thread: destroyed while it runs", 1, destroyed_while_running(0));
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
