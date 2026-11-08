#!/usr/bin/env python3
"""The iOS shader directory checked complete before packing (plan item 174, after acnh-mobile 67afe52 tools/check_invariance.py, MIT: a pack once
went out without part of its libraries). Every key of the shader stage map needs <key>_b.metallib and <key>_b.json in the chain's directory, the
json of this chain and of the map's stage; every key of the game's fout.tsv its integer variant <key>_i_b.metallib (plan item 162). Before this,
ios/run.sh only checked that some library was there: Tears of the Kingdom's profile names the same directory as manosaba's, which holds manosaba's
2,740 libraries, so a Tears of the Kingdom app would have been packed with none of its own. A packed directory (gfx/tools/shader_pack.py) holds
a twin's library once: a library its twins.tsv lists counts when the library packed in its place is there.
Usage: shader_check.py <shader map> <chain dir b-<chain>> [fout.tsv]   exit 0 complete, 1 not (the first missing keys listed), 2 usage."""
import json, os, sys

def check(shmap, d, fout=None):
    """(number of keys checked, [problems])"""
    chain = os.path.basename(os.path.normpath(d))[2:] if os.path.basename(os.path.normpath(d)).startswith('b-') else None
    have = set(os.listdir(d)) if os.path.isdir(d) else set()
    tw = os.path.join(d, 'twins.tsv')
    if os.path.exists(tw):   # (shader_pack.py) a twin's library is there when the one packed in its place is
        for ln in open(tw, encoding='utf-8'):
            c = ln.rstrip('\n').split('\t')
            if len(c) == 2 and not ln.startswith('#') and c[1] in have: have.add(c[0])
    keys, bad = {}, []
    for line in open(shmap, encoding='utf-8'):
        if line.startswith('#') or not line.strip(): continue
        c = line.rstrip('\n').split('\t'); keys[c[0]] = c[1] if len(c) > 1 else ''
    for k, st in keys.items():
        if f'{k}_b.metallib' not in have: bad.append(f'{k}: no library'); continue
        if f'{k}_b.json' not in have: bad.append(f'{k}: no reflection json'); continue
        try: j = json.load(open(os.path.join(d, f'{k}_b.json')))
        except (OSError, ValueError): bad.append(f'{k}: reflection json not readable'); continue
        if chain and j.get('chain') != chain: bad.append(f'{k}: chain {j.get("chain")}, not {chain}')
        elif st and j.get('stage') != st: bad.append(f'{k}: stage {j.get("stage")}, the map says {st}')
    if fout and os.path.exists(fout):
        for line in open(fout, encoding='utf-8'):
            if line.startswith('#') or not line.strip(): continue
            k = line.split()[0]
            if k in keys and f'{k}_i_b.metallib' not in have: bad.append(f'{k}: no integer variant {k}_i_b')
    return len(keys), bad

if __name__ == '__main__':
    if len(sys.argv) not in (3, 4): print(__doc__); sys.exit(2)
    n, bad = check(*sys.argv[1:])
    if bad:
        print(f'[red] the iOS shader directory {sys.argv[2]} is not complete for the map ({n} shaders): {len(bad)} problems, e.g. ' + '; '.join(bad[:5]))
        sys.exit(1)
    print(f'iOS shaders complete: {n} shaders of the map in {sys.argv[2]}')
