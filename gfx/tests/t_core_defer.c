// The deferred merged pass's decisions without a GPU (gfx/nvn/core/defer.c, plan item 264), on synthetic command streams with the store
// lookahead index built over them and the backend's lookups faked (a handle is the texture serial; one program: a fragment shader sampling
// texture slot 0). A pass P that clears A, then a pass on B, then Q attaching A at the same index with C: a group, P's draw kept with the draw
// state, P's clear taken from the pending clears, the exec loop's mark at P's end; P's next draw kept too; at Q the targets and P's clear made Q's
// load action, none on C; the group at Q, then drawn and released. Not made: without a pending clear; when a pass between samples A; when Q
// attaches A at another index; when a copy between writes A; when A is cleared again before Q (the index's screen); with DM_NVN_DEFER=0.
// A Q slot no P colour maps to read as an index into the group's clears (-DDM_TEST_DEFER_LMAP_ANY, the broken variant, as the replay did
// before batch 98): 1 fails. Not made either when P's target has its storage held by another live texture object too (plan item 219;
// -DDM_TEST_SHARED_NOT_SEEN, shared storage not looked for: 1 fails).
#include "../nvn/core/defer.h"
#include "../nvn/core/pass.h"
#include "../nvn/core/store_index.h"
#include "core_test.h"
#include "../../host/dm.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static prog_t g_prog;                       // the one program: a vertex shader, a fragment shader sampling texture slot 0
static nvn_refl g_rf_v, g_rf_f;
int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]) {
    if (p != &g_prog) return 0;
    for (int st = 0; st < 6; st++) { rf[st] = NULL; stage[st] = -1; }
    rf[0] = &g_rf_v; stage[0] = 0; rf[1] = &g_rf_f; stage[1] = 1; return 1;
}
int nvn_be_handle_view(uint64_t h, uint64_t *ser, view_t *v, int *has_v) { if (!h) return 0; *ser = h; memset(v, 0, sizeof *v); *has_v = 0; return 1; }

static uint64_t g_shared;   // the serial of a texture whose storage another live object holds too (0: none); plan item 219
int nvn_be_tex_shared(const tex_t *t) { return g_shared && t->serial == g_shared; }
// ---- command streams ----
static uint8_t g_mem[1 << 14] __attribute__((aligned(8))); static size_t g_len;
static const uint8_t *g_ops[256]; static int g_nops;
static const uint8_t *rec(uint32_t op, const void *pl, size_t sz) {
    uint8_t *p = g_mem + g_len; uint32_t size = (uint32_t)((8 + sz + 7) & ~(size_t)7);
    op_hdr h = {op, size}; memcpy(p, &h, 8); memset(p + 8, 0, size - 8); if (sz) memcpy(p + 8, pl, sz);
    g_len += size; g_ops[g_nops++] = p; return p;
}
static tex_t A, B, C;
static p_set_rt rt2(const tex_t *c0, const tex_t *c1) { p_set_rt r = {0}; r.n = c1 ? 2 : 1; r.color[0] = c0; r.color[1] = c1; return r; }
static const uint8_t *set_rt(const tex_t *c0, const tex_t *c1) { p_set_rt r = rt2(c0, c1); return rec(OP_SET_RT, &r, sizeof r); }
static const uint8_t *draw(void) { p_draw d = {.count = 3}; return rec(OP_DRAW, &d, sizeof d); }
static const uint8_t *clear(int index) { p_clear_color c = {index, 0xf, {0.25f, 0.5f, 0.75f, 1}}; return rec(OP_CLEAR_COLOR, &c, sizeof c); }
static const uint8_t *bind_prog(void) { p_bind_program b = {&g_prog, 0x1f}; return rec(OP_BIND_PROGRAM, &b, sizeof b); }
static const uint8_t *bind_tex(const tex_t *t) { p_bind_handles b = {0}; b.stage = 1; b.n = 1; b.h[0] = t ? t->serial : 0; return rec(OP_BIND_TEX, &b, sizeof b); }
static const uint8_t *copy(const tex_t *s, const tex_t *d) { p_copy_t2t c = {0}; c.src = s; c.dst = d; return rec(OP_COPY_T2T, &c, sizeof c); }
static const uint8_t *end(void) { return rec(OP_END, NULL, 0); }
// P's start: its targets A, its clear recorded and pending, the program bound, then P's first draw (returned)
static const uint8_t *p_start(void) {
    g_len = 0; g_nops = 0; set_rt(&A, NULL); clear(0); bind_prog(); return draw();
}
// The replay at P's first draw: the index built over the stream, replayed up to the draw; the draw state and R as the replay has them
static void at(const uint8_t *d, int pending_clear) {
    sdx_start st0 = {0}; uint64_t h0 = (uint64_t)(uintptr_t)g_mem; sdx_begin(1); sdx_build(&st0, &h0, 1);
    for (int i = 0; i < g_nops; i++) { sdx_seq(g_ops[i]); if (g_ops[i] == d) break; }
    defer_reset(); st_reset(); S.ds.dt = S.ds.dw = S.ds.st = 0; S.prog = &g_prog;
    R = (nvn_rt_state){0}; R.rt = rt2(&A, NULL); R.has_rt = 1; R.clear_c[0] = (uint8_t)pending_clear; R.cc[0] = nvn_rgba_make(0.25, 0.5, 0.75, 1);
    g_exec_cur = d; op_hdr h; memcpy(&h, d, 8); g_exec_next = d + h.size;
}
static int made(const uint8_t *d, int pending_clear) { at(d, pending_clear); p_draw dw = {.count = 3}; defer_try(&dw, NULL); g_exec_next = NULL; return g_defer_nact; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    tex_t *t[3] = {&A, &B, &C};
    for (int i = 0; i < 3; i++) { t[i]->d.target = 1; t[i]->d.width = t[i]->d.height = 64; t[i]->d.levels = 1; t[i]->serial = 301 + (uint64_t)i; }
    g_rf_f.nb = 1; g_rf_f.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0};

    // ---- a group made, kept, drawn at Q ----
    const uint8_t *d = p_start(); const uint8_t *d2 = draw(), *pe = set_rt(&B, NULL); draw(); const uint8_t *q = set_rt(&A, &C); draw(); end();
    at(d, 1); p_draw dw = {.count = 3, .first = 7}; defer_try(&dw, NULL); g_exec_next = NULL;
    defer_t *g = g_defer_cur; int slot = (int)(g - g_defer_slots);
    check("P clears A, a pass on B, then Q attaches A at index 0 with C: a group in its P range", 1, g_defer_nact == 1 && g->active && g->in_p && !g->at_q && g->q_op == q && g->p_end_op == pe);
    check("... P's draw kept with the draw state, P's clear taken from the pending clears", 1, g->n == 1 && g->snap[0].dw.first == 7 && g->snap[0].S_ && g->clear_c[0] && !R.clear_c[0] && g->cc[0].green == 0.5);
    check("... the exec loop's mark at P's end", 1, g_defer_pend == pe && defer_has_in_p());
    (void)d2; check("P's next draw kept too", 1, defer_take(&dw, NULL) && g->n == 2);
    g->in_p = 0; g_defer_pend = NULL;   // the exec loop reaching P's end
    check("Q's SetRenderTargets opens the waiting group's Q; another op does not", 1, defer_q_first(q) == slot && defer_q_first(pe) == -1);
    for (int k = 1; k < 8; k++) memset(&g->cc[k], 0xab, sizeof g->cc[k]);   // what other clear colours the group holds must not reach a slot P does not cover
    defer_q_enter(q, slot);
    check("... Q's targets, P's clear made the load action of index 0", 1, R.has_rt && R.rt.n == 2 && R.rt.color[1] == &C && R.clear_c[0] && R.cc[0].blue == 0.75);
    check("... none on C, which no P colour maps to [-DDM_TEST_DEFER_LMAP_ANY]", 0, R.clear_c[1]);
    check("... the group at Q, awaiting its draws", 1, g->at_q && g_defer_natq == 1 && defer_next(1, 0) == slot);
    defer_release_one(g); check("drawn and released: no group", 0, g_defer_nact + g_defer_natq + defer_has_active());

    // ---- not made ----
    d = p_start(); set_rt(&B, NULL); draw(); set_rt(&A, &C); draw(); end();
    check("without a pending clear: none", 0, made(d, 0));
    d = p_start(); set_rt(&B, NULL); bind_tex(&A); draw(); set_rt(&A, &C); draw(); end();
    check("a pass between samples A: none", 0, made(d, 1));
    d = p_start(); set_rt(&B, NULL); draw(); set_rt(&C, &A); draw(); end();
    check("Q attaches A at another index: none", 0, made(d, 1));
    d = p_start(); set_rt(&B, NULL); draw(); copy(&B, &A); set_rt(&A, &C); draw(); end();
    check("a copy between writes A: none", 0, made(d, 1));
    d = p_start(); set_rt(&B, NULL); draw(); set_rt(&A, &C); clear(0); draw(); end();
    check("A cleared again at Q (the index's screen): none", 0, made(d, 1));
    d = p_start(); set_rt(&B, NULL); draw(); set_rt(&A, &C); draw(); end();
    setenv("DM_NVN_DEFER", "0", 1); check("DM_NVN_DEFER=0: none", 0, made(d, 1)); unsetenv("DM_NVN_DEFER");
    check("... and with it unset again the same stream makes one", 1, made(d, 1));
    defer_reset(); g_shared = A.serial;
    check("A's storage another object's too: none [-DDM_TEST_SHARED_NOT_SEEN]", 0, made(d, 1)); g_shared = 0;
    check("... and one again without it", 1, made(d, 1));
    defer_reset();
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
