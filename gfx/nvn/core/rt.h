// gfx/nvn/core/rt.h: render target helpers in plain C (plan item 264): a mip level's extent, an attachment's subresource, whether a scissor covers
// a target; comparing target sets and colour slots (rt.c)
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"

// A render attachment selects one mip level, while the texture's size is the base level's
static inline uint64_t nvn_mip_extent(uint64_t size, uint64_t level) { return level < 64 && size >> level > 1 ? size >> level : 1; }
static inline void rt_sub(const p_set_rt *r, int k, uint64_t *ser, int *lv, int *sl) {   // Subresource of attachment k (0–7 color, 8 depth)
    const tex_t *t = k == 8 ? r->depth : r->color[k]; const view_t *v = k == 8 ? &r->dview : &r->cview[k]; int hv = k == 8 ? r->has_dview : r->has_cview[k];
    *ser = t ? t->serial : 0; *lv = hv && v->set_levels ? v->base_level : 0; *sl = hv && v->set_layers ? v->min_layer : 0;
}
static inline int scissor_full_of(const p_scissor *sc, int has_sc, const tex_t *t, const view_t *v, int has_v) {
    uint64_t level = has_v && v->set_levels ? v->base_level : 0;
    uint64_t w = nvn_mip_extent(t->d.width, level), h = nvn_mip_extent(t->d.target == 0 || t->d.target == 3 ? 1 : t->d.height, level);
    return !has_sc || (sc->x <= 0 && sc->y <= 0 && (int64_t)sc->x + sc->w >= (int64_t)w && (int64_t)sc->y + sc->h >= (int64_t)h);
}

// 1 when another live texture object holds this one's GPU storage (the Metal backend keeps one texture for every GPU-only object of the same
// storage, layout and format: mtl/textures.inc). The rewrites follow a texture by its serial, so what reads or clears it through the other object
// is not seen by them: such an attachment is written back whatever the store lookahead found and stays out of the validity ledger, and a pass
// with one is not hoisted, deferred or merged (plan item 219: a lead 62j left, from DavidZande/MacNativeGames 907138d)
int nvn_be_tex_shared(const tex_t *t);   // (the backend provides)
static inline int tex_shared(const tex_t *t) {
#ifndef DM_TEST_SHARED_NOT_SEEN   // negative control of t_core_pass, t_core_defer and t_core_fuse only: shared storage not looked for, as before
    return t && nvn_be_tex_shared(t);
#else
    (void)t; return 0;
#endif
}
int rt_any_shared(const p_set_rt *r);   // any target of r, counted and logged (a rewrite's refusal)
// Same set of targets: compared by texture serial and view (not by object pointer: the game may copy texture objects by value)
int rt_same(const p_set_rt *a, const p_set_rt *b);
// Attachment ka of a and kb of b (0–7 color, 8 depth) are the same subresource: one texture's level and layer
int rt_sub_eq(const p_set_rt *a, int ka, const p_set_rt *b, int kb);
// Colour slots i and j attach the same level / layer of one texture (by serial: records hold copies of the texture objects)
int rt_same_sub(const p_set_rt *r, int i, int j);
// Colour slot i is left out of its pass: the same level / layer of one texture attached at more than one colour slot is attached once. On the
// Switch the ROP writes only the channels a slot's write mask enables, so the slots do not interfere; on Apple's TBDR every attachment has its own
// tile memory and is stored back whole at the end of the pass, so the copy stored later (loaded or cleared, never written) overwrites the other
// one's writes. Breath of the Wild's G-buffer attaches the albedo twice (nxnative, seedds, upstream d6cd8a8, MIT); Tears of the Kingdom's
// half-resolution lighting passes attach their RG11B10F light buffer at c2 and c3, and the stored zeros left the title mural unlit (black). The slot
// kept is the one the current channel write mask (S.chmask, the pass's opening draw) writes, else the first: tomodachi-native's variant (kuosuko,
// MIT), since a pass may write only through a later slot
int rt_dup_slot(const p_set_rt *r, int i);
