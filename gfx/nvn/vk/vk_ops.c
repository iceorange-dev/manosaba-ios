// gfx/nvn/vk/vk_ops.c: the execution interface's copies, clears and passes (plan item 301e), and what the replay core asks of a backend besides.
// The replay keeps its plainest form here: the TBDR rewrites (kept and merged passes, load elision, the attachment ledger; core/tbdr.h) are off, as
// with DM_NVN_TBDR=0, until the Vulkan backend does what they need of it. Every copy and clear has a full barrier before and after it (all earlier
// work before, all later after: correct first, narrower barriers with 301f). Clears waiting in R (the replay's pending clears) are landed when the
// replay ends the pass that would have taken them as its load action; with no draws yet (301e2) that is every clear. Draws and dispatches are
// counted and dropped until the shaders come.
#include "vk.h"
#include "../nvn_copy_pitch.h"
#include "../core/state.h"
#include "../core/tbdr.h"
#include "../core/pend.h"
#include "../core/usnap.h"
#include "../core/fuse.h"
#include "../core/store_index.h"
#include <string.h>

static uint64_t g_drop_draws, g_drop_disp;
static VkClearColorValue color_of(VkFormat f, const nvn_rgba *c) {
    VkClearColorValue v; int ints = 0;
    switch (f) { case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SINT: case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT: case VK_FORMAT_R16G16B16A16_UINT: case VK_FORMAT_R16G16B16A16_SINT: case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT: case VK_FORMAT_A2B10G10R10_UINT_PACK32: ints = 1; break; default: break; }
    if (ints) { v.int32[0] = (int32_t)c->red; v.int32[1] = (int32_t)c->green; v.int32[2] = (int32_t)c->blue; v.int32[3] = (int32_t)c->alpha; }
    else { v.float32[0] = (float)c->red; v.float32[1] = (float)c->green; v.float32[2] = (float)c->blue; v.float32[3] = (float)c->alpha; }
    return v;
}
static VkImageSubresourceRange range_of(const vk_img *im, const view_t *v, int has_v, VkImageAspectFlags asp) {
    VkImageSubresourceRange r = {asp, 0, 1, 0, im->layers};
    if (has_v && v) {
        if (v->set_levels) r.baseMipLevel = v->base_level < im->levels ? v->base_level : im->levels - 1;
        if (v->set_layers) { r.baseArrayLayer = v->min_layer < im->layers ? v->min_layer : im->layers - 1; r.layerCount = v->nlayers ? v->nlayers : im->layers - r.baseArrayLayer;
                             if (r.baseArrayLayer + r.layerCount > im->layers) r.layerCount = im->layers - r.baseArrayLayer; }
    }
    return r;
}
static int compressed(const vk_img *im) { int bw, bh; nvn_fmt_bpb(im->t.d.format, &bw, &bh); return bw > 1 || bh > 1; }

// ---------- Passes: none is open until draws come (301e2); pending clears land when the replay ends a pass ----------
static void land_clears(void) {
    int any = R.clear_d || R.clear_s; for (int i = 0; i < 8; i++) any |= R.clear_c[i];
    if (!any || !R.has_rt) { memset(R.clear_c, 0, sizeof R.clear_c); R.clear_d = R.clear_s = 0; return; }
    VkCommandBuffer cb = vk_cb();
    for (int i = 0; i < R.rt.n && i < 8; i++) {
        if (!R.clear_c[i] || !R.rt.color[i]) continue;
        vk_img *im = vk_tex(R.rt.color[i]); if (!im || compressed(im)) continue;
        VkImageSubresourceRange rg = range_of(im, &R.rt.cview[i], R.rt.has_cview[i], VK_IMAGE_ASPECT_COLOR_BIT);
        VkClearColorValue c = color_of(im->fmt, &R.cc[i]);
        vkCmdClearColorImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, &c, 1, &rg);
        im->gpu_written = 1; im->uploaded = 1;
    }
    if ((R.clear_d || R.clear_s) && R.rt.depth) {
        vk_img *im = vk_tex(R.rt.depth);
        if (im) {
            VkImageAspectFlags a = (R.clear_d ? im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT : 0) | (R.clear_s ? im->aspect & VK_IMAGE_ASPECT_STENCIL_BIT : 0);
            if (a) { VkImageSubresourceRange rg = range_of(im, &R.rt.dview, R.rt.has_dview, a);
                VkClearDepthStencilValue c = {(float)R.cd, R.cs};
                vkCmdClearDepthStencilImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, &c, 1, &rg); im->gpu_written = 1; im->uploaded = 1; }
        }
    }
    vk_barrier_all(cb);
    memset(R.clear_c, 0, sizeof R.clear_c); R.clear_d = R.clear_s = 0;
}
int nvn_be_pass_mode(void) { return -1; }
void nvn_be_pass_flush(void) { land_clears(); }
void nvn_be_pass_end(void) {}
void vk_pass_end_now(void) { land_clears(); }
int nvn_be_layered(void) { return 0; }
int nvn_be_layer_continue(const p_set_rt *r) { (void)r; return 0; }

// A clear of a part (a scissor short of the target, a channel mask): in a pass of its own, vkCmdClearAttachments over the scissor's rectangle.
// A partial channel mask clears all four channels (vkCmdClearAttachments has no mask: drawn with the pipelines, 301e2)
static void clear_rect(vk_img *im, VkImageView view, VkImageAspectFlags asp, const VkClearValue *cv, VkRect2D rect, uint32_t layers) {
    VkCommandBuffer cb = vk_cb();
    VkRenderingAttachmentInfo att = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    int color = asp == VK_IMAGE_ASPECT_COLOR_BIT;
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {im->w, im->h}}, .layerCount = layers ? layers : 1,
        .colorAttachmentCount = color ? 1 : 0, .pColorAttachments = color ? &att : NULL,
        .pDepthAttachment = !color && (im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? &att : NULL, .pStencilAttachment = !color && (im->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ? &att : NULL};
    vkCmdBeginRendering(cb, &ri);
    VkClearAttachment ca = {asp, 0, *cv}; VkClearRect cr = {rect, 0, layers ? layers : 1};
    vkCmdClearAttachments(cb, 1, &ca, 1, &cr);
    vkCmdEndRendering(cb); vk_barrier_all(cb);
    im->gpu_written = 1; im->uploaded = 1;
}
static VkRect2D scissor_rect(const vk_img *im) {
    VkRect2D r = {{0, 0}, {im->w, im->h}};
    if (R.has_sc) { int x = R.sc.x < 0 ? 0 : R.sc.x, y = R.sc.y < 0 ? 0 : R.sc.y; r.offset = (VkOffset2D){x, y};
        r.extent.width = (uint32_t)(R.sc.x + R.sc.w > (int)im->w ? (int)im->w - x : R.sc.x + R.sc.w - x);
        r.extent.height = (uint32_t)(R.sc.y + R.sc.h > (int)im->h ? (int)im->h - y : R.sc.y + R.sc.h - y); }
    return r;
}
void nvn_be_clear_quad(const p_clear_color *c) {
    if (c->index < 0 || c->index >= R.rt.n || !R.rt.color[c->index]) return;
    vk_img *im = vk_tex(R.rt.color[c->index]); if (!im || compressed(im)) return;
    vk_tex_ready(im, vk_cb());
    if (c->mask != 0xf) VRED("a colour clear with channel mask %#x clears all channels until the pipelines come (301e2)", c->mask);
    VkRect2D r = scissor_rect(im); if (!r.extent.width || !r.extent.height) return;
    nvn_rgba rgba = nvn_rgba_make(c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3]);
    VkClearValue cv = {.color = color_of(im->fmt, &rgba)};
    VkImageSubresourceRange rg = range_of(im, &R.rt.cview[c->index], R.rt.has_cview[c->index], VK_IMAGE_ASPECT_COLOR_BIT);
    clear_rect(im, vk_tex_view(im, &R.rt.cview[c->index], R.rt.has_cview[c->index], 1), VK_IMAGE_ASPECT_COLOR_BIT, &cv, r, rg.layerCount);
}
void nvn_be_clear_ds_quad(const p_clear_ds *c) {
    if (!R.rt.depth) return;
    vk_img *im = vk_tex(R.rt.depth); if (!im) return;
    vk_tex_ready(im, vk_cb());
    VkImageAspectFlags a = (c->depth_mask ? im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT : 0) | ((c->stencil_mask & 255) ? im->aspect & VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    if (!a) return;
    if ((c->stencil_mask & 255) && (c->stencil_mask & 255) != 255) VRED("a stencil clear with mask %#x clears every bit until the pipelines come (301e2)", c->stencil_mask & 255);
    VkRect2D r = scissor_rect(im); if (!r.extent.width || !r.extent.height) return;
    VkClearValue cv = {.depthStencil = {c->depth, (uint32_t)c->stencil}};
    VkImageSubresourceRange rg = range_of(im, &R.rt.dview, R.rt.has_dview, a);
    clear_rect(im, vk_tex_view(im, &R.rt.dview, R.rt.has_dview, 1), a, &cv, r, rg.layerCount);
}
void nvn_be_clear_tex(const p_clear_tex *c) {
    vk_img *im = vk_tex(c->tex); if (!im || compressed(im)) return;
    VkCommandBuffer cb = vk_cb(); vk_tex_ready(im, cb);
    unsigned level = c->has_view && c->view.set_levels ? c->view.base_level : 0;
    uint32_t lw = im->w >> level ? im->w >> level : 1, lh = im->h >> level ? im->h >> level : 1;
    int whole = c->r.xoffset == 0 && c->r.yoffset == 0 && (uint32_t)c->r.width >= lw && (uint32_t)c->r.height >= lh;
    VkClearColorValue v;
    if (c->is_uint) memcpy(v.uint32, c->c.u, sizeof v.uint32); else memcpy(v.float32, c->c.f, sizeof v.float32);
    if (c->mask != 0xf) VRED("ClearTexture with channel mask %#x clears all channels until the pipelines come (301e2)", c->mask);
    VkImageSubresourceRange rg = range_of(im, &c->view, c->has_view, VK_IMAGE_ASPECT_COLOR_BIT);
    if (im->aspect != VK_IMAGE_ASPECT_COLOR_BIT) { VRED("ClearTexture of a depth / stencil texture (format %#x): not done", im->t.d.format); return; }
    if (c->r.depth > 0 && im->type != VK_IMAGE_TYPE_3D) { rg.baseArrayLayer += (uint32_t)c->r.zoffset; rg.layerCount = (uint32_t)c->r.depth; }
    if (whole) { vkCmdClearColorImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &rg); vk_barrier_all(cb); im->gpu_written = 1; im->uploaded = 1; return; }
    VkClearValue cv = {.color = v};
    clear_rect(im, vk_tex_view(im, &c->view, c->has_view, 1), VK_IMAGE_ASPECT_COLOR_BIT, &cv, (VkRect2D){{c->r.xoffset, c->r.yoffset}, {(uint32_t)c->r.width, (uint32_t)c->r.height}}, rg.layerCount);
}

// ---------- Copies ----------
void nvn_be_copy_b2b(uint64_t src, uint64_t dst, uint64_t n) {
    if (!n) return;
    pend_add(dst, n, NULL, src, vk_cur_fence()); us_drop_range(dst, n, vk_cur_fence());
    VkBuffer sb, db; VkDeviceSize so, dof;
    if (!vk_addr(src, n, &sb, &so) || !vk_addr(dst, n, &db, &dof)) VDIE("buffer copy outside the memory pools: %#llx → %#llx, %llu bytes", (unsigned long long)src, (unsigned long long)dst, (unsigned long long)n);
    VkCommandBuffer cb = vk_cb(); VkBufferCopy rg = {so, dof, n};
    vkCmdCopyBuffer(cb, sb, db, 1, &rg); vk_barrier_all(cb);
}
// Inline uniform update: the bytes copied into the arena now (the game may reuse its command memory once the submission is in), then in GPU order to
// the destination
void nvn_be_update_ub(const void *data, uint64_t dst, uint64_t n) {
    if (!n) return;
    VkBuffer db; VkDeviceSize dof;
    if (!vk_addr(dst, n, &db, &dof)) VDIE("uniform update outside the memory pools: %#llx, %llu bytes", (unsigned long long)dst, (unsigned long long)n);
    uint8_t *cpu; uint64_t off; void *ab; nvn_be_arena((size_t)n, &cpu, &off, &ab); memcpy(cpu, data, n);
    us_drop_range(dst, n, vk_cur_fence()); pend_add(dst, n, cpu, 0, vk_cur_fence());
    VkCommandBuffer cb = vk_cb(); VkBufferCopy rg = {off, dof, n};
    vkCmdCopyBuffer(cb, (VkBuffer)(uintptr_t)ab, db, 1, &rg); vk_barrier_all(cb);
}
static int region_of(const p_copy_b2t *c, vk_img *im, VkBufferImageCopy *rg, VkDeviceSize *extent) {
    int bw, bh, bpb = nvn_fmt_bpb(c->tex->d.format, &bw, &bh);
    size_t row, img, ext;
    if (!bpb || !nvn_copy_pitch(c->r.width, c->r.height, c->r.depth, bw, bh, bpb, S.copy_stride, S.copy_image_stride, &row, &img, &ext)) return 0;
    if (row % (size_t)bpb) return 0;
    unsigned level = c->has_view && c->view.set_levels ? c->view.base_level : 0, layer0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    int is3d = im->type == VK_IMAGE_TYPE_3D; uint32_t nz = c->r.depth > 0 ? (uint32_t)c->r.depth : 1;
    VkImageAspectFlags asp = im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : im->aspect;
    *rg = (VkBufferImageCopy){0, (uint32_t)(row / bpb * bw), (uint32_t)((img / row) * bh),
        {asp, level, is3d ? 0 : layer0 + (uint32_t)c->r.zoffset, is3d ? 1 : nz}, {c->r.xoffset, c->r.yoffset, is3d ? c->r.zoffset : 0}, {(uint32_t)c->r.width, (uint32_t)c->r.height, is3d ? nz : 1}};
    *extent = ext; return 1;
}
void nvn_be_copy_b2t(const p_copy_b2t *c) {
    vk_img *im = vk_tex(c->tex); if (!im) return;
    VkBufferImageCopy rg; VkDeviceSize ext;
    if (!region_of(c, im, &rg, &ext)) { VRED("buffer→texture copy with strides it cannot take (row %lld, image %lld)", (long long)S.copy_stride, (long long)S.copy_image_stride); return; }
    VkBuffer sb; VkDeviceSize so;
    if (!vk_addr(c->src, ext, &sb, &so)) { VRED("buffer→texture copy source outside the memory pools"); return; }
    VkCommandBuffer cb = vk_cb(); vk_tex_ready(im, cb);
    rg.bufferOffset = so;
    vkCmdCopyBufferToImage(cb, sb, im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &rg); vk_barrier_all(cb);
    im->gpu_written = 1; im->uploaded = 1;
}
void nvn_be_copy_t2b(const p_copy_b2t *c) {
    vk_img *im = vk_tex(c->tex); if (!im) return;
    VkBufferImageCopy rg; VkDeviceSize ext;
    if (!region_of(c, im, &rg, &ext)) { VRED("texture→buffer copy with strides it cannot take (row %lld, image %lld)", (long long)S.copy_stride, (long long)S.copy_image_stride); return; }
    VkBuffer db; VkDeviceSize dof;
    if (!vk_addr(c->src, ext, &db, &dof)) { VRED("texture→buffer copy destination outside the memory pools"); return; }
    us_drop_range(c->src, ext, vk_cur_fence());
    VkCommandBuffer cb = vk_cb(); vk_tex_ready(im, cb);
    rg.bufferOffset = dof;
    vkCmdCopyImageToBuffer(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, db, 1, &rg); vk_barrier_all(cb);
}
void nvn_be_copy_t2t(const p_copy_t2t *c) {
    vk_img *s = vk_tex(c->src), *d = vk_tex(c->dst); if (!s || !d) return;
    VkCommandBuffer cb = vk_cb(); vk_tex_ready(s, cb); vk_tex_ready(d, cb);
    unsigned sl = c->has_sv && c->sv.set_levels ? c->sv.base_level : 0, dl = c->has_dv && c->dv.set_levels ? c->dv.base_level : 0;
    unsigned sy = c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0, dy = c->has_dv && c->dv.set_layers ? c->dv.min_layer : 0;
    int s3 = s->type == VK_IMAGE_TYPE_3D, d3 = d->type == VK_IMAGE_TYPE_3D;
    uint32_t snz = c->sr.depth > 0 ? (uint32_t)c->sr.depth : 1, dnz = c->dr.depth > 0 ? (uint32_t)c->dr.depth : 1;
    VkImageAspectFlags sa = s->aspect, da = d->aspect;
    int same = c->sr.width == c->dr.width && c->sr.height == c->dr.height && snz == dnz && !(c->flags & (NVN_COPY_FLAGS_MIRROR_X_BIT | NVN_COPY_FLAGS_MIRROR_Y_BIT | NVN_COPY_FLAGS_MIRROR_Z_BIT));
    if (same && (s->fmt == d->fmt || (!compressed(s) && !compressed(d)))) {
        VkImageCopy rg = {{sa, sl, s3 ? 0 : sy + (uint32_t)c->sr.zoffset, s3 ? 1 : snz}, {c->sr.xoffset, c->sr.yoffset, s3 ? c->sr.zoffset : 0},
                          {da, dl, d3 ? 0 : dy + (uint32_t)c->dr.zoffset, d3 ? 1 : dnz}, {c->dr.xoffset, c->dr.yoffset, d3 ? c->dr.zoffset : 0},
                          {(uint32_t)c->sr.width, (uint32_t)c->sr.height, s3 ? snz : 1}};
        if (s->fmt == d->fmt) { vkCmdCopyImage(cb, s->img, VK_IMAGE_LAYOUT_GENERAL, d->img, VK_IMAGE_LAYOUT_GENERAL, 1, &rg); vk_barrier_all(cb); d->gpu_written = 1; d->uploaded = 1; return; }
    }
    if (compressed(s) || compressed(d) || sa != VK_IMAGE_ASPECT_COLOR_BIT || da != VK_IMAGE_ASPECT_COLOR_BIT) {
        VRED("texture→texture copy %dx%d→%dx%d of formats %#x→%#x: not done (no blit between them)", c->sr.width, c->sr.height, c->dr.width, c->dr.height, c->src->d.format, c->dst->d.format); return; }
    // scaled, mirrored or converting: a blit (mirroring by swapped corners)
    int mx = (c->flags & NVN_COPY_FLAGS_MIRROR_X_BIT) != 0, my = (c->flags & NVN_COPY_FLAGS_MIRROR_Y_BIT) != 0;
    VkImageBlit bl = {{sa, sl, s3 ? 0 : sy + (uint32_t)c->sr.zoffset, s3 ? 1 : snz}, {{c->sr.xoffset, c->sr.yoffset, s3 ? c->sr.zoffset : 0}, {c->sr.xoffset + c->sr.width, c->sr.yoffset + c->sr.height, s3 ? c->sr.zoffset + (int)snz : 1}},
                      {da, dl, d3 ? 0 : dy + (uint32_t)c->dr.zoffset, d3 ? 1 : dnz}, {{c->dr.xoffset + (mx ? c->dr.width : 0), c->dr.yoffset + (my ? c->dr.height : 0), d3 ? c->dr.zoffset : 0},
                                                                                       {c->dr.xoffset + (mx ? 0 : c->dr.width), c->dr.yoffset + (my ? 0 : c->dr.height), d3 ? c->dr.zoffset + (int)dnz : 1}}};
    if (!s3 && !d3 && snz != dnz) bl.dstSubresource.layerCount = bl.srcSubresource.layerCount = snz < dnz ? snz : dnz;
    vkCmdBlitImage(cb, s->img, VK_IMAGE_LAYOUT_GENERAL, d->img, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, (c->flags & NVN_COPY_FLAGS_LINEAR_FILTER_BIT) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    vk_barrier_all(cb); d->gpu_written = 1; d->uploaded = 1;
}
void nvn_be_barrier(const uint8_t *p, const void *a) { (void)p; (void)a; vk_barrier_all(vk_cb()); }
void nvn_be_set_pool(const uint8_t *p, const void *a) { (void)p; (void)a; }

// ---------- State, draws and dispatches: kept in S by the core; drawn with the shaders (301e2) ----------
void nvn_be_reset(void) { g_tbdr = 0; }
void nvn_be_state(uint32_t op, const void *a, int c) { (void)op; (void)a; (void)c; }
void nvn_be_draw(const p_draw *dw, const p_draw_idx *di) {
    (void)dw; (void)di;
    if (!g_drop_draws++) VLOG("[red] draws are not drawn yet (plan item 301e2): the clears, copies and presents run");
}
void nvn_be_dispatch(const p_dispatch *d) { (void)d; if (!g_drop_disp++) VLOG("[red] dispatches are not run yet (plan item 301e2)"); }

// ---------- What the core asks of a backend besides ----------
void nvn_be_parallel(int n, void (*fn)(void *ctx, size_t i), void *ctx) { for (int i = 0; i < n; i++) fn(ctx, (size_t)i); }
int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]) { (void)p; (void)rf; (void)stage; return 0; }
void nvn_be_bind_sers(const p_bind_handles *b, int n, uint64_t *ser) { for (int k = 0; k < n; k++) ser[k] = b->h[k]; }
uint64_t nvn_be_handle_ser(uint64_t h) { return h; }
int nvn_be_handle_view(uint64_t h, uint64_t *ser, view_t *v, int *has_v) { (void)h; (void)ser; (void)v; (void)has_v; return 0; }
void nvn_be_fuse_cover(const p_set_rt *r2, const uint8_t *first, const int8_t *p2map, const uint8_t *clr, int n1, int n) { (void)r2; (void)first; (void)p2map; (void)clr; (void)n1; (void)n; }
void vk_drop_stats(uint64_t *draws, uint64_t *disp) { *draws = g_drop_draws; *disp = g_drop_disp; }
