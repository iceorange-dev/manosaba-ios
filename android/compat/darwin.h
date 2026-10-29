// Android build (plan item 301c), force-included into every host source (-include): Darwin names the shared sources use where Linux has the same thing
// under another name. Anything that differs in what it does goes through host/plat.h instead.
#pragma once
#ifndef __ASSEMBLER__   // (also given to the .S files)
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CLOCK_UPTIME_RAW CLOCK_MONOTONIC       // Darwin's: not counting sleep. The same clock as mach_absolute_time (compat/mach/mach_time.h), as on Darwin
static inline uint64_t clock_gettime_nsec_np(clockid_t c) { struct timespec t; clock_gettime(c, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
#define st_mtimespec st_mtim                   // struct stat's modification time
#define st_atimespec st_atim
#define st_ctimespec st_ctim
#define MINCORE_INCORE 1                       // mincore's resident bit
// The kernel's thread id (pthread_t is an integer here, so the NULL Darwin's callers pass needs the cast)
static inline int dm_threadid_np(uintptr_t t, uint64_t *id) { *id = (uint64_t)(t ? pthread_gettid_np((pthread_t)t) : gettid()); return 0; }
#define pthread_threadid_np(t, id) dm_threadid_np((uintptr_t)(t), (id))
// Darwin names the calling thread only; Linux takes 15 characters at most and refuses a longer name, so it is cut there
static inline int dm_setname_self(const char *name) { char n[16]; snprintf(n, sizeof n, "%s", name); return pthread_setname_np(pthread_self(), n); }
#define pthread_setname_np(name) dm_setname_self(name)
#endif
