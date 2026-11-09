// Short sleeps (hle_thread.c sleep_precise, plan item 316b): SleepThread(20 µs) and SleepThread(5 µs) sleep at least what they ask (the shortest of
// 20 calls each; they returned at once, Tears of the Kingdom calling the 20 µs one about 100,000 times a second as a busy loop), and a 200 µs one
// within 20 µs of its deadline or past it, as before (sleep_precise stops there; batch 162 read 190 µs under load). Returning at once within 20 µs of the deadline, as before (-DDM_TEST_SLEEP_SHORT_SKIP): 2 fail.
#include "../dm.h"
#include "../game.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <mach/mach_time.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
uint64_t dm_core_migrations(void);
void (*dm_test_cv_after_release)(void);
struct dm_flags g_flag;
const dm_game_profile dm_game = { .id = "test" };
pthread_key_t g_key18, g_keytls;
uint64_t g_t0_ns;
void crash_thread_init(void) {}
void guest_thread_init(void) {}
void dm_thread_enter(uint64_t entry, uint64_t arg, uint64_t sp, uint64_t *top) { (void)entry; (void)arg; (void)sp; (void)top; abort(); }
const uint64_t *dm_hstack_record(uint64_t tsd, uint64_t top) { (void)tsd; (void)top; return NULL; }
void *guest_sym(const char *name) { (void)name; return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) { (void)id; (void)ns; (void)bytes; }
uint64_t now_ns(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return mach_absolute_time() * tb.numer / tb.denom; }
uint64_t guest_ns(void) { return now_ns(); }
int dm_detclock_enabled(void) { return 0; }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static uint64_t shortest(uint64_t ns) {   // the shortest of 20 SleepThread(ns) calls, ns
    uint64_t best = ~0ull;
    for (int i = 0; i < 20; i++) { uint64_t r[32] = {0}; r[0] = ns; uint64_t a = now_ns(); svc_thread(0x0B, r); uint64_t d = now_ns() - a; if (d < best) best = d; }
    return best;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); svc_thread_main_init();
    uint64_t s20 = shortest(20000), s5 = shortest(5000), s200 = shortest(200000);
    CHECK(s20 >= 20000, "SleepThread(20 µs) sleeps at least 20 µs (shortest %.1f µs) [-DDM_TEST_SLEEP_SHORT_SKIP: returns at once]", s20 / 1e3);
    CHECK(s5 >= 5000, "SleepThread(5 µs) sleeps at least 5 µs (shortest %.1f µs) [-DDM_TEST_SLEEP_SHORT_SKIP]", s5 / 1e3);
    CHECK(s200 >= 180000, "SleepThread(200 µs) within 20 µs of it or past, as before (shortest %.1f µs)", s200 / 1e3);
    printf("t_sleepshort: %d checks, %d failed\n", n, fail); return fail != 0;
}
