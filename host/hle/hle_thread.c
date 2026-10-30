//          0x10 GetCurrentProcessorNumber 0x25 GetThreadId 0x19 CancelSynchronization
// Thread and synchronization SVCs of the Horizon kernel, implemented natively: each guest thread = one host pthread, running on the guest stack given by the SDK.
// The SDK's nn::os runs as is; only the "kernel" underneath it is replaced by the host:
//   Threads    0x08 CreateThread 0x09 StartThread 0x0A ExitThread 0x0B SleepThread 0x0C/0x0D priority 0x0E/0x0F core mask
//              0x10 GetCurrentProcessorNumber 0x25 GetThreadId 0x19 CancelSynchronization
//   Locks      0x1A ArbitrateLock 0x1B ArbitrateUnlock (tag word = owner handle | 0x40000000 has waiters)
//   Condition  0x1C WaitProcessWideKeyAtomic 0x1D SignalProcessWideKey
//   Address    0x34 WaitForAddress 0x35 SignalToAddress
//   Events     0x45 CreateEvent 0x11 SignalEvent 0x12 ClearEvent 0x17 ResetSignal 0x18 WaitSynchronization 0x16 CloseHandle
// Semantics follow the public Horizon kernel behavior (KConditionVariable / KAddressArbiter / KSynchronization of Atmosphère mesosphère).
// All waits hang on one global lock G, with one condition variable per thread; correctness first, performance to be measured later.
#include "dm.h"
#include "schedstats.h"
#include "game.h"
#include <errno.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>
#include "plat.h"
#include <dlfcn.h>
#include <stdatomic.h>
#include <unistd.h>

#define R_TIMEOUT     0xEA01u   // Kernel module 1, description 117
#define R_CANCELLED   0xEC01u   // 118
#define R_BAD_HANDLE  0xE401u   // 114
#define R_BAD_STATE   0xFA01u   // 125
#define R_OUT_OF_RES  0xCE01u   // 103
#define R_BAD_ENUM    0xF001u   // 120 InvalidEnumValue
#define R_BUSY        0xF401u   // 122 Busy
#define R_BAD_CORE    0xE201u   // 113 InvalidCoreId
#define R_BAD_COMBO   0xE801u   // 116 InvalidCombination
#define R_BAD_ADDR    0xCC01u   // 102 InvalidAddress
#define WAITERS_BIT   0x40000000u
// The process's ideal core and core mask: NPDM MainThreadCoreNumber and the ThreadInfo kernel capability (core 0, cores 0..2 in both manosaba 2.0.4b's
// and TotK 1.4.3's main.npdm). The kernel gives the main thread an affinity of just its ideal core (KProcess::Run → InitializeUserThread)
#define PROC_IDEAL_CORE 0
#define PROC_CORE_MASK  7ull
#define H_CUR_THREAD  0xFFFF8000u

enum { O_FREE, O_THREAD, O_EVENT_R, O_EVENT_W };
// Service-layer SystemEvent bindings (hle_bind_system_event below): guest SystemEventType address → the write handle of its kernel event. An entry lives
// as long as that handle: CloseHandle on it removes the entry, so a SystemEvent destroyed and created again at the same address gets a new kernel event.
// Entries used to stay: binding again returned early (the new event was never created) and signals went to the closed handle, or to whatever event took
// its number next (nxnative 8530042, MIT; t_sysevent). Lock order: G, then g_se_lock (nothing takes G while holding g_se_lock)
static struct { void *ev; uint32_t wh; } g_se[512];
static int g_nse;
static pthread_mutex_t g_se_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t se_find(void *ev) {   // caller holds g_se_lock
    for (int i = 0; i < g_nse; i++) if (g_se[i].ev == ev) return g_se[i].wh;
    return 0;
}
static void se_unbind(uint32_t wh) {   // caller holds G
    pthread_mutex_lock(&g_se_lock);
    for (int i = 0; i < g_nse;) if (g_se[i].wh == wh) g_se[i] = g_se[--g_nse]; else i++;
    pthread_mutex_unlock(&g_se_lock);
}
typedef struct event { int signaled; int refs; } event_t;
typedef struct thr {
    uint32_t handle; uint64_t id;
    uint64_t entry, arg, stack_top; int prio, core, act; uint64_t mask;   // core = ideal core (-1 = don't care), act = the core it runs on, mask = affinity
    uint64_t born_ns, exit_ns;   // time of creation / exit (state dump: did a worker exit before the stall)
    int started, exited, cancel;
    pthread_t pt; pthread_cond_t cv;
    // Current wait: kind 0=none 1=lock 2=condition 3=address 4=synchronization object
    int wkind, woke; uint64_t waddr, wkey; uint32_t wtag; int64_t wres;
    const uint32_t *whp; int whn;   // a blocked WaitSynchronization's handle array (guest memory, valid while it waits) and count: whom a signal wakes, and the thread dump
    uint64_t wseq;             // Sequence number of wait registration: at equal priority first come, first woken (same order as the Horizon kernel's KThread wait tree)
    struct thr *next;          // Wait chain
    struct thr *all;           // All-threads chain (for the state dump)
    int last_svc; uint64_t last_lr;
    int closed;                // Handle closed: the object is freed only when this holds together with exited (Horizon: the thread object goes away with the last reference)
    uint64_t nsvc;             // SVC count (for the state dump)
    uint64_t woke_ns;          // when wake() signalled it (DM_SCHED_STATS: the wake-to-run latency, schedstats.h)
    uint64_t nyield, nyield_seen, nbackoff, nbackoff_seen;   // yields and those the back-off turned into short sleeps, both at the last power line (dm_yield_top)
    uint64_t yrun_svc, yrun_t0;   // the current run of yields: nsvc of its last yield and when it began (a run lasts while no other SVC comes between two yields)
    dm_hthread_t hth;          // Host thread handle (plat.h: stopping it, its registers and CPU time)
    uint64_t cpu_seen_us;      // CPU time (µs) at the last power line (dm_thread_cpu)
    uint32_t qos;
    // Host context saved in thread_main before switching to the guest stack. ExitThread jumps back here before publishing the exit: the SDK unmaps
    // the guest's alias stack as soon as WaitThread returns, so the publication and the pthread TSD destructors must run on the host pthread stack
    sigjmp_buf host_exit;
    int host_exit_valid;       // Touched only by the owning thread; the main thread (svc_thread_main_init) never has a saved context
    // Pausing (SetThreadActivity / GetThreadContext3, used by the IL2CPP GC to stop the world): see the "thread pausing" section of this file
    uint64_t tsd;              // This thread's slot block (slots.h dm_slot_base): to read the K18 (guest x18) / KTLS (tpidr_el0) slots
    _Atomic int in_svc;        // Currently inside an svc gate (set / cleared on gate_dispatch entry / exit)
    uint64_t *svc_regs, svc_pc;   // Guest register area saved by the current svc gate and the svc's original address
    _Atomic int pause_req, parked;   // Pause requested / stopped at the svc gate exit
    int stop_kind;             // How it was stopped: 1 = inside an svc (report from the gate's saved registers) 2 = Mach-suspended (report from the thread state)
    uint64_t hstk_top;         // Top of this thread's host stack (hstack.S; dm_thread_enter writes it), 0 = none (the main thread, DM_HSTACK=0)
    uint64_t stack_bot, stack_low;   // Guest stack: bottom (the MapMemory mapping holding its top, 0 = not known) and the lowest sp seen at an svc entry
    int stack_said;            // the low-stack line was logged
    int cur_core;              // Virtual core held while running guest code (-1 while blocked / not started; only with virtual cores, see below)
    int host_work;             // Created for host work (dm_guest_thread_spawn: the audio renderer) that waits on host primitives: holds no virtual core
    int next_host;             // This thread is about to create a host-work thread (svc CreateThread inside nn::os::CreateThread takes it)
    int preempt;               // A higher-priority thread waits for this thread's core: give it up at the next svc
    uint64_t cseq; struct thr *cnext;   // Core wait chain
} thr_t;
static thr_t *g_all;

static pthread_mutex_t G = PTHREAD_MUTEX_INITIALIZER;
#ifdef DM_TEST_HOOKS   // t_thrpub: a new thread just in the registry; a priority just set (inside the svc)
void (*dm_test_thread_linked)(void), (*dm_test_prio_set)(void);
#define THR_TEST_LINKED() { if (dm_test_thread_linked) dm_test_thread_linked(); }
#define THR_TEST_PRIO_SET() { if (dm_test_prio_set) dm_test_prio_set(); }
#else
#define THR_TEST_LINKED()
#define THR_TEST_PRIO_SET()
#endif
#define MAX_H 4096
static struct { int type; void *obj; } g_h[MAX_H];
static pthread_key_t g_keythr;
static uint64_t g_next_id = 0x51;
static thr_t *g_wait;          // All waiting threads (lock / condition / address / synchronization)

static uint32_t h_new(int type, void *obj) {     // Caller holds G
    for (int i = 1; i < MAX_H; i++) if (!g_h[i].type) { g_h[i].type = type; g_h[i].obj = obj; return 0x1000 + i; }
    die("handle table full");
}
static void *h_peek(uint32_t h, int type) {      // Lookup only, no report
    if (h == H_CUR_THREAD && type == O_THREAD) return pthread_getspecific(g_keythr);
    unsigned i = h - 0x1000;
    if (i == 0 || i >= MAX_H || g_h[i].type != type) return NULL;
    return g_h[i].obj;
}
static void *h_get(uint32_t h, int type) {       // Reports if not found (a wrong handle is a real error)
    void *o = h_peek(h, type);
    if (!o) LOG("invalid handle %#x (wanted type %d)", h, type);
    return o;
}
static thr_t *cur(void) {
    thr_t *t = pthread_getspecific(g_keythr);
    if (!t) die("thread SVC called from a non-guest thread");
    return t;
}

// ---------- Wait primitives (called holding G) ----------
static void wait_unlink(thr_t *t) {
    for (thr_t **p = &g_wait; *p; p = &(*p)->next) if (*p == t) { *p = t->next; break; }
    t->next = NULL;
}
// Pick a waiter in kernel order: numerically lower priority first, at equal priority the earlier registered first (holding G). kind 1 matches by lock address, 2 by condition key, 3 by address
static uint64_t g_wseq;
static thr_t *pick(int kind, uint64_t a) {
    thr_t *b = NULL;
    for (thr_t *t = g_wait; t; t = t->next) {
        if (t->wkind != kind || (kind == 2 ? t->wkey : t->waddr) != a) continue;
        if (!b || t->prio < b->prio || (t->prio == b->prio && t->wseq < b->wseq)) b = t;
    }
    return b;
}
// DM_SCHED_STATS=1: waits by kind, wake-to-run latency, the time taking a core back, system calls by number; a line every 5 s (plan item 255)
static int sched_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_SCHED_STATS"); v = e && atoi(e) > 0; } return v; }
static sched_stats g_sched;
static void wake(thr_t *t, int64_t res) { wait_unlink(t); t->wkind = 0; t->woke = 1; t->wres = res; if (sched_on()) t->woke_ns = now_ns(); pthread_cond_signal(&t->cv); }
// ---------- Virtual cores ----------
// Horizon runs at most one thread per core, and Tears of the Kingdom keys per-core data (allocators, job queues, the render job output blocks) by
// GetCurrentProcessorNumber without locks: its main thread and ModuleSystemWorker0 share core 0. With every guest thread a free pthread, two threads
// of one core ran at once and wrote the same per-core data: objects vanished every other frame, models were torn (Zelda's cloak, the cave walls).
// With game.json virtual_cores, a guest thread holds one of the three application cores while it runs guest code, gives it up where Horizon would
// switch threads (waits, sleep, yield, exit, host services that wait: dm_core_away) and the freed core goes to the best waiting thread (lowest
// priority value, then first come); a thread that a higher-priority waiter wants gives its core up at its next svc. Ported from MacNativeGames
// (ly05010419/MacNativeGames runtime/threads.c, MIT). DM_FREE_CORES=1 turns it off
#define NCORE 3
static thr_t *g_core_owner[NCORE];
static thr_t *g_core_wait;
static int cores_enabled(void) { static int on = -1; if (on < 0) on = dm_game.virtual_cores && !getenv("DM_FREE_CORES"); return on; }
static int core_allowed(const thr_t *t, int c) { return (int)(t->mask >> c & 1); }
static void core_grant(int c) {   // holding G: give free core c to the best waiter
    thr_t *b = NULL, **bp = NULL;
    for (thr_t **p = &g_core_wait; *p; p = &(*p)->cnext)
        if (core_allowed(*p, c) && (!b || (*p)->prio < b->prio || ((*p)->prio == b->prio && (*p)->cseq < b->cseq))) { b = *p; bp = p; }
    if (!b) return;
    *bp = b->cnext; b->cnext = NULL; b->cur_core = b->act = c; g_core_owner[c] = b; pthread_cond_signal(&b->cv);
}
static void core_put(thr_t *t) {   // holding G
    if (!t || t->cur_core < 0) return;
    int c = t->cur_core; t->cur_core = -1; t->preempt = 0; g_core_owner[c] = NULL; core_grant(c);
}
static void core_take(thr_t *t) {   // holding G; waits until a core of t's mask is handed over
    if (!cores_enabled() || !t || t->cur_core >= 0 || t->host_work) return;
    if (!(t->mask & ((1u << NCORE) - 1))) die("thread %#x core mask %#llx has no application core", t->handle, (unsigned long long)t->mask);
    int c = t->core >= 0 && t->core < NCORE && core_allowed(t, t->core) && !g_core_owner[t->core] ? t->core : -1;
    for (int i = 0; c < 0 && i < NCORE; i++) if (core_allowed(t, i) && !g_core_owner[i]) c = i;
    if (c >= 0) { t->cur_core = t->act = c; g_core_owner[c] = t; return; }
    t->cseq = ++g_wseq; t->cnext = g_core_wait; g_core_wait = t;
    for (int i = 0; i < NCORE; i++) if (core_allowed(t, i) && g_core_owner[i] && g_core_owner[i]->prio > t->prio) g_core_owner[i]->preempt = 1;
    while (t->cur_core < 0) pthread_cond_wait(&t->cv, &G);
}
// The next thread the calling thread creates runs host work (hle_guestthread.c): it holds no virtual core, since it waits on host primitives and
// would keep its core for good (the audio renderer kept core 0 and the main thread never got it back)
void dm_thread_next_host_work(void) { thr_t *t = g_keythr ? pthread_getspecific(g_keythr) : NULL; if (t) t->next_host = 1; }
// Host services that wait for other threads, the GPU or the display give the core up around the wait (no host lock held while calling these)
int dm_core_away(void) {
    thr_t *t = g_keythr ? pthread_getspecific(g_keythr) : NULL; if (!t || t->cur_core < 0) return 0;
    pthread_mutex_lock(&G); core_put(t); pthread_mutex_unlock(&G); return 1;
}
void dm_core_back(int *away) {
    thr_t *t = g_keythr ? pthread_getspecific(g_keythr) : NULL; if (!*away || !t) return;
    pthread_mutex_lock(&G); core_take(t); pthread_mutex_unlock(&G);
}
static void core_checkpoint(thr_t *t) {   // svc entry: a higher-priority thread waits for this core
    if (!t || !t->preempt) return;
    pthread_mutex_lock(&G); t->preempt = 0; core_put(t); core_take(t); pthread_mutex_unlock(&G);
}
// Suspend until woken or timed out (ns<0 infinite, 0 immediate timeout); returns wres or R_TIMEOUT. The core is given up while waiting
static int64_t block_inner(thr_t *t, int kind, int64_t ns);
static int64_t block_core(thr_t *t, int kind, int64_t ns) {
    if (ns == 0 || t->cur_core < 0) return block_inner(t, kind, ns);
    core_put(t); int64_t r = block_inner(t, kind, ns); core_take(t); return r;
}
static int64_t block(thr_t *t, int kind, int64_t ns) {   // Boot timeline: accumulated by wait kind (all threads) + the main thread listed separately
    if (sched_on() && ns != 0) {   // (holding G)
        uint64_t a = now_ns(); int had = t->cur_core >= 0; t->woke_ns = 0;
        if (had) core_put(t);
        int64_t r = block_inner(t, kind, ns); uint64_t b = now_ns();
        if (had) core_take(t);
        sched_note(&g_sched, kind & 7, a, t->woke ? t->woke_ns : 0, b, had ? now_ns() : b);
        return r;
    }
    if (!g_flag.bootprof) return block_core(t, kind, ns);
    uint64_t t0 = now_ns(); int64_t r = block_core(t, kind, ns); uint64_t d = now_ns() - t0;
    static const int id[] = {0, PROF_WAIT_LOCK, PROF_WAIT_CV, PROF_WAIT_ADDR, PROF_WAIT_SYNC};
    dm_prof_add(id[kind & 7], d, 0);
    if (t->handle == 0x1001) dm_prof_add(PROF_MAIN_WAIT, d, 0);
    return r;
}
static int64_t block_inner(thr_t *t, int kind, int64_t ns) {
    t->wkind = kind; t->woke = 0; t->next = g_wait; g_wait = t; t->wseq = ++g_wseq;
    if (ns == 0) { wait_unlink(t); t->wkind = 0; return R_TIMEOUT; }
    struct timespec dl;
    int det_wait = ns > 0 && dm_detclock_enabled();
    uint64_t guest_deadline = det_wait ? guest_ns() + (uint64_t)ns : 0;
    if (ns > 0 && !det_wait) { clock_gettime(CLOCK_REALTIME, &dl); dl.tv_sec += ns / 1000000000; dl.tv_nsec += ns % 1000000000;
                  if (dl.tv_nsec >= 1000000000) { dl.tv_sec++; dl.tv_nsec -= 1000000000; } }
    while (!t->woke) {
        int rc;
        if (det_wait) {
            uint64_t now = guest_ns();
            if (now >= guest_deadline) { wait_unlink(t); t->wkind = 0; return R_TIMEOUT; }
            (void)(guest_deadline - now);
            rc = pthread_cond_wait(&t->cv, &G);
        } else rc = ns > 0 ? pthread_cond_timedwait(&t->cv, &G, &dl) : pthread_cond_wait(&t->cv, &G);
        if (rc == ETIMEDOUT && !t->woke && (!det_wait || guest_ns() >= guest_deadline)) { wait_unlink(t); t->wkind = 0; return R_TIMEOUT; }
    }
    return t->wres;
}

// ---------- Locks ----------
static inline uint32_t ld32(uint64_t a) { return __atomic_load_n((uint32_t *)a, __ATOMIC_SEQ_CST); }
static inline uint64_t ld64(uint64_t a) { return __atomic_load_n((uint64_t *)a, __ATOMIC_SEQ_CST); }
static inline void st32(uint64_t a, uint32_t v) { __atomic_store_n((uint32_t *)a, v, __ATOMIC_SEQ_CST); }
static inline int cas32(uint64_t a, uint32_t *exp, uint32_t v) { return __atomic_compare_exchange_n((uint32_t *)a, exp, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }

// Release the lock at addr: hand it to the first thread waiting on it, or clear it to zero (holding G)
static void lock_handoff(uint64_t addr) {
    thr_t *w = pick(1, addr); int more = 0;
    if (!w) { st32(addr, 0); return; }
    for (thr_t *t = g_wait; t; t = t->next) if (t != w && t->wkind == 1 && t->waddr == addr) { more = 1; break; }
    st32(addr, w->wtag | (more ? WAITERS_BIT : 0));
    wake(w, 0);
}
// Make t take the lock at addr: if free, it gets it directly and is woken, otherwise set the waiters bit and turn into a lock wait (holding G)
static void lock_acquire_for(thr_t *t, uint64_t addr, uint32_t tag) {
    uint32_t v = ld32(addr);
    for (;;) {
        if (v == 0) { if (cas32(addr, &v, tag)) { wake(t, 0); return; } continue; }
        if (cas32(addr, &v, v | WAITERS_BIT)) break;
    }
    t->wkind = 1; t->waddr = addr; t->wtag = tag;      // Still in g_wait, changed to a lock wait
    t->wseq = ++g_wseq;                                  // The kernel inserts it anew into the lock owner's wait queue: at equal priority it goes after the existing lock waiters
}

// Thread pausing (svc 0x32 SetThreadActivity / 0x33 GetThreadContext3)
// Semantics follow Atmosphère mesosphère (kern_svc_activity.cpp / kern_k_thread.cpp SetActivity, GetThreadContext3, kern_k_thread_context.cpp GetUserContext):
//   - Not on oneself (Busy); the thread must be started and not exited (InvalidState); already paused on Paused / not paused on Runnable → InvalidState; activity is only 0 / 1 (InvalidEnumValue).
//   - Paused returns only once the target is "not running on any core": a thread in a kernel wait counts as stopped at once (even if it wakes it cannot return to user mode).
//   - GetThreadContext3 requires the thread to be paused; a thread inside an svc reports only x19–x28, fp, lr, sp, pc and v8–v15 (the callee-saved part), the rest 0; otherwise all registers.
// Host mapping:
//   - Inside an svc gate (in_svc) = in the kernel: not suspended; on leaving the gate (before gate_dispatch returns) it stops itself on G until resumed; the context comes from the gate's saved guest registers
//     (the gate block does stp x29,x30,[sp,#-16]! then blr dm_gate; dm_gate keeps x16 / x17 below that, takes the host stack and puts its record (guest sp at +16) and the 800-byte
//     register area below it ⇒ guest sp = the record's, guest lr just below the guest sp).
//   - Inside a host call or an svc gate that took the thread's host stack (hstack.S): the guest's state at that entry, from the record at the top of the host stack: guest sp,
//     x19–x28, fp, lr, and for a host call x0–x8 (its arguments). The host frames, and any guest code a host function called, are on the host stack: the guest stack above that sp
//     holds every guest frame, and a value only a host frame saved is in these registers. The IL2CPP GC scans exactly that.
//   - In guest code (pc inside a module): dm_hthread_suspend (plat.h), report from the thread state; x18 is read from the K18 slot (the rewriter moved the guest's x18 into a TSD slot).
//     Guest code holds no host locks, so keeping it suspended cannot hang other host calls during the stop-the-world.
//   - In host code (an HLE implementation): if it is parked in a system wait stub (condition variable / semaphore / sleep / mach_msg), treat it like a kernel wait: suspend and report
//     from the record above, or with DM_HSTACK=0 from the thread state (the host frames are then on the guest stack with a lower sp, so scanning upward from it covers every guest
//     frame); otherwise let it go and retry shortly, until it returns to guest code or enters a wait.
static struct { uint64_t a; } g_waitstub[16]; static int g_nwaitstub;
static void waitstub_init(void) {
#ifdef __APPLE__
    static const char *nm[] = {"__psynch_cvwait", "__semwait_signal", "__semwait_signal_nocancel", "semaphore_wait_trap", "semaphore_timedwait_trap",
                               "mach_msg_trap", "mach_msg2_trap", "__select", "kevent", "kevent_id", "__workq_kernreturn"};
#else   // bionic: its futex waits (condition variables, mutexes, semaphores) go through syscall(); sleeps and polls have stubs of their own
    static const char *nm[] = {"syscall", "nanosleep", "clock_nanosleep", "epoll_pwait", "ppoll"};
#endif
    for (unsigned i = 0; i < sizeof nm / sizeof *nm; i++) { void *p = dlsym(RTLD_DEFAULT, nm[i]); if (p) g_waitstub[g_nwaitstub++].a = (uint64_t)p; }
}
static int in_waitstub(uint64_t pc) { for (int i = 0; i < g_nwaitstub; i++) if (pc >= g_waitstub[i].a && pc < g_waitstub[i].a + 0x40) return 1; return 0; }
static uint64_t tsd_slot(const thr_t *t, dm_slot k) { return t->tsd ? ((const uint64_t *)(t->tsd & ~7ull))[k] : 0; }

void thr_svc_enter(uint64_t *regs, uint64_t pc) {
    thr_t *t = pthread_getspecific(g_keythr); if (!t) return;
    t->svc_regs = regs; t->svc_pc = pc; atomic_store(&t->in_svc, 1);
    // The least free guest stack, sampled at svc entries: the guest sp from dm_gate's record above the register area (a gate nested in a host call
    // has its record on the host stack, outside the range)
    uint64_t sp = regs[(800 + 16) / 8];
    if (sp < t->stack_low && sp > t->stack_bot && sp <= t->stack_top && t->stack_top - sp < (64u << 20)) {
        t->stack_low = sp;
        if (t->stack_bot && sp - t->stack_bot < 1024 && !t->stack_said) { t->stack_said = 1;
            module_t *m = module_of((void *)pc);
            LOG("[stack] thread %#x: %llu of its %llu bytes of guest stack left at an svc entry (%s+%#llx)", t->handle, (unsigned long long)(sp - t->stack_bot),
                (unsigned long long)(t->stack_top - t->stack_bot), m ? m->name : "?", m ? pc - (uint64_t)m->base : pc); }
    }
}
// Guest stack of a thread (the unit test): top, lowest sp at an svc entry, bottom (0 = not known); 0 when there is no such thread
int thr_stack_info(uint32_t h, uint64_t *top, uint64_t *low, uint64_t *bot, int *said) {
    pthread_mutex_lock(&G); thr_t *t = h_peek(h, O_THREAD);
    if (t) { *top = t->stack_top; *low = t->stack_low; *bot = t->stack_bot; *said = t->stack_said; }
    pthread_mutex_unlock(&G); return t != NULL;
}
void thr_svc_exit(void) {
    thr_t *t = pthread_getspecific(g_keythr); if (!t) return;
    atomic_store(&t->in_svc, 0);                 // Pairs with the pauser's "set pause_req, then read in_svc" (both sequentially consistent): at least one side sees the other
    if (!atomic_load(&t->pause_req)) return;
    pthread_mutex_lock(&G);
    atomic_store(&t->parked, 1);
    while (atomic_load(&t->pause_req)) pthread_cond_wait(&t->cv, &G);
    atomic_store(&t->parked, 0);
    pthread_mutex_unlock(&G);
}
// Inside a module's patch area (the rewriter's blocks and caves; plan item 202): a block keeps the guest values of the scratch registers it uses
// below sp until it returns to the guest code, so a context taken there reports block values in those registers and a collector scanning from sp
// up (manosaba's IL2CPP) misses the guest's (found by reading upstream 373b402's red-zone change; not seen in a run)
static int in_patch_area(uint64_t pc) { module_t *m = module_of((void *)pc); return m && m->patch_off && pc >= (uint64_t)m->base + m->patch_off; }
static uint64_t g_stop_in_patch __attribute__((unused));   // stops left inside a block after the tries (a copied exclusive section can spin on a lock the pauser holds)
static uint32_t set_activity(uint32_t h, uint32_t act) {
    if (act > 1) return R_BAD_ENUM;
    pthread_mutex_lock(&G);
    thr_t *t = h_peek(h, O_THREAD), *self = pthread_getspecific(g_keythr);
    uint32_t r = !t ? R_BAD_HANDLE : t == self ? R_BUSY : (!t->started || t->exited) ? R_BAD_STATE : (act == 1) == (atomic_load(&t->pause_req) != 0) ? R_BAD_STATE : 0;
    if (r || act == 0) {
        int mach = 0;
        if (!r) { atomic_store(&t->pause_req, 0); mach = t->stop_kind == 2; t->stop_kind = 0; pthread_cond_broadcast(&t->cv); }
        pthread_mutex_unlock(&G);
        if (mach) dm_hthread_resume(t->hth);
        return r;
    }
    atomic_store(&t->pause_req, 1);
    pthread_mutex_unlock(&G);
    static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, waitstub_init);
    for (unsigned n = 0, in_block __attribute__((unused)) = 0;; n++) {
        if (atomic_load(&t->in_svc) || atomic_load(&t->parked)) { t->stop_kind = 1; return 0; }
        if (dm_hthread_suspend(t->hth)) { LOG("[red] SetThreadActivity: suspending thread %#x failed", h); t->stop_kind = 0; return 0; }
        dm_regs st; uint64_t pc = !dm_hthread_regs(t->hth, &st, 0) ? st.pc : 0;
        if (atomic_load(&t->in_svc) || atomic_load(&t->parked)) { dm_hthread_resume(t->hth); t->stop_kind = 1; return 0; }   // Entered a gate just before being suspended
#ifndef DM_TEST_STOP_IN_PATCH   // negative control of t_hstack only: stopped inside a block too, as before
        if (pc && in_patch_area(pc) && in_block++ < 2000) { dm_hthread_resume(t->hth); usleep(20); continue; }   // let it leave the block first
        if (pc && in_patch_area(pc) && !g_stop_in_patch++) LOG("SetThreadActivity: thread %#x stayed in a rewrite block (pc=%#llx) through 2000 tries: stopped there (first case)", h, pc);
#endif
        if (pc && (module_of((void *)pc) || in_waitstub(pc))) { t->stop_kind = 2; return 0; }
        dm_hthread_resume(t->hth);
        if (n == 100000) LOG("[red] SetThreadActivity: thread %#x has been in host code for long (pc=%#llx), still waiting for it to return to guest code", h, pc);
        usleep(20);
    }
}
static uint32_t get_context3(uint8_t *out, uint32_t h) {
    pthread_mutex_lock(&G);
    thr_t *t = h_peek(h, O_THREAD), *self = pthread_getspecific(g_keythr);
    uint32_t r = !t ? R_BAD_HANDLE : t == self ? R_BUSY : !atomic_load(&t->pause_req) ? R_BAD_STATE : 0;
    pthread_mutex_unlock(&G);
    if (r) return r;
    uint64_t *x = (uint64_t *)out; memset(out, 0, 0x320);   // ThreadContext layout (as copied by sdk+0x152600)
    if (t->exited) return 0;                                // Terminated: mesosphere leaves the context unfilled
#ifndef DM_TEST_HSTACK_OLD_CONTEXT   // negative control of t_hstack only: the context as before the host stack (item 40)
    const uint64_t *rec = t->stop_kind ? dm_hstack_record(t->tsd, t->hstk_top) : NULL;
#else
    const uint64_t *rec = NULL;
#endif
    if (rec) {   // inside a host call or an svc gate that took the host stack: the guest's state at that entry
        if (rec[13] == 2) {   // svc gate: its register area is below the record
            const uint64_t *g = rec - 800 / 8;
            for (int i = 19; i < 29; i++) x[i] = g[i];
            x[29] = g[29]; x[32] = t->stop_kind == 1 && t->svc_regs == g ? t->svc_pc : rec[1];
            memcpy(out + 0x110 + 8 * 16, (const uint8_t *)g + 256 + 8 * 16, 8 * 16);   // v8–v15
            x[30] = *(const uint64_t *)(rec[2] - 8);         // the gate block's x30, just below the guest sp
        } else {              // host call: its arguments x0–x8 too, and x18 from its slot
            for (int i = 0; i < 9; i++) x[i] = rec[4 + i];
            for (int i = 19; i < 29; i++) x[i] = rec[14 + i - 19];
            x[18] = tsd_slot(t, g_key18);
            x[29] = rec[0]; x[30] = x[32] = rec[1];
        }
        x[31] = rec[2];
    } else if (t->stop_kind == 1) {
        const uint64_t *g = t->svc_regs;
        for (int i = 19; i < 29; i++) x[i] = g[i];
#ifndef DM_TEST_HSTACK_OLD_CONTEXT
        x[29] = g[29]; x[31] = g[(800 + 16) / 8]; x[30] = *(const uint64_t *)(x[31] - 8); x[32] = t->svc_pc;   // dm_gate's record, on the same stack here
#else
        x[29] = g[29]; x[30] = *(const uint64_t *)((const uint8_t *)g + 808); x[31] = (uint64_t)g + 816; x[32] = t->svc_pc;
#endif
        memcpy(out + 0x110 + 8 * 16, (const uint8_t *)g + 256 + 8 * 16, 8 * 16);   // v8–v15
    } else if (t->stop_kind == 2) {
        dm_regs st;
        if (dm_hthread_regs(t->hth, &st, 1)) { LOG("[red] GetThreadContext3: reading the state of thread %#x failed", h); return 0; }
        for (int i = 0; i < 29; i++) x[i] = st.x[i];
        x[18] = tsd_slot(t, g_key18);
        x[29] = st.fp; x[30] = st.lr; x[31] = st.sp; x[32] = st.pc;
        *(uint32_t *)(out + 0x108) = st.cpsr;
        if (st.has_v) { memcpy(out + 0x110, st.v, 0x200); *(uint32_t *)(out + 0x310) = st.fpcr; *(uint32_t *)(out + 0x314) = st.fpsr; }
    }
    x[0x318 / 8] = tsd_slot(t, g_keytls);
    static int gclog = -1; if (gclog < 0) gclog = getenv("DM_GCLOG") != NULL;   // Diagnostics: one line per context fetch (how stopped, sp relative to the stack top, where pc is)
    if (gclog) { module_t *pm = module_of((void *)x[32]); Dl_info di = {0}; if (!pm) dladdr((void *)x[32], &di);
        LOG("GetThreadContext3 %#x stop kind %d sp=%#llx (stack top %#llx, depth %#llx) pc=%s+%#llx x18=%#llx", h, t->stop_kind, x[31], t->stack_top, t->stack_top - x[31],
            pm ? pm->name : di.dli_sname ? di.dli_sname : "?", pm ? x[32] - (uint64_t)pm->base : di.dli_saddr ? x[32] - (uint64_t)di.dli_saddr : x[32], x[18]); }
    return 0;
}
uint64_t svc_thread_activity(uint64_t *regs) { regs[0] = set_activity((uint32_t)regs[0], (uint32_t)regs[1]); return 0; }
uint64_t svc_thread_context3(uint64_t *regs) { regs[0] = get_context3((uint8_t *)regs[0], (uint32_t)regs[1]); return 0; }

// ---------- Threads ----------
extern void dm_thread_enter(uint64_t entry, uint64_t arg, uint64_t sp, uint64_t *host_top);   // entry.S
void crash_thread_init(void);
void guest_thread_init(void);   // thread.c: the single place that sets the per-thread TSD slots (gate, x18, TLS, TLR)
static void thr_free(thr_t *t) {   // Holding G; unlink from the all-threads chain and free
    for (thr_t **p = &g_all; *p; p = &(*p)->all) if (*p == t) { *p = t->all; break; }
    pthread_cond_destroy(&t->cv); free(t);
}
static const char *thr_name(thr_t *t, char *buf, size_t n);
__attribute__((weak)) void wp_thread_start(void) {}   // diag.c (DM_WATCHPOINT, Mac) provides the real one; iOS and the unit tests link without it
__attribute__((weak)) uint64_t dm_mapping_of(uint64_t a, uint64_t *size) { (void)a; *size = 0; return 0; }   // hle_svc.c; the unit tests link without it
static void wake_sync_waiters(void *obj);
static void kt_dump(int last);   // the lock / condition variable trace (below svc_thread)
// Publish the exit: record the time, mark exited and wake synchronization waiters. Once G is released a waiter may close the handle and free t,
// and the guest may unmap the guest stack, so the caller must neither touch t nor run on the guest stack afterwards
static void thread_publish_exit(thr_t *t) {
    t->exit_ns = now_ns();
    pthread_mutex_lock(&G);
    core_put(t);
    t->exited = 1;
    wake_sync_waiters(t);
    pthread_setspecific(g_keythr, NULL);
    if (t->closed) thr_free(t);
    pthread_mutex_unlock(&G);
}
// The host thread's scheduling class for guest work (plat.h: interactive with set)
static uint32_t host_qos(int set) { if (set) dm_self_interactive(); return dm_self_qos(); }
static void *thread_main(void *p) {
    thr_t *const t = p;   // Never modified, so its value is well defined after siglongjmp
    pthread_setspecific(g_keythr, t);
    t->qos = host_qos(1);
    t->hth = dm_hthread_self();
    crash_thread_init();
    wp_thread_start();   // DM_WATCHPOINT (diag.c)
    guest_thread_init();
    t->tsd = dm_slot_base();   // (after guest_thread_init: on Android its first slot makes the block)
    pthread_mutex_lock(&G); while (!t->started) pthread_cond_wait(&t->cv, &G); core_take(t); pthread_mutex_unlock(&G);
    // ExitThread lands here through siglongjmp, back on this host stack with the callee-saved registers restored; the abandoned guest frames need
    // no unwinding (the SDK has finished its own teardown before ExitThread). The signal mask is not saved or restored
    if (sigsetjmp(t->host_exit, 0)) {
        t->host_exit_valid = 0;
        thread_publish_exit(t);
        return NULL;   // pthread TSD destructors (TLS / TLR blocks, the signal alternate stack) run on this host stack
    }
    t->host_exit_valid = 1;
    dm_thread_enter(t->entry, t->arg, t->stack_top, &t->hstk_top);   // Does not return: the guest must exit through ExitThread
    die("guest thread entry returned");
}
// ExitThread. A thread started by thread_main leaves the guest stack first, doing nothing on it before the jump (no clock reads, logging or locks):
// the guest stack can be a dozen KiB and is unmapped as soon as a waiter returns. The main thread runs nnMain on its native host stack, so it
// publishes in place and exits. (Fix from TommyLau/botw-mobile 4a9b641, MIT; upstream d2r-mobile published and exited on the guest stack.)
static _Noreturn void thread_exit(thr_t *t) {
#ifndef DM_TEST_EXIT_ON_GUEST_STACK   // negative control of the unit test t_thread_exit only: publishes and exits on the guest stack as before the fix
    if (t->host_exit_valid) siglongjmp(t->host_exit, 1);
#endif
    thread_publish_exit(t);
    pthread_exit(NULL);
}
void dm_thread_returned(void) { die("guest thread entry function returned (Horizon requires ExitThread)"); }

// Register the main thread and return its handle (passed to nnosInitialize as the main thread handle)
uint32_t svc_thread_main_init(void) {
    pthread_key_create(&g_keythr, NULL);
    thr_t *t = calloc(1, sizeof *t);
    pthread_cond_init(&t->cv, NULL);
    t->started = 1; t->prio = 44; t->core = t->act = PROC_IDEAL_CORE; t->mask = 1ull << PROC_IDEAL_CORE; t->pt = pthread_self(); t->hth = dm_hthread_self();
    t->tsd = dm_slot_base();
    t->cur_core = -1;
    pthread_mutex_lock(&G); t->handle = h_new(O_THREAD, t); t->id = g_next_id++; t->all = g_all; g_all = t; core_take(t); pthread_mutex_unlock(&G);
    pthread_setspecific(g_keythr, t);
    // The guest main thread runs on the host thread that calls this (the iOS app's boot thread), made without a QoS class, while every other guest
    // thread runs user-interactive (thread_main). DM_MAIN_QOS=ui gives it that class too (plan item 229, a measurement: Tears of the Kingdom's main
    // thread is its busiest, 143.6 s of CPU in 7 minutes on the iPhone, and on the frame's chain; it also runs real-time while it presents)
#ifndef DM_TEST_MAIN_QOS_IGNORED   // negative control of t_mainqos only: the switch ignored
    { const char *e = getenv("DM_MAIN_QOS"); if (e && !strcmp(e, "ui")) { host_qos(1); LOG("DM_MAIN_QOS=ui: the guest main thread runs user-interactive"); } }
#endif
    t->qos = host_qos(0);
    return t->handle;
}

static uint64_t g_main_yield_n, g_main_yield_ns;   // Written only by the guest main thread; the diagnostic line reads it on the same thread
void dm_main_yield_stats(uint64_t *n, uint64_t *ns) { *n = g_main_yield_n; *ns = g_main_yield_ns; }
uint32_t dm_guest_work_qos(unsigned *count) {
    uint32_t qos = 0; unsigned n = 0;   // (QOS_CLASS_UNSPECIFIED)
    pthread_mutex_lock(&G);
    for (thr_t *t = g_all; t; t = t->all) {
        if (t->handle == 0x1001) continue;
        if (!n) qos = t->qos; else if (qos != t->qos) qos = UINT32_MAX;
        n++;
    }
    pthread_mutex_unlock(&G);
    if (count) *count = n;
    return qos;
}

// ---------- Synchronization objects ----------
static int sig_obj(uint32_t h, int *bad) {    // 1 = signaled (holding G)
    unsigned i = h - 0x1000;
    if (h == H_CUR_THREAD) return 0;
    if (i == 0 || i >= MAX_H) { *bad = 1; return 0; }
    switch (g_h[i].type) {
    case O_THREAD: return ((thr_t *)g_h[i].obj)->exited;
    case O_EVENT_R: return ((event_t *)g_h[i].obj)->signaled;
    default: *bad = 1; return 0;
    }
}
// A signaled event or an exited thread wakes only the WaitSynchronization waiters holding a handle to it (nxnative e06a8ec, MIT): it used to wake every
// such waiter, each re-checking its handles and blocking again (holding G)
static int waits_on(const thr_t *w, const void *obj) {
    for (int i = 0; i < w->whn; i++) { unsigned x = w->whp[i] - 0x1000; if (x && x < MAX_H && g_h[x].obj == obj) return 1; }
    return 0;
}
uint64_t g_sync_wakes;   // WaitSynchronization waiters woken by a signal or a thread exit (t_waitsync counts them)
static void wake_sync_waiters(void *obj) {
    for (thr_t *w = g_wait; w; ) { thr_t *n = w->next; if (w->wkind == 4 && waits_on(w, obj)) { wake(w, -1); g_sync_wakes++; } w = n; }
}

static int64_t now_mono_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000000000ll + ts.tv_nsec; }
void dm_guest_clock_advanced(void) {
    if (!dm_detclock_enabled()) return;
    pthread_mutex_lock(&G);
    for (thr_t *t = g_wait; t; t = t->next) if (t->wkind) pthread_cond_signal(&t->cv);
    pthread_mutex_unlock(&G);
}

// Handle thread / synchronization SVCs; returns 1 = handled
static const char *thr_name(thr_t *t, char *nb, size_t n) {   // SDK ThreadType+0x1a0 = name pointer
    if (!t->arg) return t->handle == 0x1001 ? "main thread" : "?";
    uint64_t np = 0;
    if (!dm_mem_read(&np, t->arg + 0x1a0, 8) || !np || !dm_mem_read(nb, np, n - 1)) return "?";
    nb[n - 1] = 0; return nb;
}
// Name of the calling guest thread (diagnostics, DM_WATCHPOINT); NULL on a host thread
const char *thr_cur_name(char *nb, size_t n) { thr_t *t = pthread_getspecific(g_keythr); return t ? thr_name(t, nb, n) : NULL; }
// All live guest threads (used by the sampler DM_SAMPLE=ms:*): fill the pthreads into out, return the count
int thr_list(pthread_t *out, int max) {
    int n = 0;
    pthread_mutex_lock(&G);
    for (thr_t *t = g_all; t && n < max; t = t->all) if (!t->exited && t->started) out[n++] = t->pt;
    pthread_mutex_unlock(&G);
    return n;
}
// Soak readings (DM_SOAK): handle table usage, live guest threads, total thread objects (including exited but not yet freed)
void thr_counts(int *handles, int *live, int *objs) {
    int nh = 0, nl = 0, no = 0;
    pthread_mutex_lock(&G);
    for (int i = 1; i < MAX_H; i++) nh += g_h[i].type != O_FREE;
    for (thr_t *t = g_all; t; t = t->all) { no++; nl += t->started && !t->exited; }
    pthread_mutex_unlock(&G);
    *handles = nh; *live = nl; *objs = no;
}
// Get the guest thread name by pthread (used by per-thread CPU sampling DM_THRCPU, memstat.m): returns 0 if not a guest thread
int thr_name_of(pthread_t pt, char *out, size_t n) {
    int ok = 0; char nb[64];
    pthread_mutex_lock(&G);
    for (thr_t *t = g_all; t; t = t->all) if (t->pt == pt && !t->exited) { snprintf(out, n, "%#x %s", t->handle, thr_name(t, nb, sizeof nb)); ok = 1; break; }
    pthread_mutex_unlock(&G);
    return ok;
}
uint32_t thr_cur_handle(void) { thr_t *t = pthread_getspecific(g_keythr); return t ? t->handle : 0; }   // Only reads TSD, usable in a signal handler
// Find a live guest thread by thread name (used by the sampler); returns 0 if not found
int thr_find_by_name(const char *name, pthread_t *out) {
    char nb[32]; int ok = 0;
    pthread_mutex_lock(&G);
    for (thr_t *t = g_all; t; t = t->all) if (!t->exited && !strcmp(thr_name(t, nb, sizeof nb), name)) { *out = t->pt; ok = 1; break; }
    pthread_mutex_unlock(&G);
    return ok;
}
// State dump (called from crash.c on watchdog / crash; read-only, takes no lock): what each guest thread is waiting on and where its most recent SVC was issued from
// During a state dump, take the backtrace of other guest threads: suspend → read registers → walk the frame pointer chain (both guest code and gate blocks keep the x29 chain) → resume; only main / sdk frames are printed
// The thread's registers, stack top and backtrace, gathered while it is suspended and printed once it runs again: it may hold the stderr lock (in the
// middle of a log line) or malloc's, and printing while it is stopped would then wait for it forever, the dumper and the game with it (the frames-
// stopped dump runs in the product, crash.c; plan item 107). Only snprintf into a buffer on this stack and vm reads happen while it is stopped.
// Unit test tests/t_threaddump.c
static void thr_bt(thr_t *t) {
    if (!t->hth || t->exited || t->hth == dm_hthread_self()) return;
    char o[8192]; int q = 0;
#define BT_PUT(...) do { if (q < (int)sizeof o) { int k_ = snprintf(o + q, sizeof o - (size_t)q, __VA_ARGS__); if (k_ > 0) q += k_; if (q > (int)sizeof o) q = (int)sizeof o; } } while (0)
#ifdef DM_TEST_BT_PRINT_SUSPENDED   // negative control of t_threaddump only: printed while the thread is stopped, as before
#define BT_FLUSH() do { fputs(o, stderr); q = 0; o[0] = 0; } while (0)
#else
#define BT_FLUSH() do { } while (0)
#endif
    o[0] = 0;
    if (dm_hthread_suspend(t->hth)) return;
    dm_regs st;
    if (!dm_hthread_regs(t->hth, &st, 0)) {
        // Threads not waiting in a system call (running guest code, maybe spinning on a condition): the callee-saved registers x19–x28, and 128 bytes
        // at each of x19 / x20 (often the object being waited for)
        if (t->wkind == 0) { uint64_t pc = st.pc; module_t *pm = module_of((void *)pc);
            BT_PUT("[dm]   %#x registers pc=", t->handle);
            if (pm) BT_PUT("%s+%#llx", pm->name, pc - (uint64_t)pm->base); else BT_PUT("%#llx", pc);
            for (int i = 19; i <= 28; i++) BT_PUT(" x%d=%#llx", i, st.x[i]);
            BT_PUT("\n");
            for (int i = 19; i <= 20; i++) { uint8_t raw[128]; uint64_t a = st.x[i];
                if (a < 0x100000000ull || !dm_mem_read(raw, a, sizeof raw)) continue;
                uint64_t vt = *(uint64_t *)raw; module_t *vm_ = module_of((void *)vt);
                BT_PUT("[dm]   %#x x%d → 128 bytes at %#llx", t->handle, i, a);
                if (vm_) BT_PUT(" (first 8 bytes = %s+%#llx)", vm_->name, vt - (uint64_t)vm_->base);
                BT_PUT(":"); for (int k = 0; k < 128; k += 4) BT_PUT(" %08x", *(uint32_t *)(raw + k));
                BT_PUT("\n"); } }
        uint64_t fp = st.fp, lr = st.lr;
        // Threads running guest code: print the 1 KiB above the stack top as is (the registers saved by the outer frames are in there, so the wait object and resource address can be recovered afterwards)
        if (t->wkind == 0 && module_of((void *)st.pc)) { uint64_t sp = st.sp, stk[128];   // Printed only if pc is inside a guest module: a thread in sleep / yield has
            if (sp > 0x100000000ull && dm_mem_read(stk, sp, sizeof stk))
                for (int r = 0; r < 128; r += 8) BT_PUT("[dm]   %#x stack sp+%#05x: %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx\n", t->handle, r * 8,
                        stk[r], stk[r + 1], stk[r + 2], stk[r + 3], stk[r + 4], stk[r + 5], stk[r + 6], stk[r + 7]); }
        BT_PUT("[dm]   %#x backtrace:", t->handle); int nf = 0, jumps = 0;
        for (int k = 0; k < 64 && fp && nf < 20; k++) {
            module_t *m = module_of((void *)lr);
            if (m) { BT_PUT(" %s+%#llx", m->name, lr - (uint64_t)m->base); nf++; }
            uint64_t fr[2];
            if (!dm_mem_read(fr, fp, 16) || !dm_fp_step_ok(fp, fr[0], &jumps)) break;
            fp = fr[0]; lr = fr[1];
        }
        BT_PUT("\n");
        BT_FLUSH();
    }
    dm_hthread_resume(t->hth);
    fputs(o, stderr);
#undef BT_PUT
#undef BT_FLUSH
}
#ifdef DM_TEST_HOOKS
void dm_test_thr_bt_all(void) { for (thr_t *t = g_all; t; t = t->all) thr_bt(t); }   // t_threaddump: the backtraces alone, without the header lines
int dm_test_newest_has_pt(void) { pthread_mutex_lock(&G); int r = g_all && g_all->pt != (pthread_t)0; pthread_mutex_unlock(&G); return r; }   // t_thrpub
int dm_test_g_held(void) { if (pthread_mutex_trylock(&G)) return 1; pthread_mutex_unlock(&G); return 0; }   // t_thrpub: G held by someone (this thread in an svc)
#endif
void svc_thread_dump(void) {
    static const char *kinds[] = {"running / not waiting", "lock", "condition variable", "address", "sync object"};
    int nh = 0; for (int i = 1; i < MAX_H; i++) nh += g_h[i].type != O_FREE;
    fprintf(stderr, "[dm] handle table %d / %d in use, %llu threads created\n", nh, MAX_H, g_next_id - 0x51);
    for (thr_t *t = g_all; t; t = t->all) {
        module_t *m = module_of((void *)t->last_lr);
        // Thread name: SDK ThreadType+0x1a0 is the name pointer (ThreadManager::SetInitialThreadNameUnsafe 0x34a2a4); x2 of CreateThread = ThreadType*
        char nb[32]; const char *nm = thr_name(t, nb, sizeof nb);
        double cpu = -1;
        if (t->hth && !t->exited) { int64_t us = dm_hthread_cpu_us(t->hth); if (us >= 0) cpu = us / 1e6; }
        char own[96] = ""; if (t->wkind == 1 && t->waddr) { uint32_t w = 0;   // 0x40000000 | Lock wait: lock word = owner handle | 0x40000000
            if (dm_mem_read(&w, t->waddr, 4)) snprintf(own, sizeof own, " owner %#x", w & ~WAITERS_BIT); }
        if (t->wkind == 2 && t->waddr) { uint32_t w = 0, k = 0;              // Condition wait: current values of the associated lock word and the condition key (the deadlock dump lacked this item)
            dm_mem_read(&w, t->waddr, 4); dm_mem_read(&k, t->wkey, 4);
            snprintf(own, sizeof own, " lock word %#x condition key %#llx=%#x", w, t->wkey, k); }
        uint8_t raw[64];                                                                     // Raw 64 bytes of the object being waited on (the structure holding the read-write lock / condition variable): to see the counts and the owner
        int have_raw = (t->wkind == 1 || t->wkind == 2 || t->wkind == 3) && t->waddr && dm_mem_read(raw, t->waddr & ~0xfull, 64);
        char life[96]; uint64_t nowns = now_ns();
        if (t->exited) snprintf(life, sizeof life, " exited %.1f s ago", (nowns - t->exit_ns) / 1e9);
        else if (!t->born_ns) snprintf(life, sizeof life, " since process start");
        else snprintf(life, sizeof life, " created %.1f s ago", (nowns - t->born_ns) / 1e9);
        fprintf(stderr, "[dm] thread %#x \"%s\" priority %d core %d (runs on %d) mask %#llx CPU %.2fs svc %llu %s%s%s waits:%s addr=%#llx%s last svc %#x lr=%s+%#llx%s\n", t->handle, nm, t->prio, t->core, t->act, (unsigned long long)t->mask,
                cpu, t->nsvc, t->exited ? " exited" : "", t->started ? "" : " not started", life, kinds[t->wkind & 7], t->waddr, own, t->last_svc,
                m ? m->name : "?", m ? t->last_lr - (uint64_t)m->base : t->last_lr,
                g_keythr && t == pthread_getspecific(g_keythr) ? " ← the thread writing this dump (after a CRASH line: the one that crashed)" : "");
        if (t->stack_low < t->stack_top) {   // the least free guest stack at an svc entry (item 40; the blood moon crash)
            if (t->stack_bot) fprintf(stderr, "[dm]   %#x guest stack %llu bytes, %llu of them left at the deepest svc entry\n", t->handle,
                                      (unsigned long long)(t->stack_top - t->stack_bot), (unsigned long long)(t->stack_low - t->stack_bot));
            else fprintf(stderr, "[dm]   %#x guest stack %llu bytes deep at the deepest svc entry (size not known)\n", t->handle, (unsigned long long)(t->stack_top - t->stack_low));
        }
        if (t->wkind == 4 && t->whn > 0) {   // which objects a WaitSynchronization waits for, and whether each is signaled now (the address column is not used by it)
            char hl[400]; int q = 0;
            for (int i = 0; i < t->whn && i < 8; i++) { uint32_t h = t->whp[i]; unsigned x = h - 0x1000; int ty = x && x < MAX_H ? g_h[x].type : O_FREE;
                const char *what = h == H_CUR_THREAD ? "current thread" : ty == O_THREAD ? (((thr_t *)g_h[x].obj)->exited ? "thread, exited" : "thread, running") :
                                   ty == O_EVENT_R ? (((event_t *)g_h[x].obj)->signaled ? "event, signaled" : "event, not signaled") : ty == O_EVENT_W ? "event write end (invalid here)" : "no such handle";
                q += snprintf(hl + q, sizeof hl - q, "%s%#x (%s)", i ? ", " : "", h, what); if (q >= (int)sizeof hl) break; }
            fprintf(stderr, "[dm]   %#x waits for %d handle%s: %s%s\n", t->handle, t->whn, t->whn == 1 ? "" : "s", hl, t->whn > 8 ? ", ..." : "");
        }
        if (have_raw) { char hx[200]; int q = 0; for (int i = 0; i < 64; i += 4) q += snprintf(hx + q, sizeof hx - q, "%08x ", *(uint32_t *)(raw + i));
                        fprintf(stderr, "[dm]   %#x 64 bytes at the awaited object %#llx: %s\n", t->handle, t->waddr & ~0xfull, hx); }
        thr_bt(t);
    }
    kt_dump(64);
    // Threads created by the host itself (prefetch, Granny background, graphics present, audio, etc.): not in the guest thread table, enumerated by the
    // platform (plat.h: every Mach thread; on Linux those that took a handle), the first 12 frames symbolized with dladdr
    static dm_hthread_t ths[512]; int nth = dm_hthreads_all(ths, 512);
    {
        for (int i = 0; i < nth; i++) {
            int guest = 0; for (thr_t *t = g_all; t; t = t->all) if (t->hth == ths[i]) { guest = 1; break; }
            if (!guest && ths[i] != dm_hthread_self() && !dm_hthread_suspend(ths[i])) {
                dm_regs st;
                if (!dm_hthread_regs(ths[i], &st, 0)) {
                    char line[1200]; int p = snprintf(line, sizeof line, "[dm] host thread %#lx:", (unsigned long)ths[i]);
                    uint64_t fp = st.fp, pc = st.pc;
                    for (int k = 0; k < 12 && p < (int)sizeof line - 80; k++) {
                        Dl_info di; module_t *m = module_of((void *)pc);
                        if (m) p += snprintf(line + p, sizeof line - p, " %s+%#llx", m->name, pc - (uint64_t)m->base);
                        else if (dladdr((void *)pc, &di) && di.dli_sname) p += snprintf(line + p, sizeof line - p, " %s+%#lx", di.dli_sname, (unsigned long)(pc - (uint64_t)di.dli_saddr));
                        else p += snprintf(line + p, sizeof line - p, " %#llx", pc);
                        uint64_t fr[2];
                        if (!fp || !dm_mem_read(fr, fp, 16) || fr[0] <= fp) break;
                        fp = fr[0]; pc = fr[1];
                    }
                    fprintf(stderr, "%s\n", line);
                }
                dm_hthread_resume(ths[i]);
            }
        }
        dm_hthreads_release(ths, nth);
    }
}

// Yield back-off (nxnative e06a8ec, MIT), off unless DM_YIELD_BACKOFF=<cap µs>[:<start µs>] (nxnative's 250:2000): on the Switch a yield with nothing
// else to run costs nothing, but a guest loop that polls the tick and yields becomes a host busy spin (Three Houses yielded 228,105 times a frame on an
// iPhone; there, Mac CPU energy 3.5 W -> 0.5 W at equal frame times). With it, a run of yields with no other SVC between them goes as plain yields for
// its first 2 ms (the start), then each yield sleeps an eighth of the run so far, 10 µs to the cap. The start was 200 µs: that caught Tears of the
// Kingdom's main thread waiting for its ModuleSystem workers (about 350 yields a frame, runs of a few hundred µs on the frame's critical path), and a
// sleep that overran the workers' finish pushed frames past 33 ms (nxnative b703be2): on an M5 the prologue then used more CPU energy (0.32-0.38 W
// against 0.21-0.29 W with plain yields, per 300 frames; wakeups 585 -> about 2,700 a second, the work moved from efficiency to performance cores) and
// one window ran at 44 ms p50. Waits under 2 ms are handoffs between threads that work; limiters and idle polls run far longer
static uint64_t yield_backoff_ns(uint64_t run_ns) {
    static int64_t cap = -1, start = 2000000;
    if (cap < 0) { const char *e = getenv("DM_YIELD_BACKOFF"); cap = e && *e ? atoll(e) * 1000 : 0;
                   const char *c = e ? strchr(e, ':') : NULL; if (c) start = atoll(c + 1) * 1000;
                   if (cap) LOG("DM_YIELD_BACKOFF: runs of yields back off after %lld µs, to sleeps of at most %lld µs", (long long)(start / 1000), (long long)(cap / 1000)); }
    if (!cap || run_ns < (uint64_t)start) return 0;
    uint64_t s = run_ns / 8; if (s < 10000) s = 10000;
    return s < (uint64_t)cap ? s : (uint64_t)cap;
}
// The two threads that yielded most since the last call (the power line every 300 frames, hle_nvn.c): "handle name N (back-off sleeps M)"
void dm_yield_top(char *out, size_t n) {
    thr_t *top[2] = {0}; uint64_t d[2] = {0}, b[2] = {0};
    pthread_mutex_lock(&G);
    for (thr_t *t = g_all; t; t = t->all) {
        uint64_t y = t->nyield - t->nyield_seen, bo = t->nbackoff - t->nbackoff_seen; t->nyield_seen = t->nyield; t->nbackoff_seen = t->nbackoff;
        if (y > d[0]) { top[1] = top[0]; d[1] = d[0]; b[1] = b[0]; top[0] = t; d[0] = y; b[0] = bo; } else if (y > d[1]) { top[1] = t; d[1] = y; b[1] = bo; }
    }
    size_t o = 0; char nb[32];
    for (int i = 0; i < 2 && top[i] && o < n; i++)
        o += snprintf(out + o, n - o, "%s%#x \"%s\" %llu (back-off sleeps %llu)", i ? ", " : "", top[i]->handle, thr_name(top[i], nb, sizeof nb), (unsigned long long)d[i], (unsigned long long)b[i]);
    pthread_mutex_unlock(&G);
    if (!o && n) snprintf(out, n, "none");
}
// Guest-thread CPU since the last call by Switch priority band (<= 44: the main thread's level and above; 45-59; >= 60) and the three busiest threads
// with their priority, for the power line every 300 frames (hle_nvn.c power_line; nxnative 451e118, MIT). secs = the window; a thread's first report
// only takes its baseline. Checked by host/tests/t_threadcpu.c
void dm_thread_cpu(char *out, size_t n, double secs) {
    thr_t *top[3] = {0}; double d[3] = {0}, band[3] = {0};
    pthread_mutex_lock(&G);
    for (thr_t *t = g_all; t; t = t->all) {
        if (!t->hth || t->exited) continue;
        int64_t cus = dm_hthread_cpu_us(t->hth); if (cus < 0) continue;
        uint64_t us = (uint64_t)cus;
        double ms = t->cpu_seen_us && us > t->cpu_seen_us && secs > 0 ? (double)(us - t->cpu_seen_us) / 1e3 / secs : 0; t->cpu_seen_us = us;   // ms per second
        band[t->prio <= 44 ? 0 : t->prio < 60 ? 1 : 2] += ms;
        for (int i = 0; i < 3; i++) if (ms > d[i]) { for (int j = 2; j > i; j--) { top[j] = top[j - 1]; d[j] = d[j - 1]; } top[i] = t; d[i] = ms; break; }
    }
    size_t o = (size_t)snprintf(out, n, "by Switch priority <=44 %.0f / 45-59 %.0f / >=60 %.0f ms/s; busiest: ", band[0], band[1], band[2]); char nb[32];
    for (int i = 0; i < 3 && top[i] && o < n; i++)
        o += (size_t)snprintf(out + o, n - o, "%s%#x \"%s\" priority %d %.0f ms/s%s", i ? ", " : "", top[i]->handle, thr_name(top[i], nb, sizeof nb), top[i]->prio, d[i], top[i]->host_work ? " (host work)" : "");
    if (!top[0] && o < n) snprintf(out + o, n - o, "none");
    pthread_mutex_unlock(&G);
}
// SleepThread sleeps to its deadline in steps: macOS timer slack made one nanosleep of 5 ms take 6.17 ms and one of 33.3 ms 37.6 ms on this M5
// (default QoS, mean of 60 / 20), and a game pacing itself with SleepThread ran slow (nxnative a9c56fd: Metroid Dread's 30 Hz sound thread drives its
// cutscenes, 27 fps instead of 30). Each step sleeps three quarters of what is left, until within 20 µs: within 0.03 ms here
static void sleep_precise(uint64_t deadline) {
    for (int i = 0; i < 16; i++) {
        uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW); if (now + 20000 >= deadline) return;
        uint64_t rem = deadline - now, s = rem < 100000 ? rem : rem - rem / 4;
        struct timespec ts = {(time_t)(s / 1000000000u), (long)(s % 1000000000u)}; nanosleep(&ts, 0);
    }
}
// The last kernel calls on locks and condition variables, lock-free, for the thread dump (LASRecomp's ktrace, MIT): which thread did what with
// which lock word or condition key, and the words then. Waits with a timeout are left out with their keys (a 1 ms poller would fill the ring within a
// second). DM_KTRACE=0 turns it off. DM_DEBUG_CV_LATE_WAIT=<µs> starts every condition variable wait that late: it forces signal-before-wait
// races in the game's code (LASRecomp reproduced a 1-in-6 boot hang 3 times of 3 that way)
enum { KT_LOCK, KT_LOCK_END, KT_UNLOCK, KT_WAIT, KT_WAIT_END, KT_SIGNAL, KTN = 256 };
typedef struct { uint64_t ns, addr, addr2; uint32_t thread, w0, w1, arg, kind; } ktrace_t;
static ktrace_t g_kt[KTN]; static _Atomic uint64_t g_kt_n; static int g_kt_on = -1;
static uint64_t g_kt_timed[16];   // keys seen with a timed wait (holding G)
static int kt_timed_key(uint64_t key, int add) {
    for (int i = 0; i < 16; i++) { if (g_kt_timed[i] == key) return 1; if (!g_kt_timed[i] && add) { g_kt_timed[i] = key; return 1; } }
    return 0;
}
static void kt(uint32_t kind, uint64_t addr, uint64_t addr2, uint32_t w0, uint32_t w1, uint32_t arg) {
    if (g_kt_on < 0) { const char *e = getenv("DM_KTRACE"); g_kt_on = !(e && !strcmp(e, "0")); }
    if (!g_kt_on) return;
    thr_t *me = pthread_getspecific(g_keythr);
    ktrace_t *e = &g_kt[atomic_fetch_add_explicit(&g_kt_n, 1, memory_order_relaxed) % KTN];
    *e = (ktrace_t){now_ns(), addr, addr2, me ? me->handle : 0, w0, w1, arg, kind};
}
static void kt_dump(int last) {
    static const char *const nm[] = {"lock", "lock got", "unlock", "cv wait", "cv woke", "cv signal"};
    uint64_t n = atomic_load(&g_kt_n), k = n < (uint64_t)last ? n : (uint64_t)last, t1 = now_ns();
    if (!k) return;
    fprintf(stderr, "[dm] the last %llu lock / condition variable calls (oldest first; DM_KTRACE=0 off):\n", (unsigned long long)k);
    for (uint64_t i = n - k; i < n; i++) { const ktrace_t *e = &g_kt[i % KTN]; char lk[40] = "";
        if (e->addr2) snprintf(lk, sizeof lk, " (lock %#llx)", (unsigned long long)e->addr2);
        fprintf(stderr, "[dm]   %8.3f s ago  %#x %-9s %#llx%s words %#x -> %#x arg %#x\n", (t1 - e->ns) / 1e9, e->thread, nm[e->kind < 6 ? e->kind : 0],
                (unsigned long long)e->addr, lk, e->w0, e->w1, e->arg); }
}
int svc_thread(int num, uint64_t *regs) {
    { thr_t *me = pthread_getspecific(g_keythr); if (me) { me->last_svc = num; me->last_lr = regs[30]; me->nsvc++; core_checkpoint(me); } }
    if (sched_on()) {
        sched_svc(&g_sched, num); static _Atomic uint64_t next; uint64_t t = now_ns(), nx = atomic_load(&next);
        if (!nx) { pthread_mutex_lock(&G); if (!g_sched.t0) g_sched.t0 = t; pthread_mutex_unlock(&G); atomic_compare_exchange_strong(&next, &nx, t + 5000000000ull); }
        else if (t >= nx && atomic_compare_exchange_strong(&next, &nx, t + 5000000000ull)) {
            char line[1200]; pthread_mutex_lock(&G); sched_report(&g_sched, t, line, sizeof line); pthread_mutex_unlock(&G); LOG("[sched] %s", line);
        }
    }
    if (g_flag.thr && num != 0x0B) { thr_t *me = pthread_getspecific(g_keythr); LOG("[%#x] svc %#x x0=%#llx x1=%#llx x2=%#llx x3=%#llx", me ? me->handle : 0, num, regs[0], regs[1], regs[2], regs[3]); }
    switch (num) {
    case 0x32: svc_thread_activity(regs); return 1;   // SetThreadActivity(handle, 0 runnable / 1 paused)
    case 0x33: svc_thread_context3(regs); return 1;   // GetThreadContext3(→context, handle)
    case 0x08: {  // CreateThread(→w1 handle; x1 entry, x2 argument, x3 stack top, w4 priority, w5 core)
        thr_t *t = calloc(1, sizeof *t);
        pthread_cond_init(&t->cv, NULL);
        t->entry = regs[1]; t->arg = regs[2]; t->stack_top = t->stack_low = regs[3] & ~0xFull;
        { uint64_t sz = 0, b = dm_mapping_of(t->stack_top - 16, &sz); if (b && t->stack_top <= b + sz) t->stack_bot = b; }   // the SDK maps each thread's stack (MapMemory)
        int c = (int)regs[5] == -2 ? PROC_IDEAL_CORE : (int)regs[5];   // -2 = IdealCoreUseProcessValue
        if (c < 0 || c >= 64 || !(PROC_CORE_MASK >> c & 1)) { pthread_cond_destroy(&t->cv); free(t); regs[0] = R_BAD_CORE; return 1; }
        t->prio = (int)regs[4]; t->core = t->act = c; t->mask = 1ull << c; t->cur_core = -1;   // a new thread may run only on its ideal core (KThread::Initialize)
        { thr_t *me = pthread_getspecific(g_keythr); if (me && me->next_host) { me->next_host = 0; t->host_work = 1; } }
        t->born_ns = now_ns();
        pthread_mutex_lock(&G); t->handle = h_new(O_THREAD, t); t->id = g_next_id++;
#ifdef DM_TEST_THREAD_LINK_EARLY   // negative control of t_thrpub only: in the registry before pthread_create writes t->pt, as before
        t->all = g_all; g_all = t; pthread_mutex_unlock(&G); THR_TEST_LINKED(); pthread_mutex_lock(&G);
#endif
        pthread_mutex_unlock(&G);
        pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 1 << 20);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);   // Host resources are reclaimed on exit (the guest waits through the handle, no join)
        // pthread_create writes its output without G, while the registry's readers (thr_name_of, the samplers) read t->pt under G: that value and the
        // registry link are published together under it (seedds/nxnative ce99604, MIT; plan item 296)
        pthread_t pt; if (pthread_create(&pt, &a, thread_main, t)) die("pthread_create");
        pthread_attr_destroy(&a);
        pthread_mutex_lock(&G); t->pt = pt;
#ifndef DM_TEST_THREAD_LINK_EARLY
        t->all = g_all; g_all = t;
#endif
        pthread_mutex_unlock(&G);
#ifndef DM_TEST_THREAD_LINK_EARLY
        THR_TEST_LINKED();
#endif
        if (g_flag.thr) LOG("CreateThread h=%#x entry=%#llx arg=%#llx sp=%#llx prio=%d core=%d", t->handle, t->entry, t->arg, t->stack_top, t->prio, t->core);
        regs[0] = 0; regs[1] = t->handle; return 1;
    }
    case 0x09: {  // StartThread(w0)
        pthread_mutex_lock(&G);
        thr_t *t = h_get((uint32_t)regs[0], O_THREAD);
        if (t) { t->started = 1; pthread_cond_signal(&t->cv); }
        pthread_mutex_unlock(&G);
        regs[0] = t ? 0 : R_BAD_HANDLE; return 1;
    }
    case 0x0A: thread_exit(cur()); return 1;
    case 0x0B: {  // SleepThread(x0 ns); 0/-1/-2 = the various kinds of yield
        int64_t ns = (int64_t)regs[0];
        uint64_t t0 = g_flag.bootprof ? now_ns() : 0;
        thr_t *self = pthread_getspecific(g_keythr);
        if (ns > 0 && self && self->cur_core >= 0) { pthread_mutex_lock(&G); core_put(self); pthread_mutex_unlock(&G); }   // a sleeping thread leaves its core
        if (ns > 0 && dm_detclock_enabled()) {
            uint64_t deadline = guest_ns() + (uint64_t)ns;
            while (guest_ns() < deadline) { struct timespec ts = {0, 1000000}; nanosleep(&ts, 0); }
        } else if (ns > 0) sleep_precise(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + (uint64_t)ns);
        else {   // Yield (0 / -1 / -2). Count and time of the main thread's yields are recorded: the game's render task graph "wait for everything to finish" is pure polling + yield (main+0x15ee898); measure how much of a frame it takes
            thr_t *me = pthread_getspecific(g_keythr); uint64_t y0 = me ? now_ns() : 0;
            uint64_t bo = 0;
            if (me) { me->nyield++; if (me->yrun_svc + 1 != me->nsvc) me->yrun_t0 = y0; me->yrun_svc = me->nsvc; bo = yield_backoff_ns(y0 - me->yrun_t0); }
            if (bo) {   // a long run of yields: sleep a little, the virtual core given up meanwhile
                me->nbackoff++;
                if (me->cur_core >= 0) { pthread_mutex_lock(&G); core_put(me); pthread_mutex_unlock(&G); }
                struct timespec ts = {0, (long)bo}; nanosleep(&ts, 0);
                if (cores_enabled()) { pthread_mutex_lock(&G); core_take(me); pthread_mutex_unlock(&G); }
            } else {
                int give = 0;   // Horizon yield: the core goes to a waiting thread of equal or higher priority
                if (me && me->cur_core >= 0) { pthread_mutex_lock(&G); for (thr_t *w = g_core_wait; w && !give; w = w->cnext) give = core_allowed(w, me->cur_core) && w->prio <= me->prio;
                                               if (give) { core_put(me); core_take(me); } pthread_mutex_unlock(&G); }
                if (!give) sched_yield();
            }
            if (me && me->handle == 0x1001) { g_main_yield_ns += now_ns() - y0; g_main_yield_n++; }
        }
        if (ns > 0 && self && cores_enabled()) { pthread_mutex_lock(&G); core_take(self); pthread_mutex_unlock(&G); }
        if (g_flag.bootprof) { uint64_t d = now_ns() - t0; dm_prof_add(PROF_SLEEP, d, 0); thr_t *me = pthread_getspecific(g_keythr); if (me && me->handle == 0x1001) dm_prof_add(PROF_MAIN_WAIT, d, 0); }
        return 1;
    }
    // A thread's priority is the scheduler's (it compares them under G): read and set under G as well (seedds/nxnative ce99604; plan item 296)
#ifndef DM_TEST_PRIO_UNLOCKED   // negative control of t_thrpub only: without G, as before
#define PRIO_LOCK() pthread_mutex_lock(&G)
#define PRIO_UNLOCK() pthread_mutex_unlock(&G)
#else
#define PRIO_LOCK() do { } while (0)
#define PRIO_UNLOCK() do { } while (0)
#endif
    case 0x0C: { PRIO_LOCK(); thr_t *t = h_get((uint32_t)regs[1], O_THREAD); regs[0] = t ? 0 : R_BAD_HANDLE; regs[1] = t ? (uint64_t)t->prio : 0; PRIO_UNLOCK(); return 1; }
    case 0x0D: { PRIO_LOCK(); thr_t *t = h_get((uint32_t)regs[0], O_THREAD); if (t) t->prio = (int)regs[1]; THR_TEST_PRIO_SET(); regs[0] = t ? 0 : R_BAD_HANDLE; PRIO_UNLOCK(); return 1; }
    case 0x0E: { thr_t *t = h_get((uint32_t)regs[2], O_THREAD);
                 regs[0] = t ? 0 : R_BAD_HANDLE; regs[1] = t ? (uint64_t)(uint32_t)t->core : 0; regs[2] = t ? t->mask : 0; return 1; }
    case 0x0F: {  // SetThreadCoreMask(w0, w1 ideal core, x2 mask): -1 = don't care, -2 = the process's ideal core (mask = just it), -3 = keep the ideal core
        // As the Horizon kernel (Atmosphere mesosphere svc SetThreadCoreMask + KThread::SetCoreMask): validate, then if the core the thread runs on
        // leaves the mask it moves to the ideal core, or with no ideal core to the highest core of the mask. The host does not pin threads; it keeps
        // this bookkeeping because GetCurrentProcessorNumber answers from it: TotK's job system picks a per-worker output block by the current core
        // number (ModuleSystemWorker0..2, masks 1/2/4), and answering the ideal core 0 for all of them made three threads fill one block (main+0x7ad80)
        thr_t *t = h_get((uint32_t)regs[0], O_THREAD); int c = (int)regs[1]; uint64_t m = regs[2]; uint32_t r = 0;
        if (!t) r = R_BAD_HANDLE;
        else if (c == -2) { c = PROC_IDEAL_CORE; m = 1ull << c; }
#ifndef DM_TEST_CORE_NO_PROCMASK   // negative control of t_coremask only: cores outside the process's mask let through
        else if ((m | PROC_CORE_MASK) != PROC_CORE_MASK) r = R_BAD_CORE;
#endif
        else if (!m) r = R_BAD_COMBO;
        else if (c >= 0 && c < 4) { if (!(m >> c & 1)) r = R_BAD_COMBO; }   // a valid virtual core (0..3 on the Switch's four) must be in the mask
        else if (c != -3 && c != -1) r = R_BAD_CORE;
        if (!r && c == -3) { c = t->core; if (c < 0 || !(m >> c & 1)) r = R_BAD_COMBO; }
        if (!r) { t->core = c; t->mask = m; if (!(m >> t->act & 1)) t->act = c >= 0 ? c : 63 - __builtin_clzll(m);
                  if (t == cur() && t->cur_core >= 0 && !core_allowed(t, t->cur_core)) { pthread_mutex_lock(&G); core_put(t); core_take(t); pthread_mutex_unlock(&G); } }   // leaves a core it may no longer use
        if (g_flag.thr) LOG("SetThreadCoreMask h=%#x core %d mask %#llx -> %#x (ideal %d, runs on %d)", (unsigned)regs[0], (int)regs[1], (unsigned long long)regs[2], r, t ? t->core : 0, t ? t->act : 0);
        regs[0] = r; return 1;
    }
    case 0x10: { thr_t *t = cur(); regs[0] = (uint64_t)(uint32_t)(t->cur_core >= 0 ? t->cur_core : t->act); return 1; }   // GetCurrentProcessorNumber: the core the thread runs on (the virtual core it holds), not the ideal core
    case 0x25: { thr_t *t = h_get((uint32_t)regs[1], O_THREAD); regs[0] = t ? 0 : R_BAD_HANDLE; regs[1] = t ? t->id : 0; return 1; }

    case 0x1A: {  // ArbitrateLock(w0 owner handle, x1 address, w2 this thread's tag)
        thr_t *t = cur(); uint64_t addr = regs[1];
        pthread_mutex_lock(&G);
        int64_t r = 0; uint32_t v = ld32(addr);
        kt(KT_LOCK, addr, 0, v, 0, (uint32_t)regs[0]);
        if (v == ((uint32_t)regs[0] | WAITERS_BIT)) { t->waddr = addr; t->wtag = (uint32_t)regs[2]; r = block(t, 1, -1); kt(KT_LOCK_END, addr, 0, ld32(addr), 0, (uint32_t)r); }
        pthread_mutex_unlock(&G);
        regs[0] = (uint64_t)r; return 1;
    }
    case 0x1B: { pthread_mutex_lock(&G); uint32_t b = ld32(regs[0]); lock_handoff(regs[0]); kt(KT_UNLOCK, regs[0], 0, b, ld32(regs[0]), 0); pthread_mutex_unlock(&G); regs[0] = 0; return 1; }
    case 0x1C: {  // WaitProcessWideKeyAtomic(x0 lock address, x1 condition key, w2 tag, x3 timeout)
        thr_t *t = cur(); uint64_t addr = regs[0], key = regs[1];
        { static long late = -1; if (late < 0) { const char *e = getenv("DM_DEBUG_CV_LATE_WAIT"); late = e ? atol(e) : 0; } if (late > 0) usleep((useconds_t)late); }
        pthread_mutex_lock(&G);
        int traced = !kt_timed_key(key, (int64_t)regs[3] >= 0); uint32_t kb = ld32(key), mb = ld32(addr);
        // The order matters: first set the condition key = 1, then release the mutex. The SDK's InternalConditionVariable::Signal first reads the condition key in user mode, and does not issue svc 0x1D if it is 0;
        // grabbing the mutex in user mode does not go through G either. It used to do lock_handoff first and set the key after: after the release and before the key was set, another thread could grab the lock in user mode, change the condition, read key 0 and not wake anyone,
        // and this thread would then link itself in and wait forever (under parallel load the resource table read-write lock main+0x320e5e0 deadlocked: reader count 0, lock free, writer condition key 1, writer still waiting).
        // Linking into the wait chain happens inside block, still holding G: svc 0x1D has to take G, so it cannot land before this
#ifdef DM_TEST_HOOKS
#define CV_TEST_HOOK() { extern void (*dm_test_cv_after_release)(void); if (dm_test_cv_after_release) dm_test_cv_after_release(); }   // Unit test: right after releasing the lock, before linking into the wait chain
#else
#define CV_TEST_HOOK()
#endif
#ifndef DM_TEST_CV_OLD_ORDER
        st32(key, 1);
        t->wkey = key; t->waddr = addr; t->wtag = (uint32_t)regs[2];
        lock_handoff(addr); CV_TEST_HOOK();
#else   // Negative control for the unit test t_condvar only: restores the pre-fix order "release the lock first, set the key after"
        lock_handoff(addr); CV_TEST_HOOK();
        st32(key, 1);
        t->wkey = key; t->waddr = addr; t->wtag = (uint32_t)regs[2];
#endif
        if (traced) kt(KT_WAIT, key, addr, kb, mb, ld32(addr));
        int64_t r = block(t, 2, (int64_t)regs[3]);   // Timeout: the lock is not re-acquired (user mode takes it itself)
        if (traced) kt(KT_WAIT_END, key, addr, ld32(key), ld32(addr), (uint32_t)r);
        pthread_mutex_unlock(&G);
        regs[0] = (uint64_t)r; return 1;
    }
    case 0x1D: {  // SignalProcessWideKey(x0 condition key, w1 count, <=0 all)
        uint64_t key = regs[0]; int cnt = (int)regs[1], n = 0;
        pthread_mutex_lock(&G);
        for (thr_t *w; (cnt <= 0 || n < cnt) && (w = pick(2, key)); n++) lock_acquire_for(w, w->waddr, w->wtag);   // In kernel order; once turned into a lock wait or woken it is no longer kind 2
        int left = 0; for (thr_t *w = g_wait; w; w = w->next) if (w->wkind == 2 && w->wkey == key) { left = 1; break; }
        if (!left) st32(key, 0);
        if (!kt_timed_key(key, 0)) kt(KT_SIGNAL, key, 0, (uint32_t)n, (uint32_t)left, (uint32_t)cnt);
        pthread_mutex_unlock(&G);
        regs[0] = 0; return 1;
    }

    case 0x34: {  // WaitForAddress(x0 address, w1 type, x2 value, x3 timeout): 0 wait if less than, 1 decrement if less than then wait, 2 wait if equal,
                  // 3 wait if equal on a 64-bit value (nn::os::WaitForAddress(long const*, long) uses it). As Horizon's address arbiter (checked against
                  // Atmosphère): the address aligned to the value's size (InvalidAddress), then the type (InvalidEnumValue); type 3 used to be taken as
                  // "less than" on 32 bits
        thr_t *t = cur(); uint64_t addr = regs[0]; int type = (int)regs[1]; int32_t val = (int32_t)regs[2];
        if (addr & (type == 3 ? 7 : 3)) { regs[0] = R_BAD_ADDR; return 1; }
        if ((unsigned)type > 3) { regs[0] = R_BAD_ENUM; return 1; }
        pthread_mutex_lock(&G);
        int64_t r; uint32_t v = ld32(addr);
        int ok = type == 3 ? ld64(addr) == regs[2] : type == 2 ? (int32_t)v == val : (int32_t)v < val;
        if (ok && type == 1) { while (!cas32(addr, &v, v - 1)) if (!((int32_t)v < val)) { ok = 0; break; } }
        if (!ok) r = R_BAD_STATE;
        else { t->waddr = addr; r = block(t, 3, (int64_t)regs[3]); }
        pthread_mutex_unlock(&G);
        regs[0] = (uint64_t)r; return 1;
    }
    case 0x35: {  // SignalToAddress(x0 address, w1 type, w2 value, w3 count): 0 wake, 1 increment if equal and wake, 2 if equal modify the value by waiter count and wake
        uint64_t addr = regs[0]; int type = (int)regs[1]; int32_t val = (int32_t)regs[2]; int cnt = (int)regs[3];
        if (addr & 3) { regs[0] = R_BAD_ADDR; return 1; }
        if ((unsigned)type > 2) { regs[0] = R_BAD_ENUM; return 1; }   // it used to take any other type as "increment if equal"
        pthread_mutex_lock(&G);
        int nw = 0; for (thr_t *w = g_wait; w; w = w->next) if (w->wkind == 3 && w->waddr == addr) nw++;
        int64_t r = 0;
        if (type != 0) {
            int32_t nv = val + 1;
            if (type == 2) {
                if (cnt <= 0) nv = nw > 0 ? val - 1 : val + 1;
                else nv = nw == 0 ? val + 1 : nw <= cnt ? val - 1 : val;
            }
            uint32_t e = (uint32_t)val;
            if (!cas32(addr, &e, (uint32_t)nv)) r = R_BAD_STATE;
        }
        if (!r) { int n = 0;
            for (thr_t *w; (cnt <= 0 || n < cnt) && (w = pick(3, addr)); n++) wake(w, 0); }
        pthread_mutex_unlock(&G);
        regs[0] = (uint64_t)r; return 1;
    }

    case 0x45: {  // CreateEvent(→w1 write end, w2 read end)
        event_t *e = calloc(1, sizeof *e); e->refs = 2;
        pthread_mutex_lock(&G); uint32_t hw = h_new(O_EVENT_W, e), hr = h_new(O_EVENT_R, e); pthread_mutex_unlock(&G);
        regs[0] = 0; regs[1] = hw; regs[2] = hr; return 1;
    }
    case 0x11: case 0x12: case 0x17: {  // SignalEvent(write end) / ClearEvent(either end) / ResetSignal(read end, error if not signaled)
        pthread_mutex_lock(&G);
        // SignalEvent accepts only the write end; ClearEvent accepts both ends; ResetSignal accepts only the read end
        uint32_t h = (uint32_t)regs[0];
        event_t *e = num == 0x11 ? h_peek(h, O_EVENT_W) : num == 0x17 ? h_peek(h, O_EVENT_R)
                   : (h_peek(h, O_EVENT_W) ? h_peek(h, O_EVENT_W) : h_peek(h, O_EVENT_R));
        int64_t r = 0;
        if (!e) { r = (num == 0x17 && h_peek(h, O_THREAD)) ? R_BAD_STATE : R_BAD_HANDLE;
                  if (r == R_BAD_HANDLE) LOG("event SVC %#x: invalid handle %#x", num, h); }
        else if (num == 0x11) { e->signaled = 1; wake_sync_waiters(e); }
        else if (num == 0x17 && !e->signaled) r = R_BAD_STATE;
        else e->signaled = 0;
        pthread_mutex_unlock(&G);
        regs[0] = (uint64_t)r; return 1;
    }
    case 0x18: {  // WaitSynchronization(→w1 index; x1 handle array, w2 count, x3 timeout)
        thr_t *t = cur(); const uint32_t *hs = (const uint32_t *)regs[1]; int n = (int)regs[2]; int64_t ns = (int64_t)regs[3];
        int64_t deadline = ns > 0 ? (dm_detclock_enabled() ? (int64_t)guest_ns() : now_mono_ns()) + ns : 0;
        pthread_mutex_lock(&G);
        int64_t r; int idx = -1;
        // Horizon's order (KSynchronizationObject::Wait, checked against Atmosphère; LASRecomp 1144aea): a signaled object, then timeout 0, then a
        // pending CancelSynchronization (cleared), then the wait; the flag survives a successful wait and a poll (t_waitsync)
        for (;;) {
            int bad = 0;
            for (int i = 0; i < n && idx < 0; i++) if (sig_obj(hs[i], &bad)) idx = i;
            if (bad) { r = R_BAD_HANDLE; break; }
            if (idx >= 0) { r = 0; break; }
            if (ns == 0) { r = R_TIMEOUT; break; }
            if (t->cancel) { t->cancel = 0; r = R_CANCELLED; break; }
            int64_t left = ns < 0 ? -1 : deadline - (dm_detclock_enabled() ? (int64_t)guest_ns() : now_mono_ns());
            if (ns > 0 && left <= 0) { r = R_TIMEOUT; break; }
            t->whp = hs; t->whn = n > 0 ? n : 0;
            int64_t br = block(t, 4, left);
            if (br == R_TIMEOUT) { r = R_TIMEOUT; break; }
            if (br == R_CANCELLED) { r = R_CANCELLED; break; }   // CancelSynchronization while blocked
        }
        pthread_mutex_unlock(&G);
        regs[0] = (uint64_t)r; regs[1] = idx >= 0 ? (uint64_t)idx : 0; return 1;
    }
    case 0x19: {  // CancelSynchronization(w0 thread)
        pthread_mutex_lock(&G);
        thr_t *t = h_get((uint32_t)regs[0], O_THREAD);
        if (t) { if (t->wkind == 4) wake(t, R_CANCELLED); else t->cancel = 1; }   // a blocked wait ends cancelled; otherwise the next wait finds the flag
        pthread_mutex_unlock(&G);
        regs[0] = t ? 0 : R_BAD_HANDLE; return 1;
    }
    case 0x16: {  // CloseHandle(w0): an event is freed only when both ends are closed; after a thread handle is closed the slot is recycled, and the object is freed once the thread has also exited
        pthread_mutex_lock(&G);
        unsigned i = (uint32_t)regs[0] - 0x1000;
        if (i > 0 && i < MAX_H && (g_h[i].type == O_EVENT_R || g_h[i].type == O_EVENT_W)) {
            if (g_h[i].type == O_EVENT_W) se_unbind((uint32_t)regs[0]);
            event_t *e = g_h[i].obj; if (--e->refs == 0) free(e);
            g_h[i].type = O_FREE; g_h[i].obj = NULL;
        } else if (i > 0 && i < MAX_H && g_h[i].type == O_THREAD) {
            thr_t *t = g_h[i].obj;
            g_h[i].type = O_FREE; g_h[i].obj = NULL;
            t->closed = 1;
            if (t->exited) thr_free(t);
        }
        pthread_mutex_unlock(&G);
        regs[0] = 0; return 1;
    }
    }
    return 0;
}

// ---------- SystemEvent of the service layer ----------
// Real device: the service hands the read end of a kernel event to the client, and the guest waits on it through SDK nn::os. Host: on a guest thread, call the SDK's own
// CreateSystemEvent(inter-process=true), and the SDK creates the kernel event through SVC 0x45; the write-end handle is recorded, after which any thread (including the host audio thread) can signal it.
// The binding table (g_se) sits with the handle table at the top: CloseHandle unbinds through it
void hle_bind_system_event(void *ev, int autoclear) {      // Must be called on a guest thread (it has to execute SDK code)
    pthread_mutex_lock(&g_se_lock); uint32_t wh = se_find(ev); pthread_mutex_unlock(&g_se_lock);
    if (wh) return;
    if (!pthread_getspecific(g_keythr)) die("hle_bind_system_event must be called on a guest thread");
    static uint32_t (*create)(void *, int, int); static uint32_t (*getw)(const void *);
    if (!create) { create = guest_sym("_ZN2nn2os17CreateSystemEventEPNS0_15SystemEventTypeENS0_14EventClearModeEb");
                   getw = guest_sym("_ZN2nn2os30GetWritableHandleOfSystemEventEPKNS0_15SystemEventTypeE"); }
    uint32_t r = create(ev, autoclear, 1);
    if (r) die("CreateSystemEvent failed: %#x", r);
    wh = getw(ev);
    pthread_mutex_lock(&g_se_lock);
    if (g_nse >= (int)(sizeof g_se / sizeof g_se[0])) die("SystemEvent binding table full");
    g_se[g_nse].ev = ev; g_se[g_nse++].wh = wh;
    pthread_mutex_unlock(&g_se_lock);
}
void hle_signal_system_event(void *ev) {   // An unbound one is bound on the spot as manual-clear (can only happen on a guest thread)
    for (int bound = 0;; bound = 1) {   // the lookup and the signal under one hold of G: a CloseHandle cannot come between them
        pthread_mutex_lock(&G);
        pthread_mutex_lock(&g_se_lock); uint32_t wh = se_find(ev); pthread_mutex_unlock(&g_se_lock);
        if (wh) {
            event_t *e = h_get(wh, O_EVENT_W);
            if (!e) die("SystemEvent writable handle invalid: %#x", wh);
            e->signaled = 1; wake_sync_waiters(e);
            pthread_mutex_unlock(&G); return;
        }
        pthread_mutex_unlock(&G);
        if (bound) die("SystemEvent %p still unbound after binding it", ev);
        hle_bind_system_event(ev, 0);   // runs guest code (its SVCs take G): outside the lock
    }
}
