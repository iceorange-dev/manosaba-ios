// The dirty-page stamps against writes released around an upload (gfx/nvn/nvn_dirty.c; seedds/nxnative 5b0c11a and MacNativeGames 873edff, MIT):
// 1. a stamp taken before its range existed (the pool finalized, a new one on the same memory) counts as written, whatever the new range's
//    generations say [-DDM_TEST_DIRTY_NO_BORN: 1 fails];
// 2. nvn_dirty_protect_stamp: a write that faults right after the protection and is released before the caller goes on is newer than the stamp
//    [-DDM_TEST_DIRTY_STAMP_AFTER: 1 fails];
// 3. a page another thread is releasing (its generation taken, not yet published) when the protection comes is waited for and protected, so a
//    write after the upload still shows [-DDM_TEST_DIRTY_SKIP_BUSY: 1 fails].
// The interleavings are staged at the two hook points nvn_dirty.c leaves empty by default.
static void after_clock(void); static void after_protect(void);
#define DM_DIRTY_AFTER_CLOCK() after_clock()
#define DM_DIRTY_AFTER_PROTECT() after_protect()
#include "../nvn/nvn_dirty.c"
#include <stdio.h>

module_t g_mod[MAX_MOD];
int g_nmod;
module_t *module_of(const void *p) { (void)p; return NULL; }
void crash_install(void);

static int g_fail;
#define EXPECT(name, want, got) do { \
    long long want_ = (long long)(want), got_ = (long long)(got); \
    printf("%-78s expected %-4lld → got %-4lld %s\n", name, want_, got_, want_ == got_ ? "✓" : "✗"); \
    if (want_ != got_) g_fail++; \
} while (0)

static uint8_t *g_page; static size_t g_pg;
static _Atomic int g_pause_clock, g_fault_after_protect;
static void after_clock(void) { if (atomic_exchange(&g_pause_clock, 0)) usleep(150000); }   // the release's generation taken, the page still BUSY
static void *writer(void *a) { ((volatile uint8_t *)g_page)[(uintptr_t)a] = 0x5a; return NULL; }   // a game thread's store: it faults while the page is protected
static void after_protect(void) {   // a write faults on the page just protected and is released before the caller reads on
    if (!atomic_exchange(&g_fault_after_protect, 0)) return;
    pthread_t w; pthread_create(&w, NULL, writer, (void *)(uintptr_t)8); pthread_join(w, NULL);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); crash_install();
    g_pg = (size_t)getpagesize();
    g_page = mmap(NULL, 4 * g_pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_page == MAP_FAILED) { perror("mmap"); return 2; }

    // 1. a stamp from before the range
    nvn_dirty_add_range(g_page, 4 * g_pg);
    uint64_t old = nvn_dirty_protect_stamp(g_page, g_pg);
    nvn_dirty_remove_range(g_page); nvn_dirty_add_range(g_page, 4 * g_pg);   // the pool finalized, another made on its memory
    EXPECT("1. a stamp taken before its range was registered: written [-DDM_TEST_DIRTY_NO_BORN]", 1, nvn_dirty_since(g_page, 64, old));
    uint64_t fresh = nvn_dirty_protect_stamp(g_page, g_pg);
    EXPECT("... a stamp of the new range, nothing written since: clean", 0, nvn_dirty_since(g_page, 64, fresh));

    // 2. a write released right after the protection
    uint8_t *p2 = g_page + g_pg;
    atomic_store(&g_fault_after_protect, 1); g_page = p2;
    uint64_t s2 = nvn_dirty_protect_stamp(p2, g_pg);
    EXPECT("2. the write that faulted right after the protection: written since the stamp [-DDM_TEST_DIRTY_STAMP_AFTER]", 1, nvn_dirty_since(p2, 64, s2));
    EXPECT("... and it landed", 0x5a, p2[8]);

    // 3. a page being released when the protection comes
    uint8_t *p3 = g_page = p2 + g_pg;
    nvn_dirty_protect(p3, g_pg);
    atomic_store(&g_pause_clock, 1);
    pthread_t b; pthread_create(&b, NULL, writer, (void *)(uintptr_t)16);   // B: faults, its release pauses after taking its generation
    usleep(50000);                                                            // B is in that window: generation taken, page BUSY
    uint64_t s3 = nvn_dirty_protect_stamp(p3, g_pg);                          // an upload's stamp and protection now
    pthread_join(b, NULL);
    EXPECT("3. B's write landed", 0x5a, p3[16]);
    ((volatile uint8_t *)p3)[32] = 0x77;                                      // the CPU writes the page after the upload
    EXPECT("... a write after the upload: written since its stamp [-DDM_TEST_DIRTY_SKIP_BUSY]", 1, nvn_dirty_since(p3, 64, s3));
    printf("t_dirty_stamp: %s\n", g_fail ? "failed" : "all passed"); return g_fail != 0;
}
