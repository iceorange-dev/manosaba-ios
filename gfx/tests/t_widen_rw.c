// What a widened 3D draw sees changed in its uniform block (gfx/nvn/core/widen_rw.h, plan item 320d; S2Arm's rules): with 2868x1320 (k 1.221875)
// a 16:9 perspective projection's x row is divided by k, row-major and column-major; a view-projection (the projection times a rotation and a
// translation) too; an inverse projection's x row times k; the HUD's orthographic projection for 1280x720 only noted; a matrix of another shape and
// a 4:3 projection left; a screen-size vec4 (1280, 720, 1/1280, 1/720) becomes (1564, 720, 1/1564, 1/720), the other order too; the aspect pair
// (16/9, 9/16) widened; a block passed twice changed once; with the Duo's inner screen (ky 1.25) the y row divided by ky. Column-major matrices not
// recognised (-DDM_TEST_WIDEN_RW_ROW_ONLY): 2 fail.
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
    printf("t_widen_rw: %d checks, %d failed\n", n, fail); return fail != 0;
}
