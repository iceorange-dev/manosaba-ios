#!/usr/bin/env python3
"""The game data pushed by content (plan item 319; S2Arm's install pushes by content hash, Apache-2.0, written anew): ios/run.sh push-data copied
the whole RomFS each time. Now:
  push_hash.py stage <source dir> <manifest> <device listing> <stage dir>
      the files of <source dir> to copy: missing on the phone (the listing: `devicectl device info files --json-output` of the destination, a file
      of {} when it does not exist), of another size there, or changed since the last push (their SHA-256 against <manifest>, the last push's
      {"files": {path: [size, mtime_ns, sha256]}}; a file whose size and time are unchanged keeps its hash, the rest are read); linked (hard link,
      else copied) into <stage dir> at their paths, or, when every file is to be copied, only <stage dir>/.dm_all made (the source copied as it is);
      the new manifest written beside the old as <manifest>.new; prints what it staged
  push_hash.py commit <manifest>   after the copy: <manifest>.new becomes <manifest>
devicectl merges a copied directory into the destination and replaces a file there (checked on the iPhone Air: a 4-byte file changed to another
of the same size came back changed; the files not copied stayed)."""
import hashlib, json, os, shutil, sys

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()

def walk(src):
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for n in sorted(files):
            if n == '.DS_Store': continue
            p = os.path.join(root, n); yield os.path.relpath(p, src), p

def device_sizes(listing):
    """{relative path: size} of the files the phone has under the destination"""
    out = {}
    for f in ((listing or {}).get('result') or {}).get('files') or []:
        if (f.get('resources') or {}).get('isDirectory'): continue
        out[f.get('relativePath')] = (f.get('metadata') or {}).get('size')
    return out

def plan(src, old, have):
    """([relative paths to copy], new manifest files {path: [size, mtime_ns, sha]})"""
    new, todo = {}, []
    for rel, p in walk(src):
        st = os.stat(p); o = old.get(rel)
        h = o[2] if o and o[0] == st.st_size and o[1] == st.st_mtime_ns else sha256(p)
        new[rel] = [st.st_size, st.st_mtime_ns, h]
        if os.environ.get('T_PUSH_SIZE_ONLY') == '1':   # (the broken variant of tools/tests/t_push_hash.py: only missing or resized files copied)
            changed = False
        else:
            changed = not o or o[2] != h
        if have.get(rel) != st.st_size or changed: todo.append(rel)
    return todo, new

def main(a):
    if len(a) == 5 and a[0] == 'stage':
        src, man, lst, stage = a[1:]
        try: old = json.load(open(man)).get('files', {})
        except Exception: old = {}
        try: have = device_sizes(json.load(open(lst)))
        except Exception: have = {}
        todo, new = plan(src, old, have)
        size = 0; os.makedirs(stage, exist_ok=True)
        if todo and len(todo) == len(new):   # everything: nothing staged (the source is copied as it is; linking across volumes would copy it all)
            open(os.path.join(stage, '.dm_all'), 'w').close(); json.dump({'files': new}, open(man + '.new', 'w'))
            print(f'all {len(new)} files to copy ({sum(v[0] for v in new.values()) / 1048576:.1f} MB)'); return 0
        for rel in todo:
            s, d = os.path.join(src, rel), os.path.join(stage, rel); os.makedirs(os.path.dirname(d), exist_ok=True)
            try: os.link(s, d)
            except OSError: shutil.copy2(s, d)
            size += new[rel][0]
        json.dump({'files': new}, open(man + '.new', 'w'))
        print(f'{len(todo)} of {len(new)} files to copy ({size / 1048576:.1f} MB); the others are on the phone as they are here')
        return 0
    if len(a) == 2 and a[0] == 'commit':
        if os.path.exists(a[1] + '.new'): os.replace(a[1] + '.new', a[1])
        return 0
    print(__doc__, file=sys.stderr); return 2

if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
