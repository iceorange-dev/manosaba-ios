// gfx/nvn/vk/vk_draw.c: draws and dispatches (plan items 301e2, 301e2b). A draw opens the pass when none is open: dynamic rendering into R's
// targets, R's pending clears as load actions (a CLEAR load), every attachment stored; the pass stays open for the draws after it until the replay
// ends it (new targets, a copy, a dispatch, the submission's end; a barrier only as vk_pass_barrier says). Its pipeline is made from the program's two shaders, the pass's
// attachment formats and the state that is not dynamic (blend, channel masks, polygon mode, the primitive class, multisampling), hashed and kept;
// everything else is dynamic state set at each draw from S: viewport (flipped: the translation keeps NVN's clip space, whose y points up, and
// Vulkan's framebuffer y points down), scissor, depth bias, blend constants, stencil, cull mode and front face, topology, depth test / write /
// compare, stencil test and ops, primitive restart, rasterizer discard, and the vertex input (VK_EXT_vertex_input_dynamic_state) from S's
// attributes and streams. Quads are drawn as two triangles each (shared indices, or an indexed draw's indices rewritten into the arena), quad
// strips as triangle strips. The resources: vk_bind.c. A dispatch runs outside any pass with a pipeline per compute shader.
#include "vk_bind.h"
#include "../nvn_hmap.h"
#include "../core/exec.h"
#include "../core/pass.h"
#include "../core/state.h"
#include "../core/upd.h"
#include "../core/usnap.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int exec_draw_dropped(void);   // core/exec.c: rendering off, no program, empty depth bounds
static uint64_t g_draws, g_dropped, g_pipes, g_passes, g_dispatches;

// ---------- The pass ----------
static struct { int open, n, bar, sw; uint64_t draws; vk_img *col[8]; vk_img *ds; VkFormat cf[8], df, sf; uint32_t w, h, samples; } P;   // bar: a barrier in it; sw: storage written
int vk_pass_is_open(void) { return P.open; }
int nvn_be_pass_mode(void) { return P.open ? 0 : -1; }
int vk_pass_attach_index(const vk_img *im) { if (!P.open) return -1; for (int i = 0; i < P.n; i++) if (P.col[i] == im) return i; return P.ds == im ? -2 : -1; }
uint64_t vk_pass_count(void) { return g_passes; }
static uint64_t g_pe[VK_PE_N]; static int g_pe_why = -1;
int vk_pe_push(int why) { int was = g_pe_why; if (was < 0) g_pe_why = why; return was; }
void vk_pe_pop(int was) { g_pe_why = was; }
void vk_pass_ends(uint64_t out[VK_PE_N]) { memcpy(out, g_pe, sizeof g_pe); }
static int pe_cause(void) {
#ifndef DM_TEST_VK_PE_OP_ONLY   // negative control of t_vk_be only: the backend's own reasons not looked at
    if (g_pe_why >= 0) return g_pe_why;
#endif
    if (!g_exec_cur) return VK_PE_OTHER;
    op_hdr h; memcpy(&h, g_exec_cur, sizeof h);
    switch (h.op) {
    case OP_SET_RT: {
        p_set_rt r; memcpy(&r, g_exec_cur + 8, sizeof r); int same = r.n == R.rt.n && r.depth == R.rt.depth;
        for (int i = 0; same && i < r.n && i < 8; i++) same = r.color[i] == R.rt.color[i];
        return same ? VK_PE_RT_SAME : VK_PE_RT; }
    case OP_CLEAR_COLOR: case OP_CLEAR_DS: return VK_PE_CLEAR;
    case OP_COPY_B2B: case OP_COPY_B2T: case OP_COPY_T2B: case OP_COPY_T2T: case OP_CLEAR_TEX: return VK_PE_COPY;
    case OP_DISPATCH: return VK_PE_DISPATCH;
    case OP_BARRIER: return VK_PE_BARRIER;
    case OP_UPDATE_UB: return VK_PE_UB;
    default: return VK_PE_OTHER;
    }
}
static const char *const PE_NAME[VK_PE_N] = {"SetRenderTargets", "SetRenderTargets of the same textures", "a clear", "a copy", "a dispatch", "a barrier",
                                             "a uniform update", "renamed updates written", "sampling the attachment", "the submission", "other"};
static void pass_close(void) {   // (the core's continuation mark goes with it, as the Metal backend's rp_end)
    if (P.open) {
        VkCommandBuffer cb = vk_cb(); vkCmdEndRendering(cb);
        vk_ptime_key k = {P.col[0] ? P.col[0]->w : P.w, P.col[0] ? P.col[0]->h : P.h, (int32_t)P.cf[0], (int32_t)P.df}; vk_ptime_end(cb, &k, (uint32_t)P.draws);
        vk_barrier_all(cb); P.open = 0;
        int why = pe_cause(); g_pe[why]++; PD("E %llu ends: %s, %llu draws", (unsigned long long)g_stat_pass, PE_NAME[why], (unsigned long long)P.draws);
    }
    g_rt_merged = 0;
}
void vk_pass_end_now(void) { pass_close(); vk_land_clears(); }
// A barrier in the open pass (plan item 301e3): the pass goes on, as on Metal (whose backend lets barriers by: its passes end for new targets,
// copies and dispatches). Its draws are in order on its attachments, and what the pass did not write was made visible when the pass or copy
// that wrote it ended. What a barrier orders inside it is kept: a draw after it sampling an attachment of the pass ends the pass first (as in
// a continued pass), and storage written by a draw before it ends the pass at the barrier. 0: no pass open, or ended
int vk_pass_barrier(void) {
#ifndef DM_TEST_VK_BAR_NO_SPLIT   // negative control of t_vk_be only: a barrier let by whatever the draws after it sample
    if (!P.open || P.sw) return 0;
    P.bar = 1; return 1;
#else
    return P.open;
#endif
}
void nvn_be_pass_flush(void) { vk_pass_end_now(); }
void nvn_be_pass_end(void) { pass_close(); }
// A pass the core continued (the same targets set again) holds the earlier draws' results in its attachments: a draw sampling one of them
// (or storing to it) needs them written out first, so the pass is ended before it (the Metal backend's draw_samples_rt split)
static __attribute__((unused)) int samples_attachment(const vk_sh *const *sh, const int *nst, int n) {   // (unused only in t_vk_be's control)
    for (int i = 0; i < n; i++) if (sh[i]) for (int k = 0; k < sh[i]->nb; k++) {
        uint64_t ser = vk_bind_tex_serial(&sh[i]->b[k], nst[i]); if (!ser) continue;
        for (int c = 0; c < P.n; c++) if (P.col[c] && P.col[c]->serial == ser) return 1;
        if (P.ds && P.ds->serial == ser) return 1;
    }
    return 0;
}
// Before a draw opens its pass: an updated range it reads other than as a uniform buffer (its indices, vertex streams, storage buffers), or a
// uniform range with no snapshot, needs the renamed updates in memory first (the Metal backend's upd_guard_draw; texel buffers are not bound yet)
static void upd_guard(const vk_sh *const *sh, const int *nst, int n, const p_draw_idx *di) {
    if (!g_nupd) return;
    int why = -1;
    if (di) { int isz = di->itype == NVN_INDEX_TYPE_UNSIGNED_INT ? 4 : di->itype == NVN_INDEX_TYPE_UNSIGNED_SHORT ? 2 : 1; if (upd_overlap_seq(di->ibuf, di->ibuf + (uint64_t)di->count * isz)) why = 0; }
    for (int v = 0; why < 0 && sh[0] && v < sh[0]->nv; v++) {
        int a = sh[0]->vin[v].loc; if (a >= S.nattr || S.attr[a].stream >= 16) continue;
        const NVNbufferRange *r = &S.vbuf[S.attr[a].stream]; if (r->address && upd_overlap_seq(r->address, r->address + (r->size ? r->size : 1))) why = 1;
    }
    for (int i = 0; i < n && why < 0; i++) if (sh[i]) for (int k = 0; k < sh[i]->nb && why < 0; k++) {
        const vk_bind *b = &sh[i]->b[k]; if (b->reg < 0 || b->reg >= 16) continue;
        if (b->kind == VK_K_SBUF) { const p_bind_buf *u = &S.ssbo[nst[i]][b->reg]; if (u->addr && upd_overlap_seq(u->addr, u->addr + (u->size ? u->size : 1))) why = 2; }
        else if (b->kind == VK_K_CBUF) { const p_bind_buf *u = &S.ubo[nst[i]][b->reg]; if (u->addr && u->size && upd_overlap_seq(u->addr, u->addr + u->size) && !us_find(u->addr, u->size)) why = 4; }
    }
    if (why < 0) return;
    g_ren.guard++; g_ren.g[why]++; vk_upd_flush();
}
int nvn_be_layered(void) { return 0; }
int nvn_be_layer_continue(const p_set_rt *r) { (void)r; return 0; }
static VkClearColorValue clear_value(VkFormat f, const nvn_rgba *c) {
    VkClearColorValue v; VkFormatProperties fp; (void)fp;
    int ints = f == VK_FORMAT_R8_UINT || f == VK_FORMAT_R16_UINT || f == VK_FORMAT_R32_UINT || f == VK_FORMAT_R8G8B8A8_UINT || f == VK_FORMAT_R16G16B16A16_UINT ||
               f == VK_FORMAT_R32G32B32A32_UINT || f == VK_FORMAT_R32G32_UINT || f == VK_FORMAT_R16G16_UINT || f == VK_FORMAT_R8G8_UINT || f == VK_FORMAT_R8_SINT ||
               f == VK_FORMAT_R16_SINT || f == VK_FORMAT_R32_SINT || f == VK_FORMAT_R8G8B8A8_SINT || f == VK_FORMAT_R16G16B16A16_SINT || f == VK_FORMAT_R32G32B32A32_SINT;
    if (ints) { v.int32[0] = (int32_t)c->red; v.int32[1] = (int32_t)c->green; v.int32[2] = (int32_t)c->blue; v.int32[3] = (int32_t)c->alpha; }
    else { v.float32[0] = (float)c->red; v.float32[1] = (float)c->green; v.float32[2] = (float)c->blue; v.float32[3] = (float)c->alpha; }
    return v;
}
static int pass_open(void) {
    if (P.open) return 1;
    if (!R.has_rt) { VRED("a draw with no render targets set: dropped"); return 0; }
    VkRenderingAttachmentInfo ca[8] = {0}, da = {0}; uint32_t w = UINT32_MAX, h = UINT32_MAX; int n = R.rt.n > 8 ? 8 : R.rt.n;
    memset(&P, 0, sizeof P);
    for (int i = 0; i < n; i++) {
        ca[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        vk_img *im = R.rt.color[i] ? vk_tex(R.rt.color[i]) : NULL;
        if (!im || !(im->aspect & VK_IMAGE_ASPECT_COLOR_BIT)) { ca[i].imageView = VK_NULL_HANDLE; continue; }
        vk_tex_upload_now(im);
        unsigned lv = R.rt.has_cview[i] && R.rt.cview[i].set_levels ? R.rt.cview[i].base_level : 0;
        uint32_t lw = im->w >> lv ? im->w >> lv : 1, lh = im->h >> lv ? im->h >> lv : 1; if (lw < w) w = lw; if (lh < h) h = lh;
        ca[i].imageView = vk_tex_view(im, &R.rt.cview[i], R.rt.has_cview[i], 1, -1); ca[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        ca[i].loadOp = R.clear_c[i] ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD; ca[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (R.clear_c[i]) ca[i].clearValue.color = clear_value(im->fmt, &R.cc[i]);
        P.col[i] = im; P.cf[i] = im->fmt; P.samples = im->t.d.samples > 1 ? (uint32_t)im->t.d.samples : 1; im->gpu_written = 1; im->uploaded = 1;
    }
    P.n = n;
    vk_img *dm = R.rt.depth ? vk_tex(R.rt.depth) : NULL;
    if (dm && (dm->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) {
        vk_tex_upload_now(dm);
        unsigned lv = R.rt.has_dview && R.rt.dview.set_levels ? R.rt.dview.base_level : 0;
        uint32_t lw = dm->w >> lv ? dm->w >> lv : 1, lh = dm->h >> lv ? dm->h >> lv : 1; if (lw < w) w = lw; if (lh < h) h = lh;
        da = (VkRenderingAttachmentInfo){VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = vk_tex_view(dm, &R.rt.dview, R.rt.has_dview, 1, -1), .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = (R.clear_d || R.clear_s) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        if (R.clear_d || R.clear_s) da.clearValue.depthStencil = (VkClearDepthStencilValue){R.clear_d ? (float)R.cd : 0, R.clear_s ? R.cs : 0};
        // a plane cleared and the other kept: the kept one is loaded by clearing to what a CLEAR load would lose (only both or neither here)
        if ((R.clear_d != R.clear_s) && (dm->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && (dm->aspect & VK_IMAGE_ASPECT_STENCIL_BIT)) {
            da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; VRED("a clear of one plane of a depth-stencil target as a load action: drawn as a clear instead");
        }
        P.ds = dm; P.df = (dm->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? dm->fmt : VK_FORMAT_UNDEFINED; P.sf = (dm->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ? dm->fmt : VK_FORMAT_UNDEFINED;
        if (!P.samples) P.samples = dm->t.d.samples > 1 ? (uint32_t)dm->t.d.samples : 1;
        dm->gpu_written = 1; dm->uploaded = 1;
    }
    if (w == UINT32_MAX) { VRED("a pass without attachments: not drawn yet"); return 0; }
    if (!P.samples) P.samples = 1;
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {w, h}}, .layerCount = 1, .colorAttachmentCount = (uint32_t)n, .pColorAttachments = ca,
        .pDepthAttachment = P.df ? &da : NULL, .pStencilAttachment = P.sf ? &da : NULL};
    vk_ptime_begin(vk_cb()); vkCmdBeginRendering(vk_cb(), &ri);
    int partial_ds = R.clear_d != R.clear_s && P.df && P.sf;
    memset(R.clear_c, 0, sizeof R.clear_c);
    if (!partial_ds) R.clear_d = R.clear_s = 0;
    P.open = 1; P.w = w; P.h = h; g_passes++; g_stat_pass++;
    if (passdump()) {   // DM_NVN_PASSDUMP / the command channel's passdump: the attachments (the Metal backend's P lines, shorter)
        for (int i = 0; i < n; i++) if (P.col[i]) PD("P %llu c%d ser=%llu fmt=%d %ux%u ms%u %s", (unsigned long long)g_stat_pass, i, (unsigned long long)P.col[i]->serial,
                                                    (int)P.cf[i], P.col[i]->w, P.col[i]->h, P.samples, ca[i].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? "clear" : "load");
        if (P.ds) PD("P %llu d ser=%llu fmt=%d %ux%u %s", (unsigned long long)g_stat_pass, (unsigned long long)P.ds->serial, (int)P.ds->fmt, P.ds->w, P.ds->h,
                     da.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? "clear" : "load");
    }
    if (partial_ds) {   // the one plane cleared inside the pass
        VkClearAttachment c = {R.clear_d ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_STENCIL_BIT, 0, {.depthStencil = {(float)R.cd, R.cs}}}; VkClearRect r = {{{0, 0}, {w, h}}, 0, 1};
        vkCmdClearAttachments(vk_cb(), 1, &c, 1, &r); R.clear_d = R.clear_s = 0;
    }
    return 1;
}

// ---------- NVN state → Vulkan ----------
static VkBlendFactor blend_factor(int f) {
    switch (f) {
    case NVN_BLEND_FUNC_ZERO: return VK_BLEND_FACTOR_ZERO; case NVN_BLEND_FUNC_ONE: return VK_BLEND_FACTOR_ONE;
    case NVN_BLEND_FUNC_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR; case NVN_BLEND_FUNC_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case NVN_BLEND_FUNC_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA; case NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case NVN_BLEND_FUNC_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA; case NVN_BLEND_FUNC_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case NVN_BLEND_FUNC_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR; case NVN_BLEND_FUNC_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case NVN_BLEND_FUNC_SRC_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case NVN_BLEND_FUNC_SRC1_COLOR: return VK_BLEND_FACTOR_SRC1_COLOR; case NVN_BLEND_FUNC_ONE_MINUS_SRC1_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case NVN_BLEND_FUNC_SRC1_ALPHA: return VK_BLEND_FACTOR_SRC1_ALPHA; case NVN_BLEND_FUNC_ONE_MINUS_SRC1_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    case NVN_BLEND_FUNC_CONSTANT_COLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR; case NVN_BLEND_FUNC_ONE_MINUS_CONSTANT_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case NVN_BLEND_FUNC_CONSTANT_ALPHA: return VK_BLEND_FACTOR_CONSTANT_ALPHA; case NVN_BLEND_FUNC_ONE_MINUS_CONSTANT_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}
static VkBlendOp blend_op(int e) {
    switch (e) { case NVN_BLEND_EQUATION_SUB: return VK_BLEND_OP_SUBTRACT; case NVN_BLEND_EQUATION_REVERSE_SUB: return VK_BLEND_OP_REVERSE_SUBTRACT;
                 case NVN_BLEND_EQUATION_MIN: return VK_BLEND_OP_MIN; case NVN_BLEND_EQUATION_MAX: return VK_BLEND_OP_MAX; }
    return VK_BLEND_OP_ADD;
}
static VkStencilOp stencil_op(int o) {
    switch (o) { case NVN_STENCIL_OP_ZERO: return VK_STENCIL_OP_ZERO; case NVN_STENCIL_OP_REPLACE: return VK_STENCIL_OP_REPLACE; case NVN_STENCIL_OP_INCR: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                 case NVN_STENCIL_OP_DECR: return VK_STENCIL_OP_DECREMENT_AND_CLAMP; case NVN_STENCIL_OP_INVERT: return VK_STENCIL_OP_INVERT;
                 case NVN_STENCIL_OP_INCR_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP; case NVN_STENCIL_OP_DECR_WRAP: return VK_STENCIL_OP_DECREMENT_AND_WRAP; }
    return VK_STENCIL_OP_KEEP;
}
static int topology(int prim, VkPrimitiveTopology *t) {
    switch (prim) {
    case NVN_DRAW_PRIMITIVE_POINTS: *t = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; return 1; case NVN_DRAW_PRIMITIVE_LINES: *t = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; return 1;
    case NVN_DRAW_PRIMITIVE_LINE_STRIP: *t = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; return 1; case NVN_DRAW_PRIMITIVE_TRIANGLES: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; return 1;
    case NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; return 1; case NVN_DRAW_PRIMITIVE_TRIANGLE_FAN: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; return 1;
    case NVN_DRAW_PRIMITIVE_POLYGON: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; return 1;
    case NVN_DRAW_PRIMITIVE_QUADS: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; return 1;   // drawn through the quad index buffer (nvn_be_draw)
    case NVN_DRAW_PRIMITIVE_QUAD_STRIP: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; return 1;   // the same triangles (GL: a quad strip's vertices as a triangle strip's)
    case NVN_DRAW_PRIMITIVE_LINES_ADJACENCY: *t = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY; return 1;
    case NVN_DRAW_PRIMITIVE_LINE_STRIP_ADJACENCY: *t = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY; return 1;
    case NVN_DRAW_PRIMITIVE_TRIANGLES_ADJACENCY: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY; return 1;
    case NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP_ADJACENCY: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY; return 1;
    }
    return 0;   // line loops, patches: not yet
}
static int topo_class(VkPrimitiveTopology t) { return t == VK_PRIMITIVE_TOPOLOGY_POINT_LIST ? 0 : t <= VK_PRIMITIVE_TOPOLOGY_LINE_STRIP || t == VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY || t == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY ? 1 : 2; }
// A vertex attribute's format: the texture table's, but for what only vertex fetch has (the *_UI2F / *_I2F formats are USCALED / SSCALED, the
// three-component ones exist)
static VkFormat vtx_format(int f) {
    switch (f) {
    case 0x19: return VK_FORMAT_R8G8B8_UNORM; case 0x1A: return VK_FORMAT_R8G8B8_SNORM; case 0x1B: return VK_FORMAT_R8G8B8_UINT; case 0x1C: return VK_FORMAT_R8G8B8_SINT;
    case 0x1D: return VK_FORMAT_R16G16B16_SFLOAT; case 0x1E: return VK_FORMAT_R16G16B16_UNORM; case 0x1F: return VK_FORMAT_R16G16B16_SNORM; case 0x20: return VK_FORMAT_R16G16B16_UINT;
    case 0x21: return VK_FORMAT_R16G16B16_SINT; case 0x22: return VK_FORMAT_R32G32B32_SFLOAT; case 0x23: return VK_FORMAT_R32G32B32_UINT; case 0x24: return VK_FORMAT_R32G32B32_SINT;
    case 0x51: return VK_FORMAT_R8_USCALED; case 0x52: return VK_FORMAT_R8_SSCALED; case 0x53: return VK_FORMAT_R16_USCALED; case 0x54: return VK_FORMAT_R16_SSCALED;
    case 0x57: return VK_FORMAT_R8G8_USCALED; case 0x58: return VK_FORMAT_R8G8_SSCALED; case 0x59: return VK_FORMAT_R16G16_USCALED; case 0x5A: return VK_FORMAT_R16G16_SSCALED;
    case 0x5D: return VK_FORMAT_R8G8B8_USCALED; case 0x5E: return VK_FORMAT_R8G8B8_SSCALED; case 0x5F: return VK_FORMAT_R16G16B16_USCALED; case 0x60: return VK_FORMAT_R16G16B16_SSCALED;
    case 0x63: return VK_FORMAT_R8G8B8A8_USCALED; case 0x64: return VK_FORMAT_R8G8B8A8_SSCALED; case 0x65: return VK_FORMAT_R16G16B16A16_USCALED; case 0x66: return VK_FORMAT_R16G16B16A16_SSCALED;
    case 0x6B: return VK_FORMAT_A2B10G10R10_USCALED_PACK32; case 0x6C: return VK_FORMAT_A2B10G10R10_SSCALED_PACK32;
    }
    VkFormat v; return vk_fmt(f, &v, NULL) ? v : VK_FORMAT_UNDEFINED;
}

// ---------- Programs: the two shaders, their set layout and pipeline layout ----------
typedef struct { const vk_sh *vs, *ps; vk_layout L; int ok, sw; } vk_prog;   // sw: a storage buffer or image written
static nvn_hmap g_progs = {.klen = 16};   // vertex key, pixel key → vk_prog
static vk_prog *prog_of(const prog_t *p) {
    int vi = -1, pi = -1; for (int i = 0; i < (int)p->nstage && i < 6; i++) { if (p->kstage[i] == 0) vi = i; else if (p->kstage[i] == 1) pi = i; }
    if (vi < 0) { VRED("a program without a vertex shader (stages %u): its draws dropped", p->nstage); return NULL; }
    uint8_t k[16] = {0}; memcpy(k, p->key[vi], 8); if (pi >= 0) memcpy(k + 8, p->key[pi], 8);
    vk_prog *g = nvn_hm_get(&g_progs, k); if (g) return g->ok ? g : NULL;
    g = calloc(1, sizeof *g); nvn_hm_put(&g_progs, k, g);
    g->vs = vk_shader(p->key[vi]); g->ps = pi >= 0 ? vk_shader(p->key[pi]) : NULL;
    if (!g->vs || (pi >= 0 && !g->ps)) return NULL;
    const vk_sh *sh[2] = {g->vs, g->ps}; const VkShaderStageFlags st[2] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    for (int i = 0; i < 2; i++) if (sh[i]) for (int b = 0; b < sh[i]->nb; b++) if ((sh[i]->b[b].kind == VK_K_SBUF || sh[i]->b[b].kind == VK_K_IMG) && sh[i]->b[b].rw) g->sw = 1;
    if (!(g->ok = vk_layout_make(&g->L, sh, st, 2))) VLOG("[red] program %016llx / %016llx: no layout", (unsigned long long)g->vs->key, (unsigned long long)(g->ps ? g->ps->key : 0));
    return g->ok ? g : NULL;
}

// ---------- Pipelines ----------
typedef struct {
    const vk_prog *prog; uint32_t cf[8], df, sf; uint8_t n, samples, tclass, poly, blend_en, logic_en, logic_op, depth_clamp; uint32_t chmask; blend_st blend[8];
} pipe_key;
static nvn_hmap g_pipes_h = {.klen = sizeof(pipe_key)};
static VkPipelineCache g_pcache;
static VkPipelineCache pcache(void) {
    if (!g_pcache) { VkPipelineCacheCreateInfo ci = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO}; VK(vkCreatePipelineCache(V.dev, &ci, NULL, &g_pcache)); }
    return g_pcache;
}
static VkPipeline pipeline(const vk_prog *g, int tclass) {
    pipe_key k; memset(&k, 0, sizeof k);
    k.prog = g; k.n = (uint8_t)P.n; for (int i = 0; i < P.n; i++) k.cf[i] = P.cf[i]; k.df = P.df; k.sf = P.sf; k.samples = (uint8_t)P.samples; k.tclass = (uint8_t)tclass;
    k.poly = (uint8_t)S.poly.mode; k.blend_en = (uint8_t)S.color.blend_en; k.logic_en = 0; k.chmask = S.chmask.mask; k.depth_clamp = (uint8_t)!!S.depth_clamp;
    for (int i = 0; i < P.n; i++) if (S.color.blend_en >> i & 1) k.blend[i] = S.blend[i];
    VkPipeline *pp = nvn_hm_get(&g_pipes_h, &k); if (pp) return *pp;
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = g->vs->mod, .pName = "main"},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = g->ps ? g->ps->mod : VK_NULL_HANDLE, .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = tclass == 0 ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : tclass == 1 ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .depthClampEnable = k.depth_clamp,
        .polygonMode = S.poly.mode == NVN_POLYGON_MODE_POINT ? VK_POLYGON_MODE_POINT : S.poly.mode == NVN_POLYGON_MODE_LINE ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = (VkSampleCountFlagBits)(P.samples ? P.samples : 1)};
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState ba[8] = {0};
    for (int i = 0; i < P.n; i++) {
        const blend_st *b = &S.blend[i]; int on = S.color.blend_en >> i & 1;
        ba[i] = (VkPipelineColorBlendAttachmentState){on, blend_factor(b->src_rgb), blend_factor(b->dst_rgb), blend_op(b->eq_rgb), blend_factor(b->src_a), blend_factor(b->dst_a), blend_op(b->eq_a),
                                                     (VkColorComponentFlags)(S.chmask.mask >> (4 * i) & 0xf)};
    }
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = (uint32_t)P.n, .pAttachments = ba};
    VkDynamicState dyn[24]; uint32_t nd = 0;
    VkDynamicState base[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH, VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE, VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
        VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE, VK_DYNAMIC_STATE_VERTEX_INPUT_EXT};
    for (size_t i = 0; i < sizeof base / sizeof *base; i++) dyn[nd++] = base[i];   // (core 1.3, and VK_EXT_vertex_input_dynamic_state: vk_init requires it)
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = nd, .pDynamicStates = dyn};
    VkPipelineRenderingCreateInfo rci = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = (uint32_t)P.n, .pColorAttachmentFormats = (const VkFormat *)P.cf,
                                         .depthAttachmentFormat = P.df, .stencilAttachmentFormat = P.sf};
    VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rci, .stageCount = g->ps ? 2u : 1u, .pStages = st, .pVertexInputState = &vi,
        .pInputAssemblyState = &ia, .pViewportState = &vp, .pRasterizationState = &rs, .pMultisampleState = &ms, .pDepthStencilState = &ds, .pColorBlendState = &cb,
        .pDynamicState = &dy, .layout = g->L.pl};
    VkPipeline p = VK_NULL_HANDLE; uint64_t t0 = now_ns();
    VkResult r = vkCreateGraphicsPipelines(V.dev, pcache(), 1, &ci, NULL, &p);
    if (r != VK_SUCCESS) { VLOG("[red] pipeline for %016llx / %016llx: %d", (unsigned long long)g->vs->key, (unsigned long long)(g->ps ? g->ps->key : 0), (int)r); p = VK_NULL_HANDLE; }
    pipe_key *kk = malloc(sizeof *kk); *kk = k; VkPipeline *pv = malloc(sizeof *pv); *pv = p; nvn_hm_put(&g_pipes_h, kk, pv); free(kk);
    g_pipes++; if (g_pipes <= 4 || !(g_pipes % 100)) VLOG("pipeline %llu made in %.1f ms (%016llx / %016llx)", (unsigned long long)g_pipes, (now_ns() - t0) / 1e6,
                                                         (unsigned long long)g->vs->key, (unsigned long long)(g->ps ? g->ps->key : 0));
    return p;
}

// ---------- Quads' indices ----------
static struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *cpu; } g_quad;   // quad q: 4q + {0, 1, 2, 0, 2, 3}
enum { QUADS_MAX = 65536 };   // quads one draw of them may have (the Metal backend's)
static void draw_init(void) {
    static int done; if (done) return; done = 1;
    vk_bind_init();
    vk_host_buffer(QUADS_MAX * 24, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &g_quad.buf, &g_quad.mem, &g_quad.cpu);
    for (uint32_t q = 0; q < QUADS_MAX; q++) { uint32_t b = 4 * q, v[6] = {b, b + 1, b + 2, b, b + 2, b + 3}; memcpy(g_quad.cpu + 24 * q, v, 24); }
}
// An indexed draw of quads: its indices as triangles' (0, 1, 2, 0, 2, 3 of each four) into the arena, 32-bit; restart indices not looked for
static int quad_indices(const p_draw_idx *di, VkBuffer *buf, VkDeviceSize *off, uint32_t *n) {
    uint32_t nq = (uint32_t)di->count / 4, sz = di->itype == NVN_INDEX_TYPE_UNSIGNED_INT ? 4 : di->itype == NVN_INDEX_TYPE_UNSIGNED_SHORT ? 2 : 1;
    if (!nq || !mtl_pool_has(di->ibuf, (uint64_t)nq * 4 * sz)) return 0;
    uint8_t *cpu; uint64_t ao; void *ab; nvn_be_arena((size_t)nq * 24, &cpu, &ao, &ab);
    const uint8_t *src = (const uint8_t *)(uintptr_t)di->ibuf; uint32_t *d = (uint32_t *)cpu;
    for (uint32_t q = 0; q < nq; q++) {
        uint32_t x[4];
        for (int k = 0; k < 4; k++) { const uint8_t *e = src + (size_t)(4 * q + k) * sz; x[k] = sz == 4 ? *(const uint32_t *)e : sz == 2 ? *(const uint16_t *)e : *e; }
        d[6 * q] = x[0]; d[6 * q + 1] = x[1]; d[6 * q + 2] = x[2]; d[6 * q + 3] = x[0]; d[6 * q + 4] = x[2]; d[6 * q + 5] = x[3];
    }
    *buf = (VkBuffer)(uintptr_t)ab; *off = ao; *n = 6 * nq; return 1;
}

// ---------- The draw ----------
static void dynamic_state(VkCommandBuffer cb, VkPrimitiveTopology topo) {
    float x = 0, y = 0, w = (float)P.w, h = (float)P.h, n = S.dr[0], f = S.dr[1];
    if (S.has_vp) { x = S.vp[0][0]; y = S.vp[0][1]; w = S.vp[0][2]; h = S.vp[0][3]; }
    if (n == 0 && f == 0) f = 1;
    VkViewport vp = {x, y + h, w, -h, n < 0 ? 0 : n > 1 ? 1 : n, f < 0 ? 0 : f > 1 ? 1 : f};   // flipped (see the top)
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = {{0, 0}, {P.w, P.h}};
    if (R.has_sc) { int sx = R.sc.x < 0 ? 0 : R.sc.x, sy = R.sc.y < 0 ? 0 : R.sc.y; int ex = R.sc.x + R.sc.w, ey = R.sc.y + R.sc.h;
        if (ex > (int)P.w) ex = (int)P.w; if (ey > (int)P.h) ey = (int)P.h; sc = (VkRect2D){{sx, sy}, {ex > sx ? (uint32_t)(ex - sx) : 0, ey > sy ? (uint32_t)(ey - sy) : 0}}; }
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdSetLineWidth(cb, 1.0f);
    vkCmdSetDepthBias(cb, S.po.units, S.po.clamp, S.po.factor);
    vkCmdSetBlendConstants(cb, S.blend_color);
    vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_BIT, S.st_vmask[0]); vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_BACK_BIT, S.st_vmask[1]);
    vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_BIT, S.st_wmask[0]); vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_BACK_BIT, S.st_wmask[1]);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_BIT, S.st_ref[0]); vkCmdSetStencilReference(cb, VK_STENCIL_FACE_BACK_BIT, S.st_ref[1]);
    vkCmdSetCullMode(cb, S.poly.cull == NVN_FACE_FRONT ? VK_CULL_MODE_FRONT_BIT : S.poly.cull == NVN_FACE_BACK ? VK_CULL_MODE_BACK_BIT : S.poly.cull == NVN_FACE_FRONT_AND_BACK ? VK_CULL_MODE_FRONT_AND_BACK : VK_CULL_MODE_NONE);
    // the same winding: with the flipped viewport NDC's +y is the framebuffer's top, as in Metal (the Metal backend's CCW → CCW), and Vulkan's
    // area sign counts a triangle counter-clockwise as it is seen that way up
#ifndef DM_TEST_VK_WINDING_SWAPPED   // negative control of t_vk_be only: the winding turned around, as first written
    vkCmdSetFrontFace(cb, S.poly.front == NVN_FRONT_FACE_CCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
#else
    vkCmdSetFrontFace(cb, S.poly.front == NVN_FRONT_FACE_CCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
#endif
    vkCmdSetPrimitiveTopology(cb, topo);
    int has_d = P.df != VK_FORMAT_UNDEFINED, has_s = P.sf != VK_FORMAT_UNDEFINED;
    vkCmdSetDepthTestEnable(cb, has_d && S.ds.dt); vkCmdSetDepthWriteEnable(cb, has_d && S.ds.dw); vkCmdSetDepthCompareOp(cb, vk_compare_op(S.ds.df));
    vkCmdSetStencilTestEnable(cb, has_s && S.ds.st);
    vkCmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_BIT, stencil_op(S.ds.fail_f), stencil_op(S.ds.zpass_f), stencil_op(S.ds.zfail_f), vk_compare_op(S.ds.sf_f));
    vkCmdSetStencilOp(cb, VK_STENCIL_FACE_BACK_BIT, stencil_op(S.ds.fail_b), stencil_op(S.ds.zpass_b), stencil_op(S.ds.zfail_b), vk_compare_op(S.ds.sf_b));
    vkCmdSetDepthBiasEnable(cb, (S.poly.offset_en & 4) != 0);   // the fill mode's bit (POLYGON_OFFSET_ENABLE: point 1, line 2, fill 4)
    vkCmdSetPrimitiveRestartEnable(cb, S.restart_set && S.restart_en && topo_class(topo) != 0 && topo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && topo != VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    vkCmdSetRasterizerDiscardEnable(cb, S.raster_discard != 0);
}
static PFN_vkCmdSetVertexInputEXT g_set_vi;
static int vertex_input(VkCommandBuffer cb, const vk_sh *vs) {
    VkVertexInputBindingDescription2EXT bd[17]; VkVertexInputAttributeDescription2EXT ad[32]; uint32_t nb = 0, na = 0; uint32_t used = 0;
    VkBuffer vb[17]; VkDeviceSize vo[17], vsz[17];
    for (int i = 0; i < vs->nv; i++) {
        int loc = vs->vin[i].loc; VkFormat f = VK_FORMAT_UNDEFINED; uint32_t binding = 16, offset = 0;
        if (loc < S.nattr) {
            va_st a; memcpy(&a, &S.attr[loc], sizeof a); f = vtx_format((int)a.fmt);
            if (f != VK_FORMAT_UNDEFINED && a.stream < 16 && S.vbuf[a.stream].address) { binding = a.stream; offset = a.offset; }
        }
        if (binding == 16) f = vs->vin[i].ct == 0 ? VK_FORMAT_R32G32B32A32_SFLOAT : vs->vin[i].ct == 1 ? VK_FORMAT_R32G32B32A32_UINT : VK_FORMAT_R32G32B32A32_SINT;   // unbound: the zero buffer, stride 0
        ad[na++] = (VkVertexInputAttributeDescription2EXT){VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, .location = (uint32_t)loc, .binding = binding, .format = f, .offset = offset};
        used |= 1u << binding;
    }
    for (uint32_t s = 0; s <= 16; s++) {
        if (!(used >> s & 1)) continue;
        if (s == 16) { bd[nb] = (VkVertexInputBindingDescription2EXT){VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT, .binding = 16, .stride = 0, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX, .divisor = 1};
                       vb[nb] = vk_zero_buffer(); vo[nb] = 0; vsz[nb] = 65536; nb++; continue; }
        uint64_t span; VkBuffer b; VkDeviceSize o;
        if (!vk_addr_span(S.vbuf[s].address, &b, &o, &span)) { VRED("a vertex buffer at %#llx is in no pool: the draw dropped", (unsigned long long)S.vbuf[s].address); return 0; }
        uint32_t div = s < (uint32_t)S.nstream ? S.stream[s].divisor : 0;
        bd[nb] = (VkVertexInputBindingDescription2EXT){VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT, .binding = s, .stride = s < (uint32_t)S.nstream ? S.stream[s].stride : 0,
            .inputRate = div ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX, .divisor = 1};
        if (div > 1) VRED("an instanced vertex stream with divisor %u: read every instance", div);
        vb[nb] = b; vo[nb] = o; vsz[nb] = S.vbuf[s].size && S.vbuf[s].size < span ? S.vbuf[s].size : span; nb++;
    }
    if (!g_set_vi) g_set_vi = (PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(V.dev, "vkCmdSetVertexInputEXT");
    g_set_vi(cb, nb, bd, na, ad);
    for (uint32_t i = 0; i < nb; i++) vkCmdBindVertexBuffers2(cb, bd[i].binding, 1, &vb[i], &vo[i], &vsz[i], NULL);
    return 1;
}
void nvn_be_draw(const p_draw *dw, const p_draw_idx *di) {
    if (exec_draw_dropped()) return;
    draw_init();
    int prim = dw ? dw->prim : di->prim; VkPrimitiveTopology topo;
    if (!topology(prim, &topo)) { VRED("primitive %d (line loops, patches) is not drawn yet", prim); g_dropped++; return; }
    vk_prog *g = prog_of(S.prog); if (!g) { g_dropped++; return; }
    const vk_sh *sh[2] = {g->vs, g->ps}; const int nst[2] = {0, 1};
    upd_guard(sh, nst, g->ps ? 2 : 1, di);
#ifndef DM_TEST_VK_CONT_NO_SPLIT   // negative control of t_vk_be only: a continued pass kept open whatever its draws sample
    if (P.open && (g_rt_merged || P.bar) && samples_attachment(sh, nst, g->ps ? 2 : 1)) {
        static int said; if (!said++) VLOG("a draw samples its pass's own attachment after a barrier or a continuation: the pass ended before it (first case)");
        int pe = vk_pe_push(VK_PE_SAMPLES); vk_pass_end_now(); vk_pe_pop(pe);
    }
#endif
    if (!pass_open()) { g_dropped++; return; }
    P.sw |= g->sw;
    VkPipeline pipe = pipeline(g, topo_class(topo)); if (!pipe) { g_dropped++; return; }
    VkCommandBuffer cb = vk_cb();
    if (!vertex_input(cb, g->vs)) { g_dropped++; return; }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk_bind_resources(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, &g->L, sh, nst, g->ps ? 2 : 1);
    dynamic_state(cb, topo);
    if (dw && prim == NVN_DRAW_PRIMITIVE_QUADS) {   // two triangles a quad, its first vertex the vertex offset
        uint32_t nq = (uint32_t)dw->count / 4; if (nq > QUADS_MAX) { VRED("a draw of more than %d quads: cut there", QUADS_MAX); nq = QUADS_MAX; }
        vkCmdBindIndexBuffer(cb, g_quad.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, 6 * nq, dw->instances ? (uint32_t)dw->instances : 1, 0, dw->first, (uint32_t)dw->base_instance);
    } else if (dw) vkCmdDraw(cb, (uint32_t)dw->count, dw->instances ? (uint32_t)dw->instances : 1, (uint32_t)dw->first, (uint32_t)dw->base_instance);
    else if (prim == NVN_DRAW_PRIMITIVE_QUADS) {   // its indices made triangles'
        VkBuffer ib; VkDeviceSize io; uint32_t n;
        if (!quad_indices(di, &ib, &io, &n)) { VRED("indexed quads whose indices are in no pool: dropped"); g_dropped++; return; }
        vkCmdBindIndexBuffer(cb, ib, io, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, n, di->instances ? (uint32_t)di->instances : 1, 0, di->base_vertex, (uint32_t)di->base_instance);
    } else {
        VkBuffer ib; VkDeviceSize io; uint64_t span;
        if (!vk_addr_span(di->ibuf, &ib, &io, &span)) { VRED("an index buffer at %#llx is in no pool: dropped", (unsigned long long)di->ibuf); g_dropped++; return; }
        VkIndexType it = di->itype == NVN_INDEX_TYPE_UNSIGNED_INT ? VK_INDEX_TYPE_UINT32 : di->itype == NVN_INDEX_TYPE_UNSIGNED_SHORT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT8_EXT;
        if (it == VK_INDEX_TYPE_UINT8_EXT && !V.idx8) { VRED("8-bit indices without VK_EXT_index_type_uint8: dropped"); g_dropped++; return; }
        vkCmdBindIndexBuffer(cb, ib, io, it);
        vkCmdDrawIndexed(cb, (uint32_t)di->count, di->instances ? (uint32_t)di->instances : 1, 0, di->base_vertex, (uint32_t)di->base_instance);
    }
    g_draws++; P.draws++;
    if (g_draws == 1) VLOG("first draw: %d attachments %ux%u, program %016llx / %016llx", P.n, P.w, P.h, (unsigned long long)g->vs->key, (unsigned long long)(g->ps ? g->ps->key : 0));
}

// ---------- Dispatches ----------
typedef struct { const vk_sh *cs; vk_layout L; VkPipeline pipe; int ok; } vk_cprog;
static nvn_hmap g_cprogs = {.klen = 8};   // compute key → vk_cprog
static vk_cprog *cprog_of(const prog_t *p) {
    int si = -1; for (int i = 0; i < (int)p->nstage && i < 6; i++) if (p->kstage[i] == 5) si = i;
    if (si < 0) { VRED("a dispatched program has no compute shader (stages %u): dropped", p->nstage); return NULL; }
    vk_cprog *g = nvn_hm_get(&g_cprogs, p->key[si]); if (g) return g->ok ? g : NULL;
    g = calloc(1, sizeof *g); nvn_hm_put(&g_cprogs, p->key[si], g);
    if (!(g->cs = vk_shader(p->key[si]))) return NULL;
    const VkShaderStageFlags st = VK_SHADER_STAGE_COMPUTE_BIT;
    if (!vk_layout_make(&g->L, &g->cs, &st, 1)) { VLOG("[red] compute program %016llx: no layout", (unsigned long long)g->cs->key); return NULL; }
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = g->cs->mod, .pName = "main"}, .layout = g->L.pl};
    VkResult r = vkCreateComputePipelines(V.dev, pcache(), 1, &ci, NULL, &g->pipe);
    if (r != VK_SUCCESS) { VLOG("[red] compute pipeline for %016llx: %d", (unsigned long long)g->cs->key, (int)r); return NULL; }
    g_pipes++; g->ok = 1; return g;
}
// Outside any pass; the c0 bank carries the workgroup counts (vk_bind_grid); a full barrier after it, as after copies
void nvn_be_dispatch(const p_dispatch *d) {
    draw_init();
    if (!S.prog) { VRED("a dispatch with no program bound: dropped"); g_dropped++; return; }
    vk_cprog *g = cprog_of(S.prog); if (!g) { g_dropped++; return; }
    if (d->x <= 0 || d->y <= 0 || d->z <= 0) return;   // no workgroups: nothing runs
    vk_upd_flush(); vk_pass_end_now();   // (compute reads uniforms from memory: renamed updates are written first)
    VkCommandBuffer cb = vk_cb(); const int nst = 5;
    vk_bind_grid((uint32_t)d->x, (uint32_t)d->y, (uint32_t)d->z);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe);
    vk_bind_resources(cb, VK_PIPELINE_BIND_POINT_COMPUTE, &g->L, &g->cs, &nst, 1);
    vkCmdDispatch(cb, (uint32_t)d->x, (uint32_t)d->y, (uint32_t)d->z);
    vk_barrier_all(cb);
    if (!g_dispatches++) VLOG("first dispatch: %016llx, %d x %d x %d groups", (unsigned long long)g->cs->key, d->x, d->y, d->z);
}
void vk_stats_line(void) {
    vk_tex_counts c; vk_tex_stats(&c);
    VLOG("so far: %llu draws and %llu dispatches (%llu dropped), %llu passes, %llu pipelines; textures sampled as zeros %llu times; images %llu alive (%.0f MB) of "
         "%llu made, %llu uploads (%.0f MB), %llu again after CPU writes",
         (unsigned long long)g_draws, (unsigned long long)g_dispatches, (unsigned long long)g_dropped, (unsigned long long)g_passes, (unsigned long long)g_pipes,
         (unsigned long long)vk_bind_zeros(), (unsigned long long)c.live, c.live_bytes / 1e6, (unsigned long long)c.made, (unsigned long long)c.up_n, c.up_bytes / 1e6,
         (unsigned long long)c.reup_n);
    const uint64_t *e = g_pe;
    VLOG("passes ended by: SetRenderTargets %llu (the same textures %llu), clears %llu, copies %llu, dispatches %llu, barriers %llu, uniform updates "
         "%llu, renamed updates written %llu, sampling the attachment %llu, submissions %llu, other %llu", (unsigned long long)e[VK_PE_RT],
         (unsigned long long)e[VK_PE_RT_SAME], (unsigned long long)e[VK_PE_CLEAR], (unsigned long long)e[VK_PE_COPY], (unsigned long long)e[VK_PE_DISPATCH],
         (unsigned long long)e[VK_PE_BARRIER], (unsigned long long)e[VK_PE_UB], (unsigned long long)e[VK_PE_UPD], (unsigned long long)e[VK_PE_SAMPLES],
         (unsigned long long)e[VK_PE_SUBMIT], (unsigned long long)e[VK_PE_OTHER]);
    vk_ptime_line(600);   // (called every 600 presents: vk_present.c)
}
