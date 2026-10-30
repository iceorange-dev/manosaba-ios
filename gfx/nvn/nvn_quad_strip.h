// gfx/nvn/nvn_quad_strip.h: NVN's quad strips as triangle lists (plan item 291; MacNativeGames bff585f, MIT: the Hero's Path's route is one;
// written anew here). After the first pair of vertices each pair closes a quad: a = 2q, b = 2q + 1, c = 2q + 2, d = 2q + 3, the quad a b d c. Its
// triangles a b d and c a d keep the quad's winding, and both end at d, the quad's last vertex (as GL's quad strips, whose flat inputs take it).
// A strip of n vertices is n / 2 - 1 quads: an odd last vertex draws nothing (a triangle strip of the same vertices drew a triangle more there).
// Indexed, a restart index (when restart is on; an index never matches one wider than its type) ends a strip and starts the next with its own
// pairs. Both backends draw them so (mtl/draw.inc, vk/vk_draw.c). Checked by gfx/tests/t_quad_strip.c
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t qs_quads(uint32_t n) { return n >= 4 ? n / 2 - 1 : 0; }
static inline void qs_quad(uint32_t *o, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
#ifndef DM_TEST_QS_ABC   // negative control of t_quad_strip only: a plain quad's order (a b c, a c d), which crosses itself in a strip
    o[0] = a; o[1] = b; o[2] = d; o[3] = c; o[4] = a; o[5] = d;
#else
    o[0] = a; o[1] = b; o[2] = c; o[3] = a; o[4] = c; o[5] = d;
#endif
}
static inline void qs_indices(uint32_t *o, uint32_t quads) { for (uint32_t q = 0; q < quads; q++) qs_quad(o + 6 * q, 2 * q, 2 * q + 1, 2 * q + 2, 2 * q + 3); }
// count indices of width bytes (1, 2 or 4) at in as triangles' into o (room for 3 * count): the number written
static inline size_t qs_indexed(uint32_t *o, const void *in, uint32_t count, unsigned width, int restart, uint32_t ri) {
    const uint8_t *b = in; uint32_t v4[4]; size_t w = 0;
    for (uint32_t i = 0, k = 0; i < count; i++) {
        uint32_t v = 0; memcpy(&v, b + (size_t)i * width, width);
        if (restart && v == ri) { k = 0; continue; }
        if (k < 2) v4[k] = v;
        else if (!(k & 1)) v4[2] = v;
        else { v4[3] = v; qs_quad(o + w, v4[0], v4[1], v4[2], v4[3]); w += 6; v4[0] = v4[2]; v4[1] = v4[3]; }
        k++;
    }
    return w;
}
