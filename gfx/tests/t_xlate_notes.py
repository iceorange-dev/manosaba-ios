#!/usr/bin/env python3
"""The translator's own warnings in the reflections (plan item 317c): gfx/tools/dmsh_xlate/Program.cs's Accessor implements the translator's Log
(ILogger's default printed nothing) and keeps each message in the reflection's notes as "translator: <message>"; the built reflections of the
chain carry them. Prints them by kind (the numbers made into N) and the shaders each kind is in. The chain before (b-1f985598396c, the broken
variant): 1 fails.
Usage: t_xlate_notes.py [<shader cache>/b-<chain version>]   (default: $DM_ROOT/build/cache/metal_shader_cache/b-<this chain>)"""
import collections, glob, json, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..', '..')
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')
if len(sys.argv) > 1: d = sys.argv[1]
else: d = os.path.join(DM_ROOT, 'build/cache/metal_shader_cache', 'b-' + subprocess.run([sys.executable, os.path.join(HERE, '..', 'tools', 'dmsh_chain.py')], capture_output=True, text=True).stdout.strip())
n = fail = 0
def check(what, ok):
    global n, fail
    n += 1; fail += not ok; print(f'{"✓" if ok else "✗"} {what}')
src = open(os.path.join(ROOT, 'gfx', 'tools', 'dmsh_xlate', 'Program.cs'), encoding='utf-8').read()
check('the translator\'s Log kept in the notes (Program.cs)', 'public void Log(string message) => Notes.Add("translator: " + message);' in src)
kinds = collections.Counter(); shaders = collections.Counter(); refl = 0
for f in glob.glob(os.path.join(d, '*_b.json')):
    if os.path.basename(f).startswith('_'): continue
    refl += 1; seen = set()
    for note in json.load(open(f)).get('notes', []):
        if note.startswith('translator: '): k = re.sub(r'0x[0-9A-Fa-f]+|\d+', 'N', note); kinds[k] += 1; seen.add(k)
    for k in seen: shaders[k] += 1
for k, v in kinds.most_common(12): print(f'  {v:6d} in {shaders[k]:5d} shaders: {k}')
check(f'the built reflections carry them ({refl} reflections, {sum(kinds.values())} notes of {len(kinds)} kinds)', refl > 0 and sum(kinds.values()) > 0)
print(f'== {n} checks, {fail} failed ==')
sys.exit(fail != 0)
