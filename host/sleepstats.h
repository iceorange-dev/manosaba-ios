// DM_SLEEP_STATS=1 (plan item 316, a diagnostic: measured before S2Arm's back-off of short sleeps, Apache-2.0, written anew): SleepThread calls
// under 1 ms by where they come from: the address the system call returns to and its caller's (from the frame record; 0 when not on the stack).
// A sleep continues a run when the same thread's previous system call was a short sleep from the same place and at most 20 µs of guest code ran
// between them (S2Arm's run); the run's time is counted from its first sleep's start. Per place: the calls a second, the asked and the slept time,
// the share that continued a run and the share made after the run had lasted 2 ms (where S2Arm's back-off begins: each sleep max(asked, run / 8)
// up to 100 µs; sleep_backoff_ns). hle_thread.c feeds it holding its lock and logs a line every 5 s. Unit test tests/t_sleepstats.c
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define SLEEP_SITES 32
typedef struct { uint64_t lr, lr2, n, asked_ns, slept_ns, in_run, after2ms; } sleep_site;
typedef struct { sleep_site s[SLEEP_SITES]; int ns; uint64_t t0, dropped; } sleep_stats;
typedef struct { uint64_t lr, lr2, nsvc, end_ns, run_t0; } sleep_run;   // a thread's last short sleep (lr 0: none)
// How long the run a short sleep starting now continues has lasted (from its first sleep's start), or -1 when it starts a run of its own
static inline int64_t sleep_run_age(const sleep_run *r, uint64_t lr, uint64_t lr2, uint64_t nsvc, uint64_t start_ns) {
#ifndef DM_TEST_SLEEP_RUN_ANY_SVC   // negative control of t_sleepstats only: other system calls between two sleeps do not end a run
    int cont = r->lr == lr && r->lr2 == lr2 && nsvc == r->nsvc + 1 && start_ns >= r->end_ns && start_ns - r->end_ns <= 20000;
#else
    int cont = r->lr == lr && r->lr2 == lr2 && start_ns >= r->end_ns && start_ns - r->end_ns <= 20000;
#endif
    return cont ? (int64_t)(start_ns - r->run_t0) : -1;
}
static inline int64_t sleep_run_update(sleep_run *r, uint64_t lr, uint64_t lr2, uint64_t nsvc, uint64_t start_ns, uint64_t end_ns, uint64_t asked_ns) {
    if (asked_ns >= 1000000) { r->lr = 0; return -1; }
    int64_t age = sleep_run_age(r, lr, lr2, nsvc, start_ns);
    if (age < 0) r->run_t0 = start_ns;
    r->lr = lr; r->lr2 = lr2; r->nsvc = nsvc; r->end_ns = end_ns;
    return age;
}
// The back-off (plan item 316, S2Arm's): a short sleep that continues a run of 2 ms or more sleeps max(asked, run / 8), at most the cap.
// DM_SLEEP_BACKOFF=<cap µs>, 100 by default (0 off). manosaba on the iPhone Air: 17,700 short sleeps a second, 40 % of them 2 ms or more into
// their run, became 12,300, the wakeups 19,500-20,600 a second 13,600-15,700, CPU 0.13 -> 0.10 W and 0.30 -> 0.27 W in its busier windows,
// frame time p95 16.9-17.1 against 17.0-18.3 ms, the shots the same; Tears of the Kingdom's short sleeps are never in a run (it is not touched)
static inline uint64_t sleep_backoff_parse(const char *e) {   // the cap in ns from DM_SLEEP_BACKOFF's value (NULL: not set)
#ifndef DM_TEST_SLEEP_BACKOFF_DEFAULT_OFF   // negative control of t_sleepstats only: off unless asked, as before
    if (!e) return 100000;
#else
    if (!e) return 0;
#endif
    long long v = atoll(e); return v > 0 ? (uint64_t)v * 1000 : 0;
}
static inline uint64_t sleep_backoff_ns(int64_t age, uint64_t asked_ns, uint64_t cap_ns) {
    if (age < 2000000 || asked_ns >= cap_ns) return asked_ns;
    uint64_t b = (uint64_t)age / 8; if (b > cap_ns) b = cap_ns;
    return b > asked_ns ? b : asked_ns;
}
// One sleep of a thread: where from, the thread's system call count at it (the sleep included), when it began and ended, what it asked
static inline void sleep_note(sleep_stats *st, sleep_run *r, uint64_t lr, uint64_t lr2, uint64_t nsvc, uint64_t start_ns, uint64_t end_ns, uint64_t asked_ns) {
    int64_t age = sleep_run_update(r, lr, lr2, nsvc, start_ns, end_ns, asked_ns); if (asked_ns >= 1000000) return;
    int cont = age >= 0, after = age >= 2000000;
    int i = 0; while (i < st->ns && (st->s[i].lr != lr || st->s[i].lr2 != lr2)) i++;
    if (i == st->ns) { if (st->ns == SLEEP_SITES) { st->dropped++; return; } st->s[st->ns++] = (sleep_site){lr, lr2, 0, 0, 0, 0, 0}; }
    sleep_site *s = &st->s[i]; s->n++; s->asked_ns += asked_ns; s->slept_ns += end_ns - start_ns; s->in_run += cont; s->after2ms += after;
}
// The report over the time since t0 (then cleared): the five busiest places, named by name(address, buf, n) as "<return> < <caller's>"
static inline void sleep_report(sleep_stats *st, uint64_t now, void (*name)(uint64_t, char *, size_t), char *out, size_t n) {
    double sec = (now - st->t0) / 1e9; size_t o = 0; uint64_t tot = 0; if (sec <= 0) sec = 1;
    for (int i = 0; i < st->ns; i++) tot += st->s[i].n;
    o += (size_t)snprintf(out + o, n - o, "short sleeps %.0f/s from %d places%s", tot / sec, st->ns, st->dropped ? " (more not listed)" : "");
    for (int k = 0; k < 5 && o < n; k++) {
        int b = -1; for (int i = 0; i < st->ns; i++) if (st->s[i].n && (b < 0 || st->s[i].n > st->s[b].n)) b = i;
        if (b < 0) break;
        sleep_site *s = &st->s[b]; char nm[64], nm2[64]; name(s->lr, nm, sizeof nm); name(s->lr2, nm2, sizeof nm2);
        o += (size_t)snprintf(out + o, n - o, "; %s < %s %.0f/s asked %.0f µs slept %.0f µs, %.0f%% in a run, %.0f%% after 2 ms of it", nm, nm2, s->n / sec,
                              s->asked_ns / 1e3 / (double)s->n, s->slept_ns / 1e3 / (double)s->n, 100.0 * (double)s->in_run / (double)s->n, 100.0 * (double)s->after2ms / (double)s->n);
        s->n = 0;
    }
    st->ns = 0; st->dropped = 0; st->t0 = now;
}
