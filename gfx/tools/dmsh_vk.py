#!/usr/bin/env python3
"""The Vulkan shader bundle for the Android app (plan item 301e2): a game's Switch shaders → SPIR-V by the same translator and jobs as the Metal chain
(dmsh_build.py's job_for and decode_tex, taken from that file as they are, so the two chains cannot drift apart and the Metal chain's version stays),
kept as SPIR-V (Vulkan takes it as the translator writes it for TargetApi.Vulkan), with
  - each stage's bindings moved so a pipeline's stages share one descriptor set (vertex +0, pixel +64, compute +0; the translator numbers each
    shader's resources from 0 in set 0: the support buffer first),
  - its reflection: each binding's kind and NVN slot (uniform slot r = constant bank 3 + r, the driver banks c0 / c2, c1, the support buffer,
    storage buffer slot r from the translator's c0 offsets, texture / image slot r and its sampler from the c[2] handle in the resource's name),
    the vertex inputs' locations and types, the fragment outputs, the compute group size, the shader's c1 bytes,
  - every module checked by spirv-val from the NDK (Vulkan 1.3).
Usage: DM_GAME=<id> dmsh_vk.py <capture dir> <out bundle> [key...]     (DM_ROOT the workspace; XLATE the translator; J jobs)
Bundle (read by gfx/nvn/vk/vk_shader.c): "DMVK", u32 version 1, u32 count, then count entries {u64 key, u32 stage (0 vs, 1 ps, 5 cs), u32 spv_off,
u32 spv_len, u32 refl_off, u32 refl_len} sorted by key, then the blobs, each at a 4-byte boundary. refl: u16 nb, u16 nv, u16 nout, u16 c1_len,
u16 local[3], u16 flags, then nb × {u8 kind, u8 dtype, u16 binding, u16 count, i16 reg, i16 sreg, u8 rw, u8 ty, u16 off}, nv × {u8 loc, u8 ct},
nout × {u8 loc, u8 ct}, c1 bytes (a texture's off: its declared dimension, dim_of). Kinds: 1 cbuf, 2 support, 3 c1, 4 c0, 5 c2, 6 sbuf, 7 tex, 8 img, 9 tbuf, 10 a texture sampled through
image slot reg; dtype: VkDescriptorType."""
import ast, concurrent.futures as cf, json, os, re, struct, subprocess, sys, tempfile, hashlib, collections
HERE = os.path.dirname(os.path.abspath(__file__))
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')
NDK = os.environ.get('NDK', '/opt/homebrew/share/android-ndk')
SPIRV_VAL = os.path.join(NDK, 'shader-tools', 'darwin-x86_64', 'spirv-val')
XLATE = os.environ.get('XLATE', f'{DM_ROOT}/build/gfx/dmsh_xlate/dmsh_xlate')
SHIFT = {'vs': 0, 'ps': 64, 'cs': 0}
STAGE_NO = {'vs': 0, 'ps': 1, 'cs': 5}
K_CBUF, K_SUPPORT, K_C1, K_C0, K_C2, K_SBUF, K_TEX, K_IMG, K_TBUF, K_TEXI = range(1, 11)
# VkDescriptorType
D_COMBINED, D_UNIFORM_TEXEL, D_STORAGE_IMAGE, D_STORAGE_TEXEL, D_UNIFORM_BUFFER, D_STORAGE_BUFFER = 1, 4, 3, 5, 6, 7
OP_CAPABILITY, OP_TYPE_IMAGE, OP_DECORATE, DEC_BINDING = 17, 25, 71, 33
CAP_READ_NO_FMT, CAP_WRITE_NO_FMT = 55, 56   # StorageImageRead/WriteWithoutFormat

def load_game(gid):
    gj = json.load(open(os.path.join(HERE, '..', '..', 'games', gid, 'game.json')))
    return {k: [int(v[0], 16), int(v[1])] for k, v in gj['nvn_c2'].items()}, gj['depth_mode']

def from_dmsh_build(names, ns):
    """dmsh_build.py's functions of these names, defined in ns (the file runs a build when imported: only the definitions are taken)"""
    tree = ast.parse(open(os.path.join(HERE, 'dmsh_build.py')).read())
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name in names:
            exec(compile(ast.Module([node], []), 'dmsh_build.py', 'exec'), ns)
    missing = [n for n in names if n not in ns]
    if missing: sys.exit(f'[red] dmsh_build.py no longer has {missing}')

def captures(cap_dir, want):
    """{key: (stage, record)} as dmsh_build.py makes them: 0x38 empty bytes + the code area + the control section"""
    out = {}
    for fn in sorted(os.listdir(cap_dir)):
        if not fn.endswith('.ctl'): continue
        key = fn[:-4]
        if want and key not in want: continue
        ctl = open(os.path.join(cap_dir, fn), 'rb').read(); data = open(os.path.join(cap_dir, key + '.data'), 'rb').read()
        total = struct.unpack_from('<I', ctl, 0x704)[0]
        if len(ctl) != 0x880 or hashlib.sha256(ctl + data[:total]).hexdigest()[:16] != key: print(f'[red] capture {key}: not a valid shader'); continue
        if data[:4] == bytes.fromhex('78563412'):
            w0 = struct.unpack_from('<I', data, 0x30)[0]; st = {(1, 1): 'vs', (2, 5): 'ps'}.get((w0 & 0x1f, (w0 >> 10) & 0xf))
            if not st: print(f'[red] capture {key}: neither vertex nor pixel (SPH {w0:#x})'); continue
        else: st = 'cs'
        out[key] = (st, bytes(0x38) + data[:total] + ctl)
    return out

def patch_module(spv, shift):
    """The module with every Binding decoration moved by shift, and the capabilities to read and write storage images of no declared format added
    when it has such images (the translator leaves them out: spirv-val refused 2 of manosaba's shaders; the 840 has both features)"""
    w = list(struct.unpack(f'<{len(spv) // 4}I', spv)); i = 5; caps, nofmt, first_cap = set(), False, None
    while i < len(w):
        n, op = w[i] >> 16, w[i] & 0xffff
        if op == OP_DECORATE and n >= 4 and w[i + 2] == DEC_BINDING: w[i + 3] += shift
        elif op == OP_CAPABILITY: caps.add(w[i + 1]); first_cap = i if first_cap is None else first_cap
        elif op == OP_TYPE_IMAGE and n >= 9 and w[i + 7] == 2 and w[i + 8] == 0: nofmt = True
        i += n if n else 1
    add = [c for c in (CAP_READ_NO_FMT, CAP_WRITE_NO_FMT) if nofmt and c not in caps]
    if add and first_cap is not None: w[first_cap:first_cap] = [x for c in add for x in ((2 << 16) | OP_CAPABILITY, c)]
    return struct.pack(f'<{len(w)}I', *w)

# A texture's or image's declared dimension, in its binding's off field: 0 2D, 1 2D array, 2 cube, 3 cube array, 4 3D, 5 1D, 6 1D array, 7 2D
# multisample, 8 2D multisample array (spirv-cross's names; a Shadow suffix is the same dimension, image3D the same as sampler3D)
DIMS = ['sampler2D', 'sampler2DArray', 'samplerCube', 'samplerCubeArray', 'sampler3D', 'sampler1D', 'sampler1DArray', 'sampler2DMS', 'sampler2DMSArray']
def dim_of(t):
    t = re.sub(r'Shadow$', '', t or ''); t = re.sub(r'^[iu]?image', 'sampler', t); t = re.sub(r'^[iu]', '', t)   # (images first: image's own i)
    return DIMS.index(t) if t in DIMS else 0

BASE_CT = {'vec': 0, 'ivec': 2, 'uvec': 1, 'float': 0, 'int': 2, 'uint': 1}   # NVN_CT_FLOAT 0, UINT 1, INT 2 (nvn_refl.h)
def ct_of(t): m = re.match(r'(ivec|uvec|vec|float|int|uint)', t or ''); return BASE_CT[m.group(1)] if m else 0

IMG = [0, 0]
def img_slot(name):
    """An image named as a texture (cp_t_tcb_48: a compute shader's): its slot from the handle's word in c[2]'s image table"""
    m = re.match(r'[a-z]+_[ti]\w*?_(?:cb(\d+)|tcb)_([0-9A-F]+)', name)
    if not m: return None
    o = (int(m.group(2), 16) & 0x3fff) * 4
    return (o - IMG[0]) // 8 if IMG[0] <= o < IMG[1] and (o - IMG[0]) % 8 == 0 else None

def reflect(key, stage, spv, res, decode_tex):
    """spv: the module as patch_module left it (its bindings moved already); res: the translator's res.json (its bindings not moved)"""
    r = json.loads(subprocess.run(['spirv-cross', '-', '--reflect'], input=spv, capture_output=True, check=True).stdout)
    sh = 2 * SHIFT[stage] if os.environ.get('T_DMVK_BROKEN') == 'shift2' else SHIFT[stage]   # (the broken variant of t_dmsh_vk.py: moved twice, as at first)
    sb_slot = {}   # storage buffer binding → NVN slot, from the translator's c0 offsets (base: vertex 0x110, compute 0x310, pixel 0x510)
    base = {'vs': 0x110, 'cs': 0x310, 'ps': 0x510}[stage]
    for b in res.get('sbufs', []):
        off = b.get('cb_offset', -1) * 4 if b.get('cb_slot') == 0 else None
        sb_slot[b['binding']] = ((off - base) // 16 if off is not None and 0 <= off - base < 0x100 and (off - base) % 16 == 0 else -1, bool(b.get('flags', 0) & 1))
    binds, unknown = [], []
    def add(kind, dtype, e, reg=-1, sreg=-1, rw=0, ty=0, off=0):
        binds.append((kind, dtype, e['binding'] - SHIFT[stage] + sh, e.get('array', [1])[0] if e.get('array') else 1, reg, sreg, rw, ty, off))
    for e in r.get('ubos', []):
        nm = e['name']; m = re.fullmatch(r'[a-z]+_c(\d+)', nm)
        if nm == 'support_buffer': add(K_SUPPORT, D_UNIFORM_BUFFER, e)
        elif m and 3 <= int(m.group(1)) <= 16: add(K_CBUF, D_UNIFORM_BUFFER, e, reg=int(m.group(1)) - 3)
        elif m and int(m.group(1)) in (0, 1, 2): add({0: K_C0, 1: K_C1, 2: K_C2}[int(m.group(1))], D_UNIFORM_BUFFER, e)
        else: unknown.append(nm)
    for e in r.get('ssbos', []):
        reg, rw = sb_slot.get(e['binding'] - SHIFT[stage], (-1, True)); add(K_SBUF, D_STORAGE_BUFFER, e, reg=reg, rw=int(rw))
    for e in r.get('textures', []):
        d = decode_tex(e['name'])
        if (not d or d[0] is None) and img_slot(e['name']) is not None:   # sampled through an image handle (the translator's note: texture handle = image slot)
            add(K_TEXI, D_COMBINED, e, reg=img_slot(e['name']), off=dim_of(e.get('type'))); continue
        if not d or d[0] is None: unknown.append(e['name']); continue
        t, s, ty = d
        if e.get('type', '').endswith('Buffer'): add(K_TBUF, D_UNIFORM_TEXEL, e, reg=t)
        else: add(K_TEX, D_COMBINED, e, reg=t, sreg=s if (ty and s is not None) else -1, ty=ty, off=dim_of(e.get('type')))
    for e in r.get('images', []):
        d = decode_tex(e['name']); reg = d[0] if d and re.match(r'[a-z]+_i', e['name']) else img_slot(e['name'])
        if reg is None: unknown.append(e['name']); continue
        add(K_IMG, D_STORAGE_TEXEL if e.get('type', '').endswith('Buffer') else D_STORAGE_IMAGE, e, reg=reg, rw=1, off=dim_of(e.get('type')))
    for x in ('separate_images', 'separate_samplers', 'acceleration_structures'):
        for e in r.get(x, []): unknown.append(f'{x}:{e["name"]}')
    vin = [(e['location'], ct_of(e.get('type'))) for e in r.get('inputs', []) if 'location' in e] if stage == 'vs' else []
    vout = [(e['location'], ct_of(e.get('type'))) for e in r.get('outputs', []) if 'location' in e] if stage == 'ps' else []
    return binds, vin, vout, unknown

def pack_refl(binds, vin, vout, c1, local):
    b = struct.pack('<4H3HH', len(binds), len(vin), len(vout), len(c1), *(local or [0, 0, 0]), 0)
    for k in binds: b += struct.pack('<BBHHhhBBH', *k)
    for loc, ct in vin + vout: b += struct.pack('<BB', loc, ct)
    return b + c1

def game_ns(gid):
    """dmsh_build.py's job_for and decode_tex, with the game's c[2] layout and depth mode (and img_slot's image table set)"""
    C2, DEPTH = load_game(gid)
    ns = {'os': os, 'struct': struct, 're': re, 'C2': C2, 'DEPTH': DEPTH, 'stage_of': {},
          'TEX_BASE': C2['tex'][0], 'TEX_END': C2['tex'][0] + 8 * C2['tex'][1], 'SAMP_BASE': C2['samp'][0], 'SAMP_END': C2['samp'][0] + 8 * C2['samp'][1],
          'IMG_BASE': C2['img'][0], 'IMG_END': C2['img'][0] + 8 * C2['img'][1]}
    from_dmsh_build(['job_for', 'decode_tex'], ns)
    IMG[:] = [ns['IMG_BASE'], ns['IMG_END']]
    return ns

ENT = '<QIIIII'   # an entry: 28 bytes (vk_shader.c's ent, packed)
def pack_bundle(shaders):
    """[(key, stage, spv, refl)] → the bundle's bytes (sorted by key)"""
    shaders = sorted(shaders)
    stride = 32 if os.environ.get('T_DMVK_BROKEN') == 'stride' else struct.calcsize(ENT)   # (the broken variant of t_dmsh_vk.py: the first writer's 32)
    head = b'DMVK' + struct.pack('<II', 1, len(shaders)); at = len(head) + stride * len(shaders); ents, blobs = b'', b''
    for k, st, spv, refl in shaders:
        so = at + len(blobs); blobs += spv + bytes(-len(spv) % 4); ro = at + len(blobs); blobs += refl + bytes(-len(refl) % 4)
        ents += struct.pack(ENT, k, st, so, len(spv), ro, len(refl))
    return head + ents + blobs

def read_bundle(b):
    """The bundle's bytes → {key: (stage, spv, refl)}, as vk_shader.c reads them"""
    if b[:4] != b'DMVK' or struct.unpack_from('<I', b, 4)[0] != 1: raise ValueError('not a version 1 bundle')
    n = struct.unpack_from('<I', b, 8)[0]; out = {}
    for i in range(n):
        k, st, so, sl, ro, rl = struct.unpack_from(ENT, b, 12 + struct.calcsize(ENT) * i); out[k] = (st, b[so:so + sl], b[ro:ro + rl])
    return out

def main():
    if len(sys.argv) < 3: print(__doc__); sys.exit(2)
    cap_dir, out = sys.argv[1], sys.argv[2]; want = set(sys.argv[3:])
    gid = os.environ.get('DM_GAME') or sys.exit('[red] DM_GAME is not set')
    ns = game_ns(gid)
    recs = captures(cap_dir, want)
    for k, (st, _) in recs.items(): ns['stage_of'][k] = st
    env = dict(os.environ); env.setdefault('DOTNET_ROOT', '/opt/homebrew/opt/dotnet/libexec')
    nj = int(os.environ.get('J', os.cpu_count() or 8))
    with tempfile.TemporaryDirectory() as w:
        jobs, c1s = {}, {}
        for k, (st, rec) in recs.items(): jobs[k], c1s[k] = ns['job_for'](k, rec, w)
        keys = sorted(jobs); chunks = [keys[i::nj] for i in range(nj)]
        def xl(ch):
            jp = os.path.join(w, f'job_{abs(hash(ch[0]))}.json'); json.dump([jobs[k] for k in ch], open(jp, 'w'))
            return subprocess.run([XLATE, jp, w], capture_output=True, text=True, env=env)
        errs = {}
        with cf.ThreadPoolExecutor(nj) as ex:
            for r in ex.map(xl, [c for c in chunks if c]):
                for ln in r.stderr.splitlines():
                    m = re.match(r'\[red\] (\w+): (.*)', ln)
                    if m: errs[m.group(1)] = m.group(2)[:160]
        def fin(k):
            st = recs[k][0]; sp = os.path.join(w, k + '.spv')
            if not os.path.exists(sp): return k, 'translation failed', errs.get(k, 'no spv'), None
            spv = patch_module(open(sp, 'rb').read(), SHIFT[st]); res = json.load(open(os.path.join(w, k + '.res.json')))
            v = subprocess.run([SPIRV_VAL, '--target-env', 'vulkan1.3', '-'], input=spv, capture_output=True)
            if v.returncode: return k, 'spirv-val', (v.stderr or v.stdout).decode(errors='replace').strip().splitlines()[0][:160], None
            binds, vin, vout, unknown = reflect(k, st, spv, res, ns['decode_tex'])
            if unknown: return k, 'unknown binding', ' '.join(unknown)[:160], None
            return k, 'ok', '', (st, spv, pack_refl(binds, vin, vout, c1s[k], jobs[k].get('local')))
        with cf.ThreadPoolExecutor(nj) as ex: res = list(ex.map(fin, keys))
    ok = sorted((r[0], r[3]) for r in res if r[1] == 'ok')
    tmp = out + '.tmp'; open(tmp, 'wb').write(pack_bundle([(int(k, 16), STAGE_NO[st], spv, refl) for k, (st, spv, refl) in ok])); os.replace(tmp, out)
    stc = collections.Counter(r[1] for r in res)
    print(f'{out}: {len(ok)} shaders, {os.path.getsize(out) / 1e6:.1f} MB' + ''.join(f'; {k} {v}' for k, v in stc.items() if k != 'ok'))
    for lvl in [k for k in stc if k != 'ok']:
        for e, n in collections.Counter(r[2] for r in res if r[1] == lvl).most_common(6): print(f'  [{lvl}] {n:5d}  {e}')
    return 0 if ok else 1

if __name__ == '__main__': sys.exit(main())
