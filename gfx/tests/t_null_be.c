// gfx/nvn/null/null.c, the null backend (plan item 301c): a submission completes when it is committed; its event writes (set and add) and counter
// reports land then and not before (a timestamp report's counter half at once, its timestamp half from the guest clock at commit; samples passed
// {0, timestamp}); a reset drops what the replay had queued; the fence and its waits; the pool registry and the arena. No screen.
// Each landing when the replay reaches it (-DDM_TEST_NULL_EARLY, the broken variant): 4 fail.
#include "../nvn/nvn_backend.h"
#include "../nvn/nvn_mtl.h"
#include "../nvn/nvn_obj.h"
#include "../nvn/core/usnap.h"
#include "core_test.h"
#include <stdlib.h>
#include <string.h>

uint64_t guest_ns(void) { return 625000; }   // the null backend's clock: 12000 ticks of 19.2 MHz
static const uint64_t TICKS = 12000;
static void report(uint64_t type, void *dst) { uint64_t a[2] = {type, (uint64_t)(uintptr_t)dst}; nvn_be_report_counter(NULL, a); }

int main(void) {
    check("before any submission: done fence 0", 0, (long long)mtl_done_fence());
    check("this submission commits with fence 1", 1, (long long)nvn_be_sub_fence());
    volatile uint32_t ev = 5, ev2 = 5; uint64_t ts[2], sp[2]; memset(ts, 0xff, sizeof ts); memset(sp, 0xff, sizeof sp);
    nvn_be_signal_event(&ev, 0, 7); nvn_be_signal_event(&ev2, 1, 3); report(NVN_COUNTER_TYPE_TIMESTAMP, ts); report(NVN_COUNTER_TYPE_SAMPLES_PASSED, sp);
    report(NVN_COUNTER_TYPE_TIMESTAMP, NULL);   // no destination: nothing written
    check("an event write waits for the commit (still 5)", 5, ev);
    check_x("a timestamp report: its counter half 0 at once, its timestamp half not yet", 0xffffffffffffffffull, (long long)ts[1]);
    check_x("  (the counter half)", 0, (long long)ts[0]);
    check_x("a samples-passed report waits for the commit", 0xffffffffffffffffull, (long long)sp[0]);
    check("commit: fence 1", 1, (long long)mtl_sub_commit());
    check("the event set to 7", 7, ev);
    check("the other added 3 (8)", 8, ev2);
    check("the timestamp half: the guest clock's ticks", (long long)TICKS, (long long)ts[1]);
    check("samples passed {0, ticks}", 1, sp[0] == 0 && sp[1] == TICKS);
    check("done and last fence 1", 1, mtl_done_fence() == 1 && mtl_last_fence() == 1 && nvn_be_done_fence() == 1);
    check("a wait on fence 1 holds at once, on fence 2 not", 1, mtl_wait(1, 0) && !mtl_wait(2, 0));
    ev = 1; nvn_be_signal_event(&ev, 0, 9); nvn_be_reset();
    check("a reset drops what was queued: commit 2 leaves the event", 1, mtl_sub_commit() == 2 && ev == 1);
    uint8_t *mem = malloc(4096); mtl_pool_add(mem, 4096);
    check("a range inside a registered pool is in it", 1, mtl_pool_has((uint64_t)(uintptr_t)mem + 100, 100) && nvn_be_pool_span((uint64_t)(uintptr_t)mem, 4096));
    check("a range past its end is not", 0, mtl_pool_has((uint64_t)(uintptr_t)mem + 4000, 200));
    mtl_pool_del(mem); check("after the pool's removal nothing is", 0, mtl_pool_has((uint64_t)(uintptr_t)mem + 100, 100));
    uint8_t *c1, *c2; uint64_t g1, g2; void *b1, *b2; nvn_be_arena(100, &c1, &g1, &b1); nvn_be_arena(1, &c2, &g2, &b2);
    check("arena: CPU address = GPU address, 256-byte steps", 1, c1 && g1 == (uint64_t)(uintptr_t)c1 && c2 == c1 + 256 && !((uintptr_t)c1 & 255));
    memset(c1, 0x5a, 100); check("  and writable", 0x5a, c1[99]);
    check("no screen, no display link, no pending present", 1, !mtl_display_hz() && !mtl_has_window() && !mtl_link_active() && !mtl_present(NULL, 1 / 60.0, NULL));
    printf("t_null_be: %d checks, %d failed\n", n, fail); return fail != 0;
}
