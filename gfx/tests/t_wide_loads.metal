// t_wide_loads.m's kernels, written as SPIRV-Cross writes the translator's storage reads (msl_helpers.py wide_loads rewrites this file):
// k_words  the words of one read-only buffer at 0..12, 16..24 and 32..36 bytes from a value that is not 16-byte aligned (one 4-, 3- and 2-word read)
// k_alias  a store through another binding of the same memory between two neighbouring words (they stay two reads)
// k_var    a loop's variable the offsets come from, assigned between the sums of two neighbouring words (they stay two reads)
// Every name is declared once in the file, as in one SPIR-V module (wide_loads leaves a name declared twice alone).
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _29
{
    uint _m0[1];
};

struct spvDescriptorSetBuffer0
{
    const device _29* cp_s0 [[id(0)]];
    device _29* cp_s1 [[id(1)]];
    device _29* cp_s2 [[id(2)]];
};

kernel void k_words(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    int _12 = int(gl_GlobalInvocationID.x) * 40 + 4;
    uint _14 = uint(int(uint(_12) >> uint(2)));
    float _16 = as_type<float>((*spvDescriptorSet0.cp_s0)._m0[int(_14)]);
    int _18 = _12 + 4;
    uint _20 = uint(int(uint(_18) >> uint(2)));
    float _22 = as_type<float>((*spvDescriptorSet0.cp_s0)._m0[int(_20)]);
    int _24 = _12 + 8;
    uint _26 = uint(int(uint(_24) >> uint(2)));
    uint _28 = (*spvDescriptorSet0.cp_s0)._m0[int(_26)];
    int _30 = _12 + 12;
    uint _32 = uint(int(uint(_30) >> uint(2)));
    float _34 = as_type<float>((*spvDescriptorSet0.cp_s0)._m0[int(_32)]);
    float _36 = _16 + _22;
    int _38 = _12 + 16;
    uint _40 = uint(int(uint(_38) >> uint(2)));
    uint _42 = (*spvDescriptorSet0.cp_s0)._m0[int(_40)];
    int _44 = _38 + 4;
    uint _46 = uint(int(uint(_44) >> uint(2)));
    uint _48 = (*spvDescriptorSet0.cp_s0)._m0[int(_46)];
    int _50 = _44 + 4;
    uint _52 = uint(int(uint(_50) >> uint(2)));
    uint _54 = (*spvDescriptorSet0.cp_s0)._m0[int(_52)];
    int _56 = _12 + 32;
    uint _58 = uint(int(uint(_56) >> uint(2)));
    uint _60 = (*spvDescriptorSet0.cp_s0)._m0[int(_58)];
    int _62 = _56 + 4;
    uint _64 = uint(int(uint(_62) >> uint(2)));
    uint _66 = (*spvDescriptorSet0.cp_s0)._m0[int(_64)];
    uint _68 = gl_GlobalInvocationID.x * 8u;
    (*spvDescriptorSet0.cp_s2)._m0[_68] = as_type<uint>(_36);
    (*spvDescriptorSet0.cp_s2)._m0[_68 + 1u] = _28 ^ as_type<uint>(_34);
    (*spvDescriptorSet0.cp_s2)._m0[_68 + 2u] = _42;
    (*spvDescriptorSet0.cp_s2)._m0[_68 + 3u] = _48;
    (*spvDescriptorSet0.cp_s2)._m0[_68 + 4u] = _54;
    (*spvDescriptorSet0.cp_s2)._m0[_68 + 5u] = _60;
    (*spvDescriptorSet0.cp_s2)._m0[_68 + 6u] = _66;
    (*spvDescriptorSet0.cp_s2)._m0[_68 + 7u] = as_type<uint>(_34);
}

kernel void k_alias(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    int _110 = int(gl_GlobalInvocationID.x) * 16;
    uint _112 = uint(int(uint(_110) >> uint(2)));
    uint _114 = (*spvDescriptorSet0.cp_s0)._m0[int(_112)];
    int _116 = _110 + 4;
    uint _118 = uint(int(uint(_116) >> uint(2)));
    (*spvDescriptorSet0.cp_s1)._m0[int(_118)] = _114 + 1000u;
    uint _120 = (*spvDescriptorSet0.cp_s0)._m0[int(_118)];
    uint _122 = gl_GlobalInvocationID.x * 2u;
    (*spvDescriptorSet0.cp_s2)._m0[_122] = _114;
    (*spvDescriptorSet0.cp_s2)._m0[_122 + 1u] = _120;
}

kernel void k_var(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    int _210;
    _210 = int(gl_GlobalInvocationID.x) * 32;
    int _212 = _210 + 0;
    uint _214 = uint(int(uint(_212) >> uint(2)));
    _210 = _210 + 16;
    int _216 = _210 + 4;
    uint _218 = uint(int(uint(_216) >> uint(2)));
    uint _220 = (*spvDescriptorSet0.cp_s0)._m0[int(_214)];
    uint _222 = (*spvDescriptorSet0.cp_s0)._m0[int(_218)];
    uint _224 = gl_GlobalInvocationID.x * 2u;
    (*spvDescriptorSet0.cp_s2)._m0[_224] = _220;
    (*spvDescriptorSet0.cp_s2)._m0[_224 + 1u] = _222;
}
