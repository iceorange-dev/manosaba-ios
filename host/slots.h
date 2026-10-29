// Thread slots the rewritten code and the host's assembly read directly: the slot block's base (patch_nso.py emit_tp; entry.S / hstack.S TSD) plus the
// slot's offset, slot * 8, which the loader writes into each module's key page. K18 (the guest's x18), KTLS / KTLR (its tpidr_el0 / tpidrro_el0), KGATE,
// the parameter slots and the host stack top (hstack.c) are such slots. Apple: Darwin's TSD at tpidrro_el0, the slots pthread keys. Android: bionic
// keeps pthread keys out of reach, so each thread has a block of its own in bionic's TLS_SLOT_APP (tpidr_el0 + 16; tls_defines.h: "available for use by
// apps in API level 29 and later"), made by its first dm_slot_set, slots numbered from 1 (android/slots.c; plan item 301c).
#pragma once
#include <pthread.h>
#include <stdint.h>
#ifdef __APPLE__
typedef pthread_key_t dm_slot;
static inline int dm_slot_create(dm_slot *k) { return pthread_key_create(k, NULL); }
static inline void dm_slot_set(dm_slot k, const void *v) { pthread_setspecific(k, v); }
static inline void *dm_slot_get(dm_slot k) { return pthread_getspecific(k); }
static inline uint64_t dm_slot_base(void) { uint64_t b; __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(b)); return b & ~7ull; }
#else
typedef unsigned long dm_slot;
int dm_slot_create(dm_slot *k);                  // 0, or -1 when the block is full (DM_SLOTS)
void dm_slot_set(dm_slot k, const void *v);
static inline uint64_t dm_slot_base(void) { uint64_t tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp)); return ((const uint64_t *)tp)[2]; }
static inline void *dm_slot_get(dm_slot k) { uint64_t b = dm_slot_base(); return b ? ((void *const *)b)[k] : NULL; }
#endif
