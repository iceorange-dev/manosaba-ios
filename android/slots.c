// The thread slot block on Android (host/slots.h, plan item 301c): DM_SLOTS words per thread, its address in bionic's TLS_SLOT_APP (tpidr_el0 + 16),
// made by the thread's first dm_slot_set and freed when the thread exits. Slot 0 is never handed out: a key page entry of 0 would read it.
#include "slots.h"
#include "dm.h"
#include <stdatomic.h>

#define DM_SLOTS 64
static _Atomic unsigned long g_next = 1;
static pthread_key_t g_blockkey; static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static uint64_t *tls_app(void) { uint64_t tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp)); return (uint64_t *)tp + 2; }
static void block_free(void *b) { uint64_t *app = tls_app(); if (*app == (uint64_t)b) *app = 0; free(b); }
static void once(void) { if (pthread_key_create(&g_blockkey, block_free)) die("cannot create the thread slot block key"); }

int dm_slot_create(dm_slot *k) {
    unsigned long n = atomic_fetch_add(&g_next, 1);
    if (n >= DM_SLOTS) return -1;
    *k = n; return 0;
}
void dm_slot_set(dm_slot k, const void *v) {
    if (!k || k >= DM_SLOTS) die("thread slot %lu outside the block", k);
    uint64_t *app = tls_app();
    if (!*app) {
        pthread_once(&g_once, once);
        uint64_t *b = calloc(DM_SLOTS, sizeof *b); if (!b) die("no memory for a thread slot block");
        *app = (uint64_t)b; pthread_setspecific(g_blockkey, b);
    }
    ((const void **)*app)[k] = v;
}
