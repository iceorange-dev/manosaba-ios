#!/usr/bin/env python3
"""gfx/tools/shader_pack.py, a game's shader libraries for its app (plan item 311b): from a chain directory holding two games' libraries, only the
map's keys are packed (their libraries, reflections, readable-tile versions, and the integer-output variants of the game's fout.tsv keys); a
library of the same bytes as another (a twin) once, twins.tsv naming the one packed in its place, its reflection packed; ios/tools/shader_check.py
takes the packed directory as complete; the host opens every library through msl_lib_path (no library path joined to g_msl_dir by hand). With the
workspace's iOS libraries of this chain: manosaba's map packed, none of Tears of the Kingdom's, every key's library there itself or as its twin.
T_SHADER_PACK_BROKEN=all (the whole directory, as ios/run.sh copied it before): 2 fail (3 with the workspace's libraries)."""
import glob, os, re, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
DM_ROOT = os.environ.get('DM_ROOT') or os.path.join(ROOT, 'work')
sys.path.insert(0, os.path.join(ROOT, 'gfx', 'tools')); import shader_pack
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import shader_check
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))

A, B, C, D, F = 'a' * 16, 'b' * 16, 'c' * 16, 'd' * 16, 'f' * 16   # C is A's twin, D another game's, F a key of fout.tsv
with tempfile.TemporaryDirectory() as w:
    src = os.path.join(w, 'b-test'); dst = os.path.join(w, 'app', 'b-test'); os.makedirs(src)
    def put(n, b): open(os.path.join(src, n), 'wb').write(b)
    for k, lib in ((A, b'library A'), (B, b'library B'), (C, b'library A'), (D, b'library D'), (F, b'library F')):
        put(k + '_b.metallib', lib); put(k + '_b.json', ('{"chain": "test", "stage": "ps", "key": "%s_b"}' % k).encode())
    put(A + '_bf.metallib', b'tile A'); put(C + '_bf.metallib', b'tile A'); put(B + '_bf.metallib', b'tile B')
    put(F + '_i_b.metallib', b'integer F'); put(F + '_i_b.json', b'{"chain": "test", "stage": "ps"}'); put(B + '_i_b.metallib', b'integer B')
    put('_b_build.json', b'[]')
    shmap = os.path.join(w, 'shader_map.tsv'); open(shmap, 'w').write(f'# map\n{A}\tps\n{B}\tps\n{C}\tps\n{F}\tps\n')
    fout = os.path.join(w, 'fout.tsv'); open(fout, 'w').write(f'# fout\n{F}\t0\tuint\n')
    files, twins, missing = shader_pack.pack(shmap, src, dst, fout)
    got = set(os.listdir(dst))
    check('only the map\'s keys: none of the other game\'s, no build summary, no integer variant of a key fout.tsv does not list [T_SHADER_PACK_BROKEN=all]',
          not missing and not any(n.startswith(D) for n in got) and '_b_build.json' not in got and B + '_i_b.metallib' not in got
          and {F + '_i_b.metallib', F + '_i_b.json', B + '_bf.metallib', A + '_bf.metallib'} <= got, ' '.join(sorted(got)))
    tw = dict(ln.rstrip('\n').split('\t') for ln in open(os.path.join(dst, 'twins.tsv')) if not ln.startswith('#')) if 'twins.tsv' in got else {}
    check('a twin\'s library and readable-tile version packed once, twins.tsv naming the one in its place; its reflection packed [T_SHADER_PACK_BROKEN=all]',
          tw == {C + '_b.metallib': A + '_b.metallib', C + '_bf.metallib': A + '_bf.metallib'} and C + '_b.metallib' not in got and C + '_b.json' in got, f'{tw}')
    n, bad = shader_check.check(shmap, dst, fout)
    check(f'shader_check.py takes the packed directory as complete ({n} keys)', n == 4 and not bad, '; '.join(bad[:3]))
    os.unlink(os.path.join(src, B + '_b.json'))
    _, _, missing = shader_pack.plan(shmap, src, fout)
    check('a key whose reflection is missing: named, nothing packed', os.environ.get('T_SHADER_PACK_BROKEN') == 'all' or missing == [B + '_b.json'], f'{missing}')
inc = {p: open(p, encoding='utf-8').read() for p in glob.glob(os.path.join(ROOT, 'gfx', 'nvn', 'mtl', '*.inc'))}
hand = [f'{os.path.basename(p)}:{i + 1}' for p, s in inc.items() for i, ln in enumerate(s.split('\n')) if 'g_msl_dir' in ln and '.metallib' in ln]
check('the host opens every library through msl_lib_path (twins.tsv), none by a path joined to g_msl_dir by hand', not hand, ', '.join(hand))
chain = subprocess.run([sys.executable, os.path.join(ROOT, 'gfx', 'tools', 'dmsh_chain.py')], capture_output=True, text=True).stdout.strip()
ios = os.path.join(DM_ROOT, 'build', 'ios', 'dmsh_b', 'b-' + chain); mmap = os.path.join(DM_ROOT, 'build', 'mano', 'shader_map.tsv'); tmap = os.path.join(DM_ROOT, 'build', 'totk', 'shader_map.tsv')
if os.path.isdir(ios) and os.path.exists(mmap) and os.path.exists(tmap):
    keys = lambda p: [ln.split('\t')[0] for ln in open(p) if ln.strip() and not ln.startswith('#')]
    mk, tk = set(keys(mmap)), set(keys(tmap)) - set(keys(mmap))
    with tempfile.TemporaryDirectory() as w:
        files, twins, missing = shader_pack.pack(mmap, ios, os.path.join(w, 'b-' + chain), os.path.join(ROOT, 'games', 'manosaba', 'fout.tsv'))
        size = sum(os.path.getsize(os.path.join(ios, n)) for n in files)
        other = [n for n in files if n[:16] in tk]
        n, bad = shader_check.check(mmap, os.path.join(w, 'b-' + chain), os.path.join(ROOT, 'games', 'manosaba', 'fout.tsv'))
        print(f'manosaba from b-{chain}: {len(files)} files, {size / 1e6:.1f} MB, {len(twins)} libraries packed once')
        check(f'the workspace\'s iOS libraries: manosaba\'s {len(mk)} keys packed, none of Tears of the Kingdom\'s, each library there itself or as its twin [T_SHADER_PACK_BROKEN=all]',
              not missing and not other and n == len(mk) and not bad, f'{len(other)} of the other game\'s; ' + '; '.join(bad[:3] + missing[:3]))
else: print(f'(no iOS libraries of chain {chain} in the workspace: the workspace check skipped)')
print(f'{total} checks, {fails} failed')
sys.exit(1 if fails else 0)
