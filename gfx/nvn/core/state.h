// gfx/nvn/core/state.h: the replay's state (plan item 264 moved it here from the Metal backend): the draw state S and the render targets' state R,
// the commands that only set state (st_op), and the state at the start (st_reset). What a command changed is returned, so the backend marks its
// own encoder state (pipeline, depth-stencil, argument blocks) and does what it does on top (mtl/lookahead.inc state_op). Checked without a GPU
// by gfx/tests/t_core_state.c
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"
#include "core.h"

typedef struct { double red, green, blue, alpha; } nvn_rgba;   // A clear colour (the fields named as Metal's MTLClearColor)
static inline nvn_rgba nvn_rgba_make(double r, double g, double b, double a) { return (nvn_rgba){r, g, b, a}; }
// The draw state
typedef struct {
    const prog_t *prog;
    blend_st blend[8]; color_st color; chmask_st chmask; ms_st ms; poly_st poly; ds_st ds;
    va_st attr[16]; int nattr; vs_st stream[16]; int nstream;
    NVNbufferRange vbuf[16];
    p_bind_buf ubo[6][16], ssbo[6][16];
    uint64_t tex[6][128], samp[6][128], img[6][32];
    uint32_t st_vmask[2], st_wmask[2], st_ref[2];
    p_poly_offset po; float vp[16][4]; float dr[2]; int has_vp; int depth_clamp, db_en, raster_discard; float db_lo, db_hi;   // db: depth bounds (plan item 64c); raster_discard: 64d
    int restart_set, restart_en; uint32_t restart_idx;   // SetPrimitiveRestart (restart_set: the game has set it; its default is not documented)
    float blend_color[4], alpha_ref; int64_t copy_stride, copy_image_stride;
    uint8_t border_bound[2];   // the vertex / fragment stage has a sampler bound whose border colour Metal lacks (border_rebind, plan item 161)
    uint8_t lb_bound[6];       // the stage (0 vertex, 1 fragment, 5 compute) has a sampler bound whose LOD bias its shader carries (lb_rebind, plan item 318)
} nvn_draw_state;
// Render passes: SetRenderTargets opens a new pass; clears before the first command in the pass are folded into the load action
typedef struct {
    p_set_rt rt; int has_rt;
    uint8_t clear_c[8]; nvn_rgba cc[8];
    uint8_t clear_d, clear_s; double cd; uint32_t cs;
    p_scissor sc; int has_sc;
} nvn_rt_state;
extern nvn_draw_state S;
extern nvn_rt_state R;
enum { ST_STATE = 1, ST_PSO = 2, ST_DSS = 4, ST_ARGS_ALL = 8, ST_HANDLES = 16 };   // st_op's answer: a state command; the pipeline, the depth-stencil state, every stage's arguments changed; texture or sampler handles changed
#define ST_ARGS(stage) (1 << (8 + (stage)))   // ... one stage's arguments changed
void st_reset(void);   // The state at the start (the backend marks everything dirty)
int st_op(uint32_t op, const void *a);   // A command that only sets state: applied, and what changed (ST_*); 0 for any other command
