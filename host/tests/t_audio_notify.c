// AudioOut's release handoff to its notifier thread (host/hle/hle_audio.c notify_arm / notify_disarm; seedds/nxnative c76a424, MIT), as a
// store-buffering litmus: the notifier armed, then in each round, at the same moment, the notifier disarms and looks at `pending` while a
// release publishes `pending` and arms. The lost wakeup is both seeing the other's old value: the release not posting (armed still 1) and the
// notifier not seeing it pending (0); then nobody posts and the game's event never comes. Four pairs of threads at once (one pair alone seldom
// shows it on an M-series Mac: lost rounds in 2 of 19 runs; four at once lost rounds in every run, 115-230 in all), 2,000,000 rounds each:
// none lost. The disarm as a release-only store (-DDM_TEST_AUDIO_NOTIFY_STORE, as before; stlr then ldapr): lost wakeups, 1 fails.
#include "../hle/hle_audio.c"
#include <time.h>
uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
void *objmap_get(const void *k) { (void)k; return NULL; }
void objmap_put(const void *k, void *v) { (void)k; (void)v; }
void *objmap_del(const void *k) { (void)k; return NULL; }
void hle_signal_system_event(void *g) { (void)g; }
void hle_bind_system_event(void *g, int autoclear) { (void)g; (void)autoclear; }
static void (*g_test_ao_registered)(struct aout *a);
static int g_suspended;
static _Atomic unsigned g_consumer_dupes;

#define PAIRS 4
#define ROUNDS 2000000
typedef struct {
    aout a;
    _Atomic int arrive __attribute__((aligned(128)));   // both threads arrive at a round's start when it reaches 2r: they leave the spin at nearly the same moment
    int pending_seen, posted;
    long lost;
} pair_t;
static pair_t g_p[PAIRS] __attribute__((aligned(128)));
static void meet(pair_t *p, int r) { atomic_fetch_add_explicit(&p->arrive, 1, memory_order_acq_rel); while (atomic_load_explicit(&p->arrive, memory_order_acquire) < 2 * r) ; }
static void skew(int r, int side) { for (volatile int k = (r * 7 + side * 3) % 13; k > 0; k--) ; }   // the two sides' alignment varied round by round
static void *releaser(void *v) {
    pair_t *p = v;
    for (int r = 1; r <= ROUNDS; r++) { meet(p, 2 * r - 1); skew(r, 1); p->posted = notify_arm(&p->a); meet(p, 2 * r); }
    return NULL;
}
static void *notifier(void *v) {
    pair_t *p = v;
    for (int r = 1; r <= ROUNDS; r++) {   // between rounds (after meet 2r-2, before 2r-1) only this thread touches the flags
        atomic_store_explicit(&p->a.notify_pending, 0, memory_order_relaxed); atomic_store_explicit(&p->a.notify_armed, 1, memory_order_relaxed);
        meet(p, 2 * r - 1); skew(r, 0); p->pending_seen = notify_disarm(&p->a); meet(p, 2 * r);
        if (!p->pending_seen && !p->posted) p->lost++;
    }
    return NULL;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_t t[2 * PAIRS];
    for (int i = 0; i < PAIRS; i++) { pthread_create(&t[2 * i], NULL, releaser, &g_p[i]); pthread_create(&t[2 * i + 1], NULL, notifier, &g_p[i]); }
    long lost = 0;
    for (int i = 0; i < 2 * PAIRS; i++) pthread_join(t[i], NULL);
    for (int i = 0; i < PAIRS; i++) lost += g_p[i].lost;
    int ok = lost == 0;
    printf("%d pairs x %d rounds of disarm against release: %ld lost wakeups (neither posted nor seen pending) %s\n", PAIRS, ROUNDS, lost, ok ? "✓" : "✗");
    printf("t_audio_notify: %s\n", ok ? "all passed" : "failed");
    return !ok;
}
