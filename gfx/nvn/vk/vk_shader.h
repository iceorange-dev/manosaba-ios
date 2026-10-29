// gfx/nvn/vk/vk_shader.h: a shader of the bundle (vk_shader.c) and its reflection, as gfx/tools/dmsh_vk.py writes them
#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

enum { VK_K_CBUF = 1, VK_K_SUPPORT, VK_K_C1, VK_K_C0, VK_K_C2, VK_K_SBUF, VK_K_TEX, VK_K_IMG, VK_K_TBUF, VK_K_TEXI };   // binding kinds
#define VK_SH_MAXB 64
typedef struct { uint8_t kind, dtype, rw, ty; uint16_t binding, count, off; int16_t reg, sreg; } vk_bind;   // dtype: VkDescriptorType; reg / sreg: the NVN slot, -1 none
typedef struct { uint8_t loc, ct; } vk_loc;   // ct: 0 float, 1 uint, 2 int
typedef struct {
    uint64_t key; int stage, missing;          // stage: NVN's (0 vertex, 1 fragment, 5 compute)
    VkShaderModule mod;
    int nb, nv, nout; uint16_t c1_len, local[3];
    vk_bind b[VK_SH_MAXB]; vk_loc vin[32], vout[8];
    const uint8_t *c1;                          // the shader's c1 bytes (in the mapped bundle)
} vk_sh;
const vk_sh *vk_shader(const uint8_t key8[8]);
