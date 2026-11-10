// gfx/nvn/core/rt.c: comparing render target sets, attachments and colour slots (see rt.h; plan item 264 moved them here from the Metal
// backend's mtl/lookahead.inc, mtl/renderpass.inc and mtl/tile_merge.inc)
#include "rt.h"
#include "state.h"
#include <string.h>

int rt_same(const p_set_rt *a, const p_set_rt *b) {
    if (a->n != b->n || !a->depth != !b->depth || (a->depth && a->depth->serial != b->depth->serial) || a->has_dview != b->has_dview) return 0;
    if (a->has_dview && memcmp(&a->dview, &b->dview, sizeof a->dview)) return 0;
    for (int k = 0; k < a->n && k < 8; k++) {
#ifdef DM_TEST_RT_SAME_NO_VIEW   // negative control of gfx/tests t_rt_continue and t_core_pass only: the colour targets compared by texture alone
        if (!a->color[k] != !b->color[k] || (a->color[k] && a->color[k]->serial != b->color[k]->serial)) return 0;
        continue;
#endif
        if (!a->color[k] != !b->color[k] || (a->color[k] && a->color[k]->serial != b->color[k]->serial) || a->has_cview[k] != b->has_cview[k]) return 0;
        if (a->has_cview[k] && memcmp(&a->cview[k], &b->cview[k], sizeof a->cview[k])) return 0;
    }
    return 1;
}
uint64_t g_rt_shared_n;
int rt_any_shared(const p_set_rt *r) {
    const tex_t *hit = NULL;
    for (int k = 0; k < r->n && k < 8 && !hit; k++) if (tex_shared(r->color[k])) hit = r->color[k];
    if (!hit && tex_shared(r->depth)) hit = r->depth;
    if (!hit) return 0;
    uint64_t n = ++g_rt_shared_n;
    if (n <= 4 || !(n & (n - 1))) MLOG("a pass with a target whose storage another texture object holds too is not hoisted, deferred or merged (texture %llu, %ux%u; %llu so far)",
                                       (unsigned long long)hit->serial, hit->d.width, hit->d.height, (unsigned long long)n);
    return 1;
}
int rt_sub_eq(const p_set_rt *a, int ka, const p_set_rt *b, int kb) { uint64_t s1, s2; int l1, l2, x1, x2; rt_sub(a, ka, &s1, &l1, &x1); rt_sub(b, kb, &s2, &l2, &x2); return s1 && s1 == s2 && l1 == l2 && x1 == x2; }
int rt_same_sub(const p_set_rt *r, int i, int j) {
    if (!r->color[i] || !r->color[j] || r->color[j]->serial != r->color[i]->serial) return 0;
    uint8_t lv = r->has_cview[i] && r->cview[i].set_levels ? r->cview[i].base_level : 0; uint16_t ly = r->has_cview[i] && r->cview[i].set_layers ? r->cview[i].min_layer : 0;
    uint8_t lj = r->has_cview[j] && r->cview[j].set_levels ? r->cview[j].base_level : 0; uint16_t yj = r->has_cview[j] && r->cview[j].set_layers ? r->cview[j].min_layer : 0;
    return lj == lv && yj == ly;
}
int rt_dup_slot(const p_set_rt *r, int i) {
    if (!r->color[i]) return 0;
    uint32_t cm = S.chmask.mask; int first = -1, pick = -1, n = 0;
    for (int j = 0; j < r->n && j < 8; j++) if (rt_same_sub(r, i, j)) { if (first < 0) first = j; if (pick < 0 && (cm >> (4 * j) & 0xf)) pick = j; n++; }
    return n > 1 && i != (pick >= 0 ? pick : first);
}
