// NVN's quad strips as triangle lists (gfx/nvn/nvn_quad_strip.h, plan item 291): 4 vertices one quad, 0 1 3 / 2 0 3; 5 the same quad (the odd
// last vertex draws nothing), 6 two; with the vertices laid out as a strip goes (pairs across it), both triangles of every quad wind as the quad
// does and cover it exactly; every triangle ends at its quad's last vertex; indexed with 16-bit indices, a restart ends the strip and the next has
// pairs of its own (its odd last index dropped), restart off takes the index as a vertex, and an 8-bit index never matches a 16-bit restart index.
// A plain quad's order (-DDM_TEST_QS_ABC, the broken variant): 6 fail.
#include "../nvn/nvn_quad_strip.h"
#include <stdio.h>
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static int same(const uint32_t *a, const uint32_t *b, size_t k) { for (size_t i = 0; i < k; i++) if (a[i] != b[i]) return 0; return 1; }
static double area2(const double (*p)[2], uint32_t i, uint32_t j, uint32_t k) { return (p[j][0] - p[i][0]) * (p[k][1] - p[i][1]) - (p[k][0] - p[i][0]) * (p[j][1] - p[i][1]); }
int main(void) {
    uint32_t o[64];
    check("a strip of 0-3 vertices: no quad; 4: one; 5: one; 6: two", !qs_quads(0) && !qs_quads(3) && qs_quads(4) == 1 && qs_quads(5) == 1 && qs_quads(6) == 2);
    qs_indices(o, 2); const uint32_t want[12] = {0, 1, 3, 2, 0, 3, 2, 3, 5, 4, 2, 5};
    check("its triangles: 0 1 3 / 2 0 3, then 2 3 5 / 4 2 5", same(o, want, 12));
    double p[8][2]; for (int i = 0; i < 8; i++) { p[i][0] = i / 2; p[i][1] = i & 1; }   // pairs across the strip: (0,0) (0,1), (1,0) (1,1), ...
    qs_indices(o, 3); int wind = 1; double cover = 0;
    for (int q = 0; q < 3; q++) {
        double quad = area2(p, 2 * q, 2 * q + 1, 2 * q + 3) + area2(p, 2 * q, 2 * q + 3, 2 * q + 2);   // the quad a b d c, as a fan from a
        for (int t = 0; t < 2; t++) { double a = area2(p, o[6 * q + 3 * t], o[6 * q + 3 * t + 1], o[6 * q + 3 * t + 2]); wind &= (a > 0) == (quad > 0) && a != 0; cover += a > 0 ? a : -a; }
    }
    check("laid out as a strip: both triangles of each quad wind as the quad does", wind);
    check("... and cover the three quads exactly (area 3)", cover / 2 > 2.999 && cover / 2 < 3.001);
    int last = 1; for (int t = 0; t < 6; t++) last &= o[3 * t + 2] == (uint32_t)(2 * (t / 2) + 3);
    check("every triangle ends at its quad's last vertex (d: flat inputs)", last);
    const uint16_t ix[10] = {0, 1, 2, 3, 0xffff, 10, 11, 12, 13, 14};
    size_t k = qs_indexed(o, ix, 10, 2, 1, 0xffff); const uint32_t w2[12] = {0, 1, 3, 2, 0, 3, 10, 11, 13, 12, 10, 13};
    check("indexed, restart on: two strips, each its own quad, the second's odd last index dropped", k == 12 && same(o, w2, 12));
    k = qs_indexed(o, ix, 10, 2, 0, 0xffff);
    check("restart off: the index is a vertex (four quads)", k == 24 && o[9] == 0xffff);
    const uint8_t i8[6] = {0, 1, 0xff, 3, 4, 5};
    k = qs_indexed(o, i8, 6, 1, 1, 0xffff);
    check("8-bit indices with a 16-bit restart index: 0xff is a vertex (two quads)", k == 12 && o[2] == 3 && o[3] == 0xff);
    printf("t_quad_strip: %d checks, %d failed\n", n, fail); return fail != 0;
}
