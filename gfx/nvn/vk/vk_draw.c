// gfx/nvn/vk/vk_draw.c: draws (plan item 301e2). A draw opens the pass when none is open: dynamic rendering into R's targets, R's pending clears
// as load actions (a CLEAR load), every attachment stored; the pass stays open for the draws after it until the replay ends it (new targets, a
// copy, a barrier, the submission's end). Its pipeline is made from the program's two shaders, the pass's attachment formats and the state that is
// not dynamic (blend, channel masks, polygon mode, the primitive class, multisampling), hashed and kept; everything else is dynamic state set at
// each draw from S: viewport (flipped: the translation keeps NVN's clip space, whose y points up, and Vulkan's framebuffer y points down),
// scissor, depth bias, blend constants, stencil, cull mode and front face, topology, depth test / write / compare, stencil test and ops, primitive
// restart, rasterizer discard, and the vertex input (VK_EXT_vertex_input_dynamic_state) from S's attributes and streams. The program's resources
// are pushed (VK_KHR_push_descriptor; one set for both stages, the bundle moved the pixel shader's bindings by 64):
//   uniform slot r       S.ubo[stage][r] (the core's snapshot of it when there is one, usnap.h), 64 KB or to its pool's end (the shaders declare
//                        64 KB blocks; PKShield found games reading past the bound size)
//   support buffer       Ryujinx's, static: render scale 1
//   c1                   the shader's own bytes, in a bank of them (64 KB of zeros after the last)
//   c0 / c2              the driver banks as the Metal backend fills them (drv_fill: storage buffer table, compute grid, point size; the c2 handle
//                        tables), per draw into the arena
//   storage slot r       S.ssbo[stage][r]
//   texture slot r       S.tex[stage][r]: texture ID (low 20 bits) and sampler ID (high 12, or a separate sampler slot's), the pools' entries; its
//                        image uploaded first (a command buffer of its own), viewed as the shader declares it
// What is not there yet reads zeros (a red line once): images, texel buffers, textures sampled through image slots.
#include "vk.h"
#include "vk_shader.h"
#include "../nvn_hmap.h"
#include "../core/exec.h"
#include "../core/state.h"
#include "../core/usnap.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int exec_draw_dropped(void);   // core/exec.c: rendering off, no program, empty depth bounds
static uint64_t g_draws, g_dropped, g_pipes, g_passes, g_dummies;

// ---------- The pass ----------
static struct { int open, n; vk_img *col[8]; vk_img *ds; VkFormat cf[8], df, sf; uint32_t w, h, samples; } P;
int vk_pass_is_open(void) { return P.open; }
int nvn_be_pass_mode(void) { return P.open ? 0 : -1; }
int vk_pass_attach_index(const vk_img *im) { if (!P.open) return -1; for (int i = 0; i < P.n; i++) if (P.col[i] == im) return i; return P.ds == im ? -2 : -1; }
void vk_pass_end_now(void) {
    if (P.open) { VkCommandBuffer cb = vk_cb(); vkCmdEndRendering(cb); vk_barrier_all(cb); P.open = 0; }
    vk_land_clears();
}
void nvn_be_pass_flush(void) { vk_pass_end_now(); }
void nvn_be_pass_end(void) { if (P.open) { VkCommandBuffer cb = vk_cb(); vkCmdEndRendering(cb); vk_barrier_all(cb); P.open = 0; } }
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
    vkCmdBeginRendering(vk_cb(), &ri);
    int partial_ds = R.clear_d != R.clear_s && P.df && P.sf;
    memset(R.clear_c, 0, sizeof R.clear_c);
    if (!partial_ds) R.clear_d = R.clear_s = 0;
    P.open = 1; P.w = w; P.h = h; g_passes++;
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
static VkCompareOp compare_op(int f) { return f >= 1 && f <= 8 ? (VkCompareOp)(f - 1) : VK_COMPARE_OP_ALWAYS; }   // NVN NEVER..ALWAYS = 1..8, Vulkan's 0..7 in the same order
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
    case NVN_DRAW_PRIMITIVE_LINES_ADJACENCY: *t = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY; return 1;
    case NVN_DRAW_PRIMITIVE_LINE_STRIP_ADJACENCY: *t = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY; return 1;
    case NVN_DRAW_PRIMITIVE_TRIANGLES_ADJACENCY: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY; return 1;
    case NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP_ADJACENCY: *t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY; return 1;
    }
    return 0;   // quad strips, line loops, patches: not yet
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
typedef struct { const vk_sh *vs, *ps; VkDescriptorSetLayout sl; VkPipelineLayout pl; int push; } vk_prog;
static nvn_hmap g_progs = {.klen = 16};   // vertex key, pixel key → vk_prog
static vk_prog *prog_of(const prog_t *p) {
    int vi = -1, pi = -1; for (int i = 0; i < (int)p->nstage && i < 6; i++) { if (p->kstage[i] == 0) vi = i; else if (p->kstage[i] == 1) pi = i; }
    if (vi < 0) { VRED("a program without a vertex shader (stages %u): its draws dropped", p->nstage); return NULL; }
    uint8_t k[16] = {0}; memcpy(k, p->key[vi], 8); if (pi >= 0) memcpy(k + 8, p->key[pi], 8);
    vk_prog *g = nvn_hm_get(&g_progs, k); if (g) return g->pl ? g : NULL;
    g = calloc(1, sizeof *g); nvn_hm_put(&g_progs, k, g);
    g->vs = vk_shader(p->key[vi]); g->ps = pi >= 0 ? vk_shader(p->key[pi]) : NULL;
    if (!g->vs || (pi >= 0 && !g->ps)) return NULL;
    VkDescriptorSetLayoutBinding lb[2 * VK_SH_MAXB]; uint32_t nlb = 0, ndesc = 0;
    const vk_sh *sh[2] = {g->vs, g->ps};
    for (int s = 0; s < 2; s++) if (sh[s]) for (int i = 0; i < sh[s]->nb; i++) {
        const vk_bind *b = &sh[s]->b[i];
        lb[nlb++] = (VkDescriptorSetLayoutBinding){b->binding, (VkDescriptorType)b->dtype, b->count ? b->count : 1, s ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT, NULL};
        ndesc += b->count ? b->count : 1;
    }
    g->push = V.push_ds && ndesc <= V.max_push_ds;
    if (!g->push) { VLOG("[red] program %016llx / %016llx: %u descriptors, more than pushed ones can be (%u): its draws dropped for now",
                         (unsigned long long)g->vs->key, (unsigned long long)(g->ps ? g->ps->key : 0), ndesc, V.max_push_ds); return NULL; }
    VkDescriptorSetLayoutCreateInfo sci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR, .bindingCount = nlb, .pBindings = lb};
    VK(vkCreateDescriptorSetLayout(V.dev, &sci, NULL, &g->sl));
    VkPipelineLayoutCreateInfo pci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &g->sl};
    VK(vkCreatePipelineLayout(V.dev, &pci, NULL, &g->pl));
    return g;
}

// ---------- Pipelines ----------
typedef struct {
    const vk_prog *prog; uint32_t cf[8], df, sf; uint8_t n, samples, tclass, poly, blend_en, logic_en, logic_op, depth_clamp; uint32_t chmask; blend_st blend[8];
} pipe_key;
static nvn_hmap g_pipes_h = {.klen = sizeof(pipe_key)};
static VkPipelineCache g_pcache;
static VkPipeline pipeline(const vk_prog *g, int tclass) {
    pipe_key k; memset(&k, 0, sizeof k);
    k.prog = g; k.n = (uint8_t)P.n; for (int i = 0; i < P.n; i++) k.cf[i] = P.cf[i]; k.df = P.df; k.sf = P.sf; k.samples = (uint8_t)P.samples; k.tclass = (uint8_t)tclass;
    k.poly = (uint8_t)S.poly.mode; k.blend_en = (uint8_t)S.color.blend_en; k.logic_en = 0; k.chmask = S.chmask.mask; k.depth_clamp = (uint8_t)!!S.depth_clamp;
    for (int i = 0; i < P.n; i++) if (S.color.blend_en >> i & 1) k.blend[i] = S.blend[i];
    VkPipeline *pp = nvn_hm_get(&g_pipes_h, &k); if (pp) return *pp;
    if (!g_pcache) { VkPipelineCacheCreateInfo ci = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO}; VK(vkCreatePipelineCache(V.dev, &ci, NULL, &g_pcache)); }
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
        .pDynamicState = &dy, .layout = g->pl};
    VkPipeline p = VK_NULL_HANDLE; uint64_t t0 = now_ns();
    VkResult r = vkCreateGraphicsPipelines(V.dev, g_pcache, 1, &ci, NULL, &p);
    if (r != VK_SUCCESS) { VLOG("[red] pipeline for %016llx / %016llx: %d", (unsigned long long)g->vs->key, (unsigned long long)(g->ps ? g->ps->key : 0), (int)r); p = VK_NULL_HANDLE; }
    pipe_key *kk = malloc(sizeof *kk); *kk = k; VkPipeline *pv = malloc(sizeof *pv); *pv = p; nvn_hm_put(&g_pipes_h, kk, pv); free(kk);
    g_pipes++; if (g_pipes <= 4 || !(g_pipes % 100)) VLOG("pipeline %llu made in %.1f ms (%016llx / %016llx)", (unsigned long long)g_pipes, (now_ns() - t0) / 1e6,
                                                         (unsigned long long)g->vs->key, (unsigned long long)(g->ps ? g->ps->key : 0));
    return p;
}

// ---------- Resources ----------
static struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *cpu; } g_zero, g_support, g_c1, g_quad;   // a zero buffer, Ryujinx's support buffer, the c1 bank, quads' indices
enum { QUADS_MAX = 65536 };   // quads one draw of them may have (the Metal backend's)
static size_t g_c1_at; static nvn_hmap g_c1_off = {.klen = 8};   // shader key → its c1's offset + 1
enum { C1_BANK = 8u << 20, BANK_TAIL = 64u << 10 };
static void host_buffer(VkDeviceSize n, VkBufferUsageFlags u, VkBuffer *b, VkDeviceMemory *m, uint8_t **cpu) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = n, .usage = u}; VK(vkCreateBuffer(V.dev, &bi, NULL, b));
    VkMemoryRequirements rq; vkGetBufferMemoryRequirements(V.dev, *b, &rq);
    uint32_t t = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rq.size, .memoryTypeIndex = t};
    VK(vkAllocateMemory(V.dev, &mai, NULL, m)); VK(vkBindBufferMemory(V.dev, *b, *m, 0)); VK(vkMapMemory(V.dev, *m, 0, VK_WHOLE_SIZE, 0, (void **)cpu)); memset(*cpu, 0, n);
}
static void resources_init(void) {
    static int done; if (done) return; done = 1;
    VkBufferUsageFlags u = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    host_buffer(64u << 10, u, &g_zero.buf, &g_zero.mem, &g_zero.cpu);
    host_buffer(64u << 10, u, &g_support.buf, &g_support.mem, &g_support.cpu);
    for (int i = 0; i < 73; i++) *(float *)(g_support.cpu + 192 + 16 * i) = 1.0f;   // render_scale[i].x (Ryujinx SupportBuffer: the Metal backend's support_buf)
    host_buffer(C1_BANK + BANK_TAIL, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &g_c1.buf, &g_c1.mem, &g_c1.cpu);
    host_buffer(QUADS_MAX * 24, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &g_quad.buf, &g_quad.mem, &g_quad.cpu);   // quad q: 4q + {0, 1, 2, 0, 2, 3}
    for (uint32_t q = 0; q < QUADS_MAX; q++) { uint32_t b = 4 * q, v[6] = {b, b + 1, b + 2, b, b + 2, b + 3}; memcpy(g_quad.cpu + 24 * q, v, 24); }
}
static VkDeviceSize c1_offset(const vk_sh *s) {
    uintptr_t o = (uintptr_t)nvn_hm_get(&g_c1_off, &s->key); if (o) return o - 1;
    size_t at = g_c1_at; if (at + s->c1_len > C1_BANK) { VRED("the c1 bank is full: later shaders read their c1 as zeros"); return C1_BANK; }
    memcpy(g_c1.cpu + at, s->c1, s->c1_len); g_c1_at = (at + s->c1_len + 255) & ~(size_t)255;
    nvn_hm_put(&g_c1_off, &s->key, (void *)(uintptr_t)(at + 1)); return at;
}
static nvn_hmap g_samps = {.klen = sizeof(samp_par)};
static VkSamplerAddressMode wrap(int w) {
    switch (w) { case NVN_WRAP_MODE_REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT; case NVN_WRAP_MODE_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                 case NVN_WRAP_MODE_MIRROR_CLAMP: case NVN_WRAP_MODE_MIRROR_CLAMP_TO_EDGE: case NVN_WRAP_MODE_MIRROR_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
                 case NVN_WRAP_MODE_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;   // CLAMP (GL's, half border) and CLAMP_TO_EDGE
}
static VkSampler sampler(const samp_par *p0) {
    samp_par p; memset(&p, 0, sizeof p); if (p0) p = *p0; else { p.minf = NVN_MIN_FILTER_LINEAR; p.magf = NVN_MAG_FILTER_LINEAR; p.lod_max = 1000; }
    VkSampler *sp = nvn_hm_get(&g_samps, &p); if (sp) return *sp;
    int mip = p.minf >= NVN_MIN_FILTER_NEAREST_MIPMAP_NEAREST;
    VkSamplerCustomBorderColorCreateInfoEXT bc = {VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    memcpy(bc.customBorderColor.float32, p.border, sizeof p.border);
    int is_border = p.ws == NVN_WRAP_MODE_CLAMP_TO_BORDER || p.wt == NVN_WRAP_MODE_CLAMP_TO_BORDER || p.wr == NVN_WRAP_MODE_CLAMP_TO_BORDER;
    VkBorderColor bcol = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (p.border[0] == 0 && p.border[1] == 0 && p.border[2] == 0 && p.border[3] == 1) bcol = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    else if (p.border[0] == 1 && p.border[1] == 1 && p.border[2] == 1 && p.border[3] == 1) bcol = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    else if (is_border && V.custom_border && (p.border[0] || p.border[1] || p.border[2] || p.border[3])) bcol = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
    VkSamplerReductionModeCreateInfo rm = {VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO, bcol == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT ? &bc : NULL,
                                           .reductionMode = p.reduction == 1 ? VK_SAMPLER_REDUCTION_MODE_MIN : p.reduction == 2 ? VK_SAMPLER_REDUCTION_MODE_MAX : VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE};
    float an = p.aniso > 1 ? (p.aniso < V.props.limits.maxSamplerAnisotropy ? p.aniso : V.props.limits.maxSamplerAnisotropy) : 1;
    VkSamplerCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, &rm, .magFilter = p.magf == NVN_MAG_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .minFilter = (p.minf == NVN_MIN_FILTER_LINEAR || p.minf == NVN_MIN_FILTER_LINEAR_MIPMAP_NEAREST || p.minf == NVN_MIN_FILTER_LINEAR_MIPMAP_LINEAR) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .mipmapMode = (p.minf == NVN_MIN_FILTER_NEAREST_MIPMAP_LINEAR || p.minf == NVN_MIN_FILTER_LINEAR_MIPMAP_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = wrap(p.ws), .addressModeV = wrap(p.wt), .addressModeW = wrap(p.wr), .mipLodBias = p.lod_bias, .anisotropyEnable = an > 1, .maxAnisotropy = an,
        .compareEnable = p.cmp_mode != 0, .compareOp = compare_op(p.cmp_func), .minLod = p.lod_min, .maxLod = mip ? p.lod_max : 0.25f, .borderColor = bcol};
    VkSampler s; VK(vkCreateSampler(V.dev, &ci, NULL, &s));
    samp_par *kk = malloc(sizeof *kk); *kk = p; VkSampler *v = malloc(sizeof *v); *v = s; nvn_hm_put(&g_samps, kk, v); free(kk);
    return s;
}
// a 1x1 image of zeros for each view type a shader may declare (dim_of's order), sampled when a texture slot has nothing usable
static VkImageView g_dummy_view[9];
static VkImageView dummy_view(int dim) {
    if (dim < 0 || dim > 8) dim = 0;
    if (g_dummy_view[dim]) return g_dummy_view[dim];
    static VkImage img2d, img3d, img1d;
    int is3d = dim == 4, is1d = dim == 5 || dim == 6;
    VkImage *ip = is3d ? &img3d : is1d ? &img1d : &img2d;
    if (!*ip) {
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .flags = is3d || is1d ? 0 : VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, .imageType = is3d ? VK_IMAGE_TYPE_3D : is1d ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {1, 1, 1}, .mipLevels = 1, .arrayLayers = is3d || is1d ? 1 : 6, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VK(vkCreateImage(V.dev, &ci, NULL, ip)); VkMemoryRequirements rq; vkGetImageMemoryRequirements(V.dev, *ip, &rq);
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rq.size, .memoryTypeIndex = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0)};
        VkDeviceMemory m; VK(vkAllocateMemory(V.dev, &mai, NULL, &m)); VK(vkBindImageMemory(V.dev, *ip, m, 0));
        VkCommandBuffer cb = vk_once_begin();
        VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = *ip, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS}};
        VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b}; vkCmdPipelineBarrier2(cb, &di);
        VkClearColorValue z = {{0}}; VkImageSubresourceRange rg = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS};
        vkCmdClearColorImage(cb, *ip, VK_IMAGE_LAYOUT_GENERAL, &z, 1, &rg); vk_once_end(cb, 0);
    }
    static const VkImageViewType vt[9] = {VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, VK_IMAGE_VIEW_TYPE_3D,
                                          VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_VIEW_TYPE_1D_ARRAY, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D_ARRAY};
    VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = *ip, .viewType = vt[dim], .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, dim == 2 || dim == 3 ? 6 : 1}};
    VK(vkCreateImageView(V.dev, &ci, NULL, &g_dummy_view[dim]));
    return g_dummy_view[dim];
}
// The c2 handle tables' place (game.json nvn_c2; hle device.inc sets it at nvnDeviceInitialize; manosaba's until then), and the texture and sampler
// pools the command buffer set last: a handle's IDs are found at their pool's base in the tables (the Metal backend's h_tid / h_sid, plan item 64e)
static struct { int tex, ntex, samp, nsamp; } g_c2 = {0x168, 128, 0x568, 128};
static int g_tex_ns, g_samp_ns, g_tpool_n;
void mtl_set_tex_pool_n(int n) { g_tpool_n = n; }
void mtl_set_c2_layout(int tex, int ntex, int samp, int nsamp) {
    if (tex < 0 || samp < 0 || tex + 8 * ntex > 0xA00 || samp + 8 * nsamp > 0xA00) VDIE("c[2] layout tex %#x x %d / samp %#x x %d does not fit the 0xA00-byte bank", tex, ntex, samp, nsamp);
    g_c2.tex = tex; g_c2.ntex = ntex; g_c2.samp = samp; g_c2.nsamp = nsamp;
}
void nvn_be_set_pool(const uint8_t *p, const void *a) {   // SetTexturePool / SetSamplerPool
    op_hdr h; memcpy(&h, p, sizeof h); const dpool_t *d = ((const p_set_pool *)a)->pool; if (!d) return;
    if (h.op == OP_SET_TEX_POOL) { g_tex_ns = d->base; if (d->n) g_tpool_n = d->n; } else g_samp_ns = d->base;
}
static inline uint32_t h_tid(uint64_t h) { uint32_t t = (uint32_t)(h & 0xfffff); return t ? t + (uint32_t)g_tex_ns : 0; }   // 0: an empty slot
static inline uint32_t h_sid(uint64_t h) { uint32_t s = (uint32_t)(h >> 20); return s ? s + (uint32_t)g_samp_ns : 0; }
// The driver banks (Metal backend's drv_fill): c0 the storage buffer table and the point size, c2 the texture and sampler handle tables
static void drv_fill(uint8_t *d, int bank, int stage) {
    memset(d, 0, 0xA00);
    if (bank == 0) {
        int base = stage == 0 ? 0x110 : stage == 5 ? 0x310 : 0x510;
        for (int r = 0; r < 16; r++) { uint64_t a = S.ssbo[stage][r].addr; uint32_t n = (uint32_t)S.ssbo[stage][r].size; memcpy(d + base + 16 * r, &a, 8); memcpy(d + base + 16 * r + 8, &n, 4); }
        if (stage != 5) { const float point_max = 2047.9375f; memcpy(d + 0x610, &point_max, sizeof point_max); }
    } else {
        for (int r = 0; r < g_c2.ntex && r < 128; r++) { uint32_t th = (uint32_t)S.tex[stage][r]; memcpy(d + g_c2.tex + 8 * r, &th, 4); }
        for (int r = 0; r < g_c2.nsamp && r < 128; r++) { uint32_t sh = (uint32_t)S.samp[stage][r]; memcpy(d + g_c2.samp + 8 * r, &sh, 4); }
    }
}
// A uniform binding: the core's snapshot of the range when it has one, else the pool; 64 KB or to the end of what holds it
static int ubo_at(uint64_t addr, VkBuffer *b, VkDeviceSize *off, VkDeviceSize *range) {
    if (!addr) return 0;
    const us_ent *c = us_find(addr, 16);
    if (c && c->buf) { *b = (VkBuffer)(uintptr_t)c->buf; *off = c->gpu + (addr - c->lo); uint64_t left = c->hi - addr; *range = left < 65536 ? left : 65536; return 1; }
    uint64_t span; if (!vk_addr_span(addr, b, off, &span)) return 0;
    *range = span < 65536 ? span : 65536; return 1;
}
typedef struct { VkWriteDescriptorSet w[2 * VK_SH_MAXB]; VkDescriptorBufferInfo bi[2 * VK_SH_MAXB]; VkDescriptorImageInfo ii[2 * VK_SH_MAXB * 4]; int nw, nbi, nii; } pushes;
static void push_buf(pushes *p, const vk_bind *b, VkBuffer buf, VkDeviceSize off, VkDeviceSize range) {
    p->bi[p->nbi] = (VkDescriptorBufferInfo){buf, off, range};
    p->w[p->nw++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b->binding, .descriptorCount = 1, .descriptorType = (VkDescriptorType)b->dtype, .pBufferInfo = &p->bi[p->nbi++]};
}
static void stage_resources(pushes *p, const vk_sh *s, int stage) {
    for (int i = 0; i < s->nb; i++) {
        const vk_bind *b = &s->b[i]; VkBuffer buf; VkDeviceSize off, range;
        switch (b->kind) {
        case VK_K_CBUF:
            if (b->reg < 0 || b->reg >= 16 || !ubo_at(S.ubo[stage][b->reg].addr, &buf, &off, &range)) { buf = g_zero.buf; off = 0; range = 65536; }
            else if (off & (V.props.limits.minUniformBufferOffsetAlignment - 1)) { VRED("a uniform buffer at %#llx is not %llu-aligned: zeros", (unsigned long long)S.ubo[stage][b->reg].addr,
                                                                                         (unsigned long long)V.props.limits.minUniformBufferOffsetAlignment); buf = g_zero.buf; off = 0; range = 65536; }
            push_buf(p, b, buf, off, range); break;
        case VK_K_SUPPORT: push_buf(p, b, g_support.buf, 0, 65536); break;
        case VK_K_C1: { VkDeviceSize o = c1_offset(s); push_buf(p, b, g_c1.buf, o, C1_BANK + BANK_TAIL - o < 65536 ? C1_BANK + BANK_TAIL - o : 65536); break; }
        case VK_K_C0: case VK_K_C2: {
            uint8_t *cpu; uint64_t ao; void *ab; nvn_be_arena(0xA00, &cpu, &ao, &ab); drv_fill(cpu, b->kind == VK_K_C0 ? 0 : 2, stage);
            push_buf(p, b, (VkBuffer)(uintptr_t)ab, ao, 0xA00); break; }
        case VK_K_SBUF: {
            uint64_t a = b->reg >= 0 && b->reg < 16 ? S.ssbo[stage][b->reg].addr : 0, n = b->reg >= 0 && b->reg < 16 ? S.ssbo[stage][b->reg].size : 0, span;
            if (!a || !vk_addr_span(a, &buf, &off, &span)) { push_buf(p, b, g_zero.buf, 0, 65536); break; }
            if (off & (V.props.limits.minStorageBufferOffsetAlignment - 1)) VRED("a storage buffer at %#llx is not %llu-aligned: bound from below it (its data shifted)", (unsigned long long)a,
                                                                                  (unsigned long long)V.props.limits.minStorageBufferOffsetAlignment);
            VkDeviceSize al = off & ~(V.props.limits.minStorageBufferOffsetAlignment - 1);
            push_buf(p, b, buf, al, (n ? n : span) + (off - al)); break; }
        case VK_K_TEX: {
            // the texture ID from the texture slot's handle, the sampler ID from the separate sampler slot's when the shader names one, else from the same handle
            uint64_t h = b->reg >= 0 && b->reg < 128 ? S.tex[stage][b->reg] : 0; uint32_t tid = h_tid(h), sid = b->sreg >= 0 && b->sreg < 128 ? h_sid(S.samp[stage][b->sreg]) : h_sid(h);
            tex_t t; view_t v; VkImageView view = VK_NULL_HANDLE; samp_par sp; int have_s = sid && vk_samp_by_id(sid, &sp);
            if (tid && vk_tex_by_id(tid, &t, &v)) {
                vk_img *im = vk_tex(&t);
                if (im) { vk_tex_upload_now(im); int hv = v.set_levels || v.set_layers || v.set_format || v.set_target || v.set_swz; view = vk_tex_view(im, &v, hv, 0, b->off); }
            }
            else if (tid && (!g_tpool_n || tid - (uint32_t)g_tex_ns < (uint32_t)g_tpool_n)) VRED("texture ID %u is not registered: it reads zeros", tid);   // (outside the pool: an empty slot, as the game binds them)
            if (!view) { view = dummy_view(b->off); g_dummies++; }
            p->ii[p->nii] = (VkDescriptorImageInfo){sampler(have_s ? &sp : NULL), view, VK_IMAGE_LAYOUT_GENERAL};
            p->w[p->nw++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b->binding, .descriptorCount = 1, .descriptorType = (VkDescriptorType)b->dtype,
                                                   .pImageInfo = &p->ii[p->nii++]};
            break; }
        default:
            VRED("a shader binding of kind %d is not bound yet (images, texel buffers): it reads nothing", b->kind);
            if (b->dtype == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                p->ii[p->nii] = (VkDescriptorImageInfo){sampler(NULL), dummy_view(b->off), VK_IMAGE_LAYOUT_GENERAL};
                p->w[p->nw++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b->binding, .descriptorCount = 1, .descriptorType = (VkDescriptorType)b->dtype,
                                                       .pImageInfo = &p->ii[p->nii++]};
            } else if (b->dtype == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || b->dtype == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) push_buf(p, b, g_zero.buf, 0, 65536);
            // (storage images and texel buffers: no descriptor written; null descriptors are on when the device has them)
            break;
        }
    }
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
    vkCmdSetDepthTestEnable(cb, has_d && S.ds.dt); vkCmdSetDepthWriteEnable(cb, has_d && S.ds.dw); vkCmdSetDepthCompareOp(cb, compare_op(S.ds.df));
    vkCmdSetStencilTestEnable(cb, has_s && S.ds.st);
    vkCmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_BIT, stencil_op(S.ds.fail_f), stencil_op(S.ds.zpass_f), stencil_op(S.ds.zfail_f), compare_op(S.ds.sf_f));
    vkCmdSetStencilOp(cb, VK_STENCIL_FACE_BACK_BIT, stencil_op(S.ds.fail_b), stencil_op(S.ds.zpass_b), stencil_op(S.ds.zfail_b), compare_op(S.ds.sf_b));
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
                       vb[nb] = g_zero.buf; vo[nb] = 0; vsz[nb] = 65536; nb++; continue; }
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
    resources_init();
    int prim = dw ? dw->prim : di->prim; VkPrimitiveTopology topo;
    if (!topology(prim, &topo)) { VRED("primitive %d (quad strips, line loops, patches) is not drawn yet", prim); g_dropped++; return; }
    if (prim == NVN_DRAW_PRIMITIVE_QUADS && di) { VRED("indexed quads are not drawn yet"); g_dropped++; return; }
    vk_prog *g = prog_of(S.prog); if (!g) { g_dropped++; return; }
    if (!pass_open()) { g_dropped++; return; }
    VkPipeline pipe = pipeline(g, topo_class(topo)); if (!pipe) { g_dropped++; return; }
    VkCommandBuffer cb = vk_cb();
    if (!vertex_input(cb, g->vs)) { g_dropped++; return; }
    pushes *p = calloc(1, sizeof *p);
    stage_resources(p, g->vs, 0); if (g->ps) stage_resources(p, g->ps, 1);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    if (p->nw) V.push(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pl, 0, (uint32_t)p->nw, p->w);
    free(p);
    dynamic_state(cb, topo);
    if (dw && prim == NVN_DRAW_PRIMITIVE_QUADS) {   // two triangles a quad, its first vertex the vertex offset
        uint32_t nq = (uint32_t)dw->count / 4; if (nq > QUADS_MAX) { VRED("a draw of more than %d quads: cut there", QUADS_MAX); nq = QUADS_MAX; }
        vkCmdBindIndexBuffer(cb, g_quad.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, 6 * nq, dw->instances ? (uint32_t)dw->instances : 1, 0, dw->first, (uint32_t)dw->base_instance);
    } else if (dw) vkCmdDraw(cb, (uint32_t)dw->count, dw->instances ? (uint32_t)dw->instances : 1, (uint32_t)dw->first, (uint32_t)dw->base_instance);
    else {
        VkBuffer ib; VkDeviceSize io; uint64_t span;
        if (!vk_addr_span(di->ibuf, &ib, &io, &span)) { VRED("an index buffer at %#llx is in no pool: dropped", (unsigned long long)di->ibuf); g_dropped++; return; }
        VkIndexType it = di->itype == NVN_INDEX_TYPE_UNSIGNED_INT ? VK_INDEX_TYPE_UINT32 : di->itype == NVN_INDEX_TYPE_UNSIGNED_SHORT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT8_EXT;
        if (it == VK_INDEX_TYPE_UINT8_EXT && !V.idx8) { VRED("8-bit indices without VK_EXT_index_type_uint8: dropped"); g_dropped++; return; }
        vkCmdBindIndexBuffer(cb, ib, io, it);
        vkCmdDrawIndexed(cb, (uint32_t)di->count, di->instances ? (uint32_t)di->instances : 1, 0, di->base_vertex, (uint32_t)di->base_instance);
    }
    g_draws++;
    if (g_draws == 1) VLOG("first draw: %d attachments %ux%u, program %016llx / %016llx", P.n, P.w, P.h, (unsigned long long)g->vs->key, (unsigned long long)(g->ps ? g->ps->key : 0));
}
void nvn_be_dispatch(const p_dispatch *d) { (void)d; VRED("dispatches are not run yet (compute shaders: plan item 301e2's rest)"); }
void vk_stats_line(void) {
    uint64_t made, bytes, up_n, up_bytes, reup; vk_tex_stats(&made, &bytes, &up_n, &up_bytes, &reup);
    VLOG("so far: %llu draws (%llu dropped), %llu passes, %llu pipelines; textures sampled as zeros %llu times; %llu images (%.0f MB), %llu uploads (%.0f MB), %llu again after CPU writes",
         (unsigned long long)g_draws, (unsigned long long)g_dropped, (unsigned long long)g_passes, (unsigned long long)g_pipes, (unsigned long long)g_dummies,
         (unsigned long long)made, bytes / 1e6, (unsigned long long)up_n, up_bytes / 1e6, (unsigned long long)reup);
}
