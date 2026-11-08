#!/usr/bin/env python3
"""TMML in the built shaders (plan item 311; nx2android 44eca7e, MIT; yuzu #10234's rule): Maxwell returns a texture's LOD as unsigned 24.8 with a
negative LOD (a magnified texture) as 0, so after the LOD x 256 every built library converts max(lod x 256, 0) to uint; the translator used to
convert it to int, which a shader reading it as unsigned saw as about 16 million (Tears of the Kingdom's sea of clouds). Checks every Mac MSL
source of the chain that queries a LOD (2,829 in Tears of the Kingdom and manosaba): each LOD x 256 goes through a max with 0 into a uint.
The libraries of the chain before (b-ae6a8bd1acdd, the broken variant): every one fails.
Usage: t_tmml_msl.py [<shader cache>/b-<chain version>]   (default: $DM_ROOT/build/cache/metal_shader_cache/b-<this chain>)"""
import glob, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')
if len(sys.argv) > 1: d = sys.argv[1]
else: d = os.path.join(DM_ROOT, 'build/cache/metal_shader_cache', 'b-' + subprocess.run([sys.executable, os.path.join(HERE, '..', 'tools', 'dmsh_chain.py')], capture_output=True, text=True).stdout.strip())
lod = re.compile(r'calculate_unclamped_lod')
scaled = re.compile(r'float (_\w+) = (_\w+) \* 256\.0;')
files = [f for f in sorted(glob.glob(os.path.join(d, '*_b.metal'))) if 'calculate_unclamped_lod' in open(f, errors='ignore').read()]
sites = bad = 0; first = ''
for f in files:
    lines = open(f, errors='ignore').read().split('\n')
    for i, ln in enumerate(lines):
        m = scaled.search(ln)
        if not m or not any(lod.search(x) for x in lines[max(0, i - 4):i]): continue
        v = m.group(1); sites += 1
        use = next((x for x in lines[i + 1:i + 4] if re.search(r'\b' + v + r'\b', x)), '')
        ok = re.search(r'uint\((?:fast::|precise::)?max\(' + v + r', 0\.0\)\)', use) is not None   # in one expression, or through a temporary:
        m2 = re.search(r'float (_\w+) = (?:fast::|precise::)?max\(' + v + r', 0\.0\);', use)
        if m2: ok = any(re.search(r'uint \w+ = uint\(' + m2.group(1) + r'\);', x) for x in lines[i + 2:i + 5])
        if not ok:
            bad += 1
            if not first: first = f'{os.path.basename(f)}: {use.strip()[:100]}'
print(f'{len(files)} shaders query a LOD, {sites} LOD x 256 sites; {bad} not clamped at 0 into uint' + (f' (first: {first})' if first else ''))
ok = files and sites and not bad
print(f'{"✓" if ok else "✗"} every TMML result is max(LOD x 256, 0) as uint')
print(f'== 1 checks, {0 if ok else 1} failed ==')
sys.exit(0 if ok else 1)
