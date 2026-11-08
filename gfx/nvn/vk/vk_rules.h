// gfx/nvn/vk/vk_rules.h: the Vulkan backend's decisions that make no Vulkan call (plan item 308), kept apart so android/tests/t_vk_rules.c checks
// them without a GPU: each colour attachment's write mask and blending, a texture view's channel mapping, the filters a sampler keeps for a
// format, whether a saved pipeline cache belongs to this device, the frame rate voted for the window.
#pragma once
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "../nvn_obj.h"

// An integer colour format: never blended (Vulkan requires the format's blend feature; Maxwell ignores the blend state there), never filtered
static inline int vk_fmt_is_int(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_B8G8R8A8_UINT: case VK_FORMAT_B8G8R8A8_SINT: case VK_FORMAT_A8B8G8R8_UINT_PACK32: case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_A2R10G10B10_UINT_PACK32: case VK_FORMAT_A2B10G10R10_UINT_PACK32: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SINT: case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R16G16B16A16_UINT: case VK_FORMAT_R16G16B16A16_SINT: case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT: return 1;
    default: return 0;
    }
}

// 308a: the write mask and blending of each of n colour attachments. An attachment the pixel shader writes no output to gets no writes and no
// blending: Maxwell leaves targets outside the shader's output map untouched, while Vulkan leaves such an attachment's values undefined (upstream
// d2r-mobile PR #229 / issue #179: Mali wrote zeros under an empty pixel shader; the Metal backend's rule since nxnative 42a50ef). An integer target is
// not blended (MacNativeGames PR #1797's rule on Metal, which aborts there). written: bit i when the shader writes location i; ints: bit i when
// attachment i is an integer format.
static inline void vk_color_rules(int n, uint32_t chmask, uint32_t blend_en, uint32_t written, uint32_t ints, uint8_t mask[8], uint8_t blend[8]) {
    for (int i = 0; i < 8; i++) {
        int w = i < n && (written >> i & 1);
#ifdef DM_TEST_VK_MASK_CHMASK_ONLY   // negative control of t_vk_rules only: the channel mask alone, as before
        w = i < n; ints = 0;
#endif
        mask[i] = w ? (uint8_t)(chmask >> (4 * i) & 0xf) : 0;
        blend[i] = (uint8_t)(w && (blend_en >> i & 1) && !(ints >> i & 1));
    }
}

// 308b: a texture view's channel mapping. The guest's selectors name NVN's components (R..A = 2..5, 0 zero, 1 one); fmt is the format's own
// mapping of them onto the image's (an RGBX format reads alpha as one). The view's swizzle, when the view sets one, replaces the texture's, even as
// the identity, as on the Metal backend (descriptors.inc); composing the two gave a requested green as red (nx2android f13ecfa, MIT: Mario Kart 8
// Deluxe's shadows). out: VkComponentSwizzle values.
static inline void vk_swz_rules(const VkComponentSwizzle fmt[4], int tex_set, const uint8_t tex[4], int view_set, const uint8_t view[4], VkComponentSwizzle out[4]) {
    uint8_t sel[4] = {2, 3, 4, 5};
#ifndef DM_TEST_VK_SWZ_COMPOSE   // negative control of t_vk_rules only: the texture's then the view's, composed, as before
    if (view_set) memcpy(sel, view, 4); else if (tex_set) memcpy(sel, tex, 4);
    for (int c = 0; c < 4; c++) out[c] = sel[c] == 0 ? VK_COMPONENT_SWIZZLE_ZERO : sel[c] == 1 ? VK_COMPONENT_SWIZZLE_ONE : sel[c] >= 2 && sel[c] <= 5 ? fmt[sel[c] - 2] : fmt[c];
#else
    VkComponentSwizzle b[4] = {fmt[0], fmt[1], fmt[2], fmt[3]};
    const uint8_t *layer[2] = {tex_set ? tex : NULL, view_set ? view : NULL};
    for (int k = 0; k < 2; k++) if (layer[k]) { VkComponentSwizzle o[4];
        for (int c = 0; c < 4; c++) { int x = layer[k][c]; o[c] = x == 0 ? VK_COMPONENT_SWIZZLE_ZERO : x == 1 ? VK_COMPONENT_SWIZZLE_ONE : x >= 2 && x <= 5 ? b[x - 2] : b[c]; }
        memcpy(b, o, sizeof b); }
    memcpy(out, b, sizeof b);
    (void)sel;
#endif
}

// 308h: a sampler for a format the GPU cannot filter linearly (its optimal tiling lacks SAMPLED_IMAGE_FILTER_LINEAR): nearest filters and no
// anisotropy, which Vulkan requires there (upstream d2r-mobile 0733eb4 / c386573, MIT: a MediaTek device stopped at start). On the Adreno 840 every
// integer format and D32_SFLOAT_S8_UINT lack it (2026-11-08 probe), the last a depth-stencil texture sampled with compare (shadows).
static inline void vk_filter_rules(int linear_ok, samp_par *p) {
#ifdef DM_TEST_VK_FILTER_ANY   // negative control of t_vk_rules only: the sampler as the game set it, as before
    linear_ok = 1;
#endif
    if (linear_ok) return;
    p->magf = NVN_MAG_FILTER_NEAREST;
    if (p->minf == NVN_MIN_FILTER_LINEAR) p->minf = NVN_MIN_FILTER_NEAREST;
    else if (p->minf == NVN_MIN_FILTER_LINEAR_MIPMAP_NEAREST || p->minf == NVN_MIN_FILTER_LINEAR_MIPMAP_LINEAR || p->minf == NVN_MIN_FILTER_NEAREST_MIPMAP_LINEAR)
        p->minf = NVN_MIN_FILTER_NEAREST_MIPMAP_NEAREST;
    p->aniso = 1;
}

// 308e: a pipeline cache saved by an earlier run is given to the driver only when its header (VkPipelineCacheHeaderVersionOne: length 32, version
// 1, vendor, device, the pipeline cache UUID) is this device's: another driver's or a truncated file starts an empty cache (LASRecomp 202916d,
// SkyRecomp ec367c6, MIT: caches kept between launches).
static inline int vk_pcache_ok(const uint8_t *d, size_t n, uint32_t vendor, uint32_t device, const uint8_t uuid[VK_UUID_SIZE]) {
    if (n < 32) return 0;
    uint32_t len, ver, ven, dev; memcpy(&len, d, 4); memcpy(&ver, d + 4, 4); memcpy(&ven, d + 8, 4); memcpy(&dev, d + 12, 4);
#ifdef DM_TEST_VK_PCACHE_ANY   // negative control of t_vk_rules only: any file taken
    return 1;
#endif
    return len >= 32 && ver == VK_PIPELINE_CACHE_HEADER_VERSION_ONE && ven == vendor && dev == device && !memcmp(d + 16, uuid, VK_UUID_SIZE);
}

// 309: the frame rate voted for the window (ANativeWindow_setFrameRate, fixed source): the target once it has held for 60 presents, so a change
// back and forth does not switch the panel's mode each time (nx2android 70c8cb8, MIT: Super Mario Bros. Wonder voted 176 times in 3 minutes, 2
// after; the switches cost 30-40 fps). Returns the rate to vote now, 0 for none.
typedef struct { int want, held, voted; } vk_rate_vote;
static inline int vk_rate_rules(vk_rate_vote *v, int fps) {
    if (fps != v->want) { v->want = fps; v->held = 0; }
    if (v->held < 60) v->held++;
#ifdef DM_TEST_VK_VOTE_EVERY   // negative control of t_vk_rules only: voted at each change at once
    v->held = 60;
#endif
    if (v->held >= 60 && v->voted != fps) { v->voted = fps; return fps; }
    return 0;
}
