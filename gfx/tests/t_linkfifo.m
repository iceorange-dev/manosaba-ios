// Which queued frame a display-link tick shows (nvn_mtl.m link_take, plan item 140), on the production present queue without a display: by
// default the newest of three, the two before it counted dropped and marked done; with FIFO (mtl_link_set_fifo, game.json present_consume "fifo")
// three queued frames one a tick in order, none dropped, none marked done before its own present; at interval 2 a frame every other tick (the tick
// between shows nothing new); with the link off nothing. One frame every tick whatever its period (-DDM_TEST_FIFO_NO_PERIOD): 1 fails.
// The power-saving link (link_sync, plan item 324), the mode the production code picks without a display: with the target at 30 the 30 Hz mode
// at interval 2, the present rate's mode at interval 3 and for 3 s after, the ticks mtl_link_wait_tick would count from each mode; with the
// target back at 60 mode 1. 30 Hz whatever the interval (-DDM_TEST_LINK30_ALWAYS): 2 fail
#include "../nvn/nvn_mtl.m"
#include "../nvn/mtl/present.m"   // the presentation unit's statics, in this translation unit too
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
static uint64_t g_test_now;   // the clock of the power-saving link's checks (0: the real one)
uint64_t now_ns(void) { return g_test_now ? g_test_now : bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static void enqueue(double period, uint64_t frame) { g_pq[g_pq_in % PQ] = (pq_item){.period = period, .frame = frame}; g_pq_in++; }
static uint64_t tick(int k, uint64_t *frame) {   // the k-th 60 Hz tick from t = 1 s
    pq_item it = {0}; pthread_mutex_lock(&g_pq_mu); uint64_t t = link_take(1000000000ull + (uint64_t)k * 16666667ull, &it); pthread_mutex_unlock(&g_pq_mu);
    if (frame) *frame = t ? it.frame : 0;
    return t;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_dm_present_link = 1;
    for (int i = 0; i < 3; i++) enqueue(1 / 60.0, 100 + i);
    uint64_t f, t = tick(0, &f);
    CHECK(t == 3 && f == 102 && g_link_dropped == 2 && g_pq_done == 2, "the newest of three: ticket %llu (frame %llu), %llu dropped and marked done",
          (unsigned long long)t, (unsigned long long)f, (unsigned long long)g_link_dropped);
    mtl_link_set_fifo(1);
    for (int i = 0; i < 3; i++) enqueue(1 / 60.0, 103 + i);
    uint64_t a = tick(1, NULL), b = tick(2, NULL), c = tick(3, NULL), d = tick(4, NULL);
    CHECK(a == 4 && b == 5 && c == 6 && d == 0, "FIFO: three queued frames one a tick in order (%llu %llu %llu), then nothing new (%llu)",
          (unsigned long long)a, (unsigned long long)b, (unsigned long long)c, (unsigned long long)d);
    CHECK(g_link_dropped == 2 && g_pq_done == 2, "... none dropped, none marked done before its own present (done %llu)", (unsigned long long)g_pq_done);
    enqueue(2 / 60.0, 106); enqueue(2 / 60.0, 107);
    uint64_t e = tick(5, NULL), g = tick(6, NULL), h = tick(7, NULL);
    CHECK(e == 7 && g == 0 && h == 8, "interval 2: a frame every other tick (%llu, %llu, %llu)", (unsigned long long)e, (unsigned long long)g, (unsigned long long)h);
    enqueue(1 / 60.0, 108); g_dm_present_link = 0;
    CHECK(tick(9, NULL) == 0, "the link off: nothing taken");
    {   // the power-saving link: link_sync's mode (no display: no link object is made, the mode is what it would be made with)
        const uint64_t S = 1000000000ull;
#define STEP() nvn_link_step((unsigned)atomic_load(&g_link_itv), (unsigned)atomic_load(&g_link_hz), (unsigned)link_hz_of(g_link_mode))
        g_dm_present_link = 1; mtl_link_set_rate(60); mtl_fps_set(30);
        g_test_now = 10 * S; link_sync();
        CHECK(g_link_mode == 3, "target 30, no frame has waited for a tick: mode %d (3: the 30 Hz link)", g_link_mode);
        mtl_link_set_interval(2); g_test_now = 11 * S; link_sync();
        CHECK(g_link_mode == 3 && STEP() == 1, "interval 2 at 60 Hz: mode %d, %u tick a frame (30 fps)", g_link_mode, STEP());
        mtl_link_set_interval(3); g_test_now = 12 * S; link_sync();
        CHECK(g_link_mode == 1 && STEP() == 3, "interval 3 (20 fps): mode %d at %d Hz, %u ticks a frame (20 fps; 2 ticks of 30 Hz were 15)", g_link_mode, link_hz_of(g_link_mode), STEP());
        mtl_link_set_interval(2); g_test_now = 13 * S; link_sync();
        CHECK(g_link_mode == 1 && STEP() == 2, "interval 2 a second later: still mode %d, %u ticks a frame (30 fps)", g_link_mode, STEP());
        g_test_now = 12 * S + NVN_LINK30_BACK_NS; link_sync();
        CHECK(g_link_mode == 3 && STEP() == 1, "3 s after the last interval 3: mode %d, %u tick a frame", g_link_mode, STEP());
        mtl_link_set_rate(120); mtl_link_set_interval(4); g_test_now = 20 * S; link_sync();
        CHECK(g_link_mode == 3 && STEP() == 1, "a present rate of 120 Hz, the hold's interval 4: mode %d, %u tick a frame", g_link_mode, STEP());
        mtl_fps_set(60); mtl_link_set_interval(3); g_test_now = 21 * S; link_sync();
        CHECK(g_link_mode == 1 && STEP() == 3, "target 60 again: mode %d at %d Hz, %u ticks a frame (40 fps)", g_link_mode, link_hz_of(g_link_mode), STEP());
        g_dm_present_link = 0; g_test_now = 0;
#undef STEP
    }
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
