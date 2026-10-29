// gfx/nvn/vk/vk_fmt.c: NVNformat (nvn.h 0x01–0x9C) → Vulkan (plan item 301e), entry by entry in NVN order as gfx/tex/nvn_fmt_mtl.c is for Metal,
// with the same kinds: 1 exact, 2 a substitute (the storage needs converting, or the shader reads it another way), 0 none. Vulkan has what Metal
// lacks for several: 24-bit depth (D24, D24S8), BC1 without alpha (BC1_RGB), the 4444 order NVN stores (A4B4G4R4, VK_EXT_4444_formats, core in 1.3).
// The three-component formats (RGB8 / RGB16 / RGB32) have Vulkan formats the 840 does not sample: none, as on Metal. The 5-bit packed formats
// (RGB5, RGB5A1, RGB565, the BGR family but BGRX8 / BGRA8) are left out until a game uses one and its bit order is checked against the console.
// Component mapping: RGBX formats read a as one. Checked by tests/t_vk_fmt.c.
#include <vulkan/vulkan.h>

typedef struct { uint32_t vk; uint8_t kind, noalpha; } ent;
#define E(f) {VK_FORMAT_##f, 1, 0}
#define X(f) {VK_FORMAT_##f, 1, 1}   // exact, a read as one
#define S(f) {VK_FORMAT_##f, 2, 0}   // substitute
#define N {0, 0, 0}

static const ent T[0x9D] = {
    [0x01] = E(R8_UNORM), E(R8_SNORM), E(R8_UINT), E(R8_SINT),
    [0x05] = E(R16_SFLOAT), E(R16_UNORM), E(R16_SNORM), E(R16_UINT), E(R16_SINT),
    [0x0A] = E(R32_SFLOAT), E(R32_UINT), E(R32_SINT),
    [0x0D] = E(R8G8_UNORM), E(R8G8_SNORM), E(R8G8_UINT), E(R8G8_SINT),
    [0x11] = E(R16G16_SFLOAT), E(R16G16_UNORM), E(R16G16_SNORM), E(R16G16_UINT), E(R16G16_SINT),
    [0x16] = E(R32G32_SFLOAT), E(R32G32_UINT), E(R32G32_SINT),
    [0x19] = N, N, N, N, N, N, N, N, N, N, N, N,
    [0x25] = E(R8G8B8A8_UNORM), E(R8G8B8A8_SNORM), E(R8G8B8A8_UINT), E(R8G8B8A8_SINT),
    [0x29] = E(R16G16B16A16_SFLOAT), E(R16G16B16A16_UNORM), E(R16G16B16A16_SNORM), E(R16G16B16A16_UINT), E(R16G16B16A16_SINT),
    [0x2E] = E(R32G32B32A32_SFLOAT), E(R32G32B32A32_UINT), E(R32G32B32A32_SINT),
    [0x31] = E(S8_UINT), E(D16_UNORM), E(X8_D24_UNORM_PACK32), E(D32_SFLOAT), E(D24_UNORM_S8_UINT), E(D32_SFLOAT_S8_UINT),
    [0x37] = X(R8G8B8A8_SRGB), E(R8G8B8A8_SRGB),
    [0x39] = S(A4B4G4R4_UNORM_PACK16), N, N, N,
    [0x3D] = E(A2B10G10R10_UNORM_PACK32), E(A2B10G10R10_UINT_PACK32), E(B10G11R11_UFLOAT_PACK32), E(E5B9G9R9_UFLOAT_PACK32),
    [0x41] = E(BC1_RGB_UNORM_BLOCK), E(BC1_RGBA_UNORM_BLOCK), E(BC2_UNORM_BLOCK), E(BC3_UNORM_BLOCK),
    [0x45] = E(BC1_RGB_SRGB_BLOCK), E(BC1_RGBA_SRGB_BLOCK), E(BC2_SRGB_BLOCK), E(BC3_SRGB_BLOCK),
    [0x49] = E(BC4_UNORM_BLOCK), E(BC4_SNORM_BLOCK), E(BC5_UNORM_BLOCK), E(BC5_SNORM_BLOCK), E(BC7_UNORM_BLOCK), E(BC7_SRGB_BLOCK),
    [0x4F] = E(BC6H_SFLOAT_BLOCK), E(BC6H_UFLOAT_BLOCK),
    // *_UI2F / *_I2F: integer storage read as float without normalising; Vulkan has no such view either (the USCALED / SSCALED formats are vertex
    // formats): the same-width integer format, as on Metal
    [0x51] = S(R8_UINT), S(R8_SINT), S(R16_UINT), S(R16_SINT), S(R32_UINT), S(R32_SINT),
    [0x57] = S(R8G8_UINT), S(R8G8_SINT), S(R16G16_UINT), S(R16G16_SINT), S(R32G32_UINT), S(R32G32_SINT),
    [0x5D] = N, N, N, N, N, N,
    [0x63] = S(R8G8B8A8_UINT), S(R8G8B8A8_SINT), S(R16G16B16A16_UINT), S(R16G16B16A16_SINT), S(R32G32B32A32_UINT), S(R32G32B32A32_SINT),
    [0x69] = E(A2B10G10R10_SNORM_PACK32), E(A2B10G10R10_SINT_PACK32), S(A2B10G10R10_UINT_PACK32), S(A2B10G10R10_SINT_PACK32),
    [0x6D] = X(R8G8B8A8_UNORM), X(R8G8B8A8_SNORM), X(R8G8B8A8_UINT), X(R8G8B8A8_SINT),
    [0x71] = X(R16G16B16A16_SFLOAT), X(R16G16B16A16_UNORM), X(R16G16B16A16_SNORM), X(R16G16B16A16_UINT), X(R16G16B16A16_SINT),
    [0x76] = X(R32G32B32A32_SFLOAT), X(R32G32B32A32_UINT), X(R32G32B32A32_SINT),
    // 0x79–0x94 ASTC, computed below
    [0x95] = N, N, N, N,
    [0x99] = X(B8G8R8A8_UNORM), E(B8G8R8A8_UNORM), X(B8G8R8A8_SRGB), E(B8G8R8A8_SRGB),
};

int vk_fmt(int f, VkFormat *out, VkComponentMapping *swz) {
    ent e = {0, 0, 0};
    // NVN's ASTC order (4x4 5x4 5x5 6x5 6x6 8x5 8x6 8x8 10x5 10x6 10x8 10x10 12x10 12x12, LDR then sRGB) is Vulkan's, whose unorm and sRGB alternate
    if (f >= 0x79 && f <= 0x94) { int k = (f - 0x79) % 14; e = (ent){(uint32_t)(VK_FORMAT_ASTC_4x4_UNORM_BLOCK + 2 * k + (f >= 0x87)), 1, 0}; }
    else if (f > 0 && f < (int)(sizeof T / sizeof *T)) e = T[f];
    if (out) *out = e.kind ? (VkFormat)e.vk : VK_FORMAT_UNDEFINED;
    if (swz) *swz = (VkComponentMapping){VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                         e.noalpha ? VK_COMPONENT_SWIZZLE_ONE : VK_COMPONENT_SWIZZLE_IDENTITY};
    return e.kind;
}
