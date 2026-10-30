// The crash report must not hang when another thread holds the stderr lock (crash.c on_crash). Each case runs in a child process with crash.c's
// handlers installed and faults on purpose; the parent waits at most 10 s:
//   held: a second thread takes flockfile(stderr) and never releases it → the child must exit by itself (128 + signal) within a few seconds,
//         having written the report's first part (registers, backtrace) and a line that stderr stayed held through write();
//   free: nobody holds the lock → the full report (the first part, then the symbols) is printed and the child exits with 128 + signal.
//   stuck: the thread dump never returns (it reads a pipe nobody writes), and self: it takes a mutex the crashing thread holds (the OnePlus 13's
//         allocator lock, jhaizhou-ops/d2r-mobile 3ebb29bd) → the child exits by itself 3 s in, the first part written, with the line that says
//         so (plan item 298); alarm: the same stuck dump in a report for SIGALRM (DM_WATCH's limit), which runs with SIGALRM blocked, in a child
//         with that thread alone (a process's SIGALRM goes to any thread not blocking it) → the same, 128 + SIGALRM. The second part without a limit (-DDM_TEST_CRASH_NO_DEADMAN) fails 9: all three hang until killed; SIGALRM left blocked
//         (-DDM_TEST_CRASH_ALRM_BLOCKED) fails 3: the alarm case.
// Before the fix the "held" child hung forever in fprintf: the parent's 10 s limit catches that.
//   heap: DM_CRASH_HEAP=x19,x99 with x19 set before the fault → the heap lookup gets x19's value (crash.c's weak stand-in for sead.c prints it),
//         x99 is reported as not parsed.
//   slow: held, with every sleep of the child six times as long (a busy machine; GitHub's macOS runner took 5.4 s for crash.c's 100 sleeps of
//         10 ms) → it still gives up after about 1 s, measured. The sleeps counted instead (-DDM_TEST_CRASH_COUNTED_WAIT) fail 1: 6 s.
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
void crash_install(void);
void crash_set_dump_hook(void (*fn)(void));
static int g_never[2];
static void dump_stuck(void) { char c; while (read(g_never[0], &c, 1) <= 0) {} }
static pthread_mutex_t g_held;   // a normal mutex, taken by the crashing thread before it faults
static void dump_self(void) { pthread_mutex_lock(&g_held); }

static void *hold_stderr(void *a) { flockfile(stderr); *(volatile int *)a = 1; for (;;) pause(); }
static volatile int g_slow;   // the "slow" child: crash.c's sleeps go through this usleep, six times as long
int usleep(useconds_t us) { uint64_t ns = (uint64_t)us * 1000u * (g_slow ? 6u : 1u); struct timespec ts = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)}; return nanosleep(&ts, NULL); }
static void child(int hold) {
    if (hold == 6) setenv("DM_FREEZE_DUMP", "0", 1);   // no watchdog thread: the only thread blocks SIGALRM, so nothing else takes the limit's
    crash_install();
    if (hold == 3) { g_slow = 1; hold = 1; }
    if (hold == 4 || hold == 6) { if (pipe(g_never)) _exit(98); crash_set_dump_hook(dump_stuck); }
    if (hold == 6) { raise(SIGALRM); _exit(99); }
    if (hold == 5) { pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_NORMAL); pthread_mutex_init(&g_held, &a);
                     pthread_mutex_lock(&g_held); crash_set_dump_hook(dump_self); }
    if (hold == 2) {
        setenv("DM_CRASH_HEAP", "x19,x99", 1);
        __asm__ volatile("mov x19, %0\n str wzr, [%1]" :: "r"(0x5ead1234ull), "r"(8ull) : "x19", "memory");   // fault with x19 known
    }
    if (hold == 1) {
        volatile int ready = 0; pthread_t t; pthread_create(&t, NULL, hold_stderr, (void *)&ready);
        while (!ready) usleep(1000);
    }
    *(volatile int *)8 = 1;   // fault
    _exit(99);                // not reached
}
static int checks, fails;
#define EXPECT(name, ok) do { checks++; printf("%-72s %s\n", name, (ok) ? "ok" : "FAIL"); if (!(ok)) fails++; } while (0)
static void run(int hold) {
    int pfd[2]; if (pipe(pfd)) { perror("pipe"); exit(2); }
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    pid_t pid = fork();
    if (!pid) { dup2(pfd[1], 2); close(pfd[0]); close(pfd[1]); child(hold); }
    close(pfd[1]);
    int status = 0, waited = 0;
    for (; waited < 1000 && !waitpid(pid, &status, WNOHANG); waited++) usleep(10000);   // at most 10 s
    int hung = waited >= 1000;
    if (hung) { kill(pid, SIGKILL); waitpid(pid, &status, 0); }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    static char out[65536]; out[0] = 0; ssize_t n, k = 0; while (k < (ssize_t)sizeof out - 1 && (n = read(pfd[0], out + k, sizeof out - 1 - k)) > 0) k += n; out[k] = 0; close(pfd[0]);
    double s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const char *name = hold == 6 ? "alarm" : hold == 5 ? "self" : hold == 4 ? "stuck" : hold == 3 ? "slow" : hold == 2 ? "heap" : hold ? "held" : "free";
    printf("[%s] exit %d after %.2f s%s; report: %.*s\n", name, code, s, hung ? " (killed after 10 s)" : "", (int)strcspn(out, "\n"), out);
    int first = strstr(out, "[dm] CRASH sig=") && strstr(out, "x0=") && strstr(out, "[dm] bt:");   // the first part, signal-safe
    if (hold >= 4) {
        char t[96]; snprintf(t, sizeof t, "%s: the child exits by itself, 3 s in (under 5 s)", hold == 6 ? "SIGALRM's report, its dump stuck" : hold == 5 ? "a lock the crashing thread holds" : "a thread dump that never returns");
        EXPECT(t, !hung && s < 5);
        if (hold == 6) EXPECT("  exit status is 128 + SIGALRM", code == 128 + SIGALRM);
        else EXPECT("  exit status is 128 + SIGSEGV or SIGBUS", code == 128 + SIGSEGV || code == 128 + SIGBUS);
        EXPECT("  the first part written before it (registers, backtrace)", first);
        EXPECT("  a line says the second part did not finish", strstr(out, "did not finish in 3 s") != NULL);
        return;
    }
    if (hold == 3) {   // only the time matters here
        EXPECT("sleeps six times as long: still gave up after about 1 s (under 2.5 s) [-DDM_TEST_CRASH_COUNTED_WAIT: 6 s]", !hung && s < 2.5);
        return;
    }
    EXPECT(hold == 2 ? "DM_CRASH_HEAP: the child exits by itself" : hold ? "stderr held: the child exits by itself" : "stderr free: the child exits by itself", !hung);
    EXPECT("exit status is 128 + SIGSEGV or SIGBUS", code == 128 + SIGSEGV || code == 128 + SIGBUS);
    if (hold == 2) {
        EXPECT("DM_CRASH_HEAP: the heap of x19 is looked up with x19's value", strstr(out, "heap of x19:") && strstr(out, "DM_CRASH_HEAP: 0x5ead1234"));
        EXPECT("DM_CRASH_HEAP: x99 is reported as not parsed", strstr(out, "DM_CRASH_HEAP: cannot parse \"x99\"") != NULL);
    } else if (hold) {
        EXPECT("stderr held: the first part and a line that stderr stayed held, through write()", first && strstr(out, "stderr held by another thread") != NULL);
        EXPECT("stderr held: gave up after about 1 s (under 5 s)", s < 5);
    } else {
        EXPECT("stderr free: full report, the first part and the symbols", first && strstr(out, "[dm] CRASH symbols:"));
        EXPECT("stderr free: no 'held' line", strstr(out, "stderr held") == NULL);
    }
}
int main(void) {
    run(1);
    run(0);
    run(2);
    run(3);
    run(4);
    run(5);
    run(6);
    printf("t_crash_stderr: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
