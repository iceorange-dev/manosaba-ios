#!/usr/bin/env python3
"""gfx/nvn/nvn_mtl.m and its parts in gfx/nvn/mtl (plan item 261): one translation unit split by subject, so each part must be whole on its own.
Every part is included exactly once, by nvn_mtl.m or by a translation unit of its own in gfx/nvn/mtl (plan item 264: mtl/present.m has three),
and every include names a part; each part starts with its own name line, which names the file that includes it, closes every #if,
brace and @interface / @implementation it opens (a part cut inside a block would only compile in its place, and an edit that leaves one open
breaks the parts after it); each quoted include in a part names a file that exists relative to gfx/nvn/mtl; both Makefiles rebuild when a part
changes (their GFX_H lists the parts). A part with an #if left open (T_MTL_PARTS_BROKEN=1, the broken variant): 1 fails."""
import glob, os, re, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
NVN = os.path.join(ROOT, 'gfx', 'nvn'); MTL = os.path.join(NVN, 'mtl')
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<96} {"✓" if ok else "✗"}')

def balance(text):
    """(#if depth, brace depth, @implementation depth) at the end of text, and whether any of them went below 0"""
    pp = br = oc = 0; neg = False; in_block = False
    for line in text.split('\n'):
        s = line.strip()
        if not in_block:
            if re.match(r'#\s*(if|ifdef|ifndef)\b', s): pp += 1
            elif re.match(r'#\s*endif\b', s): pp -= 1
            code = s.split('//')[0]
            oc += len(re.findall(r'@(?:interface|implementation)\b', code)) - len(re.findall(r'@end\b', code))
        i = 0; q = None
        while i < len(line):
            c = line[i]
            if in_block:
                if line.startswith('*/', i): in_block = False; i += 2; continue
                i += 1; continue
            if q:
                if c == '\\': i += 2; continue
                if c == q: q = None
                i += 1; continue
            if line.startswith('//', i): break
            if line.startswith('/*', i): in_block = True; i += 2; continue
            if c in '"\'': q = c
            elif c == '{': br += 1
            elif c == '}': br -= 1
            i += 1
        neg = neg or pp < 0 or br < 0 or oc < 0
    return (pp, br, oc), neg

main = open(os.path.join(NVN, 'nvn_mtl.m'), encoding='utf-8').read()
owner = [(n, 'nvn_mtl.m') for n in re.findall(r'^#include "mtl/([\w]+)\.inc"', main, re.M)]   # (part, the file that includes it)
for u in sorted(glob.glob(os.path.join(MTL, '*.m'))):
    owner += [(n, 'mtl/' + os.path.basename(u)) for n in re.findall(r'^#include "([\w]+)\.inc"', open(u, encoding='utf-8').read(), re.M)]
inc = [n for n, _ in owner]; by = dict(owner)
files = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(MTL, '*.inc')))
check(f'nvn_mtl.m and the units in gfx/nvn/mtl include {len(inc)} parts, each once', len(inc) > 30 and len(inc) == len(set(inc)))
check('every part in gfx/nvn/mtl is included, and every include names a part', sorted(inc) == files)
parts = {n: open(os.path.join(MTL, n + '.inc'), encoding='utf-8').read() for n in files}
if os.environ.get('T_MTL_PARTS_BROKEN'): parts[inc[len(inc) // 2]] += '\n#if 1\n'
check('each part starts with its own name line, which names the file that includes it', all(t.startswith(f'// gfx/nvn/mtl/{n}.inc, part of {by.get(n)} ') for n, t in parts.items()))
open_at_end = {n: d for n, (d, neg) in ((n, balance(t)) for n, t in parts.items()) if d != (0, 0, 0) or neg}
check(f'each part closes every #if, brace and @implementation it opens {open_at_end or ""}', not open_at_end)
missing = [f'{n}: {p}' for n, t in parts.items() for p in re.findall(r'^\s*#\s*(?:include|import)\s+"([^"]+)"', t, re.M)
           if not os.path.exists(os.path.join(MTL, p))]
check(f'quoted includes in the parts resolve from gfx/nvn/mtl {missing or ""}', not missing)
mk = {p: open(os.path.join(ROOT, p), encoding='utf-8').read() for p in ('host/Makefile', 'gfx/tests/Makefile')}
check('host/Makefile and gfx/tests/Makefile list the parts among the headers (a changed part rebuilds)',
      re.search(r'^GFX_H\s*=.*\.\./gfx/nvn/mtl/\*\.inc', mk['host/Makefile'], re.M) is not None
      and re.search(r'^GFX_H\s*=.*\.\./nvn/mtl/\*\.inc', mk['gfx/tests/Makefile'], re.M) is not None)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
