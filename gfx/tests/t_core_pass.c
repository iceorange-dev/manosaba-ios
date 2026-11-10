// Pass planning without a GPU (gfx/nvn/core/pass.c, plan item 264), on synthetic command streams with the backend's lookups faked as in
// t_core_store_index.c. The replay cursor: through calls back to the caller, jumps, then the submission's remaining handles; ops run early
// skipped, and taken when the replay reaches them; the next segment after a list's end. The clear-only hoist: groups of new targets and full
// clears between this pass's draws and its targets set again run before it (their ops skipped where they stand), not when a cleared texture is
// on a slot, a clear is under a partial scissor, a copy comes between or a group draws. A pass's depth / stencil need: none
// without a depth target or a start point (3); the pending clears and the opening draw's state; later draws by their depth-stencil state, up to
// new targets; through the caller and the remaining handles; a clear inside the pass needs both. The store decision at a pass end: an attachment
// cleared in full before it is drawn again not stored, one a later draw loads stored, depth and stencil apart; all stored with the store part
// off or no op being replayed; one loaded and not written not stored (udc), with the TBDR rewrites on or off (plan item 315a), one written
// stored, the rule off by DM_NVN_UNWRITTEN_DC=0; the bandwidth ledger. With the rewrites only, as before (-DDM_TEST_UDC_TBDR_ONLY): 1 fails.
// A clear inside the pass taken as needing neither (-DDM_TEST_DS_CLEAR_FREE, a broken variant): 1 fails. A cleared texture on a slot hoisted all
// the same (-DDM_TEST_HOIST_BOUND_OK, the other): 1 fails. Store records for 9 attachments, the old size (-DDM_TEST_SD_MAX=9): 1 fails.
// A texture whose storage another live object holds too (plan item 219): written back though the index found it cleared before it is read
// (why 7), its clear-only pass not hoisted. Shared storage not looked for, as before (-DDM_TEST_SHARED_NOT_SEEN): 3 fail.
// An attachment whose texture has no memory (plan item 280): never stored, with the rewrites on or off, whatever the lookahead finds; decided
// like any other (-DDM_TEST_NOMEM_STORED): 2 fail.
#include "../nvn/core/pass.h"
#include "../nvn/core/state.h"
#include "../nvn/core/store_index.h"
#include "../nvn/core/tbdr.h"
#include "core_test.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static uint64_t g_shared;   // the serial of a texture whose storage another live object holds too (0: none); plan item 219
int nvn_be_tex_shared(const tex_t *t) { return g_shared && t->serial == g_shared; }
// ---- command streams ----
static uint8_t g_mem[4][1 << 14] __attribute__((aligned(8))); static size_t g_len[4];
static const uint8_t *g_ops[512]; static int g_nops;   // the replay order of buffer 0 (for sdx_seq)
static int g_cur;
static const uint8_t *rec(uint32_t op, const void *pl, size_t sz) {
    uint8_t *p = g_mem[g_cur] + g_len[g_cur]; uint32_t size = (uint32_t)((8 + sz + 7) & ~(size_t)7);
    op_hdr h = {op, size}; memcpy(p, &h, 8); memset(p + 8, 0, size - 8); if (sz) memcpy(p + 8, pl, sz);
    g_len[g_cur] += size; if (g_cur == 0) g_ops[g_nops++] = p;
    return p;
}
static tex_t A, B, D;   // 64x64 colour textures A B, depth-stencil D
static void reset(void) {
    memset(g_len, 0, sizeof g_len); g_nops = 0; g_cur = 0;
    tex_t *t[3] = {&A, &B, &D};
    for (int i = 0; i < 3; i++) { memset(t[i], 0, sizeof *t[i]); t[i]->d.target = 1; t[i]->d.width = t[i]->d.height = 64; t[i]->d.levels = 4; t[i]->serial = 101 + (uint64_t)i; }
}
static p_set_rt rt_of(const tex_t *c0, const tex_t *ds) { p_set_rt r = {0}; r.n = c0 ? 1 : 0; r.color[0] = c0; r.depth = ds; return r; }
static const uint8_t *set_rt(const tex_t *c0, const tex_t *ds) { p_set_rt r = rt_of(c0, ds); return rec(OP_SET_RT, &r, sizeof r); }
static const uint8_t *draw(void) { p_draw d = {.count = 3}; return rec(OP_DRAW, &d, sizeof d); }
static const uint8_t *clear(int index) { p_clear_color c = {index, 0xf, {0, 0, 0, 0}}; return rec(OP_CLEAR_COLOR, &c, sizeof c); }
static const uint8_t *clear_ds(int dm, int sm) { p_clear_ds c = {1.0f, dm, 0, sm}; return rec(OP_CLEAR_DS, &c, sizeof c); }
static const uint8_t *ds_state(int dt, int dw, int st) { p_bind_state b = {.kind = 5}; ds_st d = S.ds; d.dt = (uint8_t)dt; d.dw = (uint8_t)dw; d.st = (uint8_t)st; memcpy(b.bytes, &d, sizeof d); return rec(OP_BIND_STATE, &b, sizeof b); }
static const uint8_t *call(const void *sub) { uint64_t a = (uint64_t)(uintptr_t)sub; return rec(OP_CALL, &a, 8); }
static const uint8_t *jump(const void *to) { uint64_t a = (uint64_t)(uintptr_t)to; return rec(OP_JUMP, &a, 8); }
static const uint8_t *end(void) { return rec(OP_END, NULL, 0); }
static const uint8_t *scissor(int w, int h) { p_scissor s = {0, 0, w, h}; return rec(OP_SCISSOR, &s, sizeof s); }
static const uint8_t *copy(const tex_t *s, const tex_t *d) { p_copy_t2t c = {0}; c.src = s; c.dst = d; return rec(OP_COPY_T2T, &c, sizeof c); }
static int hoist_at(const uint8_t *draw_op, hoist_grp G[4]) { op_hdr h; memcpy(&h, draw_op, 8); g_exec_next = draw_op + h.size; int r = hoist_plan(G); g_exec_next = NULL; return r; }
static void ds_off(void) { S.ds.dt = S.ds.dw = S.ds.st = 0; }
static int need_at(const uint8_t *draw_op) { op_hdr h; memcpy(&h, draw_op, 8); g_exec_next = draw_op + h.size; int r = pass_ds_need(); g_exec_next = NULL; return r; }
static void att(int k, const tex_t *t, int depth, int idx, int load, int wrote) { g_sd[k] = (sd_att){0}; g_sd[k].ser = t->serial; g_sd[k].bytes = 1000 * (k + 1); g_sd[k].idx = idx; g_sd[k].depth = (uint8_t)depth; g_sd[k].load = (uint8_t)load; g_sd[k].wrote = (uint8_t)wrote; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setenv("DM_NVN_XV", "0", 1);   // the validity ledger is t_core_tbdr's
    st_reset(); reset();

    // ---- the replay cursor and the ops run early ----
    reset(); g_cur = 1; const uint8_t *c = draw(); end();
    g_cur = 2; const uint8_t *jd = draw(); end();
    g_cur = 3; const uint8_t *dd = draw(); end();
    g_cur = 0; const uint8_t *a0 = draw(); call(g_mem[1]); const uint8_t *b0 = draw(); jump(jd); end();
    uint64_t rest[1] = {(uint64_t)(uintptr_t)g_mem[3]}; nvn_exec_rest(rest, 1);
    const uint8_t *seen[8]; int ns = 0; op_cursor cur = {.p = a0}; const uint8_t *q;
    while (ns < 8 && op_next(&cur, &q, NULL, NULL)) seen[ns++] = q;
    check("the cursor: the call, back to the caller, the jump, the remaining handle, then the end", 1, ns == 5 && seen[0] == a0 && seen[1] == c && seen[2] == b0 && seen[3] == jd && seen[4] == dd);
    g_hoist_skip[0] = c; g_hoist_n = 1; ns = 0; cur = (op_cursor){.p = a0};
    while (ns < 8 && op_next(&cur, &q, NULL, NULL)) seen[ns++] = q;
    check("... an op run early skipped", 1, ns == 4 && seen[1] == b0);
    check("the replay reaching it takes it once", 1, hoist_take(c) && g_hoist_n == 0 && !hoist_take(c));
    g_ret[0] = b0; g_ret[1] = c; g_ret_n = 2; int ci = 0; const uint8_t *n0 = exec_cont(&ci), *n1 = exec_cont(&ci), *n2 = exec_cont(&ci), *n3 = exec_cont(&ci);
    check("after a list's end: the callers innermost first, then the remaining handles", 1, n0 == c && n1 == b0 && n2 == g_mem[3] && !n3);
    g_ret_n = 0; nvn_exec_rest(NULL, 0);

    // ---- the clear-only hoist ----
    hoist_grp G[4]; reset(); R = (nvn_rt_state){0}; R.rt = rt_of(&A, NULL); R.has_rt = 1;
    const uint8_t *f = draw(); draw(); const uint8_t *g0 = set_rt(&B, NULL), *g1 = clear(0); set_rt(&A, NULL); draw(); end();
    int ng = hoist_at(f, G);
    check("draws, new targets cleared in full, this pass's targets again: one group run early, its two ops skipped", 1, ng == 1 && G[0].rt_op == g0 && G[0].nops == 1 && G[0].ops[0] == g1 && G[0].clear_c[0] && g_hoist_n == 2);
    check("... not planned again while they are pending", 0, hoist_at(f, G)); g_hoist_n = 0;
    S.tex[1][0] = B.serial; check("the cleared texture on a slot: none [-DDM_TEST_HOIST_BOUND_OK]", 0, hoist_at(f, G)); S.tex[1][0] = 0; g_hoist_n = 0;
    g_shared = B.serial; check("the cleared texture's storage another object's too: none [-DDM_TEST_SHARED_NOT_SEEN]", 0, hoist_at(f, G)); g_shared = 0; g_hoist_n = 0;
    check("... and planned again without it", 1, hoist_at(f, G)); g_hoist_n = 0;
    reset(); f = draw(); set_rt(&B, NULL); scissor(32, 32); clear(0); set_rt(&A, NULL); draw(); end();
    check("a clear under a partial scissor: none", 0, hoist_at(f, G));
    reset(); f = draw(); copy(&D, &B); set_rt(&B, NULL); clear(0); set_rt(&A, NULL); end();
    check("a copy between: none", 0, hoist_at(f, G));
    reset(); f = draw(); set_rt(&B, NULL); clear(0); draw(); set_rt(&A, NULL); end();
    check("a group that draws: none", 0, hoist_at(f, G)); g_hoist_n = 0;

    // ---- a pass's depth / stencil need ----
    reset(); ds_off(); R = (nvn_rt_state){0}; R.rt = rt_of(&A, NULL); R.has_rt = 1;
    f = draw(); ds_state(1, 0, 0); draw(); end();
    check("no depth target: 3", 3, need_at(f));
    R.rt.depth = &D; check("no start point: 3", 3, pass_ds_need());
    reset(); ds_off(); f = draw(); ds_state(0, 0, 1); draw(); set_rt(&A, &D); ds_state(1, 1, 0); draw(); end();
    check("a later draw tests stencil, new targets after it: stencil (2)", 2, need_at(f));
    R.clear_d = 1; check("... a depth clear pending at the opening: both (3)", 3, need_at(f)); R.clear_d = 0;
    reset(); ds_off(); f = draw(); draw(); end(); S.ds.dw = 1;
    check("the opening draw writes depth, nothing after it: depth (1)", 1, need_at(f)); ds_off();
    reset(); ds_off(); f = draw(); clear_ds(1, 0); draw(); end();
    check("a clear inside the pass: both (3) [-DDM_TEST_DS_CLEAR_FREE]", 3, need_at(f));
    reset(); g_cur = 1; ds_state(1, 0, 0); draw(); end(); g_cur = 0; f = draw(); end();
    rest[0] = (uint64_t)(uintptr_t)g_mem[1]; nvn_exec_rest(rest, 1); ds_off();
    check("a remaining handle's draw tests depth: depth (1)", 1, need_at(f));
    reset(); g_cur = 1; f = draw(); end(); g_cur = 0; draw(); end(); g_cur = 2; ds_state(0, 0, 1); draw(); end();
    g_ret[0] = g_mem[0]; g_ret_n = 1; rest[0] = (uint64_t)(uintptr_t)g_mem[2]; ds_off();
    check("a draw in a called list: the caller, then the remaining handle (stencil, 2)", 2, need_at(f));
    g_ret_n = 0; nvn_exec_rest(NULL, 0);

    // ---- the store decision at a pass end ----
    // pass 1 on A + D ends at the second SetRenderTargets; pass 2 clears A's colour and D's depth in full, then draws (D's stencil loaded)
    reset(); set_rt(&A, &D); draw(); const uint8_t *e = set_rt(&A, &D); clear(0); clear_ds(1, 0); draw(); end();
    sdx_start st0 = {0}; uint64_t h0 = (uint64_t)(uintptr_t)g_mem[0]; sdx_begin(1); sdx_build(&st0, &h0, 1);
    for (int i = 0; i < g_nops; i++) { sdx_seq(g_ops[i]); if (g_ops[i] == e) break; }
    g_exec_cur = e; R.rt = rt_of(&A, &D);
    att(0, &A, 0, 0, 1, 1); att(1, &D, 1, -1, 1, 1); att(2, &D, 2, -1, 1, 1); g_nsd = 3;
    g_bw_store = 6000; uint8_t store[SD_MAX]; sd_decide(store);
    check("cleared in full before drawn again: colour and depth not stored, the stencil (loaded) stored", 1, !store[0] && !store[1] && store[2]);
    check("... why: the stencil drawn to before cleared (2)", 2, g_sd[2].why);
    check("... the ledger: 3 decided, 2 not stored, 3000 bytes off the stores", 1, g_sd_n_all == 3 && g_sd_n_dead == 2 && g_sd_dead_bytes == 3000 && g_bw_store == 3000);
    {   // 219: A's storage held by another live object too: a read through that one is under another serial
        att(0, &A, 0, 0, 1, 1); att(1, &D, 1, -1, 1, 1); g_nsd = 2; g_shared = A.serial; uint64_t sh0 = g_sd_n_shared; sd_decide(store); g_shared = 0;
        check("A's storage another object's too: A written back (7), D's depth still not [-DDM_TEST_SHARED_NOT_SEEN]", 1, store[0] && g_sd[0].why == 7 && !store[1] && g_sd_n_shared == sh0 + 1);
        check("... what the lookahead left unstored is marked lost, what it stored is not", 1, !g_sd[0].lost && g_sd[1].lost);
    }
    {   // 280: D's texture has no memory (a depth target made so): its planes are not stored though a later draw loads the stencil
        att(0, &A, 0, 0, 1, 1); att(1, &D, 1, -1, 0, 1); att(2, &D, 2, -1, 0, 1); g_sd[1].nomem = g_sd[2].nomem = 1; g_nsd = 3; uint64_t b0 = (g_bw_store = 6000); sd_decide(store);
        check("a target without memory: its depth and stencil not stored, off the stores' bytes [-DDM_TEST_NOMEM_STORED]", 1, !store[1] && !store[2] && g_bw_store == b0 - 6000);
        int t0 = g_tbdr; g_tbdr = 0; att(0, &A, 0, 0, 0, 1); att(1, &D, 1, -1, 0, 1); g_sd[1].nomem = 1; g_nsd = 2; uint8_t s2[SD_MAX] = {9, 9}; sd_decide(s2); g_tbdr = t0;
        check("... with the rewrites off too, the colour stored as before [-DDM_TEST_NOMEM_STORED]", 1, s2[0] == 1 && s2[1] == 0);
    }
    att(0, &A, 0, 0, 1, 1); g_nsd = 1; g_tbdr_off = TBP_STORE;
    store[0] = 9; sd_decide(store); check("the store part off: stored", 1, store[0]);
    g_tbdr_off = -1; att(0, &A, 0, 0, 1, 1); g_exec_cur = NULL; sd_decide(store);
    check("a pass ending with no op being replayed: stored (6)", 1, store[0] && g_sd[0].why == 6);
    att(0, &A, 0, 0, 1, 0); uint64_t sv = g_udc_saved_bytes; sd_decide(store);
    check("loaded and not written: not stored, counted as saved", 1, !store[0] && g_udc_saved_bytes == sv + 1000);
    {   // 315a: the TBDR rewrites off (Tears of the Kingdom's default)
        int t0 = g_tbdr; g_tbdr = 0; att(0, &A, 0, 0, 1, 0); att(1, &A, 0, 1, 1, 1); att(2, &D, 1, -1, 0, 0); g_nsd = 3;
        uint64_t s0 = g_udc_saved_bytes; uint8_t st[SD_MAX] = {0}; sd_decide(st);
        check("rewrites off: loaded and not written not stored, written stored, not loaded stored, 1000 bytes saved [-DDM_TEST_UDC_TBDR_ONLY]", 1, !st[0] && st[1] && st[2] && g_udc_saved_bytes == s0 + 1000);
        int u0 = g_udc_ab; g_udc_ab = 0; att(0, &A, 0, 0, 1, 0); g_nsd = 1; sd_decide(st);
        check("... DM_NVN_UNWRITTEN_DC=0: stored", 1, st[0] == 1);
        g_udc_ab = u0; g_tbdr = t0;
    }
    // a full pass: 8 colours, the depth and the stencil are 10 records (the old 9 overflowed g_sd and the store arrays) [-DDM_TEST_SD_MAX=9]
    check("the store records hold 8 colours, the depth and the stencil", 1, (int)(sizeof g_sd / sizeof g_sd[0]) >= 10);
    if ((int)(sizeof g_sd / sizeof g_sd[0]) >= 10) {   // (not run in the broken variant: it would write past the arrays)
        for (int k = 0; k < 8; k++) att(k, &A, 0, k, 1, 1);
        att(8, &D, 1, -1, 1, 1); att(9, &D, 2, -1, 1, 1); g_nsd = 10; g_exec_cur = NULL;
        uint8_t full[SD_MAX + 1]; memset(full, 7, sizeof full); sd_decide(full);
        int all = 1; for (int k = 0; k < 10; k++) all &= full[k] == 1 && g_sd[k].why == 6;
        check("... with no op being replayed all 10 stored (6), the stencil last, nothing written past them", 1, all && full[SD_MAX] == 7 && g_sd[9].depth == 2);
    }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
