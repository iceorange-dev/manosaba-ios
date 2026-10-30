// Reads of guest memory the game may have taken back (gfx/nvn/core/guard.c, plan item 290), with the host's crash handler installed: a read of
// mapped memory finishes; one reaching an inaccessible page inside the range is ended there and counted, the process going on, what was read before
// it kept; a guard used again after that works; a fault outside the range is not taken (a child dies of it: SIGSEGV / SIGBUS). A fault anywhere
// taken (-DDM_TEST_GUARD_ANY_ADDR, the broken variant): 1 fails.
#include "../nvn/core/guard.h"
#include "core_test.h"
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
void crash_install(void);

typedef struct { const volatile uint8_t *p; size_t n; uint64_t sum; size_t done; } rd_t;
static void rd(void *c) { rd_t *r = c; for (r->done = 0; r->done < r->n; r->done++) r->sum += r->p[r->done]; }

int main(void) {
    crash_install();
    long pg = sysconf(_SC_PAGESIZE);
    uint8_t *m = mmap(NULL, (size_t)pg * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); memset(m, 1, (size_t)pg * 4);
    rd_t r = {m, (size_t)pg * 4, 0, 0};
    check("mapped memory: the read finishes", 1, guard_run(m, (size_t)pg * 4, rd, &r));
    check("... all of it read", pg * 4, (long long)r.sum);
    mprotect(m + 2 * pg, (size_t)pg, PROT_NONE);   // the third page taken back
    uint64_t f0 = guard_faults(); r = (rd_t){m, (size_t)pg * 4, 0, 0};
    check("a page inside the range taken back: the read ended there", 0, guard_run(m, (size_t)pg * 4, rd, &r));
    check("... at that page, what came before it read", pg * 2, (long long)r.done);
    check("... the fault counted", 1, (long long)(guard_faults() - f0));
    r = (rd_t){m, (size_t)pg * 2, 0, 0};
    check("the guard again after a fault: a read of the rest finishes", 1, guard_run(m, (size_t)pg * 2, rd, &r));
    pid_t pid = fork();
    if (!pid) { int nul = open("/dev/null", O_WRONLY); dup2(nul, 2);   // (the crash report of the child's fault)
                r = (rd_t){m, (size_t)pg * 4, 0, 0}; guard_run(m, (size_t)pg * 2, rd, &r); _exit(0); }   // the fault at page 2, outside [0, 2 pages)
    int st = 0; waitpid(pid, &st, 0);
    check("a fault outside the range: not taken (the child dies of it)", 1, !(WIFEXITED(st) && WEXITSTATUS(st) == 0));
    printf("t_core_guard: %d checks, %d failed\n", n, fail); return fail != 0;
}
