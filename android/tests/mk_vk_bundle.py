#!/usr/bin/env python3
"""t_vk_be's shader bundle (plan item 301e2): a vertex and a pixel shader written in GLSL with the translator's names (glslc from the NDK makes
the modules), moved and reflected by gfx/tools/dmsh_vk.py as the game's shaders are, with manosaba's c[2] layout. The vertex shader passes
attribute 0 through as the position; the pixel shader writes uniform slot 0's first vector times texture slot 3 sampled at its centre.
Usage: mk_vk_bundle.py <out>   (keys: 7465737400000001 vertex, 7465737400000002 pixel)"""
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
out, shaders = sys.argv[1], []
with tempfile.TemporaryDirectory() as w:
    for key, st, stage, src in [(0x7465737400000001, 'vs', 'vert', VS), (0x7465737400000002, 'ps', 'frag', PS)]:
        g = os.path.join(w, f'{st}.glsl'); sp = os.path.join(w, f'{st}.spv'); open(g, 'w').write(src)
        subprocess.run([GLSLC, f'-fshader-stage={stage}', '--target-env=vulkan1.3', '-o', sp, g], check=True)
        spv = dv.patch_module(open(sp, 'rb').read(), dv.SHIFT[st])
        binds, vin, vout, unknown = dv.reflect(f'{key:016x}', st, spv, {}, ns['decode_tex'])
        if unknown: sys.exit(f'[red] {st}: bindings not understood: {unknown}')
        shaders.append((key, dv.STAGE_NO[st], spv, dv.pack_refl(binds, vin, vout, b'', None)))
open(out, 'wb').write(dv.pack_bundle(shaders))
print(f'{out}: {len(shaders)} shaders')
