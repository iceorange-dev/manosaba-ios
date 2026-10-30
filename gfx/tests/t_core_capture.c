// The submit call's view without a GPU (gfx/nvn/core/capture.c, plan item 264), with the backend faked as in t_core_usnap.c. At the call: a
// deferred binding record's handles are read, a record that is not deferred is left; a FenceSync record's sync gets the submission's token; the
// bound uniform ranges' bytes are taken (merged), a range in no pool is left to be read live, nothing is taken with snapshots off. At the replay:
// the handles go into the records with the submission's generation, the snapshot holds the bytes of the call (not what the game wrote after), and
// a range a GPU write reaches that had not completed at the call is not copied even when it has completed since; without a capture the snapshot
// reads memory; the test hook sees the bytes at a watched address. Lists reached through calls (plan item 226): gone through breadth first (the
// submitted list's records, then those of the lists it calls, then theirs), a list called twice twice, to call depth 32 and not 33.
// Only the writes still running at the replay counting (-DDM_TEST_XLAT_CAP_STALE, t_xlat's broken variant): 1 fails. A called list walked as its
// call is met (-DDM_TEST_CAP_DEPTH_FIRST): 1 fails.
#include "../nvn/core/capture.h"
#include "core_test.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void crash_install(void);

enum { POOL = 1 << 16 };
static uint8_t *g_pool;
int nvn_be_pool_span(uint64_t lo, uint64_t len) { uint64_t a = (uint64_t)(uintptr_t)g_pool; return lo >= a && lo + len <= a + POOL; }
static uint8_t g_arena[1 << 20]; static size_t g_arena_off;
void nvn_be_arena(size_t len, uint8_t **cpu, uint64_t *gpu, void **buf) { g_arena_off = (g_arena_off + 255) & ~(size_t)255; if (g_arena_off + len > sizeof g_arena) g_arena_off = 0; *cpu = g_arena + g_arena_off; *gpu = (uint64_t)(uintptr_t)*cpu; *buf = g_arena; g_arena_off += len; }
static uint64_t g_done;
uint64_t nvn_be_done_fence(void) { return g_done; }

static uint8_t g_mem[8192] __attribute__((aligned(8))); static size_t g_len;
static uint8_t *rec(uint32_t op, const void *pl, size_t sz) {
    uint8_t *p = g_mem + g_len; uint32_t size = (uint32_t)((8 + sz + 7) & ~(size_t)7);
    op_hdr h = {op, size}; memcpy(p, &h, 8); memset(p + 8, 0, size - 8); if (sz) memcpy(p + 8, pl, sz); g_len += size; return p + 8;
}
static uint64_t A(uint64_t off) { return (uint64_t)(uintptr_t)g_pool + off; }
static void ubo(uint64_t addr, uint64_t size) { p_bind_buf u = {1, 0, addr, size}; rec(OP_BIND_UBO, &u, sizeof u); }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); crash_install();
    if (posix_memalign((void **)&g_pool, 4096, POOL)) abort();
    for (int i = 0; i < POOL; i++) g_pool[i] = (uint8_t)i;
    g_us.on = 1;
    static uint64_t src[2] = {0x1111, 0x2222}; static uint8_t other[64];
    p_bind_handles bh = {0}; bh.stage = 1; bh.n = 2; bh.defer = 1; bh.src = (uint64_t)(uintptr_t)src;
    p_bind_handles *rd = (p_bind_handles *)rec(OP_BIND_TEX, &bh, sizeof bh);
    p_bind_handles nb = {0}; nb.stage = 1; nb.n = 1; nb.h[0] = 0x77; p_bind_handles *rn = (p_bind_handles *)rec(OP_BIND_TEX, &nb, sizeof nb);
    static sync_t sy; sync_t *syp = &sy; rec(OP_FENCE_SYNC, &syp, 8);
    ubo(A(0x100), 64); ubo(A(0x140), 64); ubo(A(0x800), 32); ubo((uint64_t)(uintptr_t)other, 16); rec(OP_END, NULL, 0);
    uint64_t h = (uint64_t)(uintptr_t)g_mem;
    g_done = 4; mtl_cap *c = mtl_sub_capture(&h, 1, 9);
    check("the deferred record's handles read at the call, the other record not taken", 1, c->nbh == 1 && c->bh[0].rec == rd && c->bh[0].h[1] == 0x2222 && rn->h[0] == 0x77);
    uint64_t fv; check("the FenceSync record's sync gets the submission's token", 1, sync_fence_load(&sy, &fv) && fv == (9 | SYNC_TOKEN));
    check("uniform ranges taken: 0x100-0x180 merged, 0x800-0x820, the one in no pool left to be read live", 1, c->nr == 3);
    { int live = 0; for (int i = 0; i < c->nr; i++) live += c->off[i] == UINT64_MAX; check("... one range read live", 1, live); }
    src[1] = 0x3333; memset(g_pool + 0x100, 0xee, 0x80);   // the game goes on writing after the call
    g_bh_gen++; mtl_sub_install(c);
    check("installed: the record holds the call's handles with the submission's generation", 1, rd->h[1] == 0x2222 && rd->defer == g_bh_gen);
    mtl_test_us_watch(A(0x108)); mtl_sub_snapshot(&h, 1);
    uint64_t v; int seen = mtl_test_us_seen(&v); uint64_t want; memcpy(&want, (uint8_t[8]){0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}, 8);
    check("the snapshot holds the bytes of the call, not what the game wrote after", 1, seen == 1 && v == want);
    check("... two copies", 2, g_us.n);
    g_cap_cur = NULL; mtl_cap_free(c);
    // a GPU write of submission 5 reaches 0x800; the call is made at fence 4, the replay runs at fence 6
    g_len = 0; ubo(A(0x800), 32); rec(OP_END, NULL, 0); g_us.gwn = 0; us_gw_note(A(0x800), 4, 5, 0);
    g_done = 4; c = mtl_sub_capture(&h, 1, 10); g_done = 6; mtl_sub_install(c); g_us.n = 0;
    mtl_sub_snapshot(&h, 1);
    check("a range a write not complete at the call reaches: not copied, though complete at the replay [-DDM_TEST_XLAT_CAP_STALE]", 0, us_find(A(0x800), 4) != NULL);
    g_cap_cur = NULL; mtl_cap_free(c);
    g_us.n = 0; mtl_sub_snapshot(&h, 1);
    check("without a capture the snapshot reads memory (the write has completed: copied)", 1, us_find(A(0x800), 4) != NULL);
    g_us.on = 0; c = mtl_sub_capture(&h, 1, 11); check("snapshots off: no range taken at the call", 0, c->nr); mtl_cap_free(c);
    // calls: l0 calls la, lb, then binds, then calls lb again; la calls lc
    static uint64_t dh[1] = {0x5555}; p_bind_handles d = {0}; d.stage = 1; d.n = 1; d.defer = 1; d.src = (uint64_t)(uintptr_t)dh;
    g_len = 0; uint64_t t;
    uint64_t lc = (uint64_t)(uintptr_t)(g_mem + g_len); void *rc = rec(OP_BIND_TEX, &d, sizeof d); rec(OP_END, NULL, 0);
    uint64_t la = (uint64_t)(uintptr_t)(g_mem + g_len); rec(OP_CALL, &lc, 8); void *ra = rec(OP_BIND_TEX, &d, sizeof d); rec(OP_END, NULL, 0);
    uint64_t lb = (uint64_t)(uintptr_t)(g_mem + g_len); void *rb = rec(OP_BIND_TEX, &d, sizeof d); rec(OP_END, NULL, 0);
    h = (uint64_t)(uintptr_t)(g_mem + g_len); rec(OP_CALL, &la, 8); rec(OP_CALL, &lb, 8); void *r0 = rec(OP_BIND_TEX, &d, sizeof d); rec(OP_CALL, &lb, 8); rec(OP_END, NULL, 0);
    c = mtl_sub_capture(&h, 1, 12);
    check("through calls: the list called twice taken twice, the one a called list calls taken", 5, c->nbh);
    { void *want[5] = {r0, ra, rb, rb, rc}; int ok = c->nbh == 5; for (int i = 0; ok && i < 5; i++) ok = c->bh[i].rec == want[i];
      check("breadth first: the submitted list's record, then its called lists' (in call order), then theirs [-DDM_TEST_CAP_DEPTH_FIRST]", 1, ok); }
    mtl_cap_free(c);
    // a chain of calls 33 deep: the list at depth 32 is gone through, the one at 33 is not
    g_len = 0; uint64_t next = (uint64_t)(uintptr_t)(g_mem + g_len); rec(OP_BIND_TEX, &d, sizeof d); rec(OP_END, NULL, 0);
    t = (uint64_t)(uintptr_t)(g_mem + g_len); rec(OP_CALL, &next, 8); void *r32 = rec(OP_BIND_TEX, &d, sizeof d); rec(OP_END, NULL, 0); next = t;
    for (int k = 31; k >= 0; k--) { t = (uint64_t)(uintptr_t)(g_mem + g_len); rec(OP_CALL, &next, 8); rec(OP_END, NULL, 0); next = t; }
    h = next; c = mtl_sub_capture(&h, 1, 13);
    check("call depth: the list at 32 gone through, the one at 33 not", 1, c->nbh == 1 && c->bh[0].rec == r32);
    mtl_cap_free(c);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
