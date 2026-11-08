// gfx/nvn/vk/vk_rules.h (plan items 308, 309), the Vulkan backend's decisions that make no Vulkan call:
//   308a write masks: an attachment the pixel shader writes no output to gets mask 0 and no blending, an integer target no blending, the others
//        their channel mask and blend bit (-DDM_TEST_VK_MASK_CHMASK_ONLY, the channel mask alone as before: 2 fail);
//   308b a view's swizzle replaces the texture's (texture R R R R, view G G G 1 → G G G one), the format's own mapping under it (an RGBX format's
//        alpha one, the RGB 16-bit family's red and blue swapped), no swizzle the format's own (-DDM_TEST_VK_SWZ_COMPOSE, composed: 1 fails);
//   308h a format not filtered linearly samples with nearest filters and no anisotropy, mip levels kept; a filterable one as set
//        (-DDM_TEST_VK_FILTER_ANY: 1 fails);
//   308e a saved pipeline cache is taken only with this device's header: another vendor, device or UUID, an old version or a short file not
//        (-DDM_TEST_VK_PCACHE_ANY: 1 fails);
//   309  the frame rate voted once it has held for 60 presents, not again while it holds, a change voted after its own 60 (-DDM_TEST_VK_VOTE_EVERY,
//        at each change at once: 2 fail).
#include "../../gfx/nvn/vk/vk_rules.h"
#include <stdio.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

int main(void) {
    // 308a: three attachments, the shader writes locations 0 and 2; attachment 2 an integer format; blending on for all; channel masks f, 3, f
    uint8_t m[8], b[8]; vk_color_rules(3, 0xf3f, 0x7, 0x5, 0x4, m, b);
    CHECK(m[0] == 0xf && b[0] == 1, "308a: a written float target keeps its channel mask and blends");
    CHECK(m[1] == 0 && b[1] == 0, "308a: an attachment the shader does not write: no writes, no blending [-DDM_TEST_VK_MASK_CHMASK_ONLY]");
    CHECK(m[2] == 0xf && b[2] == 0, "308a: an integer target written: its mask, never blended");
    // 308b
    VkComponentSwizzle rgba[4] = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A}, rgbx[4] = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ONE};
    VkComponentSwizzle bgr[4] = {VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A}, o[4];
    const uint8_t rrrr[4] = {2, 2, 2, 2}, ggg1[4] = {3, 3, 3, 1}, none[4] = {0};
    vk_swz_rules(rgba, 1, rrrr, 1, ggg1, o);
    CHECK(o[0] == VK_COMPONENT_SWIZZLE_G && o[1] == VK_COMPONENT_SWIZZLE_G && o[2] == VK_COMPONENT_SWIZZLE_G && o[3] == VK_COMPONENT_SWIZZLE_ONE,
          "308b: texture R R R R, view G G G 1: the view's replaces it (G G G one) [-DDM_TEST_VK_SWZ_COMPOSE]");
    vk_swz_rules(rgbx, 0, none, 0, none, o);
    int ok = o[0] == VK_COMPONENT_SWIZZLE_R && o[3] == VK_COMPONENT_SWIZZLE_ONE;
    vk_swz_rules(bgr, 1, (const uint8_t[4]){2, 3, 4, 5}, 0, none, o);
    ok &= o[0] == VK_COMPONENT_SWIZZLE_B && o[2] == VK_COMPONENT_SWIZZLE_R;
    CHECK(ok, "308b: no swizzle: the format's own (RGBX alpha one); the RGB 16-bit family's red and blue swapped under the identity");
    // 308h
    samp_par p; memset(&p, 0, sizeof p); p.minf = NVN_MIN_FILTER_LINEAR_MIPMAP_LINEAR; p.magf = NVN_MAG_FILTER_LINEAR; p.aniso = 8; samp_par q = p;
    vk_filter_rules(0, &p); vk_filter_rules(1, &q);
    CHECK(p.magf == NVN_MAG_FILTER_NEAREST && p.minf == NVN_MIN_FILTER_NEAREST_MIPMAP_NEAREST && p.aniso == 1,
          "308h: a format not filtered linearly: nearest filters, mip levels kept, no anisotropy [-DDM_TEST_VK_FILTER_ANY]");
    CHECK(q.magf == NVN_MAG_FILTER_LINEAR && q.minf == NVN_MIN_FILTER_LINEAR_MIPMAP_LINEAR && q.aniso == 8, "308h: a filterable format: the sampler as the game set it");
    // 308e
    uint8_t uuid[VK_UUID_SIZE], h[64]; for (int i = 0; i < VK_UUID_SIZE; i++) uuid[i] = (uint8_t)(i * 7 + 1);
    uint32_t hv[4] = {32, VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x5143, 0x44050001}; memcpy(h, hv, 16); memcpy(h + 16, uuid, VK_UUID_SIZE); memset(h + 32, 0xab, 32);
    int own = vk_pcache_ok(h, sizeof h, 0x5143, 0x44050001, uuid), shrt = vk_pcache_ok(h, 20, 0x5143, 0x44050001, uuid);
    uint8_t other[64]; memcpy(other, h, 64); other[20] ^= 1; int ouuid = vk_pcache_ok(other, 64, 0x5143, 0x44050001, uuid);
    int odev = vk_pcache_ok(h, 64, 0x5143, 0x44050002, uuid), oven = vk_pcache_ok(h, 64, 0x13b5, 0x44050001, uuid);
    CHECK(own && !shrt, "308e: this device's cache taken; a file shorter than its header not");
    CHECK(!ouuid && !odev && !oven, "308e: another pipeline cache UUID, device or vendor: started empty [-DDM_TEST_VK_PCACHE_ANY]");
    // 309
    vk_rate_vote v = {0}; int first = 0, at = 0;
    for (int i = 1; i <= 100; i++) { int r = vk_rate_rules(&v, 60); if (r && !first) { first = r; at = i; } }
    int again = 0; for (int i = 0; i < 30; i++) again |= vk_rate_rules(&v, 60);
    int flap = 0; for (int i = 0; i < 10; i++) flap |= vk_rate_rules(&v, i & 1 ? 60 : 30);   // back and forth: no vote
    int later = 0, lat = 0; for (int i = 1; i <= 70; i++) { int r = vk_rate_rules(&v, 30); if (r && !later) { later = r; lat = i; } }
    CHECK(first == 60 && at == 60 && !again, "309: 60 fps voted at the 60th present, not again while it holds");
    CHECK(!flap && later == 30 && lat == 60, "309: back and forth: no vote; a change to 30 voted after its own 60 presents [-DDM_TEST_VK_VOTE_EVERY]");
    printf("t_vk_rules: %d checks, %d failed\n", n, fail);
    return fail != 0;
}
