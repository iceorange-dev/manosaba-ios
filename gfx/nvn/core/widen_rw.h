// Widening (plan item 320d; S2Arm graphics/widen WidenRewriter, Apache-2.0, written anew, the core of its rules only): what in a uniform block a
// widened 3D draw must see changed. With the targets k times wider and the viewport k times wider, a perspective projection's x row times 1 / k
// keeps the pixels' shape and widens the view (Hor+). Recognised by shape, row- or column-major, at 16-byte alignment:
//   a projection P: w row (0, 0, ±1, 0), x and y rows only their diagonal and the z column (off-centre), |P11| / P00 ≈ 16/9 (1 %): x row × 1/k;
//   a view-projection: the xyz of the x, y and w rows of lengths 9 : 16 : 1 (|w| ≈ 1), each two orthogonal (x·w and y·w to a jitter's off-centre
//   terms), the z row's xyz parallel to w's: x row × 1/k;
//   an inverse projection: x and y rows only their diagonal and w column, diagonals in 16 : 9, z row (0, 0, 0, ±1): x row × k;
//   an orthographic projection for a 16:9 pixel canvas (w row 0001, axis-aligned, 2 / P00 and 2 / |P11| a 16:9-family size): only noted (the HUD);
// and at 4-byte alignment: a screen-size vec4 (W, H, 1/W, 1/H) or (1/W, 1/H, W, H) with W x H of the 16:9 family → W widened; the aspect pair
// (16/9, 9/16) → (16/9 · k, 9 / (16 k)). For a taller screen the y row is divided by ky (× ky for the inverse) and H widened instead.
// Rewritten values no longer match (their aspect is not 16:9 any more), so a block passed twice is changed once. gfx/tests/t_widen_rw.c
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "widen.h"
enum { WRW_PROJ, WRW_VIEWPROJ, WRW_INVPROJ, WRW_UI_ORTHO, WRW_SCREEN, WRW_ASPECT, WRW_N };
typedef struct { uint32_t n[WRW_N]; } wrw_hits;
static inline float wrw_e(const float *m, int col, int r, int c) { return col ? m[c * 4 + r] : m[r * 4 + c]; }
static inline float *wrw_p(float *m, int col, int r, int c) { return col ? &m[c * 4 + r] : &m[r * 4 + c]; }
static inline int wrw_near(double a, double b, double rel) { return fabs(a - b) <= rel * fmax(fabs(a), fabs(b)); }
static inline int wrw_zero(double v, double scale) { return fabs(v) <= 1e-5 * scale; }
static inline int wrw_finite(const float *m) { for (int i = 0; i < 16; i++) if (!isfinite(m[i])) return 0; return 1; }
static inline int wrw_is_proj(const float *m, int col) {   // w row (0,0,±1,0); x row (P00,0,P02,0); y row (0,P11,P12,0); |P11|/P00 = 16/9
    float p00 = wrw_e(m, col, 0, 0), p11 = wrw_e(m, col, 1, 1), s = fmaxf(fabsf(p00), fabsf(p11));
    if (!(p00 > 0) || s == 0) return 0;
    if (!wrw_zero(wrw_e(m, col, 3, 0), 1) || !wrw_zero(wrw_e(m, col, 3, 1), 1) || !wrw_near(fabs(wrw_e(m, col, 3, 2)), 1, 1e-4) || !wrw_zero(wrw_e(m, col, 3, 3), 1)) return 0;
    if (!wrw_zero(wrw_e(m, col, 0, 1), s) || !wrw_zero(wrw_e(m, col, 0, 3), s) || !wrw_zero(wrw_e(m, col, 1, 0), s) || !wrw_zero(wrw_e(m, col, 1, 3), s)) return 0;
    return wrw_near(fabs(p11) / p00, 16.0 / 9.0, 0.01);
}
static inline int wrw_is_viewproj(const float *m, int col) {   // |x|:|y|:|w| = 9:16:1 (xyz parts), pairwise orthogonal, z parallel to w
    double r[4][3];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) r[i][j] = wrw_e(m, col, i, j);
    double lx = sqrt(r[0][0] * r[0][0] + r[0][1] * r[0][1] + r[0][2] * r[0][2]), ly = sqrt(r[1][0] * r[1][0] + r[1][1] * r[1][1] + r[1][2] * r[1][2]);
    double lz = sqrt(r[2][0] * r[2][0] + r[2][1] * r[2][1] + r[2][2] * r[2][2]), lw = sqrt(r[3][0] * r[3][0] + r[3][1] * r[3][1] + r[3][2] * r[3][2]);
    if (!(lx > 0) || !(lz > 0) || !wrw_near(lw, 1, 0.01) || !wrw_near(ly / lx, 16.0 / 9.0, 0.01)) return 0;
    double xy = r[0][0] * r[1][0] + r[0][1] * r[1][1] + r[0][2] * r[1][2], xw = r[0][0] * r[3][0] + r[0][1] * r[3][1] + r[0][2] * r[3][2], yw = r[1][0] * r[3][0] + r[1][1] * r[3][1] + r[1][2] * r[3][2];
    // x·w and y·w carry the projection's off-centre terms: a temporal anti-aliasing jitter of about 2 / 1280 in clip space passes, a lens shifted further not
    if (fabs(xy) > 1e-3 * lx * ly || fabs(xw) > 4e-3 * lx || fabs(yw) > 4e-3 * ly) return 0;
    double cx = r[2][1] * r[3][2] - r[2][2] * r[3][1], cy = r[2][2] * r[3][0] - r[2][0] * r[3][2], cz = r[2][0] * r[3][1] - r[2][1] * r[3][0];
    return sqrt(cx * cx + cy * cy + cz * cz) <= 1e-3 * lz;
}
static inline int wrw_is_invproj(const float *m, int col) {   // x (a,0,0,b), y (0,c,0,d), z (0,0,0,±1), |c| / a = 9/16
    float a = wrw_e(m, col, 0, 0), c = wrw_e(m, col, 1, 1), s = fmaxf(fabsf(a), fabsf(c));
    if (!(a > 0) || s == 0) return 0;
    if (!wrw_zero(wrw_e(m, col, 0, 1), s) || !wrw_zero(wrw_e(m, col, 0, 2), s) || !wrw_zero(wrw_e(m, col, 1, 0), s) || !wrw_zero(wrw_e(m, col, 1, 2), s)) return 0;
    if (!wrw_zero(wrw_e(m, col, 2, 0), 1) || !wrw_zero(wrw_e(m, col, 2, 1), 1) || !wrw_zero(wrw_e(m, col, 2, 2), 1) || !wrw_near(fabs(wrw_e(m, col, 2, 3)), 1, 1e-4)) return 0;
    return wrw_near(fabs(c) / a, 9.0 / 16.0, 0.01);
}
static inline int wrw_is_ui_ortho(const float *m, int col) {   // w row 0001, axis-aligned, 2/P00 x 2/|P11| a 16:9-family pixel size
    float p00 = wrw_e(m, col, 0, 0), p11 = wrw_e(m, col, 1, 1);
    if (!(p00 > 0) || p11 == 0) return 0;
    if (!wrw_zero(wrw_e(m, col, 3, 0), 1) || !wrw_zero(wrw_e(m, col, 3, 1), 1) || !wrw_zero(wrw_e(m, col, 3, 2), 1) || !wrw_near(wrw_e(m, col, 3, 3), 1, 1e-4)) return 0;
    if (!wrw_zero(wrw_e(m, col, 0, 1), p00) || !wrw_zero(wrw_e(m, col, 1, 0), fabsf(p11))) return 0;
    double w = 2.0 / p00, h = 2.0 / fabs(p11);
    return fabs(w - round(w)) < 0.01 && fabs(h - round(h)) < 0.01 && widen_family((int)round(w), (int)round(h));
}
// What every kind above needs of the w row or the z row, tested first (plan item 320d: the whole tests on every 16-byte step cost 3.2 ms a MB on
// the M5, 2.3 MB a frame in Tears of the Kingdom): the w row's xyz of length 1 (a projection, a view-projection: 1 %), or zero with w 1 (an
// orthographic projection), or the z row (0, 0, 0, ±1) (an inverse projection). It passes everything a kind accepts; most windows fail it
static inline int wrw_maybe(const float *m, int col) {
    float a = wrw_e(m, col, 3, 0), b = wrw_e(m, col, 3, 1), c = wrw_e(m, col, 3, 2), d = wrw_e(m, col, 3, 3), l = a * a + b * b + c * c;
    if (fabsf(l - 1) < 0.021f || (l < 1e-9f && fabsf(d - 1) < 1e-3f)) return 1;
#ifdef DM_TEST_WIDEN_RW_PRE_W_ONLY   // negative control of t_widen_rw only: the z row not looked at (inverse projections skipped)
    return 0;
#endif
    a = wrw_e(m, col, 2, 0); b = wrw_e(m, col, 2, 1); c = wrw_e(m, col, 2, 2); d = wrw_e(m, col, 2, 3);
    return a * a + b * b + c * c < 1e-9f && fabsf(fabsf(d) - 1) < 1e-3f;
}
static inline void wrw_scale_row(float *m, int col, int r, float f) { for (int c = 0; c < 4; c++) *wrw_p(m, col, r, c) *= f; }
// One 64-byte window: 1 and the window rewritten when it is one of the kinds to change; *kind the kind found (WRW_UI_ORTHO is noted, returns 0)
static inline int wrw_window(const widen_t *w, float *m, int *kind) {
    *kind = -1;
    int may = wrw_maybe(m, 0) | wrw_maybe(m, 1) << 1;
    if (!may || !wrw_finite(m)) return 0;
    for (int col = 0; col < 2; col++) {
#ifdef DM_TEST_WIDEN_RW_ROW_ONLY   // negative control of t_widen_rw only: column-major matrices not recognised
        if (col) break;
#endif
        if (!(may >> col & 1)) continue;
        int k = wrw_is_proj(m, col) ? WRW_PROJ : wrw_is_invproj(m, col) ? WRW_INVPROJ : wrw_is_viewproj(m, col) ? WRW_VIEWPROJ : wrw_is_ui_ortho(m, col) ? WRW_UI_ORTHO : -1;
        if (k < 0) continue;
        *kind = k;
        if (k == WRW_UI_ORTHO) return 0;
        if (w->extra_w) wrw_scale_row(m, col, 0, k == WRW_INVPROJ ? (float)w->k : (float)(1 / w->k));
        if (w->extra_h) wrw_scale_row(m, col, 1, k == WRW_INVPROJ ? (float)w->ky : (float)(1 / w->ky));
        return 1;
    }
    return 0;
}
// A block of n bytes at offset off within its buffer (matrices at 16-byte alignment of the buffer): dst gets the block with what is to change
// changed; the count of changes returned, hits counted per kind
// keep_screen (plan item 320f): screen-size vec4s left as the game's, for a fragment shader reading the fragment coordinate, which then sees the
// game's pixels (the support buffer's render_scale divides it); still counted
static inline int wrw_block_k(const widen_t *w, const uint8_t *src, size_t n, uint64_t off, uint8_t *dst, wrw_hits *h, int keep_screen) {
    memcpy(dst, src, n); int changed = 0;
    if (!w->extra_w && !w->extra_h) return 0;
    for (size_t p = (16 - (off & 15)) & 15; p + 64 <= n; ) {
        float m[16]; memcpy(m, dst + p, 64); int kind;
        int c = wrw_window(w, m, &kind);
        if (kind >= 0 && h) h->n[kind]++;
        if (c) { memcpy(dst + p, m, 64); changed++; p += 64; } else p += 16;
    }
    for (size_t p = (4 - (off & 3)) & 3; p + 8 <= n; p += 4) {
        float v0; memcpy(&v0, dst + p, 4);   // first what the first value must be: a width, a reciprocal width (1/16 or less) or 16/9
        if (!((v0 >= 16 && v0 <= 16384) || (v0 > 0 && v0 <= 0.0625001f) || (v0 > 1.77f && v0 < 1.78f))) continue;
        float v[4] = {0}; memcpy(v, dst + p, p + 16 <= n ? 16 : 8);
        if (p + 16 <= n) for (int sw = 0; sw < 2; sw++) {   // (W, H, 1/W, 1/H) or (1/W, 1/H, W, H)
            float W = sw ? v[2] : v[0], H = sw ? v[3] : v[1], iW = sw ? v[0] : v[2], iH = sw ? v[1] : v[3];
            if (!(W >= 16 && W <= 16384 && H >= 8 && H <= 16384) || W != roundf(W) || H != roundf(H) || !widen_family((int)W, (int)H) || !wrw_near(iW * W, 1, 1e-5) || !wrw_near(iH * H, 1, 1e-5)) continue;
            if (keep_screen) { if (h) h->n[WRW_SCREEN]++; p += 12; goto next; }
            float nW = (float)widen_width(w, (int)W, (int)H), nH = (float)widen_height(w, (int)W, (int)H);
            float o[4] = {sw ? 1 / nW : nW, sw ? 1 / nH : nH, sw ? nW : 1 / nW, sw ? nH : 1 / nH};
            memcpy(dst + p, o, 16); changed++; if (h) h->n[WRW_SCREEN]++; p += 12; goto next;
        }
        if (wrw_near(v[0], 16.0 / 9.0, 1e-5) && wrw_near(v[1], 9.0 / 16.0, 1e-5)) {
            float a = (float)(16.0 / 9.0 * w->k / w->ky), o[2] = {a, 1 / a}; memcpy(dst + p, o, 8); changed++; if (h) h->n[WRW_ASPECT]++; p += 4;
        }
    next:;
    }
    return changed;
}
static inline int wrw_block(const widen_t *w, const uint8_t *src, size_t n, uint64_t off, uint8_t *dst, wrw_hits *h) { return wrw_block_k(w, src, n, off, dst, h, 0); }
