// The host's platform services on Apple (host/plat.h, plan item 301c): Mach and Darwin, as the shared sources called them before.
#include "plat.h"
#include "dm.h"
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <mach/mach_vm.h>   // (not on iOS)
#endif
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread/qos.h>
#include <signal.h>
#include <stdio.h>
#include <sys/clonefile.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <unistd.h>

size_t dm_page_size(void) { return vm_page_size; }
int dm_mem_read(void *dst, uint64_t src, size_t n) {
    vm_size_t got = 0; return vm_read_overwrite(mach_task_self(), (vm_address_t)src, n, (vm_address_t)dst, &got) == KERN_SUCCESS && got == n;
}
int dm_mem_write(uint64_t dst, const void *src, size_t n) {
#if TARGET_OS_OSX
    return mach_vm_write(mach_task_self(), dst, (vm_offset_t)(uintptr_t)src, (mach_msg_type_number_t)n) == KERN_SUCCESS;
#else
    (void)dst; (void)src; (void)n; return 0;   // iOS has no mach_vm_write (the cheat VM is Mac only there)
#endif
}
int dm_clone_tree(const char *src, const char *dst) { return clonefile(src, dst, 0); }
int dm_rename_swap(const char *a, const char *b) { return renamex_np(a, b, RENAME_SWAP); }
ssize_t dm_xattr_get(const char *path, const char *name, void *buf, size_t n) { return getxattr(path, name, buf, n, 0, XATTR_NOFOLLOW); }
int dm_xattr_set(const char *path, const char *name, const void *v, size_t n) { return setxattr(path, name, v, n, 0, XATTR_NOFOLLOW); }
int dm_xattr_remove(const char *path, const char *name) { return !removexattr(path, name, XATTR_NOFOLLOW) || errno == ENOATTR ? 0 : -1; }
int dm_fd_path(int fd, char *out, size_t n) {
    char p[MAXPATHLEN] = ""; if (fcntl(fd, F_GETPATH, p) || !p[0]) return -1;
    strlcpy(out, p, n); return 0;
}
void dm_thread_stack(uint64_t *lo, uint64_t *hi) {
    uint64_t top = (uint64_t)pthread_get_stackaddr_np(pthread_self()), ssz = pthread_get_stacksize_np(pthread_self()); *hi = top; *lo = top - ssz;
}
// p_starttime from sysctl KERN_PROC_PID (wall clock) against the wall clock now
uint64_t dm_proc_age_ns(void) {
    struct kinfo_proc kp; size_t n = sizeof kp; int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()}; struct timeval tv; gettimeofday(&tv, NULL);
    if (sysctl(mib, 4, &kp, &n, NULL, 0) || !n) return 0;
    int64_t age = (int64_t)(tv.tv_sec - kp.kp_proc.p_starttime.tv_sec) * 1000000000ll + (tv.tv_usec - kp.kp_proc.p_starttime.tv_usec) * 1000ll;
    return age > 0 ? (uint64_t)age : 0;
}
void *dm_reserve_at(uint64_t at, uint64_t size) {   // (vm_map with VM_FLAGS_FIXED fails where something is mapped)
    vm_address_t a = (vm_address_t)at;
    return vm_map(mach_task_self(), &a, (vm_size_t)size, 0, VM_FLAGS_FIXED, MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_DEFAULT) == KERN_SUCCESS ? (void *)(uintptr_t)a : NULL;
}
int dm_mem_region(uint64_t *at, uint64_t *size, int *readable) {
    vm_address_t a = (vm_address_t)*at; vm_size_t sz = 0; vm_region_basic_info_data_64_t info; mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t obj;
    if (vm_region_64(mach_task_self(), &a, &sz, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &cnt, &obj) != KERN_SUCCESS) return 0;
    *at = a; *size = sz; *readable = !!(info.protection & VM_PROT_READ); return 1;
}
void dm_attr_interactive(pthread_attr_t *a) { pthread_attr_set_qos_class_np(a, QOS_CLASS_USER_INTERACTIVE, 0); }
void dm_self_interactive(void) { pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); }
uint32_t dm_self_qos(void) { qos_class_t qc = QOS_CLASS_UNSPECIFIED; int rp = 0; pthread_get_qos_class_np(pthread_self(), &qc, &rp); return (uint32_t)qc; }
int dm_self_deadline(uint64_t period_ns, uint64_t comp_ns) {
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    thread_time_constraint_policy_data_t p = {
        .period = (uint32_t)(period_ns * tb.denom / tb.numer), .computation = (uint32_t)(comp_ns * tb.denom / tb.numer),
        .constraint = (uint32_t)(period_ns * tb.denom / tb.numer), .preemptible = 1};
    return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
}
uint64_t dm_self_sys_ns(void) {
    thread_basic_info_data_t bi; mach_msg_type_number_t n = THREAD_BASIC_INFO_COUNT; mach_port_t me = mach_thread_self(); uint64_t r = 0;
    if (thread_info(me, THREAD_BASIC_INFO, (thread_info_t)&bi, &n) == KERN_SUCCESS) r = (uint64_t)bi.system_time.seconds * 1000000000ull + (uint64_t)bi.system_time.microseconds * 1000ull;
    mach_port_deallocate(mach_task_self(), me); return r;
}
uint64_t dm_footprint(void) {
    task_vm_info_data_t vi; mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &n) == KERN_SUCCESS ? vi.phys_footprint : 0;
}
extern int thread_selfcounts(int type, void *buf, size_t nbytes);   // libSystem private (Mac and iOS): 1 = {instructions, cycles}; 4 = THSC_TIME_CPI_PER_PERF_LEVEL,
                                                                    // {instructions, cycles, user, system} per perf level (XNU bsd/sys/resource_private.h)
int dm_power_now(dm_power *o) {
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    task_power_info_v2_data_t pw; mach_msg_type_number_t n = TASK_POWER_INFO_V2_COUNT; memset(o, 0, sizeof *o);
    if (task_info(mach_task_self(), TASK_POWER_INFO_V2, (task_info_t)&pw, &n) != KERN_SUCCESS) return 0;
    o->cpu_ns = (pw.cpu_energy.total_user + pw.cpu_energy.total_system) * tb.numer / tb.denom; o->pcpu_ns = pw.task_ptime * tb.numer / tb.denom;
    o->energy_nj = pw.task_energy; o->wakeups = pw.cpu_energy.task_interrupt_wakeups; return 1;
}
int dm_self_counts(uint64_t *instr, uint64_t *cyc) { uint64_t c[2] = {0, 0}; int ok = !thread_selfcounts(1, c, sizeof c); *instr = c[0]; *cyc = c[1]; return ok; }
int dm_perf_levels(void) { int v = 0; size_t sz = sizeof v; return sysctlbyname("hw.nperflevels", &v, &sz, NULL, 0) || v < 1 ? 1 : v; }
int dm_perf_level_name(int k, char *out, size_t n) { char key[32]; snprintf(key, sizeof key, "hw.perflevel%d.name", k); return !sysctlbyname(key, out, &n, NULL, 0); }
int dm_self_level_counts(dm_level_counts *o, int n) { memset(o, 0, sizeof *o * (size_t)n); return !thread_selfcounts(4, o, sizeof *o * (size_t)n); }
int dm_self_sched(dm_sched *o) {
    thread_act_t me = pthread_mach_thread_np(pthread_self());
    thread_extended_info_data_t xi; mach_msg_type_number_t xc = THREAD_EXTENDED_INFO_COUNT; int okx = thread_info(me, THREAD_EXTENDED_INFO, (thread_info_t)&xi, &xc) == KERN_SUCCESS;
    thread_time_constraint_policy_data_t tp; mach_msg_type_number_t tc = THREAD_TIME_CONSTRAINT_POLICY_COUNT; boolean_t def = 0;
    o->rt_live = thread_policy_get(me, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&tp, &tc, &def) == KERN_SUCCESS && !def;   // def = 1: not in effect (never set / revoked)
    o->cur_pri = okx ? xi.pth_curpri : -1; o->base_pri = okx ? xi.pth_priority : -1; o->policy = okx ? xi.pth_policy : -1; return okx;
}
void dm_sem_free(dm_sem s) { dispatch_release(s); }
static void call_fn(void *fn) { ((void (*)(void))fn)(); }
void dm_after_ms(unsigned ms, void (*fn)(void)) {
    dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, (int64_t)ms * NSEC_PER_MSEC), dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), (void *)fn, call_fn);
}
// A GCD signal source on an ordinary thread (the default action ignored so that the source sees it)
void dm_on_signal(int sig, void (*fn)(void)) {
    signal(sig, SIG_IGN);
    dispatch_source_t src = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, (uintptr_t)sig, 0, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
    dispatch_set_context(src, (void *)fn); dispatch_source_set_event_handler_f(src, call_fn); dispatch_resume(src);
}

// Threads: the Mach thread port of each (pthread_mach_thread_np, no reference taken); suspension counts as Mach's does
dm_hthread_t dm_hthread_self(void) { return pthread_mach_thread_np(pthread_self()); }
int dm_hthread_suspend(dm_hthread_t h) { return thread_suspend((thread_act_t)h) == KERN_SUCCESS ? 0 : -1; }
int dm_hthread_resume(dm_hthread_t h) { return thread_resume((thread_act_t)h) == KERN_SUCCESS ? 0 : -1; }
int dm_hthread_regs(dm_hthread_t h, dm_regs *r, int with_v) {
    arm_thread_state64_t st; mach_msg_type_number_t c = ARM_THREAD_STATE64_COUNT;
    if (thread_get_state((thread_act_t)h, ARM_THREAD_STATE64, (thread_state_t)&st, &c) != KERN_SUCCESS) return -1;
    for (int i = 0; i < 29; i++) r->x[i] = st.__x[i];
    r->fp = (uint64_t)__darwin_arm_thread_state64_get_fp(st); r->lr = (uint64_t)__darwin_arm_thread_state64_get_lr(st);
    r->sp = (uint64_t)__darwin_arm_thread_state64_get_sp(st); r->pc = (uint64_t)__darwin_arm_thread_state64_get_pc(st); r->cpsr = st.__cpsr;
    r->has_v = 0;
    if (with_v) {
        arm_neon_state64_t ns; mach_msg_type_number_t cn = ARM_NEON_STATE64_COUNT;
        if (thread_get_state((thread_act_t)h, ARM_NEON_STATE64, (thread_state_t)&ns, &cn) == KERN_SUCCESS) { memcpy(r->v, &ns.__v, sizeof r->v); r->fpcr = ns.__fpcr; r->fpsr = ns.__fpsr; r->has_v = 1; }
    }
    return 0;
}
int64_t dm_hthread_cpu_us(dm_hthread_t h) {
    thread_basic_info_data_t bi; mach_msg_type_number_t n = THREAD_BASIC_INFO_COUNT;
    if (thread_info((thread_act_t)h, THREAD_BASIC_INFO, (thread_info_t)&bi, &n) != KERN_SUCCESS) return -1;
    return (int64_t)(bi.user_time.seconds + bi.system_time.seconds) * 1000000 + bi.user_time.microseconds + bi.system_time.microseconds;
}
int dm_hthreads_all(dm_hthread_t *out, int max) {
    thread_act_array_t ths; mach_msg_type_number_t nth = 0;
    if (task_threads(mach_task_self(), &ths, &nth) != KERN_SUCCESS) return 0;
    int n = 0;
    for (mach_msg_type_number_t i = 0; i < nth; i++) { if (n < max) out[n++] = ths[i]; else mach_port_deallocate(mach_task_self(), ths[i]); }
    vm_deallocate(mach_task_self(), (vm_address_t)ths, nth * sizeof *ths);
    return n;
}
void dm_hthreads_release(dm_hthread_t *h, int n) { for (int i = 0; i < n; i++) mach_port_deallocate(mach_task_self(), (mach_port_t)h[i]); }
