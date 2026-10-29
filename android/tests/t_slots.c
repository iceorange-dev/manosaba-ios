// android/slots.c, the thread slot block (host/slots.h), on the phone: the block's base is where the rewritten code and the host's assembly read it
// (bionic's TLS_SLOT_APP, tpidr_el0 + 16, then slot * 8), each thread has its own, a thread without one reads NULL, slots are numbered from 1, the
// block holds DM_SLOTS (64) words, and a thread's block is freed when it exits (TLS_SLOT_APP cleared).
#include "slots.h"
#include <stdio.h>
#include <string.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

static dm_slot ka, kb;
static uint64_t app(void) { uint64_t tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp)); return ((const uint64_t *)tp)[2]; }
static void *other(void *a) {
    void **out = a;
    out[0] = dm_slot_get(ka);   // before this thread set anything: no block
    dm_slot_set(ka, (void *)0xb0b);
    out[1] = dm_slot_get(ka); out[2] = (void *)(uintptr_t)dm_slot_base();
    return NULL;
}
static void *bare(void *a) { (void)a; return (void *)(uintptr_t)app(); }
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    CHECK(dm_slot_create(&ka) == 0 && dm_slot_create(&kb) == 0 && ka >= 1 && kb == ka + 1, "two slots, from 1 and in order (%lu, %lu)", ka, kb);
    CHECK(dm_slot_base() == 0 && dm_slot_get(ka) == NULL, "no block before the first set: base 0, get NULL");
    dm_slot_set(ka, (void *)0xa11); dm_slot_set(kb, (void *)0xb22);
    uint64_t b = dm_slot_base();
    CHECK(b && b == app(), "after a set the base is TLS_SLOT_APP (%#llx)", (unsigned long long)b);
    CHECK(((void **)b)[ka] == (void *)0xa11 && ((void **)b)[kb] == (void *)0xb22 && dm_slot_get(ka) == (void *)0xa11, "slot k is the word at base + k * 8");
    void *o[3] = {0}; pthread_t th; pthread_create(&th, NULL, other, o); pthread_join(th, NULL);
    CHECK(o[0] == NULL && o[1] == (void *)0xb0b && o[2] && (uint64_t)(uintptr_t)o[2] != b, "another thread: no block at first, then its own (%p)", o[2]);
    CHECK(dm_slot_get(ka) == (void *)0xa11, "this thread's slot unchanged by the other's set");
    pthread_create(&th, NULL, bare, NULL); void *ap = (void *)1; pthread_join(th, &ap);
    CHECK(ap == NULL, "a thread that never sets has TLS_SLOT_APP 0 (%p)", ap);
    dm_slot k = 0; int made = 2; while (dm_slot_create(&k) == 0) made++;
    CHECK(made == 63, "63 slots in a 64-word block (slot 0 kept free): %d", made);
    printf("t_slots: %d checks, %d failed\n", n, fail); return fail != 0;
}
