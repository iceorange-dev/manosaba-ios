// Uniform snapshots without a GPU (gfx/nvn/core/usnap.c, plan item 264), with the backend faked: two memory pools of 1 MiB, an arena in host
// memory (GPU address = CPU address), the worker threads run in turn, a completed fence the test sets. Covered: the walk collects each bound
// uniform range once (sizes 0 and above 1 MiB left out), through calls and jumps, and reads deferred handles once a submission (again in the
// next); the radix sort orders as qsort; the merge joins ranges up to 256 bytes apart in one pool and at most 1 MiB, trims an overlap, keeps
// pools apart; the snapshot copies each merged range at the same offset modulo 256 with its bytes, leaves a range in no pool and one a running GPU
// write reaches (and forgets writes completed); lookups find the copy holding a range, none across two, the last copy found only when it holds
// the range (alternating lookups); a GPU write drops the copies it reaches (noted once for repeats); a source made unreadable drops its copy and
// the copies after it are made (one guard for the run, one after the fault), in parts over the workers too; the capture's copies the same;
// DM_NVN_UBO_SNAPSHOT=0 copies nothing, yet the walk still reads deferred handles. A range that a running submission's renamed updates were written
// to (plan item 301e3) is copied with their bytes laid over memory, one another write reaches still not, a completed write not laid over a newer
// byte of the game, nothing copied when the pending-write table overflowed; a range noted both ways counts as written otherwise.
// The last copy found taken whatever the range (-DDM_TEST_US_LAST_UNCHECKED, t_usnap's broken variant): 7 fail. Every write taken as renamed updates
// (-DDM_TEST_US_GW_ALL_KNOWN): 2 fail; the updates not laid over (-DDM_TEST_US_NO_LAY): 2 fail; completed writes laid too
// (-DDM_TEST_PEND_LAY_DONE): 1 fails.
#include "../nvn/core/usnap.h"
#include "../nvn/core/pend.h"
#include "core_test.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

void crash_install(void);


// ---- the backend, faked ----
enum { POOL = 1 << 20 };
static uint8_t *g_pool[2];
int nvn_be_pool_span(uint64_t lo, uint64_t len) {
    for (int i = 0; i < 2; i++) { uint64_t a = (uint64_t)(uintptr_t)g_pool[i]; if (lo >= a && lo + len <= a + POOL) return 1; }
    return 0;
}
static uint8_t *g_arena; static size_t g_arena_off;
void nvn_be_arena(size_t len, uint8_t **cpu, uint64_t *gpu, void **buf) {   // 256-aligned blocks, as the backend's arena
    g_arena_off = (g_arena_off + 255) & ~(size_t)255; if (g_arena_off + len > (8u << 20)) g_arena_off = 0;
    *cpu = g_arena + g_arena_off; *gpu = (uint64_t)(uintptr_t)*cpu; *buf = g_arena; g_arena_off += len;
}
static int g_par_calls;
void nvn_be_parallel(int cnt, void (*fn)(void *ctx, size_t i), void *ctx) { g_par_calls++; for (int i = 0; i < cnt; i++) fn(ctx, (size_t)i); }
static uint64_t g_done;
uint64_t nvn_be_done_fence(void) { return g_done; }

// ---- command lists ----
static uint8_t g_mem[2][4096] __attribute__((aligned(8))); static size_t g_len[2];
static void rec(int b, uint32_t op, const void *pl, size_t sz) {
    uint8_t *p = g_mem[b] + g_len[b]; uint32_t size = (uint32_t)((8 + sz + 7) & ~(size_t)7);
    op_hdr h = {op, size}; memcpy(p, &h, 8); memset(p + 8, 0, size - 8); if (sz) memcpy(p + 8, pl, sz); g_len[b] += size;
}
static void ubo(int b, uint64_t addr, uint64_t size) { p_bind_buf u = {1, 0, addr, size}; rec(b, OP_BIND_UBO, &u, sizeof u); }
static void lists_reset(void) { g_len[0] = g_len[1] = 0; }
static uint64_t A(int pool, uint64_t off) { return (uint64_t)(uintptr_t)g_pool[pool] + off; }
static void submit(void) {   // a new submission: its generation, no copies, the snapshot of list 0
    g_bh_gen++; g_us.n = 0; uint64_t h = (uint64_t)(uintptr_t)g_mem[0]; us_snapshot(&h, 1);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); crash_install();
    for (int i = 0; i < 2; i++) if (posix_memalign((void **)&g_pool[i], 4096, POOL)) abort();
    if (posix_memalign((void **)&g_arena, 256, 8u << 20)) abort();   // 256-aligned blocks, as the backend's arena
    for (int i = 0; i < POOL; i++) { g_pool[0][i] = (uint8_t)i; g_pool[1][i] = (uint8_t)(i * 7); }
    g_us.on = 1;

    // the walk
    lists_reset(); ubo(1, A(0, 0x300), 64); { uint8_t e[8] = {0}; rec(1, OP_END, e, 0); }
    ubo(0, A(0, 0x100), 64); ubo(0, A(0, 0x100), 64); ubo(0, A(0, 0x200), 0); ubo(0, A(1, 0), (1u << 20) + 16);
    { uint64_t t = (uint64_t)(uintptr_t)g_mem[1]; rec(0, OP_CALL, &t, 8); }
    static uint64_t src[2] = {0x1111, 0x2222}; p_bind_handles bh = {0}; bh.stage = 1; bh.n = 2; bh.defer = 1; bh.src = (uint64_t)(uintptr_t)src; rec(0, OP_BIND_TEX, &bh, sizeof bh);
    rec(0, OP_END, NULL, 0);
    g_done = 100; submit();
    check("the walk: one range bound twice, one of size 0, one above 1 MiB, one in a called list: 2 collected", 2, g_us.rn);
    const p_bind_handles *rb = NULL; for (size_t o = 0; o < g_len[0];) { op_hdr h; memcpy(&h, g_mem[0] + o, 8); if (h.op == OP_BIND_TEX) rb = (const p_bind_handles *)(g_mem[0] + o + 8); o += h.size; }
    check("a deferred binding record's handles are read at the submission's start", 0x2222, rb ? (long long)rb->h[1] : -1);
    src[1] = 0x3333; { uint64_t h = (uint64_t)(uintptr_t)g_mem[0]; us_collect((const uint8_t *)(uintptr_t)h, 0, 0); }
    check("... and not again by another walk of the same submission", 0x2222, (long long)rb->h[1]);
    submit();
    check("... the next submission reads them again", 0x3333, (long long)rb->h[1]);

    // the sort
    { enum { N = 3000 }; static us_rng v[N], q[N]; uint64_t x = 88172645463325252ull;
      for (int i = 0; i < N; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; uint64_t lo = 0x900000000ull + (x % 0x40000000ull & ~15ull); v[i] = q[i] = (us_rng){lo, lo + 16}; }
      us_rng_sort(v, N); qsort(q, N, sizeof *q, us_rng_cmp); int same = 1; for (int i = 0; i < N; i++) same &= v[i].lo == q[i].lo;
      check("the radix sort orders 3,000 ranges as qsort", 1, same); }

    // the merge
    { us_rng u[] = {{A(0, 0x1000), A(0, 0x1100)}, {A(0, 0x1200), A(0, 0x1240)}, {A(0, 0x1341), A(0, 0x1380)}, {A(0, 0x1360), A(0, 0x13c0)}};
      int k = us_merge(u, 4);
      check("the merge: 256 bytes apart joined, 257 apart not, an overlap joined: 2 ranges", 2, k);
      check("... the first 0x1000-0x1240, the second 0x1341-0x13c0", 1, k == 2 && u[0].lo == A(0, 0x1000) && u[0].hi == A(0, 0x1240) && u[1].lo == A(0, 0x1341) && u[1].hi == A(0, 0x13c0)); }
    { us_rng u[] = {{A(0, POOL - 64), A(0, POOL)}, {A(0, POOL), A(0, POOL) + 64}};
      check("a range continuing past its pool's end is not joined to it", 2, us_merge(u, 2)); }
    { us_rng u[] = {{A(0, 0), A(0, 0x80000)}, {A(0, 0x80000), A(0, 0x100000)}, {A(0, 0x100000 - 16), A(0, 0x100000)}};
      int k = us_merge(u, 3); check("two halves of 1 MiB joined (at most 1 MiB), a range inside them dropped", 1, k); }
    { us_rng u[] = {{A(0, 0x100), A(0, 0x200)}, {A(0, 0x180), A(0, 0x280)}};
      us_rng w[] = {{A(0, 0), A(0, POOL)}, {A(0, 0x10), A(0, POOL) + 0x100}};
      int k1 = us_merge(u, 2), k2 = us_merge(w, 2);
      check("an overlap in one pool joins; past a full 1 MiB copy only the part beyond it is kept", 1, k1 == 1 && k2 == 2 && w[1].lo == A(0, POOL)); }

    // the snapshot
    lists_reset(); ubo(0, A(0, 0x1010), 64); ubo(0, A(0, 0x1100), 64); ubo(0, A(0, 0x4020), 32); ubo(0, (uint64_t)(uintptr_t)src, 16); rec(0, OP_END, NULL, 0);
    uint64_t s0 = g_us.skip_gpu; submit();
    check("the snapshot: 0x1010-0x1050 and 0x1100-0x1140 joined, 0x4020-0x4040 apart, the range in no pool left: 2 copies", 2, g_us.n);
    { int al = 1, eq = 1; for (int i = 0; i < g_us.n; i++) { al &= (g_us.ent[i].gpu & 255) == (g_us.ent[i].lo & 255); eq &= !memcmp(g_us.ent[i].cpu, (const void *)(uintptr_t)g_us.ent[i].lo, g_us.ent[i].hi - g_us.ent[i].lo); }
      check("... each at its source's offset modulo 256, with its bytes", 1, al && eq); }
    g_pool[0][0x1020] ^= 0xff;
    { const us_ent *c = us_find(A(0, 0x1020), 4); check("... a write to the source after the snapshot does not reach the copy", 1, c && c->cpu[0x1020 - 0x1010] == (uint8_t)0x20); }
    g_pool[0][0x1020] ^= 0xff;
    check("a binding inside a copy finds it", 1, us_find(A(0, 0x1110), 16) == &g_us.ent[0]);
    check("a binding across the two copies finds none", 0, us_find(A(0, 0x1130), 0x3000) != NULL);
    check("alternating lookups each find their own copy", 1, us_find(A(0, 0x4020), 8) == &g_us.ent[1] && us_find(A(0, 0x1010), 8) == &g_us.ent[0] && us_find(A(0, 0x4028), 8) == &g_us.ent[1]);
    check("... and a range next to the last copy found, in neither, none", 0, us_find(A(0, 0x4040), 8) != NULL);
    check("nothing skipped (no GPU write)", 0, g_us.skip_gpu - s0);
    check("a GPU write over the second copy drops it", 1, us_drop_range(A(0, 0x4030), 4, 101));
    check("... the first is kept, the second gone", 1, us_find(A(0, 0x1010), 8) && !us_find(A(0, 0x4020), 8));
    check("... a second write there drops nothing new", 0, us_drop_range(A(0, 0x4030), 4, 101));
    check("... noted once for the next submissions", 1, g_us.gwn);
    g_done = 100; s0 = g_us.skip_gpu; submit();
    check("the next submission, the write's submission still running: its range not copied, counted", 1, g_us.n == 1 && !us_find(A(0, 0x4020), 8) && g_us.skip_gpu - s0 == 1);
    g_done = 101; submit();
    check("... once it completed: copied again, and the write forgotten", 1, g_us.n == 2 && us_find(A(0, 0x4020), 8) && g_us.gwn == 0);

    // an earlier submission's renamed updates written, still running (plan item 301e3)
    lists_reset(); ubo(0, A(0, 0x8000), 64); ubo(0, A(0, 0x9000), 64); rec(0, OP_END, NULL, 0);
    static uint8_t upd1[16], upd2[8]; memset(upd1, 0xa5, sizeof upd1); memset(upd2, 0x3c, sizeof upd2);
    g_us.gwn = 0; memset(&g_pend, 0, sizeof g_pend); g_done = 200;
    pend_add(A(0, 0x8010), 16, upd1, 0, 201); us_gw_note(A(0, 0x8010), 16, 201, 1);   // as the backends' upd_flush note them
    us_drop_range(A(0, 0x9020), 8, 201);   // a storage buffer the shader may write
    s0 = g_us.skip_gpu; uint64_t l0 = g_us.laid; submit();
    { const us_ent *c = us_find(A(0, 0x8000), 64);
      check("a range a running submission's renamed updates were written to is copied (counted); one another write of it reaches is not", 1,
            c && !us_find(A(0, 0x9000), 64) && g_us.laid - l0 == 1 && g_us.skip_gpu - s0 == 1);
      check("... the copy: memory with the updates' bytes laid over", 1, c && !memcmp(c->cpu + 0x10, upd1, 16) && !memcmp(c->cpu, g_pool[0] + 0x8000, 16) && !memcmp(c->cpu + 0x20, g_pool[0] + 0x8020, 32)); }
    memcpy(g_pool[0] + 0x8010, upd1, 16); g_pool[0][0x8010] = 0x77;   // the GPU wrote them, then the game one byte
    pend_add(A(0, 0x8030), 8, upd2, 0, 202); us_gw_note(A(0, 0x8030), 8, 202, 1);
    g_done = 201; submit();
    { const us_ent *c = us_find(A(0, 0x8000), 64);
      check("... the next, that one done and another running: the game's newer byte kept, the running one's bytes laid", 1,
            c && c->cpu[0x10] == 0x77 && c->cpu[0x11] == 0xa5 && !memcmp(c->cpu + 0x30, upd2, 8)); }
    g_pend.over = 1; s0 = g_us.skip_gpu; submit();
    check("... the pending-write table overflowed: not copied", 1, !us_find(A(0, 0x8000), 64) && g_us.skip_gpu - s0 == 1);
    g_us.gwn = 0; us_gw_note(A(0, 0x9020), 8, 203, 1); us_drop_range(A(0, 0x9020), 8, 203);
    check("a range noted as renamed updates and then written otherwise in the same submission: noted once, not known", 1, g_us.gwn == 1 && !g_us.gw[0].known);
    g_us.gwn = 0; memset(&g_pend, 0, sizeof g_pend); for (int i = 0x8010; i < 0x8020; i++) g_pool[0][i] = (uint8_t)i;

    // faults
    { uint8_t *bad = g_pool[1] + 0x10000; mprotect(bad, 4096, PROT_NONE);
      lists_reset(); ubo(0, A(1, 0x100), 256); ubo(0, A(1, 0x10000), 256); ubo(0, A(1, 0x20000), 256); rec(0, OP_END, NULL, 0);
      uint64_t f0 = g_us.faults, j0 = g_us.jumps; submit();
      check("a source made unreadable: one fault caught", 1, (long long)(g_us.faults - f0));
      check("... its copy dropped, the one before and the one after it kept", 1, !us_find(A(1, 0x10000), 8) && us_find(A(1, 0x100), 8) && us_find(A(1, 0x20000), 8));
      check("... one guard for the run and one after the fault", 2, (long long)(g_us.jumps - j0));
      lists_reset(); for (int i = 0; i < 6; i++) ubo(0, A(1, 0x40000 + (uint64_t)i * 0x20000), 0x10000); ubo(0, A(1, 0x10000), 256); rec(0, OP_END, NULL, 0);
      int p0 = g_par_calls; uint64_t pn = g_us.par_n; f0 = g_us.faults; submit();
      check("over 256 KiB: copied in parts through the workers", 1, g_par_calls - p0 == 1 && g_us.par_n - pn == 1);
      check("... the fault caught there too, the other six copies made", 1, g_us.faults - f0 == 1 && g_us.n == 7 && !us_find(A(1, 0x10000), 8) && us_find(A(1, 0xe0000), 8));
      us_rng r[3] = {{A(1, 0x100), A(1, 0x140)}, {A(1, 0x10000), A(1, 0x10040)}, {A(1, 0x200), A(1, 0x240)}}; uint64_t off[3] = {0, 64, 128}; uint8_t data[192];
      f0 = g_us.faults; us_cap_copy(3, r, off, data);
      check("the capture's copies: the unreadable one marked, the others copied", 1, g_us.faults - f0 == 1 && off[1] == UINT64_MAX && !memcmp(data, g_pool[1] + 0x100, 64) && !memcmp(data + 128, g_pool[1] + 0x200, 64));
      mprotect(bad, 4096, PROT_READ | PROT_WRITE); }

    // off
    g_us.on = 0; g_bh_seen = 1; lists_reset(); ubo(0, A(0, 0x100), 64); src[1] = 0x4444; rec(0, OP_BIND_TEX, &bh, sizeof bh); rec(0, OP_END, NULL, 0);
    submit();
    rb = NULL; for (size_t o = 0; o < g_len[0];) { op_hdr h; memcpy(&h, g_mem[0] + o, 8); if (h.op == OP_BIND_TEX) rb = (const p_bind_handles *)(g_mem[0] + o + 8); o += h.size; }
    check("snapshots off: nothing copied, yet deferred handles are read", 1, g_us.n == 0 && rb && rb->h[1] == 0x4444);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
