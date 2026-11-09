#!/usr/bin/env python3
"""BRX translated as sequential branches (plan item 295; nx2android 02e336f's finding, MIT, written anew): the translator's BRX emitter
(third_party/ryujinx/.../Instructions/InstEmitFlowControl.cs) never takes the chained form, which lost the jumps to the next target where a case
jumps past the later ones, and logs each BRX into the reflection's notes ("translator: BRX at 0xN with N targets: sequential branches", with
"(the chained form would have been taken)" where it would have). Checks the source, then the built reflections: the shaders with a BRX note, and
how many the chained form would have taken; every BRX note says sequential. The chain before (b-47b5de2aa276, the broken variant): 1 fails.
Usage: t_xlate_brx.py [<shader cache>/b-<chain version>]   (default: $DM_ROOT/build/cache/metal_shader_cache/b-<this chain>)"""
import glob, json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..', '..')
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')
if len(sys.argv) > 1: d = sys.argv[1]
else: d = os.path.join(DM_ROOT, 'build/cache/metal_shader_cache', 'b-' + subprocess.run([sys.executable, os.path.join(HERE, '..', 'tools', 'dmsh_chain.py')], capture_output=True, text=True).stdout.strip())
n = fail = 0
def check(what, ok):
    global n, fail
    n += 1; fail += not ok; print(f'{"✓" if ok else "✗"} {what}')
src = open(os.path.join(ROOT, 'third_party', 'ryujinx', 'src', 'Ryujinx.Graphics.Shader', 'Instructions', 'InstEmitFlowControl.cs'), encoding='utf-8').read()
i = src.find('sequential branches{(allTargetsSinglePred'); j = src.find('allTargetsSinglePred = false;', i); k = src.find('if (allTargetsSinglePred)', i)
check('the BRX emitter logs each BRX, then never takes the chained form (InstEmitFlowControl.cs)', i > 0 and i < j < k)
if not os.path.isdir(d):
    print(f'(no reflections at {d}: the built reflections not checked)'); print(f'== {n} checks, {fail} failed =='); sys.exit(fail != 0)
with_brx = chained = notes = 0; other = []
for f in glob.glob(os.path.join(d, '*_b.json')):
    if os.path.basename(f).startswith('_'): continue
    b = [x for x in json.load(open(f)).get('notes', []) if x.startswith('translator: BRX at ')]
    if not b: continue
    with_brx += 1; notes += len(b); chained += any('the chained form would have been taken' in x for x in b)
    other += [x for x in b if 'sequential branches' not in x]
check(f'the built reflections name the shaders with a BRX ({with_brx} shaders, {notes} BRX, the chained form would have taken {chained} shaders), '
      'each as sequential branches', with_brx > 0 and not other)
print(f'== {n} checks, {fail} failed ==')
sys.exit(fail != 0)
