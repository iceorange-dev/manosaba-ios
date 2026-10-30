#!/usr/bin/env python3
"""t_vk_be's shader bundle (plan item 301e2): a vertex and a pixel shader written in GLSL with the translator's names (glslc from the NDK makes
the modules), moved and reflected by gfx/tools/dmsh_vk.py as the game's shaders are, with manosaba's c[2] layout. The vertex shader passes
attribute 0 through as the position; the pixel shader writes uniform slot 0's first vector times texture slot 3 sampled at its centre (a second
key for the same pixel shader: a program of its own; a third that also writes 1 to storage slot 0's word 0); the compute shader (groups of 4)
writes storage slot 0's word i = i + 1000 x + 100000 y from the workgroup counts in c0 (+0x200), word 64 = the green of image slot 0's texture
fetched, and image slot 1's texel (i, 0) = (16 i, 0, 0, 255) / 255. Usage: mk_vk_bundle.py <out>   (keys: 7465737400000001 vertex,
7465737400000002, 03 and 04 pixel, 7465737400000005 compute)"""
import os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'gfx', 'tools')); import dmsh_vk as dv
GLSLC = os.path.join(os.environ.get('NDK', '/opt/homebrew/share/android-ndk'), 'shader-tools', 'darwin-x86_64', 'glslc')
ns = dv.game_ns('manosaba')
TEX3 = (ns['TEX_BASE'] + 8 * 3) // 4   # the c[2] word of texture slot 3: its combined handle
VS = '''#version 450
layout(location = 0) in vec4 a_pos;
void main() { gl_Position = a_pos; }
'''
PS = '''#version 450
layout(set = 0, binding = 0) uniform fp_c3 { vec4 color; } u;
layout(set = 0, binding = 1) uniform sampler2D fp_t_cb2_%X;
layout(location = 0) out vec4 o;
void main() { o = u.color * texture(fp_t_cb2_%X, vec2(0.5)); }
''' % (TEX3, TEX3)
PS_SB = PS.replace('layout(location = 0) out', 'layout(set = 0, binding = 2) buffer fp_s0 { uint v[]; } sb;\nlayout(location = 0) out').replace('void main() {', 'void main() { sb.v[0] = 1u;')
IMG = lambda slot: (ns['IMG_BASE'] + 8 * slot) // 4   # the c[2] word of image slot n
CS = '''#version 450
layout(local_size_x = 4) in;
layout(set = 0, binding = 0) uniform cp_c0 { uvec4 d[160]; } c0;
layout(set = 0, binding = 1) buffer cp_s0 { uint v[]; } sb;
layout(set = 0, binding = 2) uniform sampler2D cp_t_tcb_%X;
layout(set = 0, binding = 3) uniform writeonly image2D cp_i_cb2_%X_;
void main() {
    uint i = gl_GlobalInvocationID.x;
    sb.v[i] = i + 1000u * c0.d[32].x + 100000u * c0.d[32].y;
    if (i == 0u) sb.v[64] = uint(texelFetch(cp_t_tcb_%X, ivec2(0), 0).g * 255.0 + 0.5);
    imageStore(cp_i_cb2_%X_, ivec2(int(i), 0), vec4(float(16u * i) / 255.0, 0.0, 0.0, 1.0));
}
''' % (IMG(0), IMG(1), IMG(0), IMG(1))
CS_RES = {'sbufs': [{'binding': 1, 'cb_slot': 0, 'cb_offset': 0x310 // 4, 'flags': 1}]}   # storage slot 0 (the compute table at c0 + 0x310), written
PS_RES = {'sbufs': [{'binding': 2, 'cb_slot': 0, 'cb_offset': 0x510 // 4, 'flags': 1}]}   # (the pixel table at c0 + 0x510)
out, shaders = sys.argv[1], []
with tempfile.TemporaryDirectory() as w:
    for key, st, stage, src, res in [(0x7465737400000001, 'vs', 'vert', VS, {}), (0x7465737400000002, 'ps', 'frag', PS, {}), (0x7465737400000003, 'ps', 'frag', PS, {}),
                                     (0x7465737400000004, 'ps', 'frag', PS_SB, PS_RES), (0x7465737400000005, 'cs', 'comp', CS, CS_RES)]:
        g = os.path.join(w, f'{st}.glsl'); sp = os.path.join(w, f'{st}.spv'); open(g, 'w').write(src)
        subprocess.run([GLSLC, f'-fshader-stage={stage}', '--target-env=vulkan1.3', '-o', sp, g], check=True)
        spv = dv.patch_module(open(sp, 'rb').read(), dv.SHIFT[st])
        binds, vin, vout, unknown = dv.reflect(f'{key:016x}', st, spv, res, ns['decode_tex'])
        if unknown: sys.exit(f'[red] {st}: bindings not understood: {unknown}')
        shaders.append((key, dv.STAGE_NO[st], spv, dv.pack_refl(binds, vin, vout, b'', [4, 1, 1] if st == 'cs' else None)))
open(out, 'wb').write(dv.pack_bundle(shaders))
print(f'{out}: {len(shaders)} shaders')
