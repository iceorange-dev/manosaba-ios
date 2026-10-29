// Host calls on a per-thread host stack (hstack.S): the TSD slot holding each guest thread's host stack top, the wrappers guest-visible host
// functions are handed out as, and the record a thread inside a host call leaves at the top of its host stack. DM_HSTACK=0 turns it off: host
// functions are handed out as they are and run on the guest stack, as before.
#include "dm.h"

#define DM_HTHUNKS 2048          // = the .rept count in hstack.S
extern char dm_hthunks[];
void *dm_hthunk_fn[DM_HTHUNKS];  // read by hstack.S
uint64_t dm_hstk_off;            // TSD offset of the host stack top (key * 8), read by hstack.S and entry.S; 0 = off
static dm_slot g_keyhstk;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_nthunk;
static uint16_t g_hash[4096];    // function → wrapper index + 1 (open addressing; at most half full)

static void hstack_once(void) {
    const char *e = getenv("DM_HSTACK");
    if (e && !strcmp(e, "0")) { LOG("DM_HSTACK=0: host calls run on the guest stack"); return; }
    if (dm_slot_create(&g_keyhstk)) die("cannot create the host stack key");
    dm_hstk_off = (uint64_t)g_keyhstk * 8;
}
void dm_hstack_init(void) { static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, hstack_once); }

// The same function always gets the same wrapper: the game may compare the pointers it got
void *dm_hwrap(void *fn) {
    dm_hstack_init();
    if (!fn || !dm_hstk_off) return fn;
    pthread_mutex_lock(&g_mu);
    unsigned h = (unsigned)(((uint64_t)fn >> 2) * 0x9E3779B97F4A7C15ull >> 52);
    while (g_hash[h] && dm_hthunk_fn[g_hash[h] - 1] != fn) h = (h + 1) & 4095;
    if (!g_hash[h]) {
        if (g_nthunk == DM_HTHUNKS) die("host call wrappers: more than %d host functions", DM_HTHUNKS);
        __atomic_store_n(&dm_hthunk_fn[g_nthunk], fn, __ATOMIC_RELEASE);
        g_hash[h] = (uint16_t)++g_nthunk;
    }
    unsigned i = g_hash[h] - 1u;
    pthread_mutex_unlock(&g_mu);
    return dm_hthunks + (size_t)i * 8;
}
unsigned dm_hwrap_count(void) { return g_nthunk; }

// A host vtable handed to the guest (hle_movie.c) with every slot wrapped, made once per table
void *const *dm_hwrap_vt(void *const *vt, int n) {
    dm_hstack_init();
    if (!dm_hstk_off) return vt;
    static struct { void *const *vt; void **w; } made[8]; static int nmade;
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < nmade; i++) if (made[i].vt == vt) { pthread_mutex_unlock(&g_mu); return (void *const *)made[i].w; }
    pthread_mutex_unlock(&g_mu);
    void **w = calloc((size_t)n, sizeof *w);
    for (int i = 0; i < n; i++) w[i] = dm_hwrap(vt[i]);
    pthread_mutex_lock(&g_mu);
    if (nmade == 8) die("host call wrappers: more than 8 vtables");
    for (int i = 0; i < nmade; i++) if (made[i].vt == vt) { pthread_mutex_unlock(&g_mu); free(w); return (void *const *)made[i].w; }   // made meanwhile
    made[nmade].vt = vt; made[nmade++].w = w;
    pthread_mutex_unlock(&g_mu);
    return (void *const *)w;
}

// The record of the host call or svc gate that holds a thread's host stack (layout in hstack.S), NULL when it holds none: tsd = the thread's
// slot block's base (slots.h), top = its host stack top. For a thread that is stopped, or parked in an svc gate.
const uint64_t *dm_hstack_record(uint64_t tsd, uint64_t top) {
    if (!dm_hstk_off || !tsd || !top) return NULL;
    if (*(const volatile uint64_t *)(tsd + dm_hstk_off)) return NULL;   // the slot holds the top while the stack is free
    return (const uint64_t *)(top - DM_HREC);
}
