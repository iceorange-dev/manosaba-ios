// The readable-tile pass merge's plan without a GPU (gfx/nvn/core/fuse.c, plan item 264), on synthetic command streams with the store lookahead
// index built over them and the backend's lookups faked (a handle is the texture serial; two programs: P's draws sample nothing, the later
// pass's fragment shader samples texture slot 0 at an interpolated coordinate taken from a vertex attribute, through an output mapping). P draws
// to A, the later pass clears B and samples A at its first draw: planned, the union A + B with B's clear on it, the later pass's SetRenderTargets
// and clear skipped where they stand, the backend's coverage asked. Not planned: when the later pass does not sample A (the index's screen),
// when B is of another size, when the later pass clears an attachment P draws to, when a copy comes between; P's targets set again between are
// taken into the merge unless the last pass opened was a clear-only one.
// Attachments of another size taken into the union (-DDM_TEST_FUSE_ANY_SIZE, the broken variant): 1 fails.
// Not planned either when a target of the union has its storage held by another live texture object too (plan item 219;
// -DDM_TEST_SHARED_NOT_SEEN, shared storage not looked for: 1 fails).
#include "../nvn/core/fuse.h"
#include "../nvn/core/pass.h"
#include "../nvn/core/store_index.h"
#include "core_test.h"
#include "../../host/dm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static prog_t g_p1, g_p2;                  // P's program (samples nothing); the later pass's (samples texture slot 0 at a vertex attribute's coordinate)
static nvn_refl g_rf_v, g_rf_f1, g_rf_f2;
int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]) {
    if (p != &g_p1 && p != &g_p2) return 0;
    for (int st = 0; st < 6; st++) { rf[st] = NULL; stage[st] = -1; }
    rf[0] = &g_rf_v; stage[0] = 0; rf[1] = p == &g_p1 ? &g_rf_f1 : &g_rf_f2; stage[1] = 1; return 1;
}
static int g_cover_calls;
void nvn_be_fuse_cover(const p_set_rt *r2, const uint8_t *first, const int8_t *p2map, const uint8_t *clr, int n1, int n) { (void)r2; (void)first; (void)p2map; (void)clr; (void)n; g_cover_calls++; g_fz.cov2[n1] = 1; }

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
static tex_t A, B, B32;
static p_set_rt rt2(const tex_t *c0, const tex_t *c1) { p_set_rt r = {0}; r.n = c1 ? 2 : 1; r.color[0] = c0; r.color[1] = c1; return r; }
static const uint8_t *set_rt(const tex_t *c0, const tex_t *c1) { p_set_rt r = rt2(c0, c1); return rec(OP_SET_RT, &r, sizeof r); }
static const uint8_t *draw(void) { p_draw d = {.count = 3}; return rec(OP_DRAW, &d, sizeof d); }
static const uint8_t *clear(int index) { p_clear_color c = {index, 0xf, {0, 0, 0, 1}}; return rec(OP_CLEAR_COLOR, &c, sizeof c); }
static const uint8_t *bind_prog(prog_t *p) { p_bind_program b = {p, 0x1f}; return rec(OP_BIND_PROGRAM, &b, sizeof b); }
static const uint8_t *bind_tex(const tex_t *t) { p_bind_handles b = {0}; b.stage = 1; b.n = 1; b.h[0] = t->serial; return rec(OP_BIND_TEX, &b, sizeof b); }
static const uint8_t *copy(const tex_t *s, const tex_t *d) { p_copy_t2t c = {0}; c.src = s; c.dst = d; return rec(OP_COPY_T2T, &c, sizeof c); }
static const uint8_t *end(void) { return rec(OP_END, NULL, 0); }
static const uint8_t *p_start(void) { g_len = 0; g_nops = 0; set_rt(&A, NULL); bind_prog(&g_p1); return draw(); }   // P on A, its opening draw returned
// The replay at P's opening draw: the index built and replayed up to it, the draw state and R as the replay has them; then the plan
static int planned(const uint8_t *d, int enc_clear_only) {
    sdx_start st0 = {0}; uint64_t h0 = (uint64_t)(uintptr_t)g_mem; sdx_begin(1); sdx_build(&st0, &h0, 1);
    for (int i = 0; i < g_nops; i++) { sdx_seq(g_ops[i]); if (g_ops[i] == d) break; }
    st_reset(); S.prog = &g_p1; R = (nvn_rt_state){0}; R.rt = rt2(&A, NULL); R.has_rt = 1;
    memset(&g_fz, 0, sizeof g_fz); g_hoist_n = 0; g_cover_calls = 0;
    g_exec_cur = d; op_hdr h; memcpy(&h, d, 8); g_exec_next = d + h.size;
    fuse_try(enc_clear_only); g_exec_next = NULL;
    return g_fz.active;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    tex_t *t[3] = {&A, &B, &B32};
    for (int i = 0; i < 3; i++) { t[i]->d.target = 1; t[i]->d.width = t[i]->d.height = i == 2 ? 32 : 64; t[i]->d.levels = 1; t[i]->serial = 401 + (uint64_t)i; }
    g_rf_v.uv_attr[0] = 0; for (int l = 1; l < 32; l++) { g_rf_v.uv_attr[l] = -1; g_rf_v.uv_aff_attr[l] = -1; } g_rf_v.uv_aff_attr[0] = -1;
    g_rf_f1.color_mask = 1;
    g_rf_f2.nb = 1; g_rf_f2.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0}; g_rf_f2.nfc = 1; g_rf_f2.fc_reg[0] = 0; g_rf_f2.fc_locn[0] = 0; g_rf_f2.color_mask = 1; g_rf_f2.nfo = 1;

    const uint8_t *d = p_start(); draw(); const uint8_t *r2 = set_rt(&B, NULL), *cl = clear(0); bind_prog(&g_p2); bind_tex(&A); const uint8_t *first = draw(); end();
    (void)first;
    check("P draws to A, the later pass clears B and samples A: planned, phase 1", 1, planned(d, 0) && g_fz.phase == 1 && g_fz.n1 == 1 && g_fz.rt2_op == r2);
    check("... the union A + B, B appended with its clear on it", 1, R.rt.n == 2 && R.rt.color[0] == &A && R.rt.color[1] == &B && g_fz.p2map[0] == 1 && R.clear_c[1] && !R.clear_c[0]);
    check("... the later pass's SetRenderTargets and clear skipped where they stand", 1, g_hoist_n == 2 && hoist_skipped(r2) && hoist_skipped(cl) && g_fz.nskip == 2);
    check("... the backend's coverage asked once", 1, g_cover_calls == 1 && g_fz.cov2[1] == 1);
    check("... the phases' maps: P draws its own slot, B not P's", 1, g_fz.lmap1[0] == 0 && g_fz.lmap1[1] == 0xff);

    d = p_start(); draw(); set_rt(&B, NULL); clear(0); bind_prog(&g_p2); draw(); end();
    check("the later pass does not sample A: none (the index's screen)", 0, planned(d, 0));
    d = p_start(); draw(); set_rt(&B32, NULL); clear(0); bind_prog(&g_p2); bind_tex(&A); draw(); end();
    check("the later pass's target of another size: none [-DDM_TEST_FUSE_ANY_SIZE]", 0, planned(d, 0));
    d = p_start(); draw(); set_rt(&B, &A); clear(1); bind_prog(&g_p2); bind_tex(&A); draw(); end();
    check("the later pass clears A, which P draws to: none", 0, planned(d, 0));
    d = p_start(); draw(); copy(&B, &B32); set_rt(&B, NULL); clear(0); bind_prog(&g_p2); bind_tex(&A); draw(); end();
    check("a copy between: none", 0, planned(d, 0));
    d = p_start(); draw(); const uint8_t *again = set_rt(&A, NULL); draw(); set_rt(&B, NULL); clear(0); bind_prog(&g_p2); bind_tex(&A); draw(); end();
    check("P's targets set again between: taken into the merge", 1, planned(d, 0) && g_fz.nreuse == 1 && g_fz.reuse[0] == again);
    check("... not after a clear-only pass", 0, planned(d, 1));
    d = p_start(); draw(); set_rt(&B, NULL); clear(0); bind_prog(&g_p2); bind_tex(&A); draw(); end();
    g_shared = B.serial; check("B's storage another object's too: none [-DDM_TEST_SHARED_NOT_SEEN]", 0, planned(d, 0)); g_shared = 0;
    check("... and planned without it", 1, planned(d, 0));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
