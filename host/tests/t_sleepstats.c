// The short-sleep statistics of DM_SLEEP_STATS (host/sleepstats.h, plan item 316): 30 sleeps of 100 µs from one address, 5 µs of guest code
// apart, make a run (29 continue it, the 11 that start 2 ms or more into it count as after 2 ms); a sleep from there with another system call
// before it, and one after 50 µs of guest code, start runs of their own; a 2 ms sleep is not counted; one sleep from a second place. Other system
// calls between two sleeps not ending a run (-DDM_TEST_SLEEP_RUN_ANY_SVC): 2 fail. The back-off's size; its cap 100 µs when DM_SLEEP_BACKOFF is not
// set, off as before (-DDM_TEST_SLEEP_BACKOFF_DEFAULT_OFF): 1 fails.
#include <string.h>
#include "../sleepstats.h"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static void name(uint64_t lr, char *out, size_t k) { snprintf(out, k, "lr%#llx", (unsigned long long)lr); }
int main(void) {
    static sleep_stats st; sleep_run r = {0}; const uint64_t us = 1000, A = 0x1000, B = 0x2000; uint64_t t = 1000000000ull, svc = 10; st.t0 = t;
    for (int k = 0; k < 30; k++) { sleep_note(&st, &r, A, 0x9000, ++svc, t, t + 105 * us, 100 * us); t += 110 * us; }
    svc++; sleep_note(&st, &r, A, 0x9000, ++svc, t, t + 105 * us, 100 * us); t += 105 * us;     // another system call before it
    t += 50 * us; sleep_note(&st, &r, A, 0x9000, ++svc, t, t + 105 * us, 100 * us); t += 110 * us;   // 50 µs of guest code before it
    sleep_note(&st, &r, A, 0x9000, ++svc, t, t + 2000 * us, 2000 * us); t += 2005 * us;              // 2 ms: not a short sleep
    sleep_note(&st, &r, B, 0x9000, ++svc, t, t + 300 * us, 250 * us);
    char line[1024]; sleep_report(&st, st.t0 + 1000000000ull, name, line, sizeof line); printf("%s\n", line);
    check("33 short sleeps a second from 2 places (the 2 ms one not counted)", strstr(line, "short sleeps 33/s from 2 places") != NULL);
    check("the first address: 32 a second, asked 100 µs, slept 105 µs", strstr(line, "lr0x1000 < lr0x9000 32/s asked 100 µs slept 105 µs") != NULL);
    check("... 29 of 32 in a run: 91 % [-DDM_TEST_SLEEP_RUN_ANY_SVC: 94 %]", strstr(line, "slept 105 µs, 91% in a run") != NULL);
    check("... 11 of 32 after 2 ms of it: 34 % [-DDM_TEST_SLEEP_RUN_ANY_SVC: 38 %]", strstr(line, "34% after 2 ms of it") != NULL);
    check("the second address once, not in a run", strstr(line, "lr0x2000 < lr0x9000 1/s asked 250 µs slept 300 µs, 0% in a run, 0% after 2 ms of it") != NULL);
    char l2[256]; sleep_report(&st, st.t0 + 1000000000ull, name, l2, sizeof l2);
    check("cleared after a report", strstr(l2, "short sleeps 0/s from 0 places") != NULL);
    // the back-off: a run of 1.5 ms sleeps as asked; of 2.4 ms, 300 µs (an eighth) capped at 100; asked above the cap, as asked
    check("back-off: 25 µs in a run of 1.5 ms stays 25 µs; at 2.4 ms becomes 100 µs (an eighth, capped); 150 µs asked stays 150; a new run stays",
          sleep_backoff_ns(1500000, 25000, 100000) == 25000 && sleep_backoff_ns(2400000, 25000, 100000) == 100000 && sleep_backoff_ns(2400000, 150000, 100000) == 150000 &&
          sleep_backoff_ns(-1, 25000, 100000) == 25000 && sleep_backoff_ns(4000000, 25000, 1000000) == 500000);
    check("DM_SLEEP_BACKOFF: 100 µs when not set [-DDM_TEST_SLEEP_BACKOFF_DEFAULT_OFF: 0], 0 off, 250 is 250 µs",
          sleep_backoff_parse(NULL) == 100000 && sleep_backoff_parse("0") == 0 && sleep_backoff_parse("250") == 250000);
    printf("t_sleepstats: %d checks, %d failed\n", n, fail); return fail != 0;
}
