// DM_SCHED_STATS=1 (plan item 255, after totkrecomp 65a9411 and AnySwitch 477f749, MIT; a diagnostic): per wait kind of the guest threads (lock,
// condition, address, sync objects) the waits a second, the time blocked, the wake-to-run latency (from the waker's signal to the waiter running
// again, before it takes a virtual core back) and the time taking the core back; and the system calls a second by number. hle_thread.c feeds it
// holding G (the svc counts atomically) and logs a line every 5 s. Unit test tests/t_schedstats.c
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { uint64_t n, blocked_ns, woken, wake_ns, wake_max_ns, core_ns, core_max_ns; } sched_kind;
typedef struct { sched_kind k[5]; _Atomic uint64_t svc[256]; uint64_t t0; } sched_stats;
// One wait: started at a, the wait returned at b (woke_at: the waker's time, 0 when it timed out), the core was back at c
static inline void sched_note(sched_stats *s, int kind, uint64_t a, uint64_t woke_at, uint64_t b, uint64_t c) {
    sched_kind *k = &s->k[kind > 0 && kind < 5 ? kind : 0];
    k->n++; k->blocked_ns += b - a;
#ifndef DM_TEST_SCHED_WAKE_FROM_START   // negative control of t_schedstats only: the latency counted from the wait's start
    if (woke_at && b >= woke_at) { uint64_t l = b - woke_at; k->woken++; k->wake_ns += l; if (l > k->wake_max_ns) k->wake_max_ns = l; }
#else
    if (woke_at) { uint64_t l = b - a; k->woken++; k->wake_ns += l; if (l > k->wake_max_ns) k->wake_max_ns = l; }
#endif
    uint64_t d = c - b; k->core_ns += d; if (d > k->core_max_ns) k->core_max_ns = d;
}
static inline void sched_svc(sched_stats *s, int num) { atomic_fetch_add_explicit(&s->svc[num & 255], 1, memory_order_relaxed); }
// The report over the time since t0 (then cleared): one line in out
static inline void sched_report(sched_stats *s, uint64_t now, char *out, size_t n) {
    static const char *const name[] = {"other", "lock", "condition", "address", "sync"};
    double sec = (now - s->t0) / 1e9; size_t o = 0; if (sec <= 0) sec = 1;
    for (int i = 1; i < 5 && o < n; i++) { const sched_kind *k = &s->k[i]; if (!k->n) continue;
        o += (size_t)snprintf(out + o, n - o, "%s%s %.0f/s blocked %.1f ms/s, wake-to-run %.3f ms (max %.2f), core back %.3f ms (max %.2f)", o ? "; " : "", name[i], k->n / sec,
                              k->blocked_ns / 1e6 / sec, k->woken ? k->wake_ns / 1e6 / (double)k->woken : 0, k->wake_max_ns / 1e6, k->core_ns / 1e6 / (double)k->n, k->core_max_ns / 1e6); }
    int top[5] = {-1, -1, -1, -1, -1}; uint64_t v[256];
    for (int i = 0; i < 256; i++) v[i] = atomic_exchange_explicit(&s->svc[i], 0, memory_order_relaxed);
    for (int i = 0; i < 256; i++) for (int j = 0; j < 5; j++) if (v[i] && (top[j] < 0 || v[i] > v[top[j]])) { for (int q = 4; q > j; q--) top[q] = top[q - 1]; top[j] = i; break; }
    for (int j = 0; j < 5 && top[j] >= 0 && o < n; j++) o += (size_t)snprintf(out + o, n - o, "%s svc %#x %.0f/s", j ? "," : o ? "; system calls:" : "system calls:", top[j], v[top[j]] / sec);
    if (!o && n) out[0] = 0;
    for (int i = 0; i < 5; i++) s->k[i] = (sched_kind){0};
    s->t0 = now;
}
// Per thread over the same span (plan item 315f, measured before S2Arm's on-demand virtual cores: there only a thread that asks for its core,
// GetCurrentProcessorNumber, or may run on one core only holds one; the others run free): its 0x10 calls and its waits for a virtual core.
// hle_thread.c fills one entry a live thread; sched_thr_report gives the waits of the threads that would still hold a core and of those that would
// not, then the six that waited longest
typedef struct { uint32_t handle; int prio; uint64_t mask; char name[24]; uint64_t cpu_calls, waits, wait_ns; int cpu_ever; } sched_thr;
static inline int sched_thr_needs_core(const sched_thr *t) {
#ifndef DM_TEST_SCHED_THR_ONE_CORE_FREE   // negative control of t_schedstats only: a thread on one core only counted as free
    if (__builtin_popcountll(t->mask & 7) == 1) return 1;
#endif
    return t->cpu_ever;
}
static inline void sched_thr_report(sched_thr *v, int nv, double sec, char *out, size_t n) {
    uint64_t need_ns = 0, free_ns = 0; int need_n = 0, free_n = 0; size_t o = 0; if (sec <= 0) sec = 1;
    for (int i = 0; i < nv; i++) { if (sched_thr_needs_core(&v[i])) { need_n++; need_ns += v[i].wait_ns; } else { free_n++; free_ns += v[i].wait_ns; } }
    o += (size_t)snprintf(out + o, n - o, "core waits: %d threads asking for their core or on one core %.2f ms/s, %d others %.2f ms/s;", need_n, need_ns / 1e6 / sec, free_n, free_ns / 1e6 / sec);
    for (int k = 0; k < 6 && o < n; k++) {
        int b = -1; for (int i = 0; i < nv; i++) if (v[i].waits && (b < 0 || v[i].wait_ns > v[b].wait_ns)) b = i;
        if (b < 0) break;
        o += (size_t)snprintf(out + o, n - o, " %#x \"%s\" prio %d mask %#llx 0x10 %.0f/s%s, %.0f waits/s %.2f ms/s;", v[b].handle, v[b].name, v[b].prio, (unsigned long long)v[b].mask,
                              v[b].cpu_calls / sec, sched_thr_needs_core(&v[b]) ? "" : " (free)", v[b].waits / sec, v[b].wait_ns / 1e6 / sec);
        v[b].waits = 0;
    }
}
