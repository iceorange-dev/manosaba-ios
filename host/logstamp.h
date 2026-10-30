// UTC milliseconds at the start of every stderr line (plan item 106; the idea from lordran-native's dm_log_timestamp.h, MIT, the code is ours).
// stderr becomes a stream over the same descriptor whose writer puts "[2026-10-20T03:04:05.678Z] " before each line, however the line arrives
// (LOG's text and its newline come in two calls, a crash dump in many), so the host's LOG, the graphics side's messages and crash.c's dump (written
// through stderr) all carry it; plain write(2) calls (the crash report's first part, signal-safe: plan item 298) go around it. The descriptor stays open and
// owned by the old stream. Lines written before it is installed (a constructor's, the iOS app's env.txt report) have none. On by default in the
// iOS app (main.m), DM_LOG_TIME=1 on the Mac (main.c); DM_LOG_TIME=0 turns it off.
// Checked by host/tests/t_logstamp.c
#pragma once
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { int fd, at_start; } logstamp_sink;
static inline int logstamp_put(int fd, const char *p, size_t n) {
    while (n) { ssize_t w = write(fd, p, n); if (w > 0) { p += w; n -= (size_t)w; } else if (w < 0 && errno == EINTR) continue; else return -1; }
    return 0;
}
static inline int logstamp_write(void *cookie, const char *p, int len) {
    logstamp_sink *s = (logstamp_sink *)cookie; size_t left = (size_t)len;
    while (left) {
        if (s->at_start) {
            struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); struct tm u; gmtime_r(&ts.tv_sec, &u);
            char b[40]; size_t k = strftime(b, sizeof b, "[%Y-%m-%dT%H:%M:%S", &u);
            k += (size_t)snprintf(b + k, sizeof b - k, ".%03ldZ] ", ts.tv_nsec / 1000000);
            if (logstamp_put(s->fd, b, k)) return -1;
            s->at_start = 0;
        }
        const char *nl = (const char *)memchr(p, '\n', left); size_t n = nl ? (size_t)(nl - p) + 1 : left;
        if (logstamp_put(s->fd, p, n)) return -1;
#ifndef DM_TEST_LOGSTAMP_EVERY_WRITE
        s->at_start = nl != NULL;
#else
        s->at_start = 1;   // broken variant: a stamp before every write, also in the middle of a line
#endif
        p += n; left -= n;
    }
    return len;
}
static inline int logstamp_close(void *cookie) { free(cookie); return 0; }
// A line-buffered stream that stamps lines written to fd (NULL if it cannot be made)
static inline FILE *logstamp_stream(int fd) {
    logstamp_sink *s = (logstamp_sink *)calloc(1, sizeof *s); if (!s) return NULL;
    s->fd = fd; s->at_start = 1;
    FILE *f = funopen(s, NULL, logstamp_write, NULL, logstamp_close);
    if (!f) { free(s); return NULL; }
    setvbuf(f, NULL, _IOLBF, 0); return f;
}
// stderr stamped from here on (unless DM_LOG_TIME=0); returns whether it is
static inline int logstamp_stderr(void) {
    const char *e = getenv("DM_LOG_TIME"); if (e && !strcmp(e, "0")) return 0;
    fflush(stderr); FILE *f = logstamp_stream(fileno(stderr)); if (!f) return 0;
    stderr = f; return 1;
}
