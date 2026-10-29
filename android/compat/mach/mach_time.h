// Darwin's mach_absolute_time for the Android build (plan item 301c): the shared sources take it with mach_timebase_info and convert, so here the
// ticks are nanoseconds of CLOCK_MONOTONIC (not counting suspend, as Darwin's does not count sleep) and the time base is 1 / 1.
#pragma once
#include <stdint.h>
#include <time.h>
#include <errno.h>
typedef struct { uint32_t numer, denom; } mach_timebase_info_data_t;
static inline int mach_timebase_info(mach_timebase_info_data_t *tb) { tb->numer = tb->denom = 1; return 0; }
static inline uint64_t mach_absolute_time(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static inline int mach_wait_until(uint64_t deadline) {
    struct timespec t = {(time_t)(deadline / 1000000000ull), (long)(deadline % 1000000000ull)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL) == EINTR) {}
    return 0;
}
