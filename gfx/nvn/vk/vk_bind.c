// gfx/nvn/vk/vk_bind.c: the resources a program's shaders bind (plan items 301e2, 301e2b), in one set for its stages (the bundle moved the pixel
// shader's bindings by 64); pushed when they fit (VK_KHR_push_descriptor: 32 on the 840), else a set of their own from a pool, written at each use:
//   uniform slot r       S.ubo[stage][r] (the core's snapshot of it when there is one, usnap.h), 64 KB or to its pool's end (the shaders declare
//                        64 KB blocks; PKShield found games reading past the bound size)
//   support buffer       Ryujinx's, static: render scale 1
//   c1                   the shader's own bytes, in a bank of them (64 KB of zeros after the last)
//   c0 / c2              the driver banks as the Metal backend fills them (drv_fill: the storage buffer table, the point size; for compute the
//                        dispatch's workgroup counts at 0x200 and the uniform table at 0x210; the c2 handle tables), per use into the arena
//   storage slot r       S.ssbo[stage][r], from the 64-byte boundary at or below it (the device's alignment; the translator counts from the
//                        address itself or from the 16-byte boundary below it, so the data is shifted when it is not 64-aligned: a red line);
//                        a writable one drops the core's uniform snapshots it reaches (the Metal backend's us_drop)
//   texture slot r       S.tex[stage][r]: texture ID (low 20 bits) and sampler ID (high 12, or a separate sampler slot's), the pools' entries; its
//                        image uploaded first (a command buffer of its own), viewed as the shader declares it
//   image slot r         S.img[stage][r]: a texture ID (images and textures share the pool), viewed as a storage image (one level); a write
//                        makes the image the GPU's (its storage no longer its content)
//   texture through      a compute shader's texture named by a handle in c[2]'s image table (cp_t_tcb_<handle>): that slot's texture, sampled
//   an image slot
// Texel buffers read zeros and lose their writes (null descriptors; a red line).
#include "vk_bind.h"
#include "../nvn_hmap.h"
#include "../core/state.h"
#include "../core/upd.h"
#include "../core/usnap.h"
#include <stdlib.h>
#include <string.h>

static struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *cpu; } g_zero, g_support, g_c1;   // a zero buffer, Ryujinx's support buffer, the c1 bank
static size_t g_c1_at; static nvn_hmap g_c1_off = {.klen = 8};   // shader key → its c1's offset + 1
enum { C1_BANK = 8u << 20, BANK_TAIL = 64u << 10 };
static uint64_t g_zeros; static uint32_t g_grid[3];
uint64_t vk_bind_zeros(void) { return g_zeros; }
VkBuffer vk_zero_buffer(void) { return g_zero.buf; }
void vk_bind_grid(uint32_t x, uint32_t y, uint32_t z) { g_grid[0] = x; g_grid[1] = y; g_grid[2] = z; }

void vk_host_buffer(VkDeviceSize n, VkBufferUsageFlags u, VkBuffer *b, VkDeviceMemory *m, uint8_t **cpu) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = n, .usage = u}; VK(vkCreateBuffer(V.dev, &bi, NULL, b));
    VkMemoryRequirements rq; vkGetBufferMemoryRequirements(V.dev, *b, &rq);
    uint32_t t = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rq.size, .memoryTypeIndex = t};
    VK(vkAllocateMemory(V.dev, &mai, NULL, m)); VK(vkBindBufferMemory(V.dev, *b, *m, 0)); VK(vkMapMemory(V.dev, *m, 0, VK_WHOLE_SIZE, 0, (void **)cpu)); memset(*cpu, 0, n);
}
void vk_bind_init(void) {
    static int done; if (done) return; done = 1;
    VkBufferUsageFlags u = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    vk_host_buffer(64u << 10, u, &g_zero.buf, &g_zero.mem, &g_zero.cpu);
    vk_host_buffer(64u << 10, u, &g_support.buf, &g_support.mem, &g_support.cpu);
    for (int i = 0; i < 73; i++) *(float *)(g_support.cpu + 192 + 16 * i) = 1.0f;   // render_scale[i].x (Ryujinx SupportBuffer: the Metal backend's support_buf)
    vk_host_buffer(C1_BANK + BANK_TAIL, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &g_c1.buf, &g_c1.mem, &g_c1.cpu);
}
static VkDeviceSize c1_offset(const vk_sh *s) {
    uintptr_t o = (uintptr_t)nvn_hm_get(&g_c1_off, &s->key); if (o) return o - 1;
    size_t at = g_c1_at; if (at + s->c1_len > C1_BANK) { VRED("the c1 bank is full: later shaders read their c1 as zeros"); return C1_BANK; }
    memcpy(g_c1.cpu + at, s->c1, s->c1_len); g_c1_at = (at + s->c1_len + 255) & ~(size_t)255;
    nvn_hm_put(&g_c1_off, &s->key, (void *)(uintptr_t)(at + 1)); return at;
}

// ---------- Samplers and the zeros an empty slot samples ----------
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
        .compareEnable = p.cmp_mode != 0, .compareOp = vk_compare_op(p.cmp_func), .minLod = p.lod_min, .maxLod = mip ? p.lod_max : 0.25f, .borderColor = bcol};
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

// ---------- Handles and the driver banks ----------
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
static int in_pool(uint32_t tid) { return !g_tpool_n || tid - (uint32_t)g_tex_ns < (uint32_t)g_tpool_n; }   // (outside the pool: an empty slot, as the game binds them)
uint64_t vk_bind_tex_serial(const vk_bind *b, int stage) {
    uint64_t h = b->kind == VK_K_TEX ? (b->reg >= 0 && b->reg < 128 ? S.tex[stage][b->reg] : 0) : (b->kind == VK_K_TEXI || b->kind == VK_K_IMG) && b->reg >= 0 && b->reg < 32 ? S.img[stage][b->reg] : 0;
    tex_t t; view_t v; uint32_t tid = h_tid(h); return tid && vk_tex_by_id(tid, &t, &v) ? t.serial : 0;
}
static void drv_fill(uint8_t *d, int bank, int stage) {
    memset(d, 0, 0xA00);
    if (bank == 0) {
        int base = stage == 0 ? 0x110 : stage == 5 ? 0x310 : 0x510;
        for (int r = 0; r < 16; r++) { uint64_t a = S.ssbo[stage][r].addr; uint32_t n = (uint32_t)S.ssbo[stage][r].size; memcpy(d + base + 16 * r, &a, 8); memcpy(d + base + 16 * r + 8, &n, 4); }
        if (stage == 5) {
#ifndef DM_TEST_VK_GRID_ZERO   // negative control of t_vk_be only: no workgroup counts (nxnative 9579030's bug: a zero count multiplied in)
            memcpy(d + 0x200, g_grid, sizeof g_grid);
#endif
            for (int r = 0; r < 16; r++) { uint64_t a = S.ubo[5][r].addr; uint32_t n = (uint32_t)S.ubo[5][r].size; memcpy(d + 0x210 + 16 * r, &a, 8); memcpy(d + 0x210 + 16 * r + 8, &n, 4); }
        } else { const float point_max = 2047.9375f; memcpy(d + 0x610, &point_max, sizeof point_max); }
    } else {
        for (int r = 0; r < g_c2.ntex && r < 128; r++) { uint32_t th = (uint32_t)S.tex[stage][r]; memcpy(d + g_c2.tex + 8 * r, &th, 4); }
        for (int r = 0; r < g_c2.nsamp && r < 128; r++) { uint32_t sh = (uint32_t)S.samp[stage][r]; memcpy(d + g_c2.samp + 8 * r, &sh, 4); }
    }
}
// A uniform binding: a private copy when renamed updates of the submission reach it (core/upd.h), else the core's snapshot of the range when it has
// one, else the pool; 64 KB or to the end of what holds it
static int ubo_at(uint64_t addr, uint64_t size, VkBuffer *b, VkDeviceSize *off, VkDeviceSize *range) {
    if (!addr) return 0;
#ifndef DM_TEST_VK_UB_NO_SHADOW   // negative control of t_vk_be only: no private copies (the snapshot or memory as before the updates)
    if (upd_any()) {
        void *rb; uint64_t g = upd_shadow_at(addr, size ? size : 16, &rb, vk_cur_fence());
        if (g) { *b = (VkBuffer)(uintptr_t)rb; *off = g; uint64_t left = vk_arena_size() - g; *range = left < 65536 ? left : 65536; return 1; }
    }
#endif
    const us_ent *c = us_find(addr, 16);
    if (c && c->buf) { *b = (VkBuffer)(uintptr_t)c->buf; *off = c->gpu + (addr - c->lo); uint64_t left = c->hi - addr; *range = left < 65536 ? left : 65536; return 1; }
    uint64_t span; if (!vk_addr_span(addr, b, off, &span)) return 0;
    *range = span < 65536 ? span : 65536; return 1;
}

// ---------- Layouts and sets ----------
int vk_layout_make(vk_layout *L, const vk_sh *const *sh, const VkShaderStageFlags *stages, int n) {
    VkDescriptorSetLayoutBinding lb[2 * VK_SH_MAXB]; uint32_t nlb = 0, ndesc = 0;
    for (int s = 0; s < n; s++) if (sh[s]) for (int i = 0; i < sh[s]->nb && nlb < 2 * VK_SH_MAXB; i++) {
        const vk_bind *b = &sh[s]->b[i]; uint32_t c = b->count ? b->count : 1;
        lb[nlb++] = (VkDescriptorSetLayoutBinding){b->binding, (VkDescriptorType)b->dtype, c, stages[s], NULL}; ndesc += c;
    }
    const char *e = getenv("DM_VK_PUSH");   // DM_VK_PUSH=0: sets of their own for every program (read at each layout: t_vk_be takes both ways)
    L->ndesc = ndesc; L->push = V.push_ds && ndesc <= V.max_push_ds && !(e && *e == '0');
    VkDescriptorSetLayoutCreateInfo sci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .flags = L->push ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0,
                                           .bindingCount = nlb, .pBindings = lb};
    if (vkCreateDescriptorSetLayout(V.dev, &sci, NULL, &L->sl) != VK_SUCCESS) return 0;
    VkPipelineLayoutCreateInfo pci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &L->sl};
    if (vkCreatePipelineLayout(V.dev, &pci, NULL, &L->pl) != VK_SUCCESS) return 0;
    static int said; if (!L->push && said++ < 4) VLOG("a program of %u descriptors (%u can be pushed): a set of its own at each use", ndesc, V.max_push_ds);
    return 1;
}
// Sets come from a pool until it is full; a full one is destroyed once the submissions that used its sets are done
static VkDescriptorPool g_dpool;
static void pool_free(void *a) { vkDestroyDescriptorPool(V.dev, (VkDescriptorPool)(uintptr_t)a, NULL); }
static VkDescriptorSet set_new(VkDescriptorSetLayout sl, int *fresh) {
    *fresh = 1;
#ifdef DM_TEST_VK_SET_STALE   // negative control of t_vk_be only: the first set used again unwritten (as a set kept past its draw would be)
    static VkDescriptorSet first; if (first) { *fresh = 0; return first; }
#endif
    for (int k = 0; k < 2; k++) {
        if (!g_dpool) {
            VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024}, {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 512}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 512}};
            VkDescriptorPoolCreateInfo ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1024, .poolSizeCount = sizeof ps / sizeof *ps, .pPoolSizes = ps};
            VK(vkCreateDescriptorPool(V.dev, &ci, NULL, &g_dpool));
        }
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = g_dpool, .descriptorSetCount = 1, .pSetLayouts = &sl};
        VkDescriptorSet s;
        if (vkAllocateDescriptorSets(V.dev, &ai, &s) == VK_SUCCESS) {
#ifdef DM_TEST_VK_SET_STALE
            first = s;
#endif
            return s;
        }
        vk_after(vk_cur_fence(), pool_free, (void *)(uintptr_t)g_dpool); g_dpool = VK_NULL_HANDLE;
    }
    VDIE("no descriptor set can be allocated");
}

// ---------- The writes ----------
typedef struct {
    VkWriteDescriptorSet w[2 * VK_SH_MAXB]; VkDescriptorBufferInfo bi[2 * VK_SH_MAXB]; VkDescriptorImageInfo ii[2 * VK_SH_MAXB]; VkBufferView tv[2 * VK_SH_MAXB];
    int nw, nbi, nii, ntv;
} writes;
static void put_buf(writes *p, const vk_bind *b, VkBuffer buf, VkDeviceSize off, VkDeviceSize range) {
    p->bi[p->nbi] = (VkDescriptorBufferInfo){buf, off, range};
    p->w[p->nw++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b->binding, .descriptorCount = 1, .descriptorType = (VkDescriptorType)b->dtype, .pBufferInfo = &p->bi[p->nbi++]};
}
static void put_img(writes *p, const vk_bind *b, VkSampler s, VkImageView v) {
    p->ii[p->nii] = (VkDescriptorImageInfo){s, v, VK_IMAGE_LAYOUT_GENERAL};
    p->w[p->nw++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b->binding, .descriptorCount = 1, .descriptorType = (VkDescriptorType)b->dtype, .pImageInfo = &p->ii[p->nii++]};
}
static void put_null_texel(writes *p, const vk_bind *b) {   // (null descriptors: zeros read, writes lost)
    p->tv[p->ntv] = VK_NULL_HANDLE;
    p->w[p->nw++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = b->binding, .descriptorCount = 1, .descriptorType = (VkDescriptorType)b->dtype, .pTexelBufferView = &p->tv[p->ntv++]};
}
static int view_set(const view_t *v) { return v->set_levels || v->set_layers || v->set_format || v->set_target || v->set_swz; }
static VkImageView sampled(uint32_t tid, int dim) {   // texture ID tid as a sampled view, or none
    tex_t t; view_t v;
    if (!tid || !vk_tex_by_id(tid, &t, &v)) { if (tid && in_pool(tid)) VRED("texture ID %u is not registered: it reads zeros", tid); return VK_NULL_HANDLE; }
    vk_img *im = vk_tex(&t); if (!im) return VK_NULL_HANDLE;
    vk_tex_upload_now(im); return vk_tex_view(im, &v, view_set(&v), 0, dim);
}
static void stage_resources(writes *p, const vk_sh *s, int stage) {
    for (int i = 0; i < s->nb; i++) {
        const vk_bind *b = &s->b[i]; VkBuffer buf; VkDeviceSize off, range;
        switch (b->kind) {
        case VK_K_CBUF:
            if (b->reg < 0 || b->reg >= 16 || !ubo_at(S.ubo[stage][b->reg].addr, S.ubo[stage][b->reg].size, &buf, &off, &range)) { buf = g_zero.buf; off = 0; range = 65536; }
            else if (off & (V.props.limits.minUniformBufferOffsetAlignment - 1)) { VRED("a uniform buffer at %#llx is not %llu-aligned: zeros", (unsigned long long)S.ubo[stage][b->reg].addr,
                                                                                         (unsigned long long)V.props.limits.minUniformBufferOffsetAlignment); buf = g_zero.buf; off = 0; range = 65536; }
            put_buf(p, b, buf, off, range); break;
        case VK_K_SUPPORT: put_buf(p, b, g_support.buf, 0, 65536); break;
        case VK_K_C1: { VkDeviceSize o = c1_offset(s); put_buf(p, b, g_c1.buf, o, C1_BANK + BANK_TAIL - o < 65536 ? C1_BANK + BANK_TAIL - o : 65536); break; }
        case VK_K_C0: case VK_K_C2: {
            uint8_t *cpu; uint64_t ao; void *ab; nvn_be_arena(0xA00, &cpu, &ao, &ab); drv_fill(cpu, b->kind == VK_K_C0 ? 0 : 2, stage);
            put_buf(p, b, (VkBuffer)(uintptr_t)ab, ao, 0xA00); break; }
        case VK_K_SBUF: {
            uint64_t a = b->reg >= 0 && b->reg < 16 ? S.ssbo[stage][b->reg].addr : 0, n = b->reg >= 0 && b->reg < 16 ? S.ssbo[stage][b->reg].size : 0, span;
            if (!a || !vk_addr_span(a, &buf, &off, &span)) { put_buf(p, b, g_zero.buf, 0, 65536); break; }
            if (b->rw) us_drop_range(a, n ? n : 1, vk_cur_fence());   // the shader may write it: later readers in this submission see memory
            VkDeviceSize al = off & ~(V.props.limits.minStorageBufferOffsetAlignment - 1);
            if (al != off) VRED("a storage buffer at %#llx is not %llu-aligned: bound from below it, its data shifted (the translator's alignment is 16)", (unsigned long long)a,
                                (unsigned long long)V.props.limits.minStorageBufferOffsetAlignment);
            put_buf(p, b, buf, al, (n ? n : span) + (off - al)); break; }
        case VK_K_TEX: {
            // the texture ID from the texture slot's handle, the sampler ID from the separate sampler slot's when the shader names one, else from the same handle
            uint64_t h = b->reg >= 0 && b->reg < 128 ? S.tex[stage][b->reg] : 0; uint32_t sid = b->sreg >= 0 && b->sreg < 128 ? h_sid(S.samp[stage][b->sreg]) : h_sid(h);
            samp_par sp; int have_s = sid && vk_samp_by_id(sid, &sp);
            VkImageView view = sampled(h_tid(h), b->off); if (!view) { view = dummy_view(b->off); g_zeros++; }
            put_img(p, b, sampler(have_s ? &sp : NULL), view); break; }
        case VK_K_TEXI: {   // the image slot's texture, sampled (a fetch: the default sampler)
            VkImageView view = sampled(b->reg >= 0 && b->reg < 32 ? h_tid(S.img[stage][b->reg]) : 0, b->off); if (!view) { view = dummy_view(b->off); g_zeros++; }
            put_img(p, b, sampler(NULL), view); break; }
        case VK_K_IMG: {
            if (b->dtype != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) { VRED("storage texel buffers are not bound yet: zeros read, writes lost"); put_null_texel(p, b); break; }
            uint32_t tid = b->reg >= 0 && b->reg < 32 ? h_tid(S.img[stage][b->reg]) : 0; tex_t t; view_t v; VkImageView view = VK_NULL_HANDLE;
            if (tid && vk_tex_by_id(tid, &t, &v)) {
                vk_img *im = vk_tex(&t);
                if (im && !(im->usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
                    VRED("texture ID %u in an image slot cannot be a storage image (format %#x, flags %#x): zeros read, writes lost", tid, t.d.format, t.d.flags); im = NULL; }
                if (im) { vk_tex_upload_now(im); view = vk_tex_view(im, &v, view_set(&v), 2, b->off); if (view && b->rw) im->gpu_written = 1; }
            } else if (tid && in_pool(tid)) VRED("texture ID %u in an image slot is not registered: zeros read, writes lost", tid);
            if (!view) g_zeros++;
            put_img(p, b, VK_NULL_HANDLE, view); break; }   // (none: a null descriptor)
        default:   // texel buffers
            VRED("a shader binding of kind %d is not bound yet (texel buffers): zeros read", b->kind);
            if (b->dtype == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || b->dtype == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) put_null_texel(p, b);
            else if (b->dtype == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) put_img(p, b, sampler(NULL), dummy_view(b->off));
            else put_buf(p, b, g_zero.buf, 0, 65536);
            break;
        }
    }
}
void vk_bind_resources(VkCommandBuffer cb, VkPipelineBindPoint bp, const vk_layout *L, const vk_sh *const *sh, const int *nst, int n) {
    writes *p = calloc(1, sizeof *p);
    for (int i = 0; i < n; i++) if (sh[i]) stage_resources(p, sh[i], nst[i]);
    if (L->push) { if (p->nw) V.push(cb, bp, L->pl, 0, (uint32_t)p->nw, p->w); }
    else {
        int fresh; VkDescriptorSet set = set_new(L->sl, &fresh);
        if (fresh) { for (int i = 0; i < p->nw; i++) p->w[i].dstSet = set; vkUpdateDescriptorSets(V.dev, (uint32_t)p->nw, p->w, 0, NULL); }
        vkCmdBindDescriptorSets(cb, bp, L->pl, 0, 1, &set, 0, NULL);
    }
    free(p);
}
