// svc 0x18 WaitSynchronization with a pending CancelSynchronization, in Horizon's order (KSynchronizationObject::Wait, checked against Atmosphère:
// termination, a signaled object, timeout 0, then the cancel flag, which is cleared): a signaled handle returns its index and a poll returns
// timed out, both keeping the flag, and only a wait that would block returns cancelled. A thread blocked in the wait when it is cancelled
// returns cancelled and keeps no flag. (LASRecomp 1144aea found the order on the same symptom; ours checked the flag first.)
// The thread dump lists the handles a blocked WaitSynchronization waits for, with each one's state (it showed only an unrelated address before).
// A signal wakes only the waiters holding a handle to that event: two threads blocked on different events, signalling one wakes one (it woke both).
// A signal completes the wait (plan item 312, seedds/nxnative db206a4): an event signalled and cleared before its waiter runs again still ends the
// wait with its index; looking at the handles again after the wake (-DDM_TEST_SYNC_RESCAN, as before) sleeps on to the 300 ms timeout: 1 fails.
#include "../dm.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
void (*dm_test_cv_after_release)(void);   // DM_TEST_HOOKS (t_condvar uses it)
void svc_thread_dump(void);
extern uint64_t g_sync_wakes;
static int n, fail;
#define EXPECT(name, want, got) do { unsigned long long w_ = (want), g_ = (got); n++; printf("%-74s expected %#-8llx → got %#-8llx %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); fail += w_ != g_; } while (0)
enum { R_TIMEOUT = 0xEA01, R_CANCELLED = 0xEC01 };
static uint32_t g_th, g_rd; static _Atomic int g_waiting; static uint64_t g_blocked_rc = 1;
static uint32_t g_w2, g_r2, g_r3; static _Atomic int g_waiting2; static uint64_t g_idx2 = 9;
static uint64_t wait1(uint32_t h, int64_t ns, uint64_t *idx) { uint64_t r[8] = {0, (uint64_t)&h, 1, (uint64_t)ns}; svc_thread(0x18, r); if (idx) *idx = r[1]; return r[0]; }
static void cancel(uint32_t th) { uint64_t r[8] = {th}; svc_thread(0x19, r); }
static uint32_t g_ea_w, g_ea_r, g_eb_w, g_eb_r; static _Atomic int g_blocked_ab;
extern void (*dm_test_sync_woken)(void);
static uint32_t g_ec_w, g_ec_r; static _Atomic int g_blocked_c; static uint64_t g_rc_c = 1, g_idx_c = 9;
static void clear_c(void) { dm_test_sync_woken = NULL; uint64_t c[8] = {g_ec_r}; svc_thread(0x12, c); }   // the waiter woken, not yet running: the event cleared
static void *waiter_c(void *a) {   // a 300 ms wait on event C
    (void)a; svc_thread_main_init(); uint32_t h = g_ec_r; uint64_t w[8] = {0, (uint64_t)&h, 1, 300000000};
    atomic_store(&g_blocked_c, 1); svc_thread(0x18, w); g_rc_c = w[0]; g_idx_c = w[1]; return NULL;
}
static void *waiter(void *a) {   // blocks on one event's read end until it is signaled
    svc_thread_main_init(); uint32_t h = (uint32_t)(uintptr_t)a; uint64_t w[8] = {0, (uint64_t)&h, 1, (uint64_t)-1};
    atomic_fetch_add(&g_blocked_ab, 1); svc_thread(0x18, w); return NULL;
}
static void *guest(void *a) {
    (void)a; g_th = svc_thread_main_init();
    uint64_t r[8] = {0}; svc_thread(0x45, r); uint32_t wr = (uint32_t)r[1]; g_rd = (uint32_t)r[2];
    cancel(g_th);                                                                            // not waiting: the flag is set
    uint64_t s[8] = {wr}; svc_thread(0x11, s);                                               // signal
    uint64_t idx = 9; EXPECT("cancel pending, handle signaled: success (index 0)", 0, wait1(g_rd, -1, &idx)); EXPECT("... index", 0, idx);
    uint64_t c[8] = {g_rd}; svc_thread(0x12, c);                                             // clear
    EXPECT("cancel pending, nothing signaled, timeout 0: timed out", R_TIMEOUT, wait1(g_rd, 0, NULL));
    EXPECT("cancel pending, a wait that would block: cancelled", R_CANCELLED, wait1(g_rd, 1000000, NULL));
    EXPECT("the flag is used up: the next poll times out", R_TIMEOUT, wait1(g_rd, 0, NULL));
    atomic_store(&g_waiting, 1); g_blocked_rc = wait1(g_rd, -1, NULL);                        // cancelled while blocked (main thread)
    EXPECT("cancelled while blocked: cancelled", R_CANCELLED, g_blocked_rc);
    EXPECT("... and no flag is left: the next poll times out", R_TIMEOUT, wait1(g_rd, 0, NULL));
    // blocked on two events for the thread dump (the main thread dumps, then signals the first)
    uint64_t e2[8] = {0}; svc_thread(0x45, e2); g_w2 = (uint32_t)e2[1]; g_r2 = (uint32_t)e2[2];
    uint64_t e3[8] = {0}; svc_thread(0x45, e3); g_r3 = (uint32_t)e3[2];
    uint32_t hs[2] = {g_r2, g_r3}; uint64_t w[8] = {0, (uint64_t)hs, 2, (uint64_t)-1};
    atomic_store(&g_waiting2, 1); svc_thread(0x18, w); g_idx2 = w[1];
    EXPECT("woken by the first event: index 0", 0, g_idx2);
    return NULL;
}
int main(void) {
    pthread_t t; pthread_create(&t, NULL, guest, NULL);
    while (!atomic_load(&g_waiting)) usleep(1000);
    usleep(50000); cancel(g_th);
    while (!atomic_load(&g_waiting2)) usleep(1000);
    usleep(50000);
    const char *td = getenv("TMPDIR"); char pth[1024]; snprintf(pth, sizeof pth, "%s/t_waitsync_dump.XXXXXX", td ? td : "/private/tmp");
    int fd = mkstemp(pth), save = dup(2); fflush(stderr); dup2(fd, 2); svc_thread_dump(); fflush(stderr); dup2(save, 2); close(save);
    char dump[65536] = {0}; lseek(fd, 0, SEEK_SET); ssize_t dn = read(fd, dump, sizeof dump - 1); close(fd); unlink(pth); (void)dn;
    char a[128]; snprintf(a, sizeof a, "waits for 2 handles: %#x (event, not signaled), %#x (event, not signaled)", g_r2, g_r3);
    EXPECT("the dump names both awaited handles and their state", 1, strstr(dump, a) != NULL);
    uint64_t s2[8] = {g_w2}; svc_thread(0x11, s2);   // release the waiter
    pthread_join(t, NULL);
    // targeted wakes: two waiters on two events; signalling A wakes one waiter, not both
    { uint64_t r[8] = {0}; svc_thread(0x45, r); g_ea_w = (uint32_t)r[1]; g_ea_r = (uint32_t)r[2]; svc_thread(0x45, r); g_eb_w = (uint32_t)r[1]; g_eb_r = (uint32_t)r[2];
      // (svc_thread_main_init remakes the thread key, so B registers only once A is inside its wait)
      pthread_t ta, tb; pthread_create(&ta, NULL, waiter, (void *)(uintptr_t)g_ea_r); while (atomic_load(&g_blocked_ab) < 1) usleep(1000); usleep(20000);
      pthread_create(&tb, NULL, waiter, (void *)(uintptr_t)g_eb_r); while (atomic_load(&g_blocked_ab) < 2) usleep(1000);
      usleep(50000);
      uint64_t w0 = g_sync_wakes; uint64_t sa[8] = {g_ea_w}; svc_thread(0x11, sa); pthread_join(ta, NULL);
      EXPECT("signal event A: one waiter woken (B, waiting on event B, is not)", 1, g_sync_wakes - w0);
      uint64_t sb[8] = {g_eb_w}; svc_thread(0x11, sb); pthread_join(tb, NULL);
      EXPECT("signal event B: its waiter woken", 2, g_sync_wakes - w0); }
    // 312: signalled, then cleared before the waiter runs: the wait still ends with the signal (index 0), not at its timeout
    { uint64_t r[8] = {0}; svc_thread(0x45, r); g_ec_w = (uint32_t)r[1]; g_ec_r = (uint32_t)r[2];
      pthread_t tc; pthread_create(&tc, NULL, waiter_c, NULL); while (!atomic_load(&g_blocked_c)) usleep(1000); usleep(30000);
      dm_test_sync_woken = clear_c; uint64_t sc[8] = {g_ec_w}; svc_thread(0x11, sc); pthread_join(tc, NULL);
      EXPECT("signalled and cleared before the waiter ran: success [-DDM_TEST_SYNC_RESCAN]", 0, g_rc_c);
      EXPECT("... index 0", 0, g_idx_c); }
    printf("t_waitsync: %d checks, %d failed\n", n, fail); return fail != 0;
}
