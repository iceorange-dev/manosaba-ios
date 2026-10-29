// gfx/nvn/vk/vk_tex.c: textures and samplers (plan item 301e). A texture object (by its serial, as on Metal) gets a device-local image of its own,
// made on first use and kept in the GENERAL layout (the upstream Android ports keep every image there too; transitions come with 301f if they
// pay). What its storage holds is copied in before the GPU first reads it, deswizzled on the CPU (gfx/tex/nvn_deswizzle.c) into a staging buffer,
// unless the GPU wrote it first (rendered, cleared or copied into: then the storage no longer tells its content). CPU texel writes go to the storage
// and to the image; FlushTexels (the CPU wrote the storage itself) has it copied in again. ReadTexels of a GPU-written texture reads the image back.
// Colour images whose format has an sRGB twin may be viewed as either, listed (VK_KHR_image_format_list) rather than any format, which keeps
// Adreno's framebuffer compression (nx2android tex.c: UBWC lost with free MUTABLE_FORMAT; MIT, a fact taken).
#include "vk.h"
#include "../nvn_dirty.h"
#include "../nvn_hmap.h"
#include "../nvn_tex.h"
#include "../../tex/nvn_deswizzle.h"
#include <stdlib.h>
#include <string.h>

static nvn_hmap g_img = {.klen = 8};   // serial → vk_img
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_made, g_bytes;

static int fmt_is_depth(int f) { return f >= 0x31 && f <= 0x36; }
static VkImageAspectFlags aspect_of(int f) {
    if (f == 0x31) return VK_IMAGE_ASPECT_STENCIL_BIT;
    if (f == 0x35 || f == 0x36) return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (fmt_is_depth(f)) return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}
static VkFormat srgb_twin(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_SRGB; case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB; case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
    default: return VK_FORMAT_UNDEFINED;
    }
}
// What a format can be used for here, by its optimal-tiling features; a depth format the device lacks falls back to a wider one
static VkFormatFeatureFlags feats(VkFormat f) { VkFormatProperties p; vkGetPhysicalDeviceFormatProperties(V.pd, f, &p); return p.optimalTilingFeatures; }
static VkFormat usable(VkFormat f, int depth) {
    if (feats(f) & (depth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return f;
    if (f == VK_FORMAT_X8_D24_UNORM_PACK32 || f == VK_FORMAT_D16_UNORM) return VK_FORMAT_D32_SFLOAT;
    if (f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_S8_UINT) return VK_FORMAT_D32_SFLOAT_S8_UINT;
    return feats(f) ? f : VK_FORMAT_UNDEFINED;
}

static int cf_cube(const vk_img *im) { return im->cube; }
static vk_img *make(const tex_t *t) {
    const nvn_texdesc *d = &t->d; VkFormat f; VkComponentMapping swz;
    int kind = vk_fmt(d->format, &f, &swz);
    if (!kind) { static int said; if (said++ < 8) VLOG("[red] texture format %#x has no Vulkan format here (serial %llu)", d->format, (unsigned long long)t->serial); return NULL; }
    int depth = fmt_is_depth(d->format); VkFormat uf = usable(f, depth);
    if (!uf) { static int said; if (said++ < 8) VLOG("[red] texture format %#x (Vulkan %d) is not usable on this device", d->format, (int)f); return NULL; }
    vk_img *im = calloc(1, sizeof *im); im->serial = t->serial; im->t = *t; im->fmt = uf; im->aspect = aspect_of(d->format);
    if (uf != f) im->aspect = (uf == VK_FORMAT_D32_SFLOAT_S8_UINT) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
    nvn_texlayout L; nvn_tex_layout(d, &L);
    im->w = d->width ? d->width : 1; im->h = d->height ? d->height : 1; im->levels = d->levels ? d->levels : 1;
    int tg = d->target; VkImageCreateFlags cf = 0;
    im->type = tg == NVN_TEXTURE_TARGET_1D || tg == NVN_TEXTURE_TARGET_1D_ARRAY ? VK_IMAGE_TYPE_1D : tg == NVN_TEXTURE_TARGET_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    im->d = tg == NVN_TEXTURE_TARGET_3D ? (d->depth ? d->depth : 1) : 1;
    im->layers = tg == NVN_TEXTURE_TARGET_3D ? 1 : (L.layers ? L.layers : 1);
    if (tg == NVN_TEXTURE_TARGET_1D_ARRAY) im->h = 1;
    if (im->type == VK_IMAGE_TYPE_2D && im->layers >= 6 && im->w == im->h && !(d->samples > 1)) { cf |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; im->cube = 1; }   // (viewable as a cube whatever its target)
    if (im->type == VK_IMAGE_TYPE_3D) cf |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;   // its slices as render targets
    VkFormatFeatureFlags ff = feats(uf);
    VkImageUsageFlags us = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (ff & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) us |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (ff & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) us |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (ff & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) us |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if ((d->flags & NVN_TEXTURE_FLAGS_IMAGE) && (ff & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) us |= VK_IMAGE_USAGE_STORAGE_BIT;
    VkFormat list[2] = {uf, srgb_twin(uf)};
    VkImageFormatListCreateInfo fl = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, .viewFormatCount = 2, .pViewFormats = list};
    if (list[1]) cf |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    int samples = d->samples > 1 ? d->samples : 1;
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, list[1] ? &fl : NULL, .flags = cf, .imageType = im->type, .format = uf,
        .extent = {im->w, im->h, im->d}, .mipLevels = im->levels, .arrayLayers = im->layers, .samples = (VkSampleCountFlagBits)samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = us, .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult r = vkCreateImage(V.dev, &ci, NULL, &im->img);
    if (r != VK_SUCCESS) { VLOG("[red] image %ux%ux%u ×%u, %u levels, format %d: %d", im->w, im->h, im->d, im->layers, im->levels, (int)uf, (int)r); free(im); return NULL; }
    VkMemoryRequirements rq; vkGetImageMemoryRequirements(V.dev, im->img, &rq);
    uint32_t mt = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VkMemoryDedicatedAllocateInfo dai = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = im->img};
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &dai, .allocationSize = rq.size, .memoryTypeIndex = mt};
    if (mt == UINT32_MAX || (r = vkAllocateMemory(V.dev, &mai, NULL, &im->mem)) != VK_SUCCESS) {
        VLOG("[red] image memory of %llu KB refused (%d)", (unsigned long long)(rq.size >> 10), (int)r); vkDestroyImage(V.dev, im->img, NULL); free(im); return NULL; }
    VK(vkBindImageMemory(V.dev, im->img, im->mem, 0));
    g_made++; g_bytes += rq.size;
    return im;
}
static void to_general(vk_img *im, VkCommandBuffer cb) {
    VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = im->img, .subresourceRange = {im->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}};
    VkDependencyInfo di = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &di);
}
typedef struct { VkImage img; VkDeviceMemory mem; VkImageView views[16]; int nview; } dead_img;
static void destroy_img(void *a) { dead_img *d = a; for (int i = 0; i < d->nview; i++) vkDestroyImageView(V.dev, d->views[i], NULL); vkDestroyImage(V.dev, d->img, NULL); vkFreeMemory(V.dev, d->mem, NULL); free(d); }
static int views_take_locked(vk_img *im, VkImageView *out, int max);
static void retire_locked(vk_img *im) {   // destroyed once the GPU is past what may use it (g_mu held)
    dead_img *d = calloc(1, sizeof *d); d->img = im->img; d->mem = im->mem;
    d->nview = views_take_locked(im, d->views, 16);
    vk_after(mtl_last_fence() + 1, destroy_img, d); free(im);
}
vk_img *vk_tex(const tex_t *t) {
    if (!t || !t->serial) return NULL;
    pthread_mutex_lock(&g_mu);
    vk_img *im = nvn_hm_get(&g_img, &t->serial);
    if (im && memcmp(&im->t.d, &t->d, sizeof t->d)) { nvn_hm_del(&g_img, &t->serial); retire_locked(im); im = NULL; }   // the serial's object described anew
    if (!im) {
        im = make(t);
        if (im) {
            nvn_hm_put(&g_img, &t->serial, im);
            VkCommandBuffer cb = vk_once_begin(); to_general(im, cb); vk_once_end(cb, 0);   // (queued before the submission being recorded)
        }
    } else im->t = *t;
    pthread_mutex_unlock(&g_mu);
    return im;
}
void mtl_tex_release(tex_t *t) {
    if (!t || !t->serial) return;
    pthread_mutex_lock(&g_mu); vk_img *im = nvn_hm_del(&g_img, &t->serial); if (im) retire_locked(im); pthread_mutex_unlock(&g_mu);
}

// ---------- Uploads of what the storage holds ----------
typedef struct { VkBuffer buf; VkDeviceMemory mem; } staging;
static void staging_free(void *a) { staging *s = a; vkDestroyBuffer(V.dev, s->buf, NULL); vkFreeMemory(V.dev, s->mem, NULL); free(s); }
static staging *staging_new(uint64_t n, uint8_t **cpu) {
    staging *s = calloc(1, sizeof *s);
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = n ? n : 4, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VK(vkCreateBuffer(V.dev, &bi, NULL, &s->buf)); VkMemoryRequirements rq; vkGetBufferMemoryRequirements(V.dev, s->buf, &rq);
    uint32_t t = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
    if (t == UINT32_MAX) t = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rq.size, .memoryTypeIndex = t};
    VK(vkAllocateMemory(V.dev, &mai, NULL, &s->mem)); VK(vkBindBufferMemory(V.dev, s->buf, s->mem, 0));
    VK(vkMapMemory(V.dev, s->mem, 0, VK_WHOLE_SIZE, 0, (void **)cpu));
    return s;
}
static void level_dims(const vk_img *im, unsigned lv, uint32_t *w, uint32_t *h, uint32_t *d) {
    *w = im->w >> lv ? im->w >> lv : 1; *h = im->h >> lv ? im->h >> lv : 1; *d = im->d >> lv ? im->d >> lv : 1;
}
static uint64_t g_up_bytes, g_up_n, g_reup_n;
void vk_tex_ready(vk_img *im, VkCommandBuffer cb) {
    if (!im || im->uploaded || im->gpu_written) return;
    im->uploaded = 1;
    const nvn_texdesc *d = &im->t.d; const uint8_t *src = im->t.mem;
    if (!src) return;   // (no storage: nothing to copy)
    im->stamp = nvn_dirty_protect_stamp(src, im->t.size);   // taken before the copy: a CPU write after it shows in nvn_dirty_since (the Metal backend's pkg_upload)
    int bw, bh, bpb = nvn_fmt_bpb(d->format, &bw, &bh); if (!bpb || d->samples > 1) return;
    int linear = (d->flags & NVN_TEXTURE_FLAGS_LINEAR) != 0;
    // every level and layer, tightly packed by block rows
    uint64_t total = 0;
    for (unsigned lv = 0; lv < im->levels; lv++) { uint32_t w, h, z; level_dims(im, lv, &w, &h, &z);
        total += (uint64_t)((w + bw - 1) / bw) * bpb * ((h + bh - 1) / bh) * z * im->layers; }
    uint8_t *cpu; staging *s = staging_new(total, &cpu);
    VkBufferImageCopy *rg = calloc((size_t)im->levels * im->layers, sizeof *rg); int nrg = 0; uint64_t at = 0;
    nvn_texlayout L; nvn_tex_layout(d, &L);
    for (unsigned lv = 0; lv < im->levels; lv++) {
        uint32_t w, h, z; level_dims(im, lv, &w, &h, &z);
        size_t pitch = (size_t)((w + bw - 1) / bw) * bpb, rows = (h + bh - 1) / bh, img = pitch * rows * z;
        for (unsigned ly = 0; ly < im->layers; ly++) {
            if (linear) {   // already row by row: the stride is the texture's
                const uint8_t *lvp = src + nvn_tex_level_offset(d, lv) + (uint64_t)ly * L.layer_size; size_t sp = d->stride ? d->stride : pitch;
                for (size_t r = 0; r < rows * z; r++) memcpy(cpu + at + r * pitch, lvp + r * sp, pitch);
            } else if (nvn_deswizzle(d, lv, ly, src, cpu + at, pitch)) { static int said; if (said++ < 4) VLOG("[red] texture format %#x: no deswizzle for level %u", d->format, lv); }
            rg[nrg++] = (VkBufferImageCopy){at, 0, 0, {im->aspect & VK_IMAGE_ASPECT_STENCIL_BIT && !(im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_IMAGE_ASPECT_STENCIL_BIT :
                im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, lv, ly, 1}, {0, 0, 0}, {w, h, z}};
            at += img;
        }
    }
    vkCmdCopyBufferToImage(cb, s->buf, im->img, VK_IMAGE_LAYOUT_GENERAL, (uint32_t)nrg, rg);
    vk_barrier_all(cb);
    free(rg); g_up_bytes += total; g_up_n++;
    vk_after(vk_cur_fence(), staging_free, s);
}

// ---------- Views ----------
typedef struct { uint8_t key[12]; VkImageView v; } view_ent;
typedef struct { view_ent e[16]; int n; } view_set;
static nvn_hmap g_views = {.klen = 8};   // serial → view_set (under g_mu)
static int views_take_locked(vk_img *im, VkImageView *out, int max) {
    view_set *vs = nvn_hm_del(&g_views, &im->serial); int n = 0;
    if (vs) { for (int i = 0; i < vs->n && n < max; i++) out[n++] = vs->e[i].v; free(vs); }
    return n;
}
VkImageView vk_tex_view(vk_img *im, const view_t *v, int has_v, int attach, int dim) {
    if (!im) return VK_NULL_HANDLE;
    uint8_t key[12] = {0}; if (has_v && v) memcpy(key, v, sizeof *v); key[10] = (uint8_t)(attach | (dim + 1) << 1); key[11] = (uint8_t)has_v;
    pthread_mutex_lock(&g_mu);
    view_set *vs = nvn_hm_get(&g_views, &im->serial);
    if (vs) for (int i = 0; i < vs->n; i++) if (!memcmp(vs->e[i].key, key, sizeof key)) { VkImageView r = vs->e[i].v; pthread_mutex_unlock(&g_mu); return r; }
    unsigned base = 0, nl = im->levels, l0 = 0, nly = im->layers; VkFormat f = im->fmt;
    if (has_v && v) {
        if (v->set_levels) { base = v->base_level; nl = v->nlevels ? v->nlevels : im->levels - base; }
        if (v->set_layers) { l0 = v->min_layer; nly = v->nlayers ? v->nlayers : im->layers - l0; }
        if (v->set_format) { VkFormat vf; if (vk_fmt(v->format, &vf, NULL) && (vf == im->fmt || vf == srgb_twin(im->fmt))) f = vf; }
    }
    if (attach) nl = 1;
    if (base >= im->levels) base = im->levels - 1; if (base + nl > im->levels) nl = im->levels - base;
    if (l0 >= im->layers) l0 = im->layers - 1; if (l0 + nly > im->layers) nly = im->layers - l0;
    VkImageViewType vt = im->type == VK_IMAGE_TYPE_3D ? (attach ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_3D) :
        im->type == VK_IMAGE_TYPE_1D ? (nly > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D) : nly > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    int tg = has_v && v && v->set_target ? v->target : im->t.d.target;
    if (!attach && (tg == NVN_TEXTURE_TARGET_CUBEMAP || tg == NVN_TEXTURE_TARGET_CUBEMAP_ARRAY) && nly >= 6 && im->w == im->h)
        vt = tg == NVN_TEXTURE_TARGET_CUBEMAP && nly == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    // the dimension the shader declares (dmsh_vk.py dim_of), when the image can be viewed so: a 2D texture read as an array of one, an array's
    // first layer as 2D, layers as a cube
    if (!attach && dim >= 0) {
        static const VkImageViewType want[9] = {VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, VK_IMAGE_VIEW_TYPE_3D,
                                                VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_VIEW_TYPE_1D_ARRAY, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D_ARRAY};
        VkImageViewType w = dim < 9 ? want[dim] : vt;
        int img2d = im->type == VK_IMAGE_TYPE_2D, cube_ok = img2d && im->w == im->h && (cf_cube(im)) && nly >= 6;
        if (w == VK_IMAGE_VIEW_TYPE_2D && img2d) { vt = w; nly = 1; }
        else if (w == VK_IMAGE_VIEW_TYPE_2D_ARRAY && img2d) vt = w;
        else if ((w == VK_IMAGE_VIEW_TYPE_CUBE || w == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) && cube_ok) { vt = w; if (w == VK_IMAGE_VIEW_TYPE_CUBE) nly = 6; else nly -= nly % 6; }
        else if (w == VK_IMAGE_VIEW_TYPE_3D && im->type == VK_IMAGE_TYPE_3D) vt = w;
        else if ((w == VK_IMAGE_VIEW_TYPE_1D || w == VK_IMAGE_VIEW_TYPE_1D_ARRAY) && im->type == VK_IMAGE_TYPE_1D) { vt = w; if (w == VK_IMAGE_VIEW_TYPE_1D) nly = 1; }
        else VRED("a texture's image (type %d, %u layers) cannot be viewed as the shader declares it (%d): viewed as it is", (int)im->type, im->layers, dim);
    }
    VkComponentMapping cm = {0};
    const nvn_texdesc *d = &im->t.d;
    if (!attach) {   // the format's own (RGBX: a one), then the texture's swizzle over it, then the view's
        VkComponentMapping fm; VkFormat ignore; vk_fmt(d->format, &ignore, &fm);
        VkComponentSwizzle b[4] = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, fm.a == VK_COMPONENT_SWIZZLE_ONE ? VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_A};
        int sw[2][4] = {{d->swz[0], d->swz[1], d->swz[2], d->swz[3]}, {0}};
        int nsw = 0; if (d->swz_set) nsw = 1;
        if (has_v && v && v->set_swz) { sw[nsw][0] = v->swz_r; sw[nsw][1] = v->swz_g; sw[nsw][2] = v->swz_b; sw[nsw][3] = v->swz_a; nsw++; }
        for (int k = 0; k < nsw; k++) {
            VkComponentSwizzle o[4];
            for (int c = 0; c < 4; c++) { int x = sw[k][c]; o[c] = x == NVN_TEXTURE_SWIZZLE_ZERO ? VK_COMPONENT_SWIZZLE_ZERO : x == NVN_TEXTURE_SWIZZLE_ONE ? VK_COMPONENT_SWIZZLE_ONE :
                                                     x >= NVN_TEXTURE_SWIZZLE_R && x <= NVN_TEXTURE_SWIZZLE_A ? b[x - NVN_TEXTURE_SWIZZLE_R] : b[c]; }
            memcpy(b, o, sizeof b);
        }
        cm = (VkComponentMapping){b[0], b[1], b[2], b[3]};
    }
    VkImageAspectFlags asp = im->aspect;
    if (!attach && asp == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) asp = d->ds_mode ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;   // what a depth-stencil texture samples
    VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = im->img, .viewType = vt, .format = f, .components = cm, .subresourceRange = {asp, base, nl, l0, nly}};
    VkImageView view; VkResult r = vkCreateImageView(V.dev, &ci, NULL, &view);
    if (r != VK_SUCCESS) { pthread_mutex_unlock(&g_mu); VLOG("[red] image view (type %d, format %d): %d", (int)vt, (int)f, (int)r); return VK_NULL_HANDLE; }
    if (!vs) { vs = calloc(1, sizeof *vs); nvn_hm_put(&g_views, &im->serial, vs); }
    if (vs->n < 16) { memcpy(vs->e[vs->n].key, key, sizeof key); vs->e[vs->n++].v = view; }
    else { static int said; if (!said++) VLOG("[red] more than 16 views of one texture: views made again"); }
    pthread_mutex_unlock(&g_mu);
    return view;
}

// ---------- CPU texel reads and writes ----------
static VkImageAspectFlags copy_aspect(const vk_img *im) { return im->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : im->aspect; }
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *dst, size_t row, size_t img) {
    vk_img *im = vk_tex(t);
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    uint32_t wb = (uint32_t)((r->width + bw - 1) / bw) * (uint32_t)bpb, hb = (uint32_t)(r->height + bh - 1) / bh, nz = r->depth ? (uint32_t)r->depth : 1;
    if (!im || !im->gpu_written) {   // the storage is the content
        if (t->mem && !nvn_region_move(&t->d, level, layer0, t->mem, dst, row, img, (uint32_t)(r->xoffset / bw) * bpb, (uint32_t)(r->yoffset / bh), (uint32_t)r->zoffset, wb, hb, nz, 1)) return;
        static int said; if (said++ < 4) VLOG("[red] ReadTexels of format %#x: the storage could not be read", t->d.format); return;
    }
    mtl_finish();
    uint8_t *cpu; staging *s = staging_new((uint64_t)wb * hb * nz, &cpu);
    int is3d = im->type == VK_IMAGE_TYPE_3D;
    VkBufferImageCopy rg = {0, 0, 0, {copy_aspect(im), level, is3d ? 0 : layer0 + (uint32_t)r->zoffset, is3d ? 1 : nz}, {r->xoffset, r->yoffset, is3d ? r->zoffset : 0},
                            {(uint32_t)r->width, (uint32_t)r->height, is3d ? nz : 1}};
    VkCommandBuffer cb = vk_once_begin(); vkCmdCopyImageToBuffer(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, s->buf, 1, &rg); vk_once_end(cb, 1);
    for (uint32_t z = 0; z < nz; z++) for (uint32_t y = 0; y < hb; y++) memcpy(dst + z * img + y * row, cpu + ((size_t)z * hb + y) * wb, wb);
    staging_free(s);
}
void mtl_write_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, const uint8_t *src, size_t row, size_t img) {
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    uint32_t wb = (uint32_t)((r->width + bw - 1) / bw) * (uint32_t)bpb, hb = (uint32_t)(r->height + bh - 1) / bh, nz = r->depth ? (uint32_t)r->depth : 1;
    if (t->mem) nvn_region_move(&t->d, level, layer0, t->mem, (void *)src, row, img, (uint32_t)(r->xoffset / bw) * bpb, (uint32_t)(r->yoffset / bh), (uint32_t)r->zoffset, wb, hb, nz, 0);
    vk_img *im = vk_tex(t); if (!im || !im->uploaded) return;   // not copied in yet: the storage has it now
    uint8_t *cpu; staging *s = staging_new((uint64_t)wb * hb * nz, &cpu);
    for (uint32_t z = 0; z < nz; z++) for (uint32_t y = 0; y < hb; y++) memcpy(cpu + ((size_t)z * hb + y) * wb, src + z * img + y * row, wb);
    int is3d = im->type == VK_IMAGE_TYPE_3D;
    VkBufferImageCopy rg = {0, 0, 0, {copy_aspect(im), level, is3d ? 0 : layer0 + (uint32_t)r->zoffset, is3d ? 1 : nz}, {r->xoffset, r->yoffset, is3d ? r->zoffset : 0},
                            {(uint32_t)r->width, (uint32_t)r->height, is3d ? nz : 1}};
    VkCommandBuffer cb = vk_once_begin(); vkCmdCopyBufferToImage(cb, s->buf, im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &rg); vk_once_end(cb, 1);
    staging_free(s);
}
void mtl_flush_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r) {
    (void)level; (void)layer0; (void)r;
    vk_img *im = vk_tex(t); if (im) { im->uploaded = 0; im->gpu_written = 0; }   // the storage is the content again: copied in before the next read
}

// ---------- Texture and sampler pools (the IDs the shaders' handles name; used by draws, 301e2) ----------
typedef struct { tex_t t; view_t v; int set; } reg_tex;
static reg_tex *g_rtex; static int g_nrtex; static samp_par *g_rsamp; static uint8_t *g_rsamp_set; static int g_nrsamp;
void mtl_reg_tex(int id, const tex_t *t, const view_t *v) {
    if (id < 0 || id >= (1 << 20)) return;
    pthread_mutex_lock(&g_mu);
    if (id >= g_nrtex) { int n = id + 1024; g_rtex = realloc(g_rtex, (size_t)n * sizeof *g_rtex); memset(g_rtex + g_nrtex, 0, (size_t)(n - g_nrtex) * sizeof *g_rtex); g_nrtex = n; }
    g_rtex[id].t = *t; if (v) g_rtex[id].v = *v; else memset(&g_rtex[id].v, 0, sizeof g_rtex[id].v); g_rtex[id].set = 1;
    pthread_mutex_unlock(&g_mu);
}
void mtl_reg_samp(int id, const samp_par *p) {
    if (id < 0 || id >= (1 << 12)) return;
    pthread_mutex_lock(&g_mu);
    if (id >= g_nrsamp) { int n = id + 256; g_rsamp = realloc(g_rsamp, (size_t)n * sizeof *g_rsamp); g_rsamp_set = realloc(g_rsamp_set, (size_t)n);
        memset(g_rsamp_set + g_nrsamp, 0, (size_t)(n - g_nrsamp)); g_nrsamp = n; }
    g_rsamp[id] = *p; g_rsamp_set[id] = 1;
    pthread_mutex_unlock(&g_mu);
}
void vk_tex_stats(uint64_t *made, uint64_t *bytes, uint64_t *up_n, uint64_t *up_bytes, uint64_t *reup_n) { *made = g_made; *bytes = g_bytes; *up_n = g_up_n; *up_bytes = g_up_bytes; *reup_n = g_reup_n; }

// The texture and sampler a handle names (texture ID: the low 20 bits, sampler ID: the high 12), as the pools registered them
int vk_tex_by_id(uint32_t id, tex_t *t, view_t *v) {
    pthread_mutex_lock(&g_mu); int ok = id < (uint32_t)g_nrtex && g_rtex[id].set; if (ok) { *t = g_rtex[id].t; *v = g_rtex[id].v; } pthread_mutex_unlock(&g_mu); return ok;
}
int vk_samp_by_id(uint32_t id, samp_par *p) {
    pthread_mutex_lock(&g_mu); int ok = id < (uint32_t)g_nrsamp && g_rsamp_set[id]; if (ok) *p = g_rsamp[id]; pthread_mutex_unlock(&g_mu); return ok;
}
// Uploaded now in a command buffer of its own (queued before the submission being recorded): a draw's textures, inside or outside its pass
void vk_tex_upload_now(vk_img *im) {
    if (!im || im->gpu_written) return;
    // the CPU wrote it since: again (outside the tracked pools, state 2, nothing tells: kept, as before the tracking)
    if (im->uploaded && im->t.mem && im->t.size && nvn_dirty_state(im->t.mem, im->t.size, im->stamp) == 1) { im->uploaded = 0; g_reup_n++; }
    if (im->uploaded) return;
    VkCommandBuffer cb = vk_once_begin(); vk_tex_ready(im, cb); vk_once_end(cb, 0);
}
