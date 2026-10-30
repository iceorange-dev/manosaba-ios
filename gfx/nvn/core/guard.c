// gfx/nvn/core/guard.c: guard_run (guard.h). The fault hook is the host's (crash.c: page faults go to the registered hooks before a crash is
// reported); it takes only a fault in the range this thread armed, and jumps back to guard_run, the guard disarmed. The signal mask is saved and
// restored with the jump (sigsetjmp's second argument): the fault's signal is blocked while its handler runs
#include "guard.h"
#include "../../../host/dm.h"
#include <pthread.h>
#include <setjmp.h>
#include <stdatomic.h>

static __thread struct { sigjmp_buf jb; uintptr_t lo, hi; int armed; } t_g;
static _Atomic uint64_t g_faults;
static int g_fault(void *addr, int is_write, void *uc) {
    (void)is_write; (void)uc; uintptr_t a = (uintptr_t)addr;
#ifndef DM_TEST_GUARD_ANY_ADDR   // negative control of t_core_guard only: a fault anywhere taken
    if (!t_g.armed || a < t_g.lo || a >= t_g.hi) return 0;
#else
    if (!t_g.armed) return 0;
    (void)a;
#endif
    t_g.armed = 0; siglongjmp(t_g.jb, 1);
}
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void hook(void) { dm_fault_hook_register(g_fault); }
int guard_run(const void *lo, size_t n, void (*fn)(void *), void *ctx) {
    pthread_once(&g_once, hook);
    if (sigsetjmp(t_g.jb, 1)) { atomic_fetch_add(&g_faults, 1); return 0; }
    t_g.lo = (uintptr_t)lo; t_g.hi = (uintptr_t)lo + n; t_g.armed = 1;
    fn(ctx);
    t_g.armed = 0;
    return 1;
}
uint64_t guard_faults(void) { return atomic_load(&g_faults); }
