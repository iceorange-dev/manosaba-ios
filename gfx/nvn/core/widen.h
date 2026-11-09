// Widening a 16:9 picture to the screen's shape on the GPU (plan item 320; S2Arm graphics/widen, Apache-2.0, written anew). 320a: k and the sizes.
// The screen's aspect a gives k snapped so that 1920 gains an even number of columns (the centre band's offset is then a whole pixel and the 16:9
// band, the HUD in it, lines up with the original picture pixel for pixel): extra = 2·round(960·(a / (16/9) − 1)), k = 1 + extra / 1920 (2868x1320:
// 426 columns, k 1.221875, offset 213). A screen narrower than 16:9 (1 % either way counts as 16:9) is made taller instead, the same way in rows
// over 1080 (the Duo's inner screen, 1.42: 270 rows, 1350). A texture is of the 16:9 family when |w·9 / (h·16) − 1| < 4 % (1280x720, 960x540,
// 30x17). Its widened width: w + w·extra / 1920 when that is whole for w or for w doubled up to 4096 (1280 → 1564, 1600 → 1955, 160 → 195 =
// 1564 >> 3: a mip chain made of separate textures and the levels of a large one stay the same sizes, copies between them whole), else w plus the
// nearest even count (1024 → 1252: the centre offset whole). Heights alike with the rows. gfx/tests/t_widen.c
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct { int extra_w, extra_h; double k, ky; } widen_t;   // columns over 1920 / rows over 1080 (even, one of them 0); k, ky
static inline widen_t widen_for_screen(int sw, int sh) {
    widen_t w = {0, 0, 1.0, 1.0};
    if (sw <= 0 || sh <= 0) return w;
    double a = (double)sw / sh, src = 16.0 / 9.0;
    if (a > src * 1.01) { w.extra_w = 2 * (int)lround(960 * (a / src - 1)); w.k = 1 + w.extra_w / 1920.0; }
    else if (a < src * 0.99) { w.extra_h = 2 * (int)lround(540 * (src / a - 1)); w.ky = 1 + w.extra_h / 1080.0; }
    return w;
}
static inline int widen_family(int w, int h) { return h >= 8 && w >= 16 && fabs(w * 9.0 / (h * 16.0) - 1) < 0.04; }
static inline int widen_len(int n, int extra, int base) {   // n widened by extra over base (1920 columns / 1080 rows)
    if (!extra) return n;
    long long e = (long long)n * extra;
    for (long long b = n; b <= 4096; b <<= 1)
#ifndef DM_TEST_WIDEN_EVEN_ONLY   // negative control of t_widen only: always the even count (1600 → 1956, 160 → 196: the mip chain no longer lines up)
        if (b * extra % base == 0) return n + (int)(e / base);
#else
        (void)b;
#endif
    return n + 2 * (int)llround((double)e / (2.0 * base));
}
static inline int widen_width(const widen_t *w, int tw, int th) { return w->extra_w && widen_family(tw, th) ? widen_len(tw, w->extra_w, 1920) : tw; }
static inline int widen_height(const widen_t *w, int tw, int th) { return w->extra_h && widen_family(tw, th) ? widen_len(th, w->extra_h, 1080) : th; }
