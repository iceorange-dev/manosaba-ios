#!/usr/bin/env python3
"""A game's shader libraries for its app (plan item 311b): from the chain's directory, which holds every game's (Tears of the Kingdom's and
manosaba's profiles name the same one: 31,415 libraries), only the files of the game's shader map, and a library whose bytes another of them has
only once. Before this the app took the whole directory: manosaba's IPA carried Tears of the Kingdom's 28,652 libraries (354 MB of libraries and
76 MB of reflection, 447 MB as an IPA).
Per key of the map: <key>_b.metallib and <key>_b.json, <key>_bf.metallib (the readable-tile version, when built), and for the keys of the game's
fout.tsv <key>_i_b.metallib / .json (the integer-output variant). The libraries <key>_b and <key>_bf of shaders the translator cannot tell apart
come out byte for byte the same (MacNativeGames fb9b734's finding, facts; 3,327 of Tears of the Kingdom's 3,328 twins, all 1,441 of manosaba's):
the first by name is packed and the others are listed in twins.tsv ("<library>\t<the library packed in its place>"), which the host reads
(gfx/nvn/mtl/shaders.inc msl_lib_path). Every reflection json is packed (each names its own key). Files are cloned (cp -c).
Usage: shader_pack.py <shader map> <chain dir b-<V>> <the app's b-<V> dir> [fout.tsv]   prints what it packed; exit 1 when a key's library or json is
missing (ios/tools/shader_check.py checks the source first). T_SHADER_PACK_BROKEN=all: the whole directory, as before (tools/tests/t_shader_pack.py)."""
import hashlib, os, subprocess, sys

def plan(shmap, src, fout=None):
    """(the files to pack, {twin library: the library packed in its place}, [missing])"""
    have = set(os.listdir(src))
    if os.environ.get('T_SHADER_PACK_BROKEN') == 'all': return sorted(have), {}, []
    keys = [ln.split('\t')[0] for ln in open(shmap, encoding='utf-8') if ln.strip() and not ln.startswith('#')]
    fk = set()
    if fout and os.path.exists(fout): fk = {ln.split()[0] for ln in open(fout, encoding='utf-8') if ln.strip() and not ln.startswith('#')}
    want, missing = [], []
    for k in keys:
        for n in (f'{k}_b.metallib', f'{k}_b.json'):
            if n in have: want.append(n)
            else: missing.append(n)
        if f'{k}_bf.metallib' in have: want.append(f'{k}_bf.metallib')
        if k in fk: want += [n for n in (f'{k}_i_b.metallib', f'{k}_i_b.json') if n in have]
    first, twins = {}, {}
    for n in sorted(set(want)):   # libraries of the same bytes: the first by name packed
        if not (n.endswith('_bf.metallib') or (n.endswith('_b.metallib') and not n.endswith('_i_b.metallib'))): continue
        p = os.path.join(src, n); d = (n.endswith('_bf.metallib'), os.path.getsize(p), hashlib.sha256(open(p, 'rb').read()).hexdigest())
        if d in first: twins[n] = first[d]
        else: first[d] = n
    return sorted(n for n in set(want) if n not in twins), twins, missing

def pack(shmap, src, dst, fout=None):
    files, twins, missing = plan(shmap, src, fout)
    if missing: return files, twins, missing
    os.makedirs(dst, exist_ok=True)
    for i in range(0, len(files), 400):
        subprocess.run(['cp', '-c', *[os.path.join(src, n) for n in files[i:i + 400]], dst], check=True)
    if twins:
        with open(os.path.join(dst, 'twins.tsv'), 'w', encoding='utf-8') as f:
            f.write('# libraries packed once (gfx/tools/shader_pack.py): library, the library of the same bytes packed in its place\n')
            for n in sorted(twins): f.write(f'{n}\t{twins[n]}\n')
    return files, twins, missing

if __name__ == '__main__':
    if len(sys.argv) not in (4, 5): print(__doc__); sys.exit(2)
    files, twins, missing = pack(*sys.argv[1:])
    if missing: print(f'[red] {len(missing)} files of the map are not in {sys.argv[2]}, e.g. ' + ', '.join(missing[:4])); sys.exit(1)
    size = sum(os.path.getsize(os.path.join(sys.argv[2], n)) for n in files)
    print(f'shaders packed: {len(files)} files, {size / 1e6:.1f} MB; {len(twins)} libraries packed once (twins.tsv)')
