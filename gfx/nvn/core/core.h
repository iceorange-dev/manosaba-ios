// gfx/nvn/core: the replay's decisions in plain C, without Metal (plan item 264), so that a second backend (Vulkan) can share them. Each module here
// is its own translation unit with a header that declares all the rest of the graphics layer may use of it; what a module needs from the backend
// (gfx/nvn/mtl, the Metal one) its header declares under "The backend provides". The modules build into the host and the iOS app (host/Makefile
// GFX) and into the graphics tests; each has a unit test that needs no GPU (gfx/tests/t_core_<module>.c); tools/tests/t_core.py checks that every
// file here compiles as plain C and that the build takes them.
// This header: the log lines and the red lines printed once a tag, shared with the backend (the same prefix, so whoever reads the log sees the same
// lines as before the move), the pass dump with the frame number and the pass and draw counts it names, and a clock for statistics.
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
#include <os/lock.h>
#else
#include <sched.h>
#endif

#define MLOG(...) do { fprintf(stderr, "[dm] [nvn-mtl] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define MDIE(...) do { MLOG("FATAL " __VA_ARGS__); abort(); } while (0)
// A red line printed once per tag (plan item 125; tools/tests/t_red_once.py: one message a tag, every tag within the array)
extern int g_red_once[124];
#define RED_ONCE(k, ...) do { if (!g_red_once[k]++) MLOG("[red] " __VA_ARGS__); } while (0)
// A monotonic clock in nanoseconds for statistics: the uptime clock on Apple's systems (as the backend measures), the monotonic one elsewhere
static inline uint64_t core_now_ns(void) {
#ifdef __APPLE__
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
#endif
}
// The frame the game is on (mtl_set_frame), and the replay's pass and draw counts so far
extern _Atomic uint64_t g_cur_frame;
extern uint64_t g_stat_draw, g_stat_pass;
// DM_NVN_PASSDUMP: records pass attachments / samples / presents one by one (textures by serial) for offline data-flow analysis; the pass numbers of P / D / R / S lines agree (= g_stat_pass after the pass is opened)
// DM_NVN_PASSDUMP=frame number, or start-end (inclusive at both ends)
// The command channel's "passdump <n>" (host/front/cmdchan.c) sets the range to the next n frames while the game runs
extern _Atomic long g_pd_a0, g_pd_a1;
long passdump_read(void);   // The first call: DM_NVN_PASSDUMP; returns the range's start (-1 off)
static inline int passdump(void) {
    long a0 = atomic_load(&g_pd_a0);
    if (a0 == -2) a0 = passdump_read();
    if (a0 < 0) return 0;   // off: no read of the frame number (plan item 247, after shh632058099 e460049; called a few times a draw)
    long f = (long)atomic_load(&g_cur_frame); return f >= a0 && f <= atomic_load(&g_pd_a1);
}
void mtl_passdump_next(long n);
#define PD(...) do { if (passdump()) { fprintf(stderr, "[pass] F%llu ", (unsigned long long)atomic_load(&g_cur_frame)); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
// A short lock, free when zeroed: os_unfair_lock on Apple's systems (as the backend used), a spin lock that yields elsewhere
#ifdef __APPLE__
typedef os_unfair_lock core_lock;
static inline void core_lock_take(core_lock *l) { os_unfair_lock_lock(l); }
static inline void core_lock_give(core_lock *l) { os_unfair_lock_unlock(l); }
#else
typedef _Atomic int core_lock;
static inline void core_lock_take(core_lock *l) { int z; while (z = 0, !atomic_compare_exchange_weak_explicit(l, &z, 1, memory_order_acquire, memory_order_relaxed)) sched_yield(); }
static inline void core_lock_give(core_lock *l) { atomic_store_explicit(l, 0, memory_order_release); }
#endif
