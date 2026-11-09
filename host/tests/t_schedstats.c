// The scheduling statistics of DM_SCHED_STATS (host/schedstats.h, plan item 255): two lock waits (woken 0.2 and 0.6 ms before they ran, 1 ms of core
// wait in one) and a timed-out condition wait over one second give their rate, time blocked, wake-to-run latency (mean 0.4 ms, max 0.6) and core
// time; the system calls a second come out in order. The latency counted from the wait's start (-DDM_TEST_SCHED_WAKE_FROM_START): 1 fails.
// Per thread (plan item 315f): a thread on core 0 only that never asks for its core and one that asked once count among those that would hold a core,
// one free on cores 0-2 that never asked among the others; the waits a second and the longest first. A thread on one core only counted as free
// (-DDM_TEST_SCHED_THR_ONE_CORE_FREE): 1 fails.
#include <string.h>
#include "../schedstats.h"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-90s %s\n", what, ok ? "✓" : "✗"); }
int main(void) {
    static sched_stats s; s.t0 = 1000000000ull; const uint64_t ms = 1000000ull;
    sched_note(&s, 1, s.t0, s.t0 + 4 * ms + 800000, s.t0 + 5 * ms, s.t0 + 5 * ms);              // a lock wait: woken 0.2 ms before it ran
    sched_note(&s, 1, s.t0 + 10 * ms, s.t0 + 11 * ms + 400000, s.t0 + 12 * ms, s.t0 + 13 * ms); // woken 0.6 ms before, 1 ms to take the core back
    sched_note(&s, 2, s.t0 + 20 * ms, 0, s.t0 + 30 * ms, s.t0 + 30 * ms);                        // a condition wait that timed out
    for (int i = 0; i < 30; i++) sched_svc(&s, 0x34); for (int i = 0; i < 10; i++) sched_svc(&s, 0x1d);
    char line[1024]; sched_report(&s, s.t0 + 1000 * ms, line, sizeof line); printf("%s\n", line);
    check("lock: 2 a second, 7 ms blocked", strstr(line, "lock 2/s blocked 7.0 ms/s") != NULL);
    check("... wake-to-run 0.4 ms on average, 0.6 at most [-DDM_TEST_SCHED_WAKE_FROM_START: 3.0 / 5.0]", strstr(line, "wake-to-run 0.400 ms (max 0.60)") != NULL);
    check("... core back 0.5 ms on average, 1 at most", strstr(line, "core back 0.500 ms (max 1.00)") != NULL);
    check("condition: 1 a second, 10 ms blocked, none woken", strstr(line, "condition 1/s blocked 10.0 ms/s, wake-to-run 0.000") != NULL);
    check("system calls a second: 0x34 30, then 0x1d 10", strstr(line, "system calls: svc 0x34 30/s, svc 0x1d 10/s") != NULL);
    char line2[256]; sched_report(&s, s.t0 + 1000 * ms, line2, sizeof line2);
    check("cleared after a report", line2[0] == 0);
    sched_thr v[3] = {{0x1001, 28, 0x1, "main", 0, 4, 6 * ms, 0}, {0x1002, 16, 0x7, "Worker", 50, 2, 1 * ms, 1}, {0x1003, 44, 0x7, "Free", 0, 10, 3 * ms, 0}};
    char tl[1024]; sched_thr_report(v, 3, 2.0, tl, sizeof tl); printf("%s\n", tl);
    check("core waits: 2 threads that would hold a core 3.5 ms/s, 1 other 1.5 ms/s [-DDM_TEST_SCHED_THR_ONE_CORE_FREE]",
          strstr(tl, "2 threads asking for their core or on one core 3.50 ms/s, 1 others 1.50 ms/s") != NULL);
    char *m = strstr(tl, "0x1001"), *f = strstr(tl, "0x1003"), *w = strstr(tl, "0x1002");
    check("... the longest first: main, Free (free), Worker", m && f && w && m < f && f < w && strstr(tl, "\"Free\" prio 44 mask 0x7 0x10 0/s (free), 5 waits/s 1.50 ms/s"));
    check("... Worker's 0x10 calls a second", strstr(tl, "\"Worker\" prio 16 mask 0x7 0x10 25/s, 1 waits/s 0.50 ms/s") != NULL);
    printf("t_schedstats: %d checks, %d failed\n", n, fail); return fail != 0;
}
