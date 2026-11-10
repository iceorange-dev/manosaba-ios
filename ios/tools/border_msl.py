#!/usr/bin/env python3
"""The MSL sources the iOS app carries for sampler border variants (plan item 214): gfx/nvn/nvn_mtl.m border_variant compiles a variant of a
shader that samples through a sampler whose border colour Metal lacks from its .metal beside its library, which an iOS app did not have (a red
line, and the edge colour drawn). The shaders are listed in games/<id>/border_msl.txt, and with each listed VERTEX shader every vertex shader
whose texture and sampler bindings are the same (names, kinds, registers, from the app's reflection json): Tears of the Kingdom's grass is drawn
by hundreds of such vertex shaders, and each walk on the iPhone met another one (2026-10-22: 8126ed56ae359349_b, 86b5745642f11b32_b,
11687c00bab7d73b_b; 312 share their bindings, 7.2 MB of MSL). A source carried costs only its size: the variant is compiled only when such a
sampler is bound. The sources are the Mac's (the same MSL the iOS libraries are compiled from).
The same for the shaders of games/<id>/lodbias_msl.txt (plan item 318; nvn_mtl.m lb_variant: a sampler's LOD bias carried by the shader where the
GPU ignores the sampler's or the OS is before 26), named by a fourth argument.
  border_msl.py <list> <Mac MSL dir b-V> <the app's MSL dir b-V (its .json read for the bindings)> [what the variants are for: "border" by default]
Exit 0 fine, 1 a listed source missing (named), 2 usage."""
import glob, json, os, shutil, sys

def keys_of(path):
    return [l.strip() for l in open(path, encoding='utf-8') if l.strip() and not l.startswith('#')]

def refl(app, k):
    try: return json.load(open(os.path.join(app, k + '.json'), encoding='utf-8'))
    except (OSError, ValueError): return None

def tex_sig(j):
    return frozenset((b.get('msl_name'), b.get('kind'), b.get('reg')) for b in j.get('bindings', []) if b.get('kind') in ('tex', 'samp_tex', 'samp'))

def expand(keys, app):   # the listed keys, then the vertex shaders with a listed vertex shader's texture bindings
    sigs = set()
    for k in keys:
        j = refl(app, k)
        if j and j.get('stage') == 'vs' and tex_sig(j): sigs.add(tex_sig(j))
    out = list(keys)
    if os.environ.get('DM_TEST_BORDER_MSL_LISTED_ONLY') == '1': return out   # negative control of tools/tests/t_border_msl.py only: the list alone
    seen = set(keys)
    for p in sorted(glob.glob(os.path.join(app, '*.json'))):
        k = os.path.basename(p)[:-5]
        if k in seen: continue
        j = refl(app, k)
        if j and j.get('stage') == 'vs' and tex_sig(j) in sigs: out.append(k); seen.add(k)
    return out

def copy(keys, src, dst):
    missing = [k for k in keys if not os.path.isfile(os.path.join(src, k + '.metal'))]
    if missing: return missing
    if os.environ.get('DM_TEST_BORDER_MSL_NONE') == '1': return []   # negative control of tools/tests/t_border_msl.py only: nothing copied
    for k in keys: shutil.copyfile(os.path.join(src, k + '.metal'), os.path.join(dst, k + '.metal'))
    return []

if __name__ == '__main__':
    if len(sys.argv) not in (4, 5): print(__doc__); sys.exit(2)
    what = sys.argv[4] if len(sys.argv) == 5 else 'border'
    listed = keys_of(sys.argv[1]); keys = expand(listed, sys.argv[3]); missing = copy(keys, sys.argv[2], sys.argv[3])
    if missing: print(f'[red] border_msl: no MSL source for {", ".join(missing[:8])}{" ..." if len(missing) > 8 else ""} in {sys.argv[2]}'); sys.exit(1)
    size = sum(os.path.getsize(os.path.join(sys.argv[3], k + '.metal')) for k in keys) if os.environ.get('DM_TEST_BORDER_MSL_NONE') != '1' else 0
    print(f'MSL sources for {what} variants: {len(keys)} ({len(listed)} listed, {len(keys) - len(listed)} vertex shaders with the same bindings), {size / 1e6:.1f} MB')
