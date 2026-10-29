// The host's platform services on Linux / Android (host/plat.h, plan item 301c).
#include "plat.h"
#include "dm.h"
#include <asm/sigcontext.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/futex.h>
#include <mach/mach_time.h>   // (compat/: the ticks the assembly reads, dm_mach_ticks)
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/xattr.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

size_t dm_page_size(void) { static size_t p; if (!p) p = (size_t)sysconf(_SC_PAGESIZE); return p; }
uint64_t dm_mach_ticks(void);
uint64_t dm_mach_ticks(void) { return mach_absolute_time(); }   // what the assembly calls for mach_absolute_time (runtime/asm.h MACH_TICKS)
// process_vm_readv / writev on ourselves: an unmapped (or, for a write, read-only) page fails the call with EFAULT instead of faulting
int dm_mem_read(void *dst, uint64_t src, size_t n) {
    struct iovec l = {dst, n}, r = {(void *)(uintptr_t)src, n};
    return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == (ssize_t)n;
}
int dm_mem_write(uint64_t dst, const void *src, size_t n) {
    struct iovec l = {(void *)src, n}, r = {(void *)(uintptr_t)dst, n};
    return process_vm_writev(getpid(), &l, 1, &r, 1, 0) == (ssize_t)n;
}

// A file copied with its mode and extended attributes (the save's guest names live in them, hle_fs.c); a directory tree entry by entry. Only the
// "user." attributes, ours: the others are the system's (security.selinux is set from the policy for a new file, and writing it is refused)
static int copy_xattrs(const char *src, const char *dst) {
    char names[4096]; ssize_t n = llistxattr(src, names, sizeof names);
    if (n < 0) return errno == ENOTSUP ? 0 : -1;
    for (ssize_t i = 0; i < n; i += (ssize_t)strlen(names + i) + 1) {
        if (strncmp(names + i, "user.", 5)) continue;
        char v[4096]; ssize_t k = lgetxattr(src, names + i, v, sizeof v);
        if (k < 0 || lsetxattr(dst, names + i, v, (size_t)k, 0)) return -1;
    }
    return 0;
}
static int copy_file(const char *src, const char *dst, mode_t mode) {
    int in = open(src, O_RDONLY | O_CLOEXEC); if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode & 07777); if (out < 0) { int e = errno; close(in); errno = e; return -1; }
    char buf[65536]; ssize_t k; int r = 0;
    while ((k = read(in, buf, sizeof buf)) > 0) for (ssize_t o = 0; o < k;) { ssize_t w = write(out, buf + o, (size_t)(k - o)); if (w < 0) { r = -1; break; } o += w; }
    if (k < 0) r = -1;
    int e = errno; close(in); if (close(out)) r = -1;
    if (!r) r = copy_xattrs(src, dst);
    errno = e; return r;
}
int dm_clone_tree(const char *src, const char *dst) {
    struct stat st; if (lstat(src, &st)) return -1;
    if (!S_ISDIR(st.st_mode)) return copy_file(src, dst, st.st_mode);
    if (mkdir(dst, st.st_mode & 07777)) return -1;
    DIR *d = opendir(src); if (!d) return -1;
    int r = 0; struct dirent *e;
    while (!r && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char a[PATH_MAX], b[PATH_MAX];
        if (snprintf(a, sizeof a, "%s/%s", src, e->d_name) >= (int)sizeof a || snprintf(b, sizeof b, "%s/%s", dst, e->d_name) >= (int)sizeof b) { errno = ENAMETOOLONG; r = -1; break; }
        r = dm_clone_tree(a, b);
    }
    if (!r) r = copy_xattrs(src, dst);
    int er = errno; closedir(d); errno = er; return r;
}
static const char *xname(const char *name, char *b, size_t n) { snprintf(b, n, "user.%s", name); return b; }
ssize_t dm_xattr_get(const char *path, const char *name, void *buf, size_t n) { char b[256]; return lgetxattr(path, xname(name, b, sizeof b), buf, n); }
int dm_xattr_set(const char *path, const char *name, const void *v, size_t n) { char b[256]; return lsetxattr(path, xname(name, b, sizeof b), v, n, 0); }
int dm_xattr_remove(const char *path, const char *name) { char b[256]; return !lremovexattr(path, xname(name, b, sizeof b)) || errno == ENODATA ? 0 : -1; }
int dm_rename_swap(const char *a, const char *b) { return (int)syscall(SYS_renameat2, AT_FDCWD, a, AT_FDCWD, b, 2 /* RENAME_EXCHANGE */); }
int dm_fd_path(int fd, char *out, size_t n) {
    char l[64]; snprintf(l, sizeof l, "/proc/self/fd/%d", fd);
    ssize_t k = readlink(l, out, n ? n - 1 : 0); if (k <= 0 || out[0] != '/') return -1;
    out[k] = 0; return 0;
}
// pthread_getattr_np's range, from the bottom up past the pages that do not read (bionic's guard page: Unity's collector reads the whole range,
// RogelioLB/nxnative-android). A guest thread runs on a pthread of a fixed size (the main one too, front_headless.c), never a growing main stack
void dm_thread_stack(uint64_t *lo, uint64_t *hi) {
    pthread_attr_t a; void *base = NULL; size_t size = 0; *lo = *hi = 0;
    if (pthread_getattr_np(pthread_self(), &a)) return;
    pthread_attr_getstack(&a, &base, &size); pthread_attr_destroy(&a);
    uint64_t l = (uint64_t)(uintptr_t)base, h = l + size, pg = dm_page_size(); uint8_t b;
    while (l < h && !dm_mem_read(&b, l, 1)) l += pg;
    *lo = l; *hi = h;
}
// /proc/self/stat's starttime (field 22, clock ticks since boot) against CLOCK_BOOTTIME now
uint64_t dm_proc_age_ns(void) {
    char b[1024]; int fd = open("/proc/self/stat", O_RDONLY | O_CLOEXEC); if (fd < 0) return 0;
    ssize_t k = read(fd, b, sizeof b - 1); close(fd); if (k <= 0) return 0;
    b[k] = 0; char *p = strrchr(b, ')'); if (!p) return 0;   // the name in parentheses may hold spaces
    unsigned long long start = 0; int f = 2;
    char *save; for (char *t = strtok_r(p + 1, " ", &save); t; t = strtok_r(NULL, " ", &save)) if (++f == 22) { start = strtoull(t, NULL, 10); break; }
    struct timespec ts; clock_gettime(CLOCK_BOOTTIME, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec, at = start * (1000000000ull / (uint64_t)sysconf(_SC_CLK_TCK));
    return now > at ? now - at : 0;
}
void *dm_reserve_at(uint64_t at, uint64_t size) {
    void *p = mmap((void *)(uintptr_t)at, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) return NULL;
    if (p != (void *)(uintptr_t)at) { munmap(p, size); return NULL; }   // (a kernel before 4.17 takes the flag as a hint)
    return p;
}
int dm_mem_region(uint64_t *at, uint64_t *size, int *readable) {
    FILE *f = fopen("/proc/self/maps", "re"); if (!f) return 0;
    char line[512]; int found = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long long lo, hi; char perm[8];
        if (sscanf(line, "%llx-%llx %7s", &lo, &hi, perm) != 3 || hi <= *at) continue;
        *at = lo; *size = hi - lo; *readable = perm[0] == 'r'; found = 1; break;   // the region's own start, below *at when *at is inside it (as vm_region)
    }
    fclose(f); return found;
}
struct dm_sem_s { sem_t s; };
dm_sem dm_sem_new(void) { dm_sem s = malloc(sizeof *s); if (!s || sem_init(&s->s, 0, 0)) die("cannot make a semaphore"); return s; }
void dm_sem_post(dm_sem s) { sem_post(&s->s); }
int dm_sem_wait(dm_sem s, int64_t ns) {
    if (ns < 0) { while (sem_wait(&s->s)) if (errno != EINTR) return 0; return 1; }
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); uint64_t at = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec + (uint64_t)ns;
    t.tv_sec = (time_t)(at / 1000000000ull); t.tv_nsec = (long)(at % 1000000000ull);
    while (sem_clockwait(&s->s, CLOCK_MONOTONIC, &t)) if (errno != EINTR) return 0;
    return 1;
}
void dm_sem_free(dm_sem s) { sem_destroy(&s->s); free(s); }
typedef struct { unsigned ms; void (*fn)(void); } after_t;
static void *after_main(void *p) { after_t a = *(after_t *)p; free(p); usleep(a.ms * 1000u); a.fn(); return NULL; }
void dm_after_ms(unsigned ms, void (*fn)(void)) {
    after_t *a = malloc(sizeof *a); if (!a) return;
    a->ms = ms; a->fn = fn; pthread_t th;
    if (pthread_create(&th, NULL, after_main, a)) { free(a); return; }
    pthread_detach(th);
}
// The handler only writes the signal's number into a pipe; a thread of its own reads it and calls the function
static int g_sigpipe[2] = {-1, -1}; static void (*g_sigfn[65])(void);
static void on_sig(int sig) { int e = errno; unsigned char c = (unsigned char)sig; (void)!write(g_sigpipe[1], &c, 1); errno = e; }
static void *sig_main(void *p) { (void)p; unsigned char c; while (read(g_sigpipe[0], &c, 1) == 1) if (c < 65 && g_sigfn[c]) g_sigfn[c](); return NULL; }
void dm_on_signal(int sig, void (*fn)(void)) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    if (sig <= 0 || sig >= 65) return;
    pthread_mutex_lock(&mu);
    if (g_sigpipe[0] < 0 && !pipe2(g_sigpipe, O_CLOEXEC)) { pthread_t th; if (!pthread_create(&th, NULL, sig_main, NULL)) pthread_detach(th); }
    g_sigfn[sig] = fn;
    pthread_mutex_unlock(&mu);
    struct sigaction sa = {0}; sa.sa_handler = on_sig; sa.sa_flags = SA_RESTART; sigemptyset(&sa.sa_mask); sigaction(sig, &sa, NULL);
}

// Scheduling: an app has no real-time class or deadline declaration here (Android's performance hint sessions are 301f's), so threads run as created
void dm_attr_interactive(pthread_attr_t *a) { (void)a; }
void dm_self_interactive(void) {}
uint32_t dm_self_qos(void) { return 0; }
int dm_self_deadline(uint64_t period_ns, uint64_t comp_ns) { (void)period_ns; (void)comp_ns; return 0; }
uint64_t dm_self_sys_ns(void) {
    struct rusage u; if (getrusage(RUSAGE_THREAD, &u)) return 0;
    return (uint64_t)u.ru_stime.tv_sec * 1000000000ull + (uint64_t)u.ru_stime.tv_usec * 1000ull;
}
// Energy, per-thread hardware counters (perf_event_open is closed to an app) and core kinds (sysfs differs by phone) are not read here yet: 301f
int dm_power_now(dm_power *o) { memset(o, 0, sizeof *o); return 0; }
int dm_self_counts(uint64_t *instr, uint64_t *cyc) { *instr = *cyc = 0; return 0; }
int dm_perf_levels(void) { return 1; }
int dm_perf_level_name(int k, char *out, size_t n) { (void)k; if (n) *out = 0; return 0; }
int dm_self_level_counts(dm_level_counts *o, int n) { memset(o, 0, sizeof *o * (size_t)n); return 0; }
int dm_self_sched(dm_sched *o) { o->cur_pri = o->base_pri = o->policy = -1; o->rt_live = 0; return 0; }
uint64_t dm_footprint(void) {
    FILE *f = fopen("/proc/self/status", "re"); if (!f) return 0;
    char line[128]; uint64_t kb = 0, v; int n = 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "RssAnon: %" SCNu64, &v) == 1 || sscanf(line, "RssShmem: %" SCNu64, &v) == 1 || sscanf(line, "VmSwap: %" SCNu64, &v) == 1) { kb += v; n++; }
    fclose(f); return n == 3 ? kb << 10 : 0;
}
// Threads. A handle is the thread's record: taking it installs (once) the handler of SIG_STOP, a real-time signal. dm_hthread_suspend sends it and
// waits for the handler to have stored the thread's registers from its context; the handler then waits on a futex until dm_hthread_resume. On the
// thread's signal stack where it has one (SA_ONSTACK: crash.c gives each thread one), so the stopped thread's own stack holds nothing new. Suspensions
// count as Mach's do. DM_TEST_STOP_NO_V (negative control of android/tests/t_hthread.c only): the vector registers not taken from the context
#define SIG_STOP (SIGRTMIN + 5)
typedef struct hrec { pid_t tid; pthread_t pt; _Atomic int count, stopped, go, dead; int refs; dm_regs r; struct hrec *next; } hrec;   // refs, dead: under g_mu
static __thread hrec *t_rec;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER; static hrec *g_recs;
static long futex(_Atomic int *a, int op, int v, const struct timespec *to) { return syscall(SYS_futex, a, op, v, to, NULL, 0); }
static void on_stop(int sig, siginfo_t *si, void *uc_) {
    (void)sig; (void)si; hrec *r = t_rec; if (!r) return;
    int e = errno; const ucontext_t *uc = uc_; const mcontext_t *m = &uc->uc_mcontext;
    for (int i = 0; i < 29; i++) r->r.x[i] = m->regs[i];
    r->r.fp = m->regs[29]; r->r.lr = m->regs[30]; r->r.sp = m->sp; r->r.pc = m->pc; r->r.cpsr = (uint32_t)m->pstate; r->r.has_v = 0;
#ifndef DM_TEST_STOP_NO_V
    for (const struct _aarch64_ctx *h = (const void *)m->__reserved; h->magic && (const uint8_t *)h < m->__reserved + sizeof m->__reserved; h = (const void *)((const uint8_t *)h + h->size))
        if (h->magic == FPSIMD_MAGIC) { const struct fpsimd_context *f = (const void *)h; memcpy(r->r.v, f->vregs, sizeof r->r.v); r->r.fpsr = f->fpsr; r->r.fpcr = f->fpcr; r->r.has_v = 1; break; }
#endif
    atomic_store(&r->stopped, 1); futex(&r->stopped, FUTEX_WAKE, INT_MAX, NULL);
    while (!atomic_load(&r->go)) futex(&r->go, FUTEX_WAIT, 0, NULL);
    atomic_store(&r->go, 0); atomic_store(&r->stopped, 0); futex(&r->stopped, FUTEX_WAKE, INT_MAX, NULL);
    errno = e;
}
// The record leaves the list when its thread exits (a tid is reused later); one that dm_hthreads_all handed out stays, marked dead, until released
static pthread_key_t g_reckey;
static void rec_free(void *p) {
    hrec *r = p; pthread_mutex_lock(&g_mu);
    for (hrec **q = &g_recs; *q; q = &(*q)->next) if (*q == r) { *q = r->next; break; }
    atomic_store(&r->dead, 1); int keep = r->refs > 0;
    pthread_mutex_unlock(&g_mu); t_rec = NULL; if (!keep) free(r);
}
static void stop_install(void) {
    struct sigaction sa = {0}; sa.sa_sigaction = on_stop; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART; sigemptyset(&sa.sa_mask);
    if (sigaction(SIG_STOP, &sa, NULL)) die("cannot install the thread stop handler (signal %d)", SIG_STOP);
    if (pthread_key_create(&g_reckey, rec_free)) die("cannot create the thread record key");
}
dm_hthread_t dm_hthread_self(void) {
    if (!t_rec) {
        static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, stop_install);
        hrec *r = calloc(1, sizeof *r); if (!r) die("no memory for a thread record");
        r->tid = gettid(); r->pt = pthread_self();
        pthread_mutex_lock(&g_mu); r->next = g_recs; g_recs = r; pthread_mutex_unlock(&g_mu);
        t_rec = r; pthread_setspecific(g_reckey, r);
    }
    return (dm_hthread_t)t_rec;
}
int dm_hthread_suspend(dm_hthread_t h) {
    hrec *r = (hrec *)h; if (!r || r == t_rec || atomic_load(&r->dead)) return -1;
    if (atomic_fetch_add(&r->count, 1) > 0) return 0;   // already stopped
    if (syscall(SYS_tgkill, getpid(), r->tid, SIG_STOP)) { atomic_fetch_sub(&r->count, 1); return -1; }
    struct timespec to = {0, 10000000};
    for (int n = 0; !atomic_load(&r->stopped); n++) {
        futex(&r->stopped, FUTEX_WAIT, 0, &to);
        if (n > 500 && syscall(SYS_tgkill, getpid(), r->tid, 0)) { atomic_fetch_sub(&r->count, 1); return -1; }   // 5 s and the thread is gone
    }
    return 0;
}
int dm_hthread_resume(dm_hthread_t h) {
    hrec *r = (hrec *)h; if (!r || atomic_load(&r->count) <= 0) return -1;
    if (atomic_fetch_sub(&r->count, 1) > 1) return 0;
    atomic_store(&r->go, 1); futex(&r->go, FUTEX_WAKE, INT_MAX, NULL);
    while (atomic_load(&r->stopped)) futex(&r->stopped, FUTEX_WAIT, 1, NULL);   // out of the handler before another stop
    return 0;
}
int dm_hthread_regs(dm_hthread_t h, dm_regs *out, int with_v) {
    hrec *r = (hrec *)h; if (!r || !atomic_load(&r->stopped)) return -1;
    *out = r->r; if (!with_v) out->has_v = 0;
    return 0;
}
int64_t dm_hthread_cpu_us(dm_hthread_t h) {
    hrec *r = (hrec *)h; clockid_t c; struct timespec ts;
    if (!r || pthread_getcpuclockid(r->pt, &c) || clock_gettime(c, &ts)) return -1;
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
// The threads that took a handle and still run, each record held until dm_hthreads_release (its thread may exit meanwhile)
int dm_hthreads_all(dm_hthread_t *out, int max) {
    int n = 0; pthread_mutex_lock(&g_mu);
    for (hrec *r = g_recs; r && n < max; r = r->next) if (!syscall(SYS_tgkill, getpid(), r->tid, 0)) { r->refs++; out[n++] = (dm_hthread_t)r; }
    pthread_mutex_unlock(&g_mu); return n;
}
void dm_hthreads_release(dm_hthread_t *h, int n) {
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < n; i++) { hrec *r = (hrec *)h[i]; if (r && --r->refs == 0 && atomic_load(&r->dead)) free(r); }
    pthread_mutex_unlock(&g_mu);
}
