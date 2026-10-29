#!/usr/bin/env python3
"""gfx/tools/dmsh_vk.py, the Vulkan shader bundle (plan item 301e2), without the translator: the bundle written and read back as gfx/nvn/vk/
vk_shader.c reads it (28-byte entries, each blob where its entry says, at a 4-byte boundary, sorted by key), a module's bindings moved and the
storage-image capabilities added only when it has an image of no declared format, a reflection record laid out as vk_shader.c's parse_refl takes
it, the c[2] handles of manosaba's layout decoded (dmsh_build.py's decode_tex, as the bundle tool takes it) to a texture slot and a separate
sampler slot, the declared dimensions. When the workspace has the game's bundle, every entry of it is a SPIR-V module with a reflection that
reads, and the bindings of a sample of its modules (40 of each stage) are their reflection's. T_DMVK_BROKEN=stride (the first writer: offsets
counted with 32-byte entries): 2 fail; T_DMVK_BROKEN=shift2 (a pixel shader's bindings moved again in its reflection, as at first): 1 fails."""
import json, os, struct, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'gfx', 'tools')); import dmsh_vk as dv
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))

def words(*w): return struct.pack(f'<{len(w)}I', *w)
SPV_MAGIC = 0x07230203
def module(*body): return words(SPV_MAGIC, 0x10500, 0, 100, 0) + b''.join(body)   # a header and the instructions given
def ins(op, *ops): return words(((1 + len(ops)) << 16) | op, *ops)

# ---- the bundle ----
shaders = [(0x30, 1, module(ins(17, 1)), b'\x01\x02\x03'), (0x10, 0, module(ins(17, 1), ins(17, 2)), b'\x09' * 17), (0x20, 5, module(), b'')]
b = dv.pack_bundle(shaders); r = dv.read_bundle(b)
ents = [struct.unpack_from(dv.ENT, b, 12 + 28 * i) for i in range(3)]
check('the bundle reads back: each shader\'s SPIR-V and reflection where its entry says', all(r.get(k) == (st, spv, refl) for k, st, spv, refl in shaders),
      f'{[(hex(k), len(v[1]), len(v[2])) for k, v in r.items()]}')
check('entries of 28 bytes (vk_shader.c\'s packed ent): the first blob right after them', struct.calcsize(dv.ENT) == 28 and ents[0][2] == 12 + 28 * 3, f'first blob at {ents[0][2]}')
check('entries sorted by key (vk_shader.c searches them by halves), every blob at a 4-byte boundary',
      [e[0] for e in ents] == [0x10, 0x20, 0x30] and all(e[2] % 4 == 0 and e[4] % 4 == 0 for e in ents))

# ---- patch_module ----
OP_DECORATE, DEC_BINDING, OP_TYPE_IMAGE, OP_CAPABILITY = 71, 33, 25, 17
img_nofmt = ins(OP_TYPE_IMAGE, 7, 6, 1, 0, 0, 0, 2, 0)   # result, sampled type, 2D, depth, arrayed, MS, sampled 2 (storage), format Unknown
img_fmt = ins(OP_TYPE_IMAGE, 7, 6, 1, 0, 0, 0, 2, 1)     # ... format Rgba32f
m = dv.patch_module(module(ins(OP_CAPABILITY, 1), ins(OP_DECORATE, 9, DEC_BINDING, 3), ins(OP_DECORATE, 9, 34, 0), img_nofmt), 64)
w = struct.unpack(f'<{len(m) // 4}I', m)
caps = [w[i + 1] for i in range(5, len(w) - 1) if w[i] == ((2 << 16) | OP_CAPABILITY)]
binding = next(w[i + 3] for i in range(5, len(w) - 3) if w[i] == ((4 << 16) | OP_DECORATE) and w[i + 2] == DEC_BINDING)
dset = next(w[i + 3] for i in range(5, len(w) - 3) if w[i] == ((4 << 16) | OP_DECORATE) and w[i + 2] == 34)
check('a module\'s Binding moved by the stage\'s shift (pixel +64), its DescriptorSet kept', binding == 67 and dset == 0, f'binding {binding}, set {dset}')
check('an image of no declared format: StorageImageRead/WriteWithoutFormat added beside the module\'s capabilities', sorted(caps) == [1, 55, 56], f'{caps}')
m2 = dv.patch_module(module(ins(OP_CAPABILITY, 1), img_fmt), 0)
check('... and none when its images declare their format', m2 == module(ins(OP_CAPABILITY, 1), img_fmt))

# ---- the reflection record (vk_shader.c parse_refl) ----
binds = [(dv.K_TEX, 1, 66, 1, 3, 5, 0, 1, 2), (dv.K_CBUF, 6, 1, 1, 0, -1, 0, 0, 0)]
rf = dv.pack_refl(binds, [(0, 0), (3, 2)], [], b'\xAA\xBB', [8, 4, 1])
h = struct.unpack_from('<8H', rf, 0); rec = struct.unpack_from('<BBHHhhBBH', rf, 16)
check('a reflection: 16-byte header (bindings, inputs, outputs, c1 bytes, group size), 14-byte binding records, inputs, then the c1 bytes',
      h[:7] == (2, 2, 0, 2, 8, 4, 1) and rec == binds[0] and struct.unpack_from('<BBHHhhBBH', rf, 30) == binds[1] and rf[44:48] == b'\x00\x00\x03\x02' and rf[48:] == b'\xAA\xBB',
      f'{h} {rec}')

# ---- c[2] handles, manosaba's layout ----
gj = json.load(open(os.path.join(ROOT, 'games', 'manosaba', 'game.json')))['nvn_c2']
TB, SB = int(gj['tex'][0], 16), int(gj['samp'][0], 16)
ns = {'os': os, 'struct': struct, 're': __import__('re'), 'TEX_BASE': TB, 'TEX_END': TB + 8 * gj['tex'][1], 'SAMP_BASE': SB, 'SAMP_END': SB + 8 * gj['samp'][1],
      'IMG_BASE': int(gj['img'][0], 16), 'IMG_END': int(gj['img'][0], 16) + 8 * gj['img'][1]}
dv.from_dmsh_build(['decode_tex'], ns); dt = ns['decode_tex']
comb = (TB + 8 * 3) // 4; sep = (1 << 28) | (((SB + 8 * 5) // 4) << 14) | comb
check('a combined handle in c[2]\'s texture table: its texture slot, no sampler slot', dt(f'fp_t_cb2_{comb:X}') == (3, None, 0), f'{dt(f"fp_t_cb2_{comb:X}")}')
check('a separate pair: the texture slot, and the sampler slot from the sampler table', dt(f'fp_t_cb2_{sep:X}') == (3, 5, 1), f'{dt(f"fp_t_cb2_{sep:X}")}')
check('declared dimensions: 2D array (shadow), integer cube, 3D, an unknown type as 2D',
      (dv.dim_of('sampler2DArrayShadow'), dv.dim_of('isamplerCube'), dv.dim_of('sampler3D'), dv.dim_of('image2D')) == (1, 2, 4, 0))

# ---- a pixel shader's reflection (glslc from the NDK makes the module; the translator's names): bindings as the moved module has them ----
NDK = os.environ.get('NDK', '/opt/homebrew/share/android-ndk'); GLSLC = os.path.join(NDK, 'shader-tools', 'darwin-x86_64', 'glslc')
FRAG = '''#version 450
layout(set = 0, binding = 0) uniform support_buffer { vec4 x; } support_buffer_i;
layout(set = 0, binding = 1) uniform fp_c4 { vec4 y; } fp_c4_i;
layout(set = 0, binding = 2) buffer fp_s1 { vec4 z[]; } fp_s1_i;
layout(set = 0, binding = 3) uniform sampler2DArray fp_t_cb2_%X;
layout(location = 0) in vec4 v;
layout(location = 0) out vec4 o;
void main() { o = support_buffer_i.x + fp_c4_i.y + fp_s1_i.z[0] + texture(fp_t_cb2_%X, vec3(v.xy, 0)); fp_s1_i.z[1] = v; }
''' % (comb, comb)
import shutil, subprocess, tempfile
if os.path.exists(GLSLC) and shutil.which('spirv-cross'):
    with tempfile.TemporaryDirectory() as w:
        open(os.path.join(w, 'f.frag'), 'w').write(FRAG)
        subprocess.run([GLSLC, '-fshader-stage=frag', '--target-env=vulkan1.3', '-o', os.path.join(w, 'f.spv'), os.path.join(w, 'f.frag')], check=True)
        spv = dv.patch_module(open(os.path.join(w, 'f.spv'), 'rb').read(), dv.SHIFT['ps'])
    dv.IMG[:] = [ns['IMG_BASE'], ns['IMG_END']]
    res = {'sbufs': [{'binding': 2, 'cb_slot': 0, 'cb_offset': (0x510 + 16 * 1) // 4, 'flags': 1}]}   # storage slot 1 (pixel table at c0 + 0x510), written
    bs, vin, vout, unk = dv.reflect('t', 'ps', spv, res, dt)
    got = sorted((x[2], x[0], x[4], x[5], x[6], x[8]) for x in bs)   # binding, kind, reg, sreg, rw, off
    want = [(64, dv.K_SUPPORT, -1, -1, 0, 0), (65, dv.K_CBUF, 1, -1, 0, 0), (66, dv.K_SBUF, 1, -1, 1, 0), (67, dv.K_TEX, 3, -1, 0, 1)]
    check('a pixel shader\'s reflection: each binding as its moved module has it (64 + n), its slot (uniform 4 → 1, storage 1 written, texture 3 as a 2D array)',
          got == want and not unk and vout == [(0, 0)], f'{got} {unk} {vout}')
else: print('(no glslc or spirv-cross: the reflection check skipped)')

# ---- the workspace's bundle, when there is one ----
ws = os.environ.get('DM_ROOT') or os.path.join(ROOT, 'work'); bp = os.path.join(ws, 'build', 'android', 'manosaba.dmvk')
if os.path.exists(bp):
    rb = dv.read_bundle(open(bp, 'rb').read()); bad = []
    for k, (st, spv, refl) in rb.items():
        if len(spv) < 20 or struct.unpack_from('<I', spv)[0] != SPV_MAGIC or len(refl) < 16: bad.append(hex(k)); continue
        nb, nv, nout, c1 = struct.unpack_from('<4H', refl)
        if nb > 64 or nv > 32 or nout > 8 or 16 + 14 * nb + 2 * (nv + nout) + c1 != len(refl): bad.append(hex(k))
    check(f'the workspace\'s manosaba bundle: each of its {len(rb)} entries a SPIR-V module, its reflection the length its header says', rb and not bad, f'{len(bad)} bad: {bad[:4]}')
    if shutil.which('spirv-cross'):
        off = []; per = {}
        for k, (st, spv, refl) in sorted(rb.items()):
            if per.get(st, 0) >= 40 or len(spv) < 20: continue
            per[st] = per.get(st, 0) + 1
            r = json.loads(subprocess.run(['spirv-cross', '-', '--reflect'], input=spv, capture_output=True, check=True).stdout)
            mod = sorted(e['binding'] for x in ('ubos', 'ssbos', 'textures', 'images') for e in r.get(x, []))
            nb = struct.unpack_from('<H', refl)[0]; rec = sorted(struct.unpack_from('<BBHHhhBBH', refl, 16 + 14 * i)[2] for i in range(nb))
            if mod != rec: off.append(f'{k:016x}: module {mod[:4]} reflection {rec[:4]}')
        check(f'... the bindings of {sum(per.values())} of its modules (stages {sorted(per)}) are their reflection\'s', not off, '; '.join(off[:3]))
else: print(f'(no bundle at {bp}: its check skipped)')
print(f't_dmsh_vk: {total} checks, {fails} failed'); sys.exit(fails != 0)
