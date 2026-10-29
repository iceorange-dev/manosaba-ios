// What the shared host sources need of the operating system beyond POSIX (plan item 301c): Apple's (Mach) in runtime/plat_apple.c, Linux / Android's
// in android/plat_linux.c. Each function answers the same on both; where one side cannot do something it says so in its result, not by a different
// meaning.
#pragma once
#include <stddef.h>
#include <stdint.h>

size_t dm_page_size(void);                                   // the host's page (16 KB on Apple silicon, 4 KB on the phone)
int dm_mem_read(void *dst, uint64_t src, size_t n);          // n bytes from memory that may be unmapped: 1 when all were read, 0 without a fault otherwise
int dm_mem_write(uint64_t dst, const void *src, size_t n);   // the same into memory that may be unmapped or read-only (the cheat VM's writes)
int dm_clone_tree(const char *src, const char *dst);         // a copy of a file or a directory tree at dst, which must not exist (Apple: clonefile, copy-on
                                                             // write; Linux: copied file by file): 0, or -1 with errno
int dm_rename_swap(const char *a, const char *b);            // exchange two paths at once (renamex_np RENAME_SWAP / renameat2 RENAME_EXCHANGE): 0 or -1
int dm_fd_path(int fd, char *out, size_t n);                 // the path an open descriptor names: 0, or -1 when it has none
// Extended attributes of a path, not following a final symlink. name has no namespace: Linux keeps it in "user." (the only one an app may write)
#include <sys/types.h>
ssize_t dm_xattr_get(const char *path, const char *name, void *buf, size_t n);   // its length, or -1 with errno (DM_ENOATTR: none)
int dm_xattr_set(const char *path, const char *name, const void *v, size_t n);   // 0 or -1
int dm_xattr_remove(const char *path, const char *name);                         // 0, also when there was none; -1
#ifdef __APPLE__
#define DM_ENOATTR ENOATTR
#else
#define DM_ENOATTR ENODATA
#endif
void dm_thread_stack(uint64_t *lo, uint64_t *hi);           // the calling thread's stack, without its guard page (a collector reads all of it)
uint64_t dm_proc_age_ns(void);                               // how long ago this process was created (0 when it cannot be told)
void *dm_reserve_at(uint64_t at, uint64_t size);             // address space at exactly [at, at + size), no access, failing (NULL) where something is mapped
int dm_mem_region(uint64_t *at, uint64_t *size, int *readable);   // the mapped region at or above *at: its start, size and whether it reads: 1, 0 past the last
void dm_after_ms(unsigned ms, void (*fn)(void));             // fn once, on a thread of its own, ms from now
void dm_on_signal(int sig, void (*fn)(void));                // fn on an ordinary thread (not in a handler) each time sig arrives

// Scheduling and accounting of threads. Latency-bound threads (the audio notifier, the translation thread, guest work) ask for the interactive class:
// Apple's QoS user-interactive; elsewhere nothing yet (the phone's own means are plan item 301f)
#include <pthread.h>
void dm_attr_interactive(pthread_attr_t *a);                 // a thread created with a runs at the interactive class
void dm_self_interactive(void);                              // the calling thread does from now on
uint32_t dm_self_qos(void);                                  // the calling thread's class in Apple's qos_class_t values (0 unspecified, so always elsewhere)
int dm_self_deadline(uint64_t period_ns, uint64_t comp_ns);  // declare comp_ns of work each period_ns for the calling thread (Apple: the time-constraint
                                                             // policy): 1 when it holds, 0 when refused or where there is no such declaration
uint64_t dm_self_sys_ns(void);                               // the calling thread's CPU time in the kernel
uint64_t dm_footprint(void);                                 // the process's memory as the system charges it, 0 when unknown (Apple: phys_footprint, which
                                                             // iOS's limit is on; Linux: RssAnon + RssShmem + VmSwap of /proc/self/status, its private pages)
// Readings of the performance lines (gfx/nvn's timing): 1, or 0 where the system gives an app no such reading (then the outputs are 0 / -1)
typedef struct { uint64_t energy_nj, cpu_ns, pcpu_ns, wakeups; } dm_power;
int dm_power_now(dm_power *o);                               // the process's CPU energy (Apple silicon), CPU time and its part on performance cores, wakeups
int dm_self_counts(uint64_t *instr, uint64_t *cyc);          // the calling thread's retired instructions and cycles
typedef struct { uint64_t instr, cyc, user, sys; } dm_level_counts;   // (user / sys in mach_absolute_time's units)
int dm_perf_levels(void);                                    // the kinds of CPU core, 0 the fastest (1 where not told)
int dm_perf_level_name(int k, char *out, size_t n);
int dm_self_level_counts(dm_level_counts *o, int n);         // the calling thread's counts per kind of core, n kinds
typedef struct { int cur_pri, base_pri, policy, rt_live; } dm_sched;
int dm_self_sched(dm_sched *o);                              // the calling thread's priorities and policy (Mach's), and whether its deadline holds

// A counting semaphore with a timed wait (hle_audio.c's release notices): GCD's on Apple, a POSIX one on Linux
#ifdef __APPLE__
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t dm_sem;
static inline dm_sem dm_sem_new(void) { return dispatch_semaphore_create(0); }
static inline void dm_sem_post(dm_sem s) { dispatch_semaphore_signal(s); }
static inline int dm_sem_wait(dm_sem s, int64_t ns) { return dispatch_semaphore_wait(s, ns < 0 ? DISPATCH_TIME_FOREVER : dispatch_time(DISPATCH_TIME_NOW, ns)) == 0; }   // 1 taken, 0 timed out
void dm_sem_free(dm_sem s);   // (in plat_apple.c: an ARC translation unit that includes this may not call dispatch_release)
#else
typedef struct dm_sem_s *dm_sem;
dm_sem dm_sem_new(void);
void dm_sem_post(dm_sem s);
int dm_sem_wait(dm_sem s, int64_t ns);   // ns < 0: forever; 1 taken, 0 timed out
void dm_sem_free(dm_sem s);
#endif

// Host threads: stopping another thread where it runs and reading its registers (hle_thread.c SetThreadActivity / GetThreadContext3 and its dumps,
// diag.c's sampling). A handle comes from dm_hthread_self on that thread; Linux stops a thread through a signal its handler waits in (on the thread's
// signal stack), so only threads that took their handle can be stopped there
typedef uintptr_t dm_hthread_t;
typedef struct { uint64_t x[29], fp, lr, sp, pc; uint32_t cpsr, fpcr, fpsr; int has_v; uint8_t v[32][16]; } dm_regs;
dm_hthread_t dm_hthread_self(void);
int dm_hthread_suspend(dm_hthread_t h);                      // 0 once it is stopped
int dm_hthread_resume(dm_hthread_t h);                       // 0
int dm_hthread_regs(dm_hthread_t h, dm_regs *r, int with_v); // a stopped thread's registers (the vector registers too with with_v): 0
int64_t dm_hthread_cpu_us(dm_hthread_t h);                   // user + system CPU time, -1 when it cannot be read
// Every host thread of the process, for a dump of the host's own threads: up to max handles that dm_hthread_suspend / regs take, released after
int dm_hthreads_all(dm_hthread_t *out, int max);
void dm_hthreads_release(dm_hthread_t *h, int n);
