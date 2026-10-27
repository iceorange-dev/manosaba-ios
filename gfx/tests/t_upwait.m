// Waiting for upload batches (up_wait_zero / up_notify): a waiter returns within 0.1 ms (the median of 30 rounds) of the counter reaching zero and
// the completion handler's notify; the usleep(1000) polling it replaces returned up to a millisecond late (-DDM_TEST_UPWAIT_POLL: fails 1). The
// median, not the mean: under the batch's load a round or two preempted for a millisecond put the mean over 0.1 ms once (batch 120: 0.180 ms on
// average, the worst 0.937 ms; it passed run alone). A waiter on a counter that is already zero returns at once.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static _Atomic int g_ctr; static _Atomic uint64_t g_cleared;
static uint64_t mono(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static void *completer(void *a) { (void)a; usleep(5000); atomic_store(&g_cleared, mono()); atomic_store(&g_ctr, 0); up_notify(); return NULL; }
int main(void) { @autoreleasepool {
    enum { R = 30 }; double d[R], sum = 0;
    for (int i = 0; i < R; i++) {
        atomic_store(&g_ctr, 1); pthread_t t; pthread_create(&t, NULL, completer, NULL);
        up_wait_zero(&g_ctr); d[i] = (double)(mono() - atomic_load(&g_cleared)); pthread_join(t, NULL);
        sum += d[i];
    }
    for (int i = 1; i < R; i++) { double x = d[i]; int j = i - 1; while (j >= 0 && d[j] > x) { d[j + 1] = d[j]; j--; } d[j + 1] = x; }
    double med = (d[R / 2 - 1] + d[R / 2]) / 2;
    CHECK(med < 100000, "a waiter returns %.3f ms after the completion, the median of %d rounds (under 0.1 ms; mean %.3f, worst %.3f ms) [-DDM_TEST_UPWAIT_POLL]",
          med / 1e6, R, sum / R / 1e6, d[R - 1] / 1e6);
    uint64_t t0 = mono(); atomic_store(&g_ctr, 0); up_wait_zero(&g_ctr);
    CHECK(mono() - t0 < 1000000, "a counter already at zero: returns at once (%.3f ms)", (mono() - t0) / 1e6);
    printf("t_upwait: %d checks, %d failed\n", n, fail); return fail != 0;
} }
