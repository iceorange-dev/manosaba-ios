// A guest thread's host handle and priority published under the scheduler lock (hle_thread.c, plan item 296; seedds/nxnative ce99604, MIT):
// CreateThread puts the new thread in the registry (which thr_name_of and the samplers walk under G, reading t->pt) only with its pthread handle
// already there; SetThreadPriority and GetThreadPriority run under G, which the scheduler's priority comparisons hold. Each checked at a hook
// inside the svc: a thread in the registry without its handle (-DDM_TEST_THREAD_LINK_EARLY, linked before pthread_create as before) fails 1; a
// priority set without G (-DDM_TEST_PRIO_UNLOCKED) fails 1.
#include "../dm.h"
#include <stdio.h>
#include <string.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
int dm_test_newest_has_pt(void); int dm_test_g_held(void);
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
extern void (*dm_test_thread_linked)(void), (*dm_test_prio_set)(void);   // (hle_thread.c's hooks)
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static int g_linked_with_pt = -1, g_prio_under_g = -1;
static void on_linked(void) { g_linked_with_pt = dm_test_newest_has_pt(); }
static void on_prio(void) { g_prio_under_g = dm_test_g_held(); }
static uint8_t g_stack[64 << 10] __attribute__((aligned(16)));
int main(void) {
    uint32_t h = svc_thread_main_init();
    dm_test_thread_linked = on_linked; dm_test_prio_set = on_prio;
    uint64_t r[32] = {0}; r[1] = 0x1000; r[2] = 0; r[3] = (uint64_t)(uintptr_t)(g_stack + sizeof g_stack); r[4] = 44; r[5] = 0;
    svc_thread(0x08, r);   // CreateThread (never started: it waits)
    CHECK(r[0] == 0 && g_linked_with_pt == 1, "CreateThread: the new thread is in the registry only with its pthread handle (result %#llx, handle there %d)", (unsigned long long)r[0], g_linked_with_pt);
    memset(r, 0, sizeof r); r[0] = h; r[1] = 50; svc_thread(0x0D, r);
    CHECK(r[0] == 0 && g_prio_under_g == 1, "SetThreadPriority sets it holding the scheduler lock (held %d)", g_prio_under_g);
    memset(r, 0, sizeof r); r[1] = h; svc_thread(0x0C, r);
    CHECK(r[0] == 0 && r[1] == 50, "GetThreadPriority reads it back (%llu)", (unsigned long long)r[1]);
    printf("t_thrpub: %d checks, %d failed\n", n, fail); return fail != 0;
}
