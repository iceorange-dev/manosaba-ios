#!/usr/bin/env python3
"""The resolution scale per axis in the built shaders (plan item 320f; the translator's SupportBuffer.cs, InstEmitAttribute.cs,
HelperFunctionManager.cs, TexturePass.cs): the support buffer gains render_scale_y after its last field, at the offset the hosts fill
(gfx/nvn/mtl/args.inc SUPPORT_RENDER_SCALE_Y, gfx/nvn/vk/vk_bind.c), measured here from the MSL struct by Metal's alignment; every fragment
shader divides the fragment coordinate's y by render_scale_y[0] (its x by render_scale[0]), none y by render_scale[0]; every helper
reading render_scale[i] also reads render_scale_y[i] (the axis' scale: TexelFetchScale, TextureSizeUnscale); the reflections give texture
bindings their scale index (Tears of the Kingdom's upscale ac58fc18eda5f387: its one texture 0, as its MSL passes) and fragment shaders
frag_coord; widen_push after render_scale_y at the offset the Metal host writes (SUPPORT_WIDEN_PUSH), and every vertex shader ends with the
edge push (EmitterContext.EmitWidenPush: its three values read, the 0.994009 bound). The chain before (b-ce89a37d8939, the broken variant):
5 fail; b-8f8b45e7aaca (no push yet): 1 fails.
Usage: t_scale_msl.py [<shader cache>/b-<chain version>]   (default: $DM_ROOT/build/cache/metal_shader_cache/b-<this chain>)"""
import glob, json, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..', '..')
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')
if len(sys.argv) > 1: d = sys.argv[1]
else: d = os.path.join(DM_ROOT, 'build/cache/metal_shader_cache', 'b-' + subprocess.run([sys.executable, os.path.join(HERE, '..', 'tools', 'dmsh_chain.py')], capture_output=True, text=True).stdout.strip())
n = fail = 0
def check(what, ok):
    global n, fail
    n += 1; fail += not ok; print(f'{"✓" if ok else "✗"} {what}')
files = sorted(glob.glob(os.path.join(d, '*_b.metal')))
SIZE = {'uint': (4, 4), 'int': (4, 4), 'float': (4, 4), 'uint4': (16, 16), 'int4': (16, 16), 'float4': (16, 16), 'char': (1, 1)}
def layout(src):   # the support buffer's struct: [(member, type, count, offset)] by Metal's alignment (spirv-cross pads with char arrays where needed)
    m = re.search(r'constant (\w+)\* support_buffer \[\[id\(\d+\)\]\];', src)
    if not m: return None
    body = re.search(r'struct %s\s*\{(.*?)\};' % m.group(1), src, re.S)
    out, off = [], 0
    for t, nm, cnt in re.findall(r'^\s*(\w+) (\w+)(?:\[(\d+)\])?;', body.group(1), re.M):
        sz, al = SIZE[t]; off = (off + al - 1) // al * al; out.append((nm, t, int(cnt or 1), off)); off += sz * int(cnt or 1)
    return out
lay = next((l for l in (layout(open(f, errors='ignore').read()) for f in files[:400]) if l), None)
arrays = [x for x in (lay or []) if x[1] == 'float4' and x[2] == 73]
host_mtl = re.search(r'#define SUPPORT_RENDER_SCALE_Y (\d+)', open(os.path.join(ROOT, 'gfx/nvn/mtl/args.inc')).read())
host_vk = re.search(r'g_support\.cpu \+ (\d+) \+ 16 \* i\) = 1\.0f;  // render_scale_y', open(os.path.join(ROOT, 'gfx/nvn/vk/vk_bind.c')).read())
yoff = arrays[1][3] if len(arrays) == 2 else None
print(f'support buffer: render_scale at {arrays[0][3] if arrays else None}, render_scale_y at {yoff}; the hosts fill y at {host_mtl and host_mtl.group(1)} (Metal), {host_vk and host_vk.group(1)} (Vulkan)')
check('render_scale_y after the last field, where both hosts fill it', len(arrays) == 2 and arrays[0][3] == 192 and host_mtl and host_vk and yoff == int(host_mtl.group(1)) == int(host_vk.group(1)))
xm, ym = (arrays[0][0], arrays[1][0]) if len(arrays) == 2 else (arrays[0][0] if arrays else '_m5', None)
fx = fy_y = fy_x = 0; helpers = helpers_y = 0
for f in files:
    s = open(f, errors='ignore').read()
    for m0 in re.finditer(r'float (_\w+) = gl_FragCoord\.([xy]);', s):
        var, ax = m0.group(1), m0.group(2)
        m = re.search(r'float (_\w+) = \(\*spvDescriptorSet0\.support_buffer\)\.(_m\d+)\[0\]\.x;\s*float _\w+ = %s / \1;' % var, s)
        if not m: continue
        if ax == 'x' and m.group(2) == xm: fx += 1
        if ax == 'y': fy_y += m.group(2) == ym; fy_x += m.group(2) == xm
    for body in re.findall(r'\nint _\w+\(thread const int& [^)]*constant \w+& support_buffer[^)]*\)\n\{(.*?)\n\}', s, re.S):
        if re.search(r'support_buffer\.%s\[_\w+\]' % xm, body): helpers += 1; helpers_y += bool(ym and re.search(r'support_buffer\.%s\[_\w+\]' % ym, body))
print(f'{len(files)} shaders: fragment coordinate x / render_scale[0] {fx}, y / render_scale_y[0] {fy_y}, y / render_scale[0] {fy_x}; scale helpers {helpers}, of them reading render_scale_y {helpers_y}')
check('every fragment coordinate y divided by render_scale_y[0] (x by render_scale[0]), none by render_scale[0]', fx > 0 and fy_y > 0 and fy_x == 0)
check('every scale helper reads its axis\' table (render_scale_y beside render_scale)', helpers > 0 and helpers_y == helpers)
up = os.path.join(d, 'ac58fc18eda5f387_b.json'); refl = json.load(open(up)) if os.path.exists(up) else {}
tex = [b for b in refl.get('bindings', []) if b.get('kind') == 'tex']
print(f'ac58fc18eda5f387: texture bindings {[(b["msl_name"], b.get("scale_ix")) for b in tex]}, frag_coord {refl.get("frag_coord")}')
check('the reflection gives the upscale\'s one texture its scale index 0, and fragment shaders frag_coord', len(tex) == 1 and tex[0].get('scale_ix') == 0 and 'frag_coord' in refl)
pm = (lay or [])[-1] if lay and lay[-1][1] == 'float4' and lay[-1][2] == 1 and len(arrays) == 2 and lay[-1][3] > arrays[1][3] else None
host_push = re.search(r'#define SUPPORT_WIDEN_PUSH (\d+)', open(os.path.join(ROOT, 'gfx/nvn/mtl/args.inc')).read())
vs = tails = 0
for f in files:
    jf = f[:-6] + '.json'
    if not os.path.exists(jf) or json.load(open(jf)).get('stage') != 'vs': continue
    vs += 1; src = open(f, errors='ignore').read()
    tails += bool(pm and all(re.search(r'support_buffer\)\.%s\.%s;' % (pm[0], c), src) for c in 'xyz') and '0.9940090179443359375' in src)
print(f'widen_push at {pm and pm[3]} (the Metal host writes {host_push and host_push.group(1)}); vertex shaders {vs}, ending with the edge push {tails}')
check('widen_push where the host writes it, and every vertex shader ends with the edge push', pm is not None and host_push and pm[3] == int(host_push.group(1)) and vs > 0 and tails == vs)
print(f'== {n} checks, {fail} failed ==')
sys.exit(fail != 0)
