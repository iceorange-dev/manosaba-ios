// gfx/nvn/vk/vk_bind.h: what a program's shaders bind (vk_bind.c), for the draws and dispatches of vk_draw.c
#pragma once
#include "vk.h"
#include "vk_shader.h"

// A program's set layout and pipeline layout: its shaders' bindings in one set, pushed (VK_KHR_push_descriptor) when they fit the device's limit,
// else a set of its own written at each use
typedef struct { VkDescriptorSetLayout sl; VkPipelineLayout pl; int push; uint32_t ndesc; } vk_layout;
int vk_layout_make(vk_layout *L, const vk_sh *const *sh, const VkShaderStageFlags *stages, int n);   // 0: none can be made
// The resources of n shaders (sh[i] of NVN stage nst[i]) bound for the next draw or dispatch at bp
void vk_bind_resources(VkCommandBuffer cb, VkPipelineBindPoint bp, const vk_layout *L, const vk_sh *const *sh, const int *nst, int n);
void vk_bind_grid(uint32_t x, uint32_t y, uint32_t z);   // the dispatch's workgroup counts, for the compute stage's c0 (0x200)
void vk_bind_init(void);                                  // (once, before the first use)
void vk_host_buffer(VkDeviceSize n, VkBufferUsageFlags u, VkBuffer *b, VkDeviceMemory *m, uint8_t **cpu);   // host-visible, coherent, zeroed, mapped
uint64_t vk_bind_zeros(void);                             // texture slots sampled as zeros so far
VkBuffer vk_zero_buffer(void);                            // 64 KB of zeros (unbound vertex attributes read it)
static inline VkCompareOp vk_compare_op(int f) { return f >= 1 && f <= 8 ? (VkCompareOp)(f - 1) : VK_COMPARE_OP_ALWAYS; }   // NVN NEVER..ALWAYS = 1..8, Vulkan's 0..7 in the same order
