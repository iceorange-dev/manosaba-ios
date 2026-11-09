#!/usr/bin/env python3
"""The Mac shader libraries' minimum system (plan item 311; MacNativeGames #1845): gfx/tools/dmsh_build.py compiles them for MAC_MIN, the Metal 4
host's minimum system (make -C host print-macos-min); built with Xcode 27.2 and no minimum they target macOS 27.2 (air64_v29-apple-macosx27.2.0 in
the library) and fail to load on macOS 26. A kernel compiled with dmsh_build.py's flags records macosx26.0; the built libraries of this chain
record no newer system than MAC_MIN (a sample of 40). The flag left out (T_MAC_MIN_OLD=1, the broken variant, as before): 2 fail."""
import glob, os, re, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
DM_ROOT = os.environ.get('DM_ROOT') or os.path.join(ROOT, 'work')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
src = open(os.path.join(ROOT, 'gfx', 'tools', 'dmsh_build.py'), encoding='utf-8').read()
mac_min = re.search(r"^MAC_MIN = '([0-9.]+)'", src, re.M).group(1)
flags = re.search(r"^MAC_METAL = (\[.*\])", src, re.M).group(1)
metal = eval(flags.replace("'-mmacosx-version-min=' + MAC_MIN", repr('-mmacosx-version-min=' + mac_min)))
if os.environ.get('T_MAC_MIN_OLD'): metal = [a for a in metal if not a.startswith('-mmacosx-version-min')]
host_min = subprocess.run(['make', '-s', '-C', os.path.join(ROOT, 'host'), 'GAME=manosaba', 'print-macos-min'], capture_output=True, text=True).stdout.strip()
check(f'dmsh_build.py compiles for the Metal 4 host\'s minimum system ({mac_min} = {host_min})', mac_min == host_min and any(a == '-mmacosx-version-min=' + mac_min for a in metal))
def target(lib):   # the macOS version the library records
    out = subprocess.run(['strings', '-a', lib], capture_output=True, text=True).stdout
    m = re.search(r'air64[\w]*-apple-macosx([0-9]+(?:\.[0-9]+)*)', out); return m.group(1) if m else None
ver = lambda s: tuple((tuple(int(x) for x in s.split('.')) + (0, 0, 0))[:3])   # 26.0.0 == 26.0
with tempfile.TemporaryDirectory() as w:
    k = os.path.join(w, 'k.metal'); open(k, 'w').write('#include <metal_stdlib>\nusing namespace metal;\nkernel void k(device float *o [[buffer(0)]]) { o[0] = 1.0; }\n')
    subprocess.run([*metal, k, '-o', os.path.join(w, 'k.air')], check=True)
    subprocess.run(['xcrun', '-sdk', 'macosx', 'metallib', os.path.join(w, 'k.air'), '-o', os.path.join(w, 'k.metallib')], check=True)
    t = target(os.path.join(w, 'k.metallib'))
check(f'a kernel compiled with its flags records macOS {mac_min} or older (records {t}) [T_MAC_MIN_OLD]', t is not None and ver(t) <= ver(mac_min))
chain = subprocess.run([sys.executable, os.path.join(ROOT, 'gfx', 'tools', 'dmsh_chain.py')], capture_output=True, text=True).stdout.strip()
libs = sorted(glob.glob(os.path.join(DM_ROOT, 'build', 'cache', 'metal_shader_cache', 'b-' + chain, '*_b.metallib')))[:40]
newer = [os.path.basename(l) for l in libs if (lambda v: v is None or ver(v) > ver(mac_min))(target(l))]
if not libs: print(f'(no built libraries of chain {chain}: that check skipped)')
elif not os.environ.get('T_MAC_MIN_OLD'):
    check(f'the built libraries of chain {chain} record no newer system ({len(libs)} sampled)', libs and not newer, ', '.join(newer[:4]))
print(f'{total} checks, {fails} failed')
sys.exit(1 if fails else 0)
