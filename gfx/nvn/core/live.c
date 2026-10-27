// gfx/nvn/core/live.c: the live texture at a storage address and the texture objects' host copies, see live.h
#include "live.h"
#include "../nvn_hmap.h"
#include <pthread.h>
#include <stdlib.h>

static nvn_hmap g_live = {.klen = 8}, g_dead = {.klen = 8}; static pthread_mutex_t g_live_mu = PTHREAD_MUTEX_INITIALIZER;
_Atomic uint64_t g_fin_gen, g_live_follows;
static uint64_t g_alias_n;   // textures initialized over the storage of one not finalized (two live textures sharing storage: MacNativeGames' stor_*)
void mtl_tex_live(const tex_t *t) {   // hle_nvn.c nvnTextureInitialize
    if (!t->mem || !t->serial) return;
    uint64_t k = (uint64_t)(uintptr_t)t->mem; tex_t *c = malloc(sizeof *c); if (!c) MDIE("live texture"); *c = *t;
    pthread_mutex_lock(&g_live_mu);
    tex_t *old = nvn_hm_put(&g_live, &k, c);
    if (old && old->serial != t->serial && !nvn_hm_get(&g_dead, &old->serial)) {
        uint64_t n = ++g_alias_n;
        if (n <= 8 || !(n % 1000)) MLOG("texture %llu (%ux%ux%u format %#x flags %#x) initialized over the storage of live texture %llu (%ux%ux%u format %#x flags %#x): two textures share storage (%llu so far)",
            (unsigned long long)t->serial, t->d.width, t->d.height, t->d.depth, t->d.format, t->d.flags, (unsigned long long)old->serial, old->d.width, old->d.height, old->d.depth, old->d.format, old->d.flags, (unsigned long long)n);
    }
    pthread_mutex_unlock(&g_live_mu); free(old);
}
void live_finalized(const tex_t *t) {
    pthread_mutex_lock(&g_live_mu);
    uint64_t s = t->serial; nvn_hm_put(&g_dead, &s, (void *)1);
    if (t->mem) { uint64_t k = (uint64_t)(uintptr_t)t->mem; tex_t *cur = nvn_hm_get(&g_live, &k); if (cur && cur->serial == t->serial) free(nvn_hm_del(&g_live, &k)); }
    pthread_mutex_unlock(&g_live_mu);
    atomic_fetch_add(&g_fin_gen, 1);
}
void live_released(uint64_t serial) { pthread_mutex_lock(&g_live_mu); nvn_hm_del(&g_dead, &serial); pthread_mutex_unlock(&g_live_mu); }   // fin_drain
int live_follow(const tex_t *reg, int released, tex_t *out) {
    if (!reg->mem) return 0;
    uint64_t k = (uint64_t)(uintptr_t)reg->mem, s = reg->serial; int ok = 0;
    pthread_mutex_lock(&g_live_mu);
    if (released || nvn_hm_get(&g_dead, &s)) {
        const tex_t *cur = nvn_hm_get(&g_live, &k);
#ifndef DM_TEST_LIVE_ANY_SHAPE   // negative control of t_core_live only: the texture living there followed whatever its shape
        ok = cur && cur->serial != reg->serial && cur->size == reg->size && cur->d.format == reg->d.format && cur->d.width == reg->d.width && cur->d.height == reg->d.height &&
             cur->d.depth == reg->d.depth && cur->d.target == reg->d.target && cur->d.levels == reg->d.levels && cur->d.samples == reg->d.samples && cur->d.flags == reg->d.flags;
#else
        ok = cur && cur->serial != reg->serial;
#endif
        if (ok) *out = *cur;
    }
    pthread_mutex_unlock(&g_live_mu);
    return ok;
}

enum { SNAP_BUCKETS = 4096 };
typedef struct snap { struct snap *next; uint64_t due; tex_t t; } snap_t;
static snap_t *g_snap[SNAP_BUCKETS], *g_snap_old, **g_snap_old_tail = &g_snap_old; static pthread_mutex_t g_snap_mu = PTHREAD_MUTEX_INITIALIZER; static uint64_t g_snap_live;   // g_snap_old: released copies in due order (FIFO)
const tex_t *mtl_tex_snap(const tex_t *t) {
    if (!t || !t->serial) return t;   // not a texture this layer initialized: kept as is (replay reports it)
    pthread_mutex_lock(&g_snap_mu);
    snap_t **b = &g_snap[t->serial % SNAP_BUCKETS], *e = *b;
    while (e && e->t.serial != t->serial) e = e->next;
    if (!e && (e = malloc(sizeof *e))) { e->t = *t; e->due = 0; e->next = *b; *b = e; g_snap_live++; }
    pthread_mutex_unlock(&g_snap_mu);
    return e ? &e->t : t;
}
void tex_snap_retire(uint64_t serial, uint64_t due) {
    pthread_mutex_lock(&g_snap_mu);
    for (snap_t **b = &g_snap[serial % SNAP_BUCKETS]; *b; b = &(*b)->next)
        if ((*b)->t.serial == serial) { snap_t *e = *b; *b = e->next; e->due = due; e->next = NULL; *g_snap_old_tail = e; g_snap_old_tail = &e->next; break; }   // dues only grow: appended in order
    pthread_mutex_unlock(&g_snap_mu);
}
void tex_snap_reap(uint64_t now) {
    pthread_mutex_lock(&g_snap_mu);
    // From the head until the first copy not due (the list was walked whole every submission: 0.7 ms with 18,000 copies in Tears of the Kingdom)
    while (g_snap_old && g_snap_old->due <= now) { snap_t *e = g_snap_old; g_snap_old = e->next; free(e); g_snap_live--; }
    if (!g_snap_old) g_snap_old_tail = &g_snap_old;
    pthread_mutex_unlock(&g_snap_mu);
}
int tex_snap_copy(tex_t *out, int cap) {   // the live copies into out (at most cap), for a census; how many there are
    pthread_mutex_lock(&g_snap_mu); int n = 0;
    for (int b = 0; b < SNAP_BUCKETS; b++) for (snap_t *e = g_snap[b]; e; e = e->next) { if (n < cap) out[n] = e->t; n++; }
    pthread_mutex_unlock(&g_snap_mu);
    return n;
}
uint64_t mtl_tex_snap_count(void) { pthread_mutex_lock(&g_snap_mu); uint64_t n = g_snap_live; pthread_mutex_unlock(&g_snap_mu); return n; }
