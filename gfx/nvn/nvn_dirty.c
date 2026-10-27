// Write-protect dirty-page tracking for guest memory: pool pages of packaged textures (CPU writes, GPU reads) are set read-only after upload, and as soon as the game writes it faults -> mark dirty, unprotect;
// before use, check the write generation of the covered pages and re-upload if newer than at upload time. NVN semantics: a texture descriptor points to pool memory, and the GPU reads the current content every time
// (measured: the game writes streamed assets into texture storage that is still being sampled, without re-registering).
// Signal handler constraint: the page-fault path uses only atomic operations and mprotect, no allocation, no locks.
#include "nvn_dirty.h"
#include "../../host/dm.h"
#include <stdatomic.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <sched.h>

// Order of page state and mprotect (the side changing protection first sets the state to BUSY and sets the final state only after mprotect has landed; other threads wait when they see BUSY):
//   set read-only: NONE -> BUSY -> mprotect(read-only) -> PROT; unprotect: PROT -> BUSY -> mprotect(writable) -> NONE.
// Invariant: a page whose state is NONE is always writable, and one whose state is PROT is always read-only. Setting read-only used to be "record PROT first, batch up a run, then mprotect"; during that time another thread's unprotect-before-write
// could set the state back to NONE, and then the batched mprotect landed => the page read-only with state NONE: when the game thread wrote to it the page-fault handler only returned "re-execute" and spun forever;
// when the host read a file into it with a system call, prep_write saw state NONE and skipped, the system call did not go through the page-fault handler and failed with EFAULT, leaving the data incomplete.
// BUSY lasts at most one mprotect (plus, when setting read-only, the few atomic writes that change the state of the pages in the run). The wait has an upper limit BUSY_WAIT_MS and reports red beyond it:
// in the page-fault handler, exceeding it returns "not our page" and lets the crash handler report the crash truthfully instead of spinning silently. The wait only reads atomics and takes no lock.
enum { PG_NONE = 0, PG_PROT = 1, PG_BUSY = 2 };
enum { BUSY_WAIT_MS = 2000 };
// Tracked ranges = the memory pools (registered by MemoryPoolInitialize). After a pool is destroyed its live=0 slot is reused; the signal handler scans without locks.
// The table grows in blocks of 1,024 slots up to 262,144; a block is never moved or freed, so the signal handler reads it without locks (seedds/nxnative
// 7dde056, MIT: Breath of the Wild reaches 4,006 pools in its first shrine). It was a fixed 256 slots, then 4,096; Tears of the Kingdom peaks at
// 3,067. A pool past the end goes untracked: its textures count as dirty and are re-uploaded at every use. The scans only cover slots in use
enum { BLK_SHIFT = 10, BLK = 1 << BLK_SHIFT, NBLK = 256, MAX_RANGE = BLK * NBLK };
// Ranges are rounded to host pages (16 KB) but guest memory pools are only 4 KB aligned: adjacent pools share their first / last page, so one
// address can fall in several registered ranges. So "is this texture tracked" looks for the range that holds its whole span; a fault or a host
// pre-write releases every range covering the page; destroying a pool finds its range by the pool's exact start (mem). (From seedds/nxnative
// c2c0105, MIT: taking the first range containing the address judged a texture whose pool shares its first page with a smaller preceding pool
// untracked, so it was re-uploaded on every use; a fault on a shared page released one range and left the other marked protected; destroying a
// pool could unregister its neighbour.)
// born: the clock when the range was registered. A stamp older than its range is of other memory (the pool was finalized and its memory reused):
// whatever it says, the pages count as written since (MacNativeGames 873edff, MIT: a GPU write-back record that outlived its pool read as fresh,
// because a new range's generations start at 0, and wrote an old rendering into the game's heap)
typedef struct { uintptr_t base, end, mem; _Atomic uint8_t *state; _Atomic uint64_t *gen; size_t cap; _Atomic int live; uint64_t born; } range_t;
static _Atomic(range_t *) g_blk[NBLK];
static inline range_t *rg(int i) { return &atomic_load_explicit(&g_blk[i >> BLK_SHIFT], memory_order_acquire)[i & (BLK - 1)]; }
static _Atomic int g_nr;
static uintptr_t g_pg, g_shift;
static _Atomic uint64_t g_now = 1;
static _Atomic uint64_t g_faults, g_preps;
static _Atomic uint64_t g_adds_total;
static _Atomic int g_live, g_live_peak;   // number of registered pools and peak (for the memory readout line)
void nvn_dirty_stat(int *live, int *peak, int *cap) { *live = atomic_load(&g_live); *peak = atomic_load(&g_live_peak); int b = 0; while (b < NBLK && atomic_load(&g_blk[b])) b++; *cap = b * BLK; }   // cap: slots allocated so far
static _Atomic int g_overflow_logged;
static pthread_mutex_t g_slot_mu = PTHREAD_MUTEX_INITIALIZER;   // Pool creation / destruction (guest threads, possibly concurrent) pick slots under a mutex; the page-fault handling path does not touch it
static _Atomic uint64_t g_mp_calls, g_mp_pages, g_mp_ticks;   // This module's mprotect: call count, pages covered, time spent (mach timebase, safe in the page-fault path too)
static void mp(uintptr_t a, size_t len, int prot) {
    uint64_t t = mach_absolute_time(); mprotect((void *)a, len, prot);
    atomic_fetch_add(&g_mp_ticks, mach_absolute_time() - t); atomic_fetch_add(&g_mp_calls, 1); atomic_fetch_add(&g_mp_pages, len >> g_shift);
}

static mach_timebase_info_data_t g_tb;   // Obtained when a pool is first registered (the fault path only multiplies and divides)
static _Atomic int g_busy_red;
// Wait for this page to leave BUSY. Returns 1 = it has left; 0 = waited past the limit (reported red once). Usable in a signal handler: only atomic reads, mach_absolute_time and sched_yield
static int busy_wait(range_t *r, uintptr_t p) {
    uint64_t t0 = 0;
    for (unsigned n = 1; atomic_load(&r->state[p]) == PG_BUSY; n++) {
        if (n & 0x3ff) continue;
        uint64_t now = mach_absolute_time(); if (!t0) t0 = now;
        if ((now - t0) * g_tb.numer / g_tb.denom > (uint64_t)BUSY_WAIT_MS * 1000000ull) {
            if (!atomic_exchange(&g_busy_red, 1)) LOG("[red] dirty-page tracking: page %p stuck 'busy' for over %d ms (the thread changing protection has not finished), giving up waiting", (void *)(r->base + (p << g_shift)), BUSY_WAIT_MS);
            return 0; }
        sched_yield();
    }
    return 1;
}
static _Atomic uint64_t g_hint_walks;   // nvn_dirty_state_hint calls that had to walk the ranges
uint64_t nvn_dirty_hint_walks(void) { return atomic_load(&g_hint_walks); }
static int live_at(int i) { return atomic_load_explicit(&rg(i)->live, memory_order_acquire); }
// The registered range that holds all of [a, a+len) (a texture's tracking, protection and dirty check use it)
static range_t *find_span(uintptr_t a, size_t len) {
    int n = atomic_load(&g_nr);
    for (int i = 0; i < n; i++) { range_t *r = rg(i); if (live_at(i) && a >= r->base && a + len <= r->end) return r; }
    return NULL;
}
// A write released through one range stamps every range on that host page (plan item 118; acnh-mobile c2af194's case, MIT): only a range's first
// and last page can be shared (pools are 4 KiB aligned and do not overlap), and a range registered after the page was protected starts "not
// protected" there, so the fault released only the range that held the protection and the other never saw the write (a texture in it kept stale
// contents; and find_span may answer a query about one pool with the other's range). Atomics only: called from the fault handler too
static void stamp_shared(range_t *r, uintptr_t p, uint64_t g) {
#ifndef DM_TEST_DIRTY_STAMP_ONE   // negative control of t_dirty_shared only: the releasing range alone, as before
    uintptr_t np = (r->end - r->base) >> g_shift; if (p != 0 && p + 1 != np) return;
    uintptr_t a = r->base + (p << g_shift); int n = atomic_load(&g_nr);
    for (int i = 0; i < n; i++) {
        range_t *o = rg(i); if (o == r || !live_at(i) || a < o->base || a >= o->end) continue;
        _Atomic uint64_t *gp = &o->gen[(a - o->base) >> g_shift]; uint64_t cur = atomic_load(gp);
        while (cur < g && !atomic_compare_exchange_weak(gp, &cur, g)) {}
    }
#else
    (void)r; (void)p; (void)g;
#endif
}
#ifndef DM_DIRTY_AFTER_CLOCK
#define DM_DIRTY_AFTER_CLOCK()   // t_dirty_stamp stages a release paused here: its generation taken, the page still BUSY
#endif
static void unprotect_page(range_t *r, uintptr_t p) {   // The caller has already changed the state from PROT to BUSY
    mp(r->base + (p << g_shift), g_pg, PROT_READ | PROT_WRITE);
    uint64_t g = atomic_fetch_add(&g_now, 1) + 1; DM_DIRTY_AFTER_CLOCK();
    atomic_store(&r->gen[p], g); stamp_shared(r, p, g);
    atomic_store(&r->state[p], PG_NONE);
}
// Release one page of a range: if protected, unprotect and stamp it; if another thread is changing its protection, wait for it to finish and look
// again (the other side may have been setting read-only: the page is then PROT after the wait and still has to be released; a pre-write never
// reaches the fault handler). Returns 1 = this page is managed here; 0 = it is not, or the wait on BUSY timed out
static int release_page(range_t *r, uintptr_t p, _Atomic uint64_t *count) {
    for (;;) {
        uint8_t st = PG_PROT;
        if (atomic_compare_exchange_strong(&r->state[p], &st, PG_BUSY)) { unprotect_page(r, p); if (count) atomic_fetch_add(count, 1); return 1; }
        if (st != PG_BUSY) return atomic_load(&r->gen[p]) != 0;   // already NONE: just released by another thread (re-execute); pages never protected are not ours
        if (!busy_wait(r, p)) return 0;
    }
}
static int on_fault(void *addr, int is_write, void *uc) {
    (void)uc;
    if (!is_write) return 0;
    uintptr_t a = (uintptr_t)addr; int ours = 0, n = atomic_load(&g_nr);
    for (int i = 0; i < n; i++)   // every range sharing the page: releasing only one leaves the other's state "protected", and its later writes are missed
        { range_t *r = rg(i); if (live_at(i) && a >= r->base && a < r->end) ours |= release_page(r, (a - r->base) >> g_shift, &g_faults); }
    return ours;
}
// Before a host write: the protected pages it covers become writable, consecutive ones in one mprotect (nxnative e06a8ec's release_pages, MIT;
// a large host write used to take one call per page). Pages another thread is changing (BUSY) go through release_page, which waits for it
static void prep_write(void *addr, size_t len) {
    uintptr_t a = (uintptr_t)addr, e = a + len; int n = atomic_load(&g_nr);
    if (!len) return;
    for (int i = 0; i < n; i++) {
        range_t *r = rg(i);
        if (!live_at(i) || e <= r->base || a >= r->end) continue;
        uintptr_t lo = a > r->base ? a : r->base, hi = e < r->end ? e : r->end, run = 0; int open = 0;
        for (uintptr_t p = (lo - r->base) >> g_shift, q = (hi - 1 - r->base) >> g_shift; p <= q + 1; p++) {
            uint8_t st = PG_NONE; int took = p <= q && (st = PG_PROT, atomic_compare_exchange_strong(&r->state[p], &st, PG_BUSY));
            if (took) { if (!open) { run = p; open = 1; } continue; }
            if (open) {   // pages [run, p) are ours (BUSY): one mprotect, then each stamped and released
                mp(r->base + (run << g_shift), (p - run) << g_shift, PROT_READ | PROT_WRITE);
                uint64_t g = atomic_fetch_add(&g_now, 1) + 1; DM_DIRTY_AFTER_CLOCK();
                for (uintptr_t k = run; k < p; k++) { atomic_store(&r->gen[k], g); stamp_shared(r, k, g); atomic_store(&r->state[k], PG_NONE); }
                atomic_fetch_add(&g_preps, p - run); open = 0;
            }
            if (p <= q && st == PG_BUSY) release_page(r, p, &g_preps);
        }
    }
}

void nvn_dirty_add_range(const void *mem, size_t size) {
    static int inited;
    if (!inited) { inited = 1; g_pg = (uintptr_t)getpagesize(); g_shift = __builtin_ctzl(g_pg); mach_timebase_info(&g_tb); dm_fault_hook_register(on_fault); dm_guest_write_prep_register(prep_write); }
    atomic_fetch_add(&g_adds_total, 1);
    pthread_mutex_lock(&g_slot_mu);
    int n = atomic_load(&g_nr);
    int slot = -1;
    for (int i = 0; i < n; i++) if (!live_at(i)) { slot = i; break; }
    if (slot < 0) {
        if (n >= MAX_RANGE) {
            int expected = 0;
            if (atomic_compare_exchange_strong(&g_overflow_logged, &expected, 1)) LOG("[red] dirty-page tracking ranges full (%d), pool %p not tracked", MAX_RANGE, mem);
            pthread_mutex_unlock(&g_slot_mu); return;
        }
        slot = n;
        if (!atomic_load_explicit(&g_blk[n >> BLK_SHIFT], memory_order_acquire)) {   // published before g_nr counts the slot (the scans read g_nr first)
            range_t *b = calloc(BLK, sizeof *b); if (!b) die("dirty-page tracking: cannot allocate range block %d", n >> BLK_SHIFT);
            atomic_store_explicit(&g_blk[n >> BLK_SHIFT], b, memory_order_release);
        }
    }
    { int lv = atomic_fetch_add(&g_live, 1) + 1, pk = atomic_load(&g_live_peak); while (lv > pk && !atomic_compare_exchange_weak(&g_live_peak, &pk, lv)) {} }
    range_t *r = rg(slot);
    uintptr_t base = (uintptr_t)mem & ~(g_pg - 1), end = ((uintptr_t)mem + size + g_pg - 1) & ~(g_pg - 1);
    size_t np = (end - base) >> g_shift;
    _Atomic uint8_t *state = r->state;
    _Atomic uint64_t *gen = r->gen;
    if (r->cap < np) {
        state = calloc(np, 1); gen = calloc(np, sizeof *gen);
        if (!state || !gen) die("dirty-page table allocation failed (%zu pages)", np);
        // The old array is not freed: the signal handler may still have a very brief stale reader; leak = 9 bytes per page.
        r->cap = np;
    } else {
        memset((void *)state, 0, np); memset((void *)gen, 0, np * sizeof *gen);
    }
    r->base = base; r->end = end; r->mem = (uintptr_t)mem; r->state = state; r->gen = gen; r->born = atomic_fetch_add(&g_now, 1) + 1;
    if (slot == n) atomic_store(&g_nr, n + 1);
    atomic_store_explicit(&r->live, 1, memory_order_release);
    pthread_mutex_unlock(&g_slot_mu);
}
// Pool destruction: first restore still-protected pages to writable (the pool memory may be reused for something else once returned to the game), then mark invalid
void nvn_dirty_remove_range(const void *mem) {
    pthread_mutex_lock(&g_slot_mu);
    range_t *r = NULL; int n = atomic_load(&g_nr);
    for (int i = 0; i < n && !r; i++) if (live_at(i) && rg(i)->mem == (uintptr_t)mem) r = rg(i);   // the exact pool start: a neighbour sharing a page may have the same rounded base
    if (r) { prep_write((void *)r->base, r->end - r->base); atomic_store_explicit(&r->live, 0, memory_order_release); atomic_fetch_sub(&g_live, 1); }
    pthread_mutex_unlock(&g_slot_mu);
}
int nvn_dirty_tracked(const void *addr, size_t len) { return find_span((uintptr_t)addr, len) != NULL; }
// Set read-only: mark the page BUSY first, and mark it PROT only after mprotect has landed (for the order and the invariant see the state notes at the top of the file)
void nvn_dirty_protect(const void *addr, size_t len) {
    range_t *r = len ? find_span((uintptr_t)addr, len) : NULL;
    if (!r) return;
    uintptr_t a = (uintptr_t)addr, p = (a - r->base) >> g_shift, q = (a + len - 1 - r->base) >> g_shift;
    // Consecutive pages set to PROT are merged into one mprotect (per-page calls ran to over a thousand per frame when large textures were first uploaded; character select, startup frame 5: 4888 calls).
    // Pages already PROT break a run and are not protected again. A page another thread is changing (BUSY: a write being released) is waited
    // for and then taken, the run closed first so no page of ours is held BUSY meanwhile: left alone, it ended writable after the caller's stamp,
    // with a generation the release took before that stamp, and every later write went unseen (seedds/nxnative 5b0c11a, MIT: Tears of the
    // Kingdom's glyph letters missing; t_dirty_stamp)
    uintptr_t run = 0; int open = 0;
    for (uintptr_t i = p; i <= q + 1; i++) {
        uint8_t st = PG_NONE;
        int took = i <= q && atomic_compare_exchange_strong(&r->state[i], &st, PG_BUSY);
        if (took && !open) { run = i; open = 1; }
        if (!took && open) { mp(r->base + (run << g_shift), (i - run) << g_shift, PROT_READ); for (uintptr_t k = run; k < i; k++) atomic_store(&r->state[k], PG_PROT); open = 0; }
#ifndef DM_TEST_DIRTY_SKIP_BUSY   // negative control of t_dirty_stamp only: a BUSY page left alone, as before
        if (!took && i <= q && st == PG_BUSY && busy_wait(r, i)) i--;   // NONE or PROT now: look at it again
#endif
    }
}
// The stamp of the content about to be read, then its pages set read-only, in this order: a write released after the protection began has a
// newer generation than the stamp. Protecting first let a write that faulted in between take the very generation the stamp then read, so it
// counted as part of a copy that may not have seen it, and its page stayed writable (nxnative 5b0c11a; t_dirty_stamp)
#ifndef DM_DIRTY_AFTER_PROTECT
#define DM_DIRTY_AFTER_PROTECT()   // t_dirty_stamp lets a write fault and be released here
#endif
uint64_t nvn_dirty_protect_stamp(const void *addr, size_t len) {
#ifdef DM_TEST_DIRTY_STAMP_AFTER   // negative control of t_dirty_stamp only: protected first, as before
    nvn_dirty_protect(addr, len); DM_DIRTY_AFTER_PROTECT(); return nvn_dirty_now();
#else
    uint64_t s = nvn_dirty_now(); nvn_dirty_protect(addr, len); DM_DIRTY_AFTER_PROTECT(); return s;
#endif
}
uint64_t nvn_dirty_now(void) { return atomic_load(&g_now); }
static int span_written(const range_t *r, uintptr_t a, size_t len, uint64_t stamp) {
#ifndef DM_TEST_DIRTY_NO_BORN   // negative control of t_dirty_stamp only: a stamp older than its range read against the new range's generations
    if (stamp < r->born) return 1;   // taken before this range existed: of other memory (see range_t)
#endif
    uintptr_t p = (a - r->base) >> g_shift, q = (a + len - 1 - r->base) >> g_shift;
    for (uintptr_t i = p; i <= q; i++) if (atomic_load(&r->gen[i]) > stamp) return 1;   // The generation of the last write before upload is <= the stamp; any later write must be > the stamp
    return 0;
}
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp) {
    if (!len) return 0;
    range_t *r = find_span((uintptr_t)addr, len);
    if (!r) return 1;   // When untracked, conservatively treat as dirty: better to upload more than to show stale content
    return span_written(r, (uintptr_t)addr, len, stamp);
}
int nvn_dirty_state(const void *addr, size_t len, uint64_t stamp) {   // one walk of the ranges where tracked + since took two
    range_t *r = find_span((uintptr_t)addr, len);
    if (!r) return 2;
    return len && span_written(r, (uintptr_t)addr, len, stamp);
}
// nvn_dirty_state with the caller's remembered range (*hint: its slot, -1 at first; nxnative e06a8ec's nvn_dirty_span, MIT): a texture's storage
// stays in one pool, so the walk over every range (about 3,000 in Tears of the Kingdom) is done once and later calls only check that the slot is still
// live and still holds the span; a removed or reused slot falls back to the walk
int nvn_dirty_state_hint(const void *addr, size_t len, uint64_t stamp, int *hint) {
    uintptr_t a = (uintptr_t)addr; int n = atomic_load(&g_nr), h = *hint; range_t *r = NULL;
    if (h >= 0 && h < n && live_at(h)) { range_t *c = rg(h); if (a >= c->base && a + len <= c->end) r = c; }
    if (!r) {
        for (int i = 0; i < n && !r; i++) { range_t *c = rg(i); if (live_at(i) && a >= c->base && a + len <= c->end) { r = c; *hint = i; } }
        if (!r) { *hint = -1; return 2; }
        atomic_fetch_add(&g_hint_walks, 1);
    }
    return len && span_written(r, a, len, stamp);
}
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total) {
    int n = atomic_load(&g_nr), active = 0;
    for (int i = 0; i < n; i++) active += live_at(i) != 0;
    *live = active; *used_slots = n; *adds_total = atomic_load(&g_adds_total);
}
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps) { *faults = atomic_load(&g_faults); *preps = atomic_load(&g_preps); }
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns) {
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    *calls = atomic_load(&g_mp_calls); *pages = atomic_load(&g_mp_pages); *ns = atomic_load(&g_mp_ticks) * tb.numer / tb.denom;
}
