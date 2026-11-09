// What a widened 3D draw sees changed in its uniform block (gfx/nvn/core/widen_rw.h, plan item 320d; S2Arm's rules): with 2868x1320 (k 1.221875)
// a 16:9 perspective projection's x row is divided by k, row-major and column-major; a view-projection (the projection times a rotation and a
// translation) too; an inverse projection's x row times k; the HUD's orthographic projection for 1280x720 only noted; a matrix of another shape and
// a 4:3 projection left; a screen-size vec4 (1280, 720, 1/1280, 1/720) becomes (1564, 720, 1/1564, 1/720), the other order too; the aspect pair
// (16/9, 9/16) widened; a block passed twice changed once; with the Duo's inner screen (ky 1.25) the y row divided by ky; the fast test before
// the whole ones (wrw_maybe) skips no window they accept, over 64 KB of random values with each kind planted in both orders; only the slots a
// shader reads (wrw_block_r, plan item 320g): a projection at slot 4 with slots 4-7 read changed, one at slot 8 with slots 0-3 read left (64 of
// 256 bytes looked at), every slot read the same as the whole block's scan. Every slot looked at (-DDM_TEST_WIDEN_RW_ALL_SLOTS): 1 fails. Column-major
// matrices not recognised (-DDM_TEST_WIDEN_RW_ROW_ONLY): 2 fail; the fast test without the z row (-DDM_TEST_WIDEN_RW_PRE_W_ONLY): 2 fail.
#include <stdio.h>
#include "../nvn/core/widen_rw.h"
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("  %s %s\n", ok ? "✓" : "✗", what); }
static void mul(const float *a, const float *b, float *o) { for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { float s = 0; for (int k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c]; o[r * 4 + c] = s; } }
static void transpose(const float *a, float *o) { for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) o[c * 4 + r] = a[r * 4 + c]; }
static void proj(float *m, double aspect, float jx, float jy) {   // row-major, right-handed, w row (0,0,-1,0), off-centre jx, jy (a jitter)
    double f = 1 / tan(30 * M_PI / 180), nn = 0.1, ff = 1000;
    float p[16] = {(float)(f / aspect), 0, jx, 0,  0, (float)f, jy, 0,  0, 0, (float)((ff + nn) / (nn - ff)), (float)(2 * ff * nn / (nn - ff)),  0, 0, -1, 0};
    memcpy(m, p, 64);
}
static int ref_kind(const float *m) {   // the whole tests on one window, without wrw_maybe
    if (!wrw_finite(m)) return -1;
    for (int col = 0; col < 2; col++) {
#ifdef DM_TEST_WIDEN_RW_ROW_ONLY
        if (col) break;
#endif
        int k = wrw_is_proj(m, col) ? WRW_PROJ : wrw_is_invproj(m, col) ? WRW_INVPROJ : wrw_is_viewproj(m, col) ? WRW_VIEWPROJ : wrw_is_ui_ortho(m, col) ? WRW_UI_ORTHO : -1;
        if (k >= 0) return k;
    }
    return -1;
}
static int block1(const widen_t *w, const float *m, float *out, wrw_hits *h) { uint8_t s[64], d[64]; memcpy(s, m, 64); int c = wrw_block(w, s, 64, 0, d, h); memcpy(out, d, 64); return c; }
int main(void) {
    widen_t w = widen_for_screen(2868, 1320); float P[16], o[16], Pc[16]; wrw_hits h = {{0}};
    proj(P, 16.0 / 9.0, 0.02f, -0.01f);
    int c = block1(&w, P, o, &h);
    check("a 16:9 projection (row-major): its x row divided by k, the rest the same", c == 1 && fabsf(o[0] - P[0] / 1.221875f) < 1e-6f && fabsf(o[2] - P[2] / 1.221875f) < 1e-6f && o[5] == P[5] && o[10] == P[10] && h.n[WRW_PROJ] == 1);
    transpose(P, Pc); c = block1(&w, Pc, o, &h);
    check("... column-major: the same row (its first column here) [-DDM_TEST_WIDEN_RW_ROW_ONLY: left]", c == 1 && fabsf(o[0] - Pc[0] / 1.221875f) < 1e-6f && fabsf(o[8] - Pc[8] / 1.221875f) < 1e-6f && o[5] == Pc[5]);
    float J[16]; proj(J, 16.0 / 9.0, 2.0f / 1280, -1.0f / 720);   // a jitter of a pixel or so
    double a = 30 * M_PI / 180; float V[16] = {(float)cos(a), 0, (float)sin(a), 5,  0, 1, 0, -2,  (float)-sin(a), 0, (float)cos(a), 7,  0, 0, 0, 1}, VP[16]; mul(J, V, VP);
    c = block1(&w, VP, o, &h);
    check("a view-projection (a jittered projection × rotation × translation): x row divided by k", c == 1 && fabsf(o[0] - VP[0] / 1.221875f) < 1e-6f && fabsf(o[3] - VP[3] / 1.221875f) < 1e-5f && o[4] == VP[4] && h.n[WRW_VIEWPROJ] == 1);
    float I[16] = {(float)(1 / P[0]), 0, 0, 0.03f,  0, (float)(1 / P[5]), 0, -0.02f,  0, 0, 0, -1,  0, 0, 4.995f, 5.005f};
    c = block1(&w, I, o, &h);
    check("an inverse projection: x row times k", c == 1 && fabsf(o[0] - I[0] * 1.221875f) < 1e-6f && h.n[WRW_INVPROJ] == 1);
    float U[16] = {2.0f / 1280, 0, 0, -1,  0, -2.0f / 720, 0, 1,  0, 0, 1, 0,  0, 0, 0, 1};
    c = block1(&w, U, o, &h);
    check("the HUD's orthographic projection for 1280x720: noted, not changed", c == 0 && !memcmp(o, U, 64) && h.n[WRW_UI_ORTHO] == 1);
    float R4[16]; proj(R4, 4.0 / 3.0, 0, 0); float X[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    check("a 4:3 projection and a matrix of no such shape: left", block1(&w, R4, o, &h) == 0 && block1(&w, X, o, &h) == 0);
    float sv[4] = {1280, 720, 1.0f / 1280, 1.0f / 720}, sr[4] = {1.0f / 1280, 1.0f / 720, 1280, 720}; uint8_t sb[32], sd[32];
    memcpy(sb, sv, 16); memcpy(sb + 16, sr, 16); c = wrw_block(&w, sb, 32, 0, sd, &h); float so[8]; memcpy(so, sd, 32);
    check("screen sizes (1280, 720, 1/1280, 1/720) and (1/1280, 1/720, 1280, 720): the width 1564", c == 2 && so[0] == 1564 && so[1] == 720 && fabsf(so[2] - 1.0f / 1564) < 1e-9f && so[6] == 1564 && fabsf(so[4] - 1.0f / 1564) < 1e-9f);
    float ap[2] = {16.0f / 9, 9.0f / 16}; uint8_t ab[8], ad[8]; memcpy(ab, ap, 8); c = wrw_block(&w, ab, 8, 0, ad, &h); float ao[2]; memcpy(ao, ad, 8);
    check("the aspect pair (16/9, 9/16) → (16/9 · k, its inverse)", c == 1 && fabsf(ao[0] - 16.0f / 9 * 1.221875f) < 1e-5f && fabsf(ao[1] * ao[0] - 1) < 1e-5f);
    float once[16]; block1(&w, P, once, &h); c = block1(&w, once, o, &h);
    check("a block passed twice: changed once", c == 0 && !memcmp(o, once, 64));
    widen_t d = widen_for_screen(2670, 1878); proj(P, 16.0 / 9.0, 0, 0); transpose(P, Pc); c = block1(&d, Pc, o, &h);
    check("the Duo's inner screen: a column-major projection's y row divided by ky 1.25, x the same [-DDM_TEST_WIDEN_RW_ROW_ONLY: left]", c == 1 && fabsf(o[5] - Pc[5] / 1.25f) < 1e-6f && o[0] == Pc[0]);
    {   // the fast test: every 16-byte window of 64 KB of random values (a third 0, some 1 and -1) with each kind planted in both orders, as the whole tests find it
        enum { NB = 65536 }; static float buf[NB / 4]; uint32_t x = 12345;
        for (int i = 0; i < NB / 4; i++) { x = x * 1664525u + 1013904223u; int r = x >> 28; buf[i] = r < 5 ? 0 : r < 7 ? 1 : r < 8 ? -1 : (float)((x >> 8) & 0xffff) / 16384 - 2; }
        proj(P, 16.0 / 9.0, 0.02f, -0.01f); const float *kinds[4] = {P, VP, I, U}; int planted = 0;
        for (int i = 0; i < 64; i++) { float t[16]; if (i & 1) transpose(kinds[(i >> 1) & 3], t); else memcpy(t, kinds[(i >> 1) & 3], 64); memcpy(&buf[(i * 1021 % (NB / 64 - 1)) * 16], t, 64); planted++; }
        int found = 0, same = 1;
        for (size_t p = 0; p + 64 <= NB; p += 16) { float m[16], mm[16]; memcpy(m, (uint8_t *)buf + p, 64); memcpy(mm, m, 64); int k; wrw_window(&w, mm, &k); int rk = ref_kind(m); same &= k == rk; found += rk >= 0; }
        check("the fast test skips no window the whole tests accept (64 KB, each kind planted in both orders) [-DDM_TEST_WIDEN_RW_PRE_W_ONLY: inverses skipped]", same && found >= planted / 2);
    }
    {   // plan item 320g: only the slots read
        static uint8_t blk[256], out[256], ref[256]; proj(P, 16.0 / 9.0, 0, 0); memset(blk, 0, sizeof blk); memcpy(blk + 64, P, 64);
        uint16_t r47[1][2] = {{4, 7}}, r03[1][2] = {{0, 3}}, all[1][2] = {{0, 15}}; size_t seen = 0; wrw_hits hh = {{0}};
        int c1 = wrw_block_r(&w, blk, 256, out, &hh, 0, r47, 1, &seen); float o1[16]; memcpy(o1, out + 64, 64);
        memset(blk, 0, sizeof blk); memcpy(blk + 128, P, 64); int c2 = wrw_block_r(&w, blk, 256, out, &hh, 0, r03, 1, &seen); size_t seen2 = seen;
        int c3 = wrw_block_r(&w, blk, 256, out, &hh, 0, all, 1, &seen); int c4 = wrw_block(&w, blk, 256, 0, ref, &hh);
        check("only the slots read: a projection at slot 4 with 4-7 read changed; at slot 8 with 0-3 read left, 64 bytes looked at [-DDM_TEST_WIDEN_RW_ALL_SLOTS: changed]",
              c1 == 1 && fabsf(o1[0] - P[0] / 1.221875f) < 1e-6f && c2 == 0 && seen2 == 64);
        check("... every slot read: the same as the whole block's scan", c3 == c4 && !memcmp(out, ref, 256));
    }
    printf("t_widen_rw: %d checks, %d failed\n", n, fail); return fail != 0;
}
