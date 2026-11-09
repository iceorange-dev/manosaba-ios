#!/usr/bin/env python3
"""ios/tools/push_hash.py (plan item 319): the first push copies every file; after it, nothing; a file changed to another of the same size is copied,
one missing on the phone and one of another size there too; a file whose size and time are unchanged is not read again (its hash kept); commit
makes the new manifest the one. Only missing or resized files copied (T_PUSH_SIZE_ONLY=1, the broken variant): 2 fail."""
import json, os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import push_hash as ph
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
def listing(files): return {'result': {'files': [{'relativePath': k, 'metadata': {'size': v}, 'resources': {'isDirectory': False}} for k, v in files.items()]
                                       + [{'relativePath': 'sub', 'metadata': {'size': 96}, 'resources': {'isDirectory': True}}]}}
with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, 'src'); os.makedirs(os.path.join(src, 'sub'))
    for rel, body in (('a.bin', b'AAAA'), ('sub/b.bin', b'BB'), ('c.bin', b'CCC')): open(os.path.join(src, rel), 'wb').write(body)
    todo, man = ph.plan(src, {}, {})
    check('the first push: every file', sorted(todo) == ['a.bin', 'c.bin', 'sub/b.bin'], str(todo))
    have = {'a.bin': 4, 'sub/b.bin': 2, 'c.bin': 3}
    todo, man2 = ph.plan(src, man, have)
    check('... after it: nothing', todo == [], str(todo))
    open(os.path.join(src, 'a.bin'), 'wb').write(b'ZZZZ'); os.utime(os.path.join(src, 'a.bin'), ns=(1, 1))
    todo, _ = ph.plan(src, man2, have)
    check('a file changed to another of the same size: copied [T_PUSH_SIZE_ONLY=1: not]', todo == ['a.bin'], str(todo))
    todo, _ = ph.plan(src, man2, {'a.bin': 4, 'c.bin': 9})
    check('... one missing on the phone and one of another size there: copied', sorted(todo) == ['a.bin', 'c.bin', 'sub/b.bin'] or (os.environ.get('T_PUSH_SIZE_ONLY') == '1' and sorted(todo) == ['c.bin', 'sub/b.bin']), str(todo))
    fake = dict(man2); fake['c.bin'] = [man2['c.bin'][0], man2['c.bin'][1], 'kept']
    _, man3 = ph.plan(src, fake, have)
    check('a file of unchanged size and time keeps its hash (not read again)', man3['c.bin'][2] == 'kept', str(man3['c.bin']))
    check('the device listing: files with their sizes, directories left out', ph.device_sizes(listing({'x': 1})) == {'x': 1})
    m = os.path.join(d, 'm.json'); st = os.path.join(d, 'stage'); l = os.path.join(d, 'l.json'); json.dump(listing(have), open(l, 'w'))
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'ios', 'tools', 'push_hash.py'), 'stage', src, m, l, st], capture_output=True, text=True)
    staged = sorted(os.path.relpath(os.path.join(a, f), st) for a, _, fs in os.walk(st) for f in fs)
    subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'ios', 'tools', 'push_hash.py'), 'commit', m])
    check('stage, with no manifest yet: all to copy, marked, nothing linked; the manifest committed', r.returncode == 0 and staged == ['.dm_all'] and os.path.exists(m) and not os.path.exists(m + '.new'), r.stdout + r.stderr + str(staged))
    open(os.path.join(src, 'c.bin'), 'wb').write(b'CCCX')
    if os.path.exists(os.path.join(st, '.dm_all')): os.unlink(os.path.join(st, '.dm_all'))
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'ios', 'tools', 'push_hash.py'), 'stage', src, m, l, st], capture_output=True, text=True)
    staged = sorted(os.path.relpath(os.path.join(a, f), st) for a, _, fs in os.walk(st) for f in fs)
    check('... then one file changed: only it linked into the stage', r.returncode == 0 and staged == ['c.bin'] and os.path.samefile(os.path.join(st, 'c.bin'), os.path.join(src, 'c.bin')), r.stdout + r.stderr + str(staged))
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
