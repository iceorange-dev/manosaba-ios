#!/usr/bin/env python3
# Offline shader build: the game's Switch shaders → SPIR-V (dmsh_xlate, the Ryujinx translator) → spirv-cross MSL → metallib + reflection JSON
# for the replay layer. Nothing is translated at run time (iOS has no JIT).
# Usage: dmsh_build.py <capture dir> <shader cache root> [key...]   (no keys = all)
#   capture dir: <key>.ctl (the 0x880-byte NVN control section) + <key>.data (the code the game passes to nvnProgramSetShaders), as written by
#   the host's DM_SHDUMP or by an engine extractor (engines/unity/shader_extract.py); key = the first 16 hex digits of sha256(control section + code
#   area), the code area being the length at control +0x704 (the host's shader_key in hle_nvn.c: many shaders share a control section).
#   Output goes to <cache root>/b-<chain version>/ (dmsh_chain.py): <key>_b.metallib, <key>_b.json (reflection, including drv_reads: run-time reads
#   of the driver banks c0 / c2), <key>_b.metal (MSL), and <key>_bf.metallib readable-tile variants of fragment shaders (dmsh_fetch.py).
# Environment: DM_GAME = the game id (games/<id>/game.json gives nvn_c2, the c[2] handle table layout of its shaders; required);
#   IOSOUT=<dir> also builds iphoneos metallibs (ios_metal.py) into <dir>/b-<chain version>/; XLATE = the translator; J = jobs;
#   DMSH_CAS = content-addressed cache (default <cache root>/cas); DMSH_BF_WANT = file listing keys that also get an output-mapping-only variant.
# Ends with a summary: successes / failures by reason, and the driver bank reads the replay layer cannot fill.
import ast, inspect, os, re, sys, json, struct, hashlib, shutil, subprocess, tempfile, collections, threading, concurrent.futures as cf
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # Workspace root (game files, build outputs and run artifacts are all under it)
HOME = os.path.expanduser('~')
cap_dir, out = sys.argv[1:3]; want = set(sys.argv[3:]); cache_root = out
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import dmsh_chain, dmsh_fetch, dmsh_mtlcache, msl_helpers   # translation chain: the tools are checked against the lock file; output goes into b-<V>/
_bad = dmsh_chain.check_tools()
if _bad: sys.exit('\n'.join('[red] translation chain tool differs from the lock file: ' + b for b in _bad) + '\n(after changing a tool: dmsh_chain.py --update-lock and commit it; if only the translator was not rebuilt: rebuild it)')
CHAIN = dmsh_chain.version(); out = os.path.join(out, 'b-' + CHAIN)
if f'"{CHAIN}"' not in open(dmsh_chain.HEADER).read(): sys.exit(f'[red] nvn/dmsh_chain.h is not the current chain version {CHAIN}: run dmsh_chain.py --header first (or the host will not find these libraries)')
XLATE = os.environ.get('XLATE', f'{DM_ROOT}/build/gfx/dmsh_xlate/dmsh_xlate')
IOSOUT = os.environ.get('IOSOUT')   # if set, also build the iOS version (iphoneos metallib + reflection) into that directory
if IOSOUT:
    import ios_metal
    IOSOUT = os.path.join(IOSOUT, 'b-' + CHAIN); os.makedirs(IOSOUT, exist_ok=True)
BF_WANT_FILE = os.environ.get('DMSH_BF_WANT')
BF_WANT = {line.strip()[:16] for line in open(BF_WANT_FILE) if line.strip() and not line.lstrip().startswith('#')} if BF_WANT_FILE else set()
XC = ['--msl', '--msl-version', '20300', '--msl-argument-buffers', '--msl-texture-buffer-native', '--msl-pad-fragment-output']
CAS = os.environ.get('DMSH_CAS', os.path.join(cache_root, 'cas'))
CAS_LOCKS, CAS_LOCKS_GUARD = {}, threading.Lock()
PRE_STATS = collections.Counter()
# Metal compile convention (macOS; ios_metal.py has the iOS one with the same flags):
# -fpreserve-invariance: vertex positions are marked [[invariant]], but without the flag Metal does not promise that two different shaders compute the
#   same expression to the same value; on Maxwell the same multiply-adds always agree. A depth pre-pass drawn by one shader and a G-buffer drawn by
#   another with depth test less-or-equal then failed on whole blocks (seedds/nxnative f1cc6be, MIT: Super Mario Odyssey's ground in 32x32 blocks)
# -fmetal-math-mode=safe: no unsafe floating-point optimizations (the default, fast, broke the integer division a Tears of the Kingdom tile-light vertex
#   shader does in float: MacNativeGames compiles every shader with MTLMathModeSafe, MIT)
# -mmacosx-version-min=26.0: the minimum system of the Metal 4 Mac host (make -C host print-macos-min); without it the libraries target the build
#   machine's SDK, and built with Xcode 27.2 they would not load on macOS 26 (MacNativeGames #1845: language version 4.1; plan item 311). The
#   older-API package recompiles them for its own minimum (msl_retarget.sh)
MAC_MIN = '26.0'
MAC_METAL = ['xcrun', '-sdk', 'macosx', 'metal', '-c', '-mmacosx-version-min=' + MAC_MIN, '-fpreserve-invariance', '-fmetal-math-mode=safe']
MAC_RECORD = ' '.join(MAC_METAL) + '; xcrun -sdk macosx metallib'
def mac_compile(msl, air, lib):   # the interface of ios_metal.compile
    subprocess.run([*MAC_METAL, msl, '-o', air], check=True)
    subprocess.run(['xcrun', '-sdk', 'macosx', 'metallib', air, '-o', lib], check=True)
# c[2] handle tables of the game's NVN driver (game.json nvn_c2: {"tex": [base, slots], "samp": [...], "img": [...]}, bases as hex strings, 8 bytes
# per slot). manosaba: textures 0x168, samplers 0x568, images 0x120 (inferred from cs_lut_combine_c writing u4 = 0x140, checked with u5 = 0x148);
# Tears of the Kingdom: textures 0x20 (as nxnative for the games on the same SDK). Each translator job carries tex / img, each reflection all three.
GAME_ID = os.environ.get('DM_GAME') or sys.exit('[red] DM_GAME is not set: the c[2] layout of the shaders comes from games/<DM_GAME>/game.json nvn_c2')
_gj = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'games', GAME_ID, 'game.json'), encoding='utf-8'))
if 'nvn_c2' not in _gj: sys.exit(f'[red] games/{GAME_ID}/game.json has no nvn_c2')
C2 = {k: [int(v[0], 16), int(v[1])] for k, v in _gj['nvn_c2'].items()}
# Depth convention the game sets with nvnDeviceSetDepthMode (game.json depth_mode): near_is_minus_w makes the translator remap z in every vertex shader
DEPTH = _gj.get('depth_mode') or sys.exit(f'[red] games/{GAME_ID}/game.json has no depth_mode')
ALPHA = _gj.get('alpha_test', 'off') == 'on'   # game.json alpha_test: fragment shaders carry the NVN alpha test (alpha_test; off: none of our games calls ColorStateSetAlphaTest)
TEX_BASE, TEX_END = C2['tex'][0], C2['tex'][0] + 8 * C2['tex'][1]
SAMP_BASE, SAMP_END = C2['samp'][0], C2['samp'][0] + 8 * C2['samp'][1]
IMG_BASE, IMG_END = C2['img'][0], C2['img'][0] + 8 * C2['img'][1]

def clone(src, dst): dmsh_mtlcache.clone(src, dst)   # APFS copy-on-write through the clonefile call (no cp process per file)

def atomic_json(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.tmp-' + str(os.getpid()) + '-' + str(threading.get_ident())
    with open(tmp, 'w') as f: json.dump(value, f, ensure_ascii=False, indent=1)
    os.replace(tmp, path)

def source_without_rule_bodies():
    source = open(__file__, 'r').read(); lines = source.splitlines(True); tree = ast.parse(source)
    names = {f.__name__ for f, _ in RULES}
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name in names and node.body:
            first = node.body[0].lineno - 1
            last = max(getattr(x, 'end_lineno', x.lineno) for x in ast.walk(node) if getattr(x, 'lineno', 0) >= node.body[0].lineno)
            indent = re.match(r'\s*', lines[first]).group(0); lines[first:last] = [indent + 'pass\n']
    return ''.join(lines)

def shader_rule_source(name):
    return b''.join(inspect.getsource(f).encode() for f, applies in RULES if applies(name))

def tool_fingerprint():
    sc = subprocess.run(['spirv-cross', '--version'], capture_output=True, text=True, timeout=30)
    return ('lock\0' + open(dmsh_chain.LOCK).read() + '\0options\0' + dmsh_chain.options() +
            '\0xlate\0' + dmsh_chain.xlate_source_sha() + '\0spirv-cross\0' + sc.stdout + sc.stderr +
            '\0xc\0' + repr(XC) + '\0mac\0' + MAC_RECORD + '\0ios\0' + (ios_metal.RECORD if IOSOUT else ''))

def shader_key(key, name, rec):
    payload = (b'record\0' + rec + b'\0tools\0' + TOOL_FP + b'\0code\0' + CODE_FP.encode() + b'\0rule\0' + shader_rule_source(name) + b'\0c2\0' + json.dumps(C2, sort_keys=True).encode() + b'\0depth\0' + DEPTH.encode() + b'\0alpha\0' + str(ALPHA).encode())
    return hashlib.sha256(payload).hexdigest()

def cache_meta_path(key): return os.path.join(CAS, 'index', key + '.json')
def cache_path(digest): return os.path.join(CAS, 'shader', digest)

def cache_outputs(key, digest, cached=False):
    root = cache_path(digest); files = {}
    for label, directory, names in (
            ('mac', out, [key + '_b.metallib', key + '_b.metal', key + '_b.json']),
            ('ios', IOSOUT, [key + '_b.metallib', key + '_b.json'])):
        if not directory: continue
        base = os.path.join(root, label) if cached else directory
        found = [n for n in names if os.path.exists(os.path.join(base, n))]
        if key + '_b.metallib' not in found or key + '_b.json' not in found: return None
        files[label] = found
    return root, files

def restore_cache(key, digest):
    found = cache_outputs(key, digest, cached=True)
    if not found: return False
    root, files = found
    for label, names in files.items():
        directory = out if label == 'mac' else IOSOUT
        for name in names:
            src = os.path.join(root, label, name); dst = os.path.join(directory, name)
            if name.endswith('.json'):
                tmp = dst + '.tmp-' + str(os.getpid()) + '-' + str(threading.get_ident())
                with open(tmp, 'wb') as f: f.write(re.sub(rb'("chain"\s*:\s*")[^"]*"', rb'\g<1>' + CHAIN.encode() + b'"', open(src, 'rb').read(), count=1))
                os.replace(tmp, dst)
            else: clone(src, dst)
    return True

def cache_lookup(key, digest, rule_fp, code_fp):
    with CAS_LOCKS_GUARD: lock = CAS_LOCKS.setdefault(('shader', key), threading.Lock())
    with lock:
        if restore_cache(key, digest): PRE_STATS['hit'] += 1; return True, 'hit'
        old = json.load(open(cache_meta_path(key))) if os.path.exists(cache_meta_path(key)) else None
        if old and old.get('code_fp') != code_fp: reason = 'code fingerprint changed: everything rebuilt'
        elif old and old.get('rule_fp') != rule_fp: reason = 'rule changed'
        else: reason = 'new key'
        PRE_STATS['miss'] += 1; PRE_STATS[reason] += 1; return False, reason

def publish_cache(key, digest, rule_fp, code_fp):
    found = cache_outputs(key, digest)
    if not found: return
    root, files = found
    for label, names in files.items():
        d = os.path.join(root, label); os.makedirs(d, exist_ok=True)
        for name in names: clone(os.path.join(out if label == 'mac' else IOSOUT, name), os.path.join(d, name))
        for name in names:
            if name.endswith('.json'):
                src = os.path.join(out if label == 'mac' else IOSOUT, name)
                value = json.load(open(src)); value.pop('fetch', None)
                atomic_json(os.path.join(d, name), value)
    atomic_json(cache_meta_path(key), {'digest': digest, 'rule_fp': rule_fp, 'code_fp': code_fp})

BF_STATS = collections.Counter()

def bf_digest(msl, key):
    h = hashlib.sha256()
    h.update(msl.encode())
    h.update(b'\0mac\0' + MAC_RECORD.encode())
    h.update(b'\0ios\0' + (ios_metal.RECORD if IOSOUT else '').encode())
    h.update(b'\0bf-want\0' + (b'1' if key in BF_WANT else b'0'))
    return h.hexdigest()

def bf_cache_root(digest): return os.path.join(CAS, 'variant', digest)

def bf_cache_restore(key, digest, fmeta, fmsl):
    root = bf_cache_root(digest)
    names = ['metal', 'mac.metallib'] + (['ios.metallib'] if IOSOUT else [])
    if not all(os.path.exists(os.path.join(root, n)) for n in names): return False
    clone(os.path.join(root, 'metal'), fmsl)
    clone(os.path.join(root, 'mac.metallib'), os.path.join(out, key + '_bf.metallib'))
    if IOSOUT: clone(os.path.join(root, 'ios.metallib'), os.path.join(IOSOUT, key + '_bf.metallib'))
    BF_STATS['hit'] += 1
    return True

def bf_cache_publish(digest, fmsl, maclib, ioslib, fmeta):
    root = bf_cache_root(digest); os.makedirs(root, exist_ok=True)
    clone(fmsl, os.path.join(root, 'metal'))
    clone(maclib, os.path.join(root, 'mac.metallib'))
    if IOSOUT: clone(ioslib, os.path.join(root, 'ios.metallib'))
    atomic_json(os.path.join(root, 'fetch.json'), fmeta)

def write_refl(key, refl):
    for d, rec in ((out, MAC_RECORD),) + (((IOSOUT, ios_metal.RECORD),) if IOSOUT else ()):
        value = dict(refl, chain_metal=rec)
        atomic_json(os.path.join(d, key + '_b.json'), value)

def fetch_finish(key, src, refl):
    if refl['stage'] != 'ps' or key.endswith('_i'): return refl   # (an integer-output variant has no readable-tile variant of its own)
    # Those with rewritable samples get variants as before; those without get an output-mapping-only variant, and only for keys in the list (bf_want.tsv, produced by the host planner)
    fv, fmeta = dmsh_fetch.variant(src, refl['bindings'], om_only_ok=key in BF_WANT)
    if not fv:
        for path in (os.path.join(out, key + '_bf.metallib'), os.path.join(out, key + '_bf.metal')) + ((os.path.join(IOSOUT, key + '_bf.metallib'),) if IOSOUT else ()):
            try: os.unlink(path)
            except FileNotFoundError: pass
        write_refl(key, refl)
        return refl
    BF_STATS['generated'] += 1
    fmsl = os.path.join(out, key + '_bf.metal')
    digest = bf_digest(fv, key)
    with CAS_LOCKS_GUARD: lock = CAS_LOCKS.setdefault(('variant', digest), threading.Lock())
    with lock:
        try:
            if not bf_cache_restore(key, digest, fmeta, fmsl):
                BF_STATS['miss'] += 1
                open(fmsl, 'w').write(fv)
                fair = fmsl[:-6] + '.air'; flib = os.path.join(out, key + '_bf.metallib')
                try:
                    tmp_flib = flib + '.tmp-' + str(os.getpid())
                    mac_compile(fmsl, fair, tmp_flib)
                    os.replace(tmp_flib, flib)
                    ilib = None
                    if IOSOUT:
                        ilib = os.path.join(IOSOUT, key + '_bf.metallib')
                        ios_metal.compile(fmsl, fmsl[:-6] + '.ios.air', ilib + '.tmp')
                        os.replace(ilib + '.tmp', ilib)
                except subprocess.CalledProcessError:
                    return refl
                BF_STATS['compile'] += 1
                bf_cache_publish(digest, fmsl, flib, ilib, fmeta)
        finally:
            for path in (fmsl, fmsl[:-6] + '.air', fmsl[:-6] + '.ios.air'):
                try: os.unlink(path)
                except FileNotFoundError: pass
    refl['fetch'] = fmeta
    write_refl(key, refl)
    return refl

# Capture directory: a record in the layout of a Switch shader package entry is synthesized from each key: 0x38 empty bytes + the code area (length
# at control section +0x704; graphics = 0x30 header + SPH + instructions, compute = instructions) + the control section.
# Stage: graphics read the first SPH word (+0x30): type bits 0–4 (1 = VTG, 2 = pixel), shader type bits 10–13 (1 = vertex, 5 = pixel); data that does
# not start with the code area marker 78563412 is compute.
stage_of, CAP_RECS = {}, {}
for fn in sorted(os.listdir(cap_dir)):
    if not fn.endswith('.ctl'): continue
    key = fn[:-4]; ctl = open(os.path.join(cap_dir, fn), 'rb').read(); data = open(os.path.join(cap_dir, key + '.data'), 'rb').read()
    if len(ctl) != 0x880 or ctl[:4] != bytes.fromhex('34127698'): sys.exit(f'[red] capture {key}: bad control section (length {len(ctl):#x})')
    total = struct.unpack_from('<I', ctl, 0x704)[0]
    if hashlib.sha256(ctl + data[:total]).hexdigest()[:16] != key: sys.exit(f'[red] capture {key}: the hash of control section + code area does not match the name')
    if data[:4] == bytes.fromhex('78563412'):
        w0 = struct.unpack_from('<I', data, 0x30)[0]; st = {(1, 1): 'vs', (2, 5): 'ps'}.get((w0 & 0x1f, (w0 >> 10) & 0xf))
        if not st: sys.exit(f'[red] capture {key}: first SPH word {w0:#x} is neither vertex nor pixel (tessellation / geometry shaders are not supported)')
    else: st = 'cs'
    if len(data) < total: sys.exit(f'[red] capture {key}: data has {len(data):#x} bytes, the code area needs {total:#x}')
    CAP_RECS[key] = bytes(0x38) + data[:total] + ctl; stage_of[key] = st

def job_for(key, rec, w):
    ci = len(rec) - 0x880; code = rec[0x38:ci]; stage = stage_of[key]
    c1o, c1t = struct.unpack_from('<2I', rec, ci + 0x700)   # control section +0x700: c1 relative to the instruction area, +0x704: instruction area length (c1 lies between)
    c1 = rec[0x38 + c1o:0x38 + c1t]
    if stage != 'cs': code = code[0x30:]
    cp = os.path.join(w, key + '.bin'); open(cp, 'wb').write(code)
    # Translation options (dmsh_xlate/Program.cs); relaxed precision is on by default (last-bit GPU float differences do not reach game state).
    # XLATE_DEBUG=0 is refused here too, where the Vulkan bundle (dmsh_vk.py) takes its jobs (dmsh_chain.py, plan item 200)
    if os.environ.get('XLATE_DEBUG', '1') == '0': sys.exit(dmsh_chain.XLATE_DEBUG_REFUSED)
    j = {'key': key, 'stage': stage, 'code': cp, 'tex': {}, 'img': {}, 'attr': {}, 'fout': {}, 'c1': c1.hex(), 'c2': {'tex': C2['tex'], 'img': C2['img']}, 'depth': DEPTH,
         'debug': os.environ.get('XLATE_DEBUG', '1') != '0', 'reduced': os.environ.get('XLATE_REDUCED', '1') == '1'}
    # Compute thread group size: control section +0x748 / +0x74c / +0x750 (u32 x / y / z; matches Unity's threadGroupSize metadata for every kernel checked)
    local = list(struct.unpack_from('<3I', rec, ci + 0x748)) if stage == 'cs' else None
    if local is not None: j['local'] = local
    if stage == 'ps': j['alpha_output'] = bool(struct.unpack_from('<I', code, 18 * 4)[0] & 8)   # SPH word 18 (OmapTargets) bit 3: colour 0's alpha is exported
    # Control section +0x718 bit 0: the game asks for early fragment tests (the NVN driver's EarlyZForce; plan item 239, after MacNativeGames PR #222
    # and nxport, MIT): Metal otherwise tests depth after any fragment function that writes memory, which also stops hidden surface removal for it
    if stage == 'ps': j['early_z'] = bool(rec[ci + 0x718] & 1)
    j['pc_missing'] = True   # texture / attribute / output type queries are answered with the translator's defaults
    return j, c1

def xlate_identity(job):
    """What the translator reads of a job (plan item 311b, after MacNativeGames fb9b734's finding, facts: 3,416 of Tears of the Kingdom 1.4.3's 28,771
    shaders differ from another only in control bytes no translation reads, nearly all at +0x7d0..+0x7df): its code and every field but the key, the
    code file's name and the padding (applied after the translation, per key). Jobs with one identity get one translation (wave, dmsh_vk.py)"""
    h = hashlib.sha256(open(job['code'], 'rb').read())
    h.update(json.dumps({k: v for k, v in job.items() if k not in ('key', 'code', 'pad_want')}, sort_keys=True).encode())
    return h.hexdigest()

def xlate_share(keys, jobs, w, xerr):
    """keys → (the keys to translate, a function that gives each other key its first twin's output under its own name, after the translation)"""
    first, twin = {}, {}
    for k in keys:
        i = xlate_identity(jobs[k])
        if i in first: twin[k] = first[i]
        else: first[i] = k
    def spread():
        for k, c in twin.items():
            if c in xerr: xerr[k] = xerr[c]
            for ext in ('.spv', '.res.json'):
                if os.path.exists(os.path.join(w, c + ext)): shutil.copyfile(os.path.join(w, c + ext), os.path.join(w, k + ext))
        return len(twin)
    return [k for k in keys if k not in twin], spread

def raster_facts(stage, src):
    """Static facts needed for the runtime proof that the load can be skipped (full-coverage first draw) (nvn_mtl.m first_draw_covers):
        Vertex: whether position x / y / z are taken unchanged from the .x / .y / .z of one and the same vertex attribute (w is the constant 1 or the same attribute's .w), with no branches and no clip distances in the main function - if so, pos_attr = attribute slot;
        Fragment: whether it may discard (discard_fragment), whether it writes sample_mask, and which color indices it writes. Anything unrecognized is treated as "unprovable" (pos_attr = -1 / discard = 1)"""
    if stage == 'ps':
        out = re.search(r'struct main0_out\s*\{(.*?)\};', src, re.S)
        return {'discard': int('discard_fragment' in src), 'sample_mask': int('sample_mask' in (out.group(1) if out else '')),
                'colors': sorted({int(x) for x in re.findall(r'\[\[color\((\d+)\)\]\]', out.group(1) if out else '')})}
    if stage != 'vs': return {}
    body = re.search(r'\bvertex\s+\w+\s+\w+\s*\(.*?\)\s*\{(.*)\}\s*$', src, re.S)   # the main function is the last function in the file
    if not body or re.search(r'\b(if|for|while|switch|do)\b|clip_distance|ClipDistance', body.group(1)): return {'pos_attr': -1}
    b = body.group(1)
    attr = {m.group(2): int(m.group(3)) for m in re.finditer(r'(\w+)\s+(\w+)\s*\[\[attribute\((\d+)\)\]\]', src)}
    var = {m.group(1): (m.group(2), m.group(3)) for m in re.finditer(r'float\s+(_\w+)\s*=\s*in\.(\w+)\.([xyzw]);', b)}
    last = {}
    for m in re.finditer(r'out\.gl_Position\.([xyzw])\s*=\s*([^;]+);', b): last[m.group(1)] = m.group(2).strip()
    src_of = {c: var.get(v) for c, v in last.items()}
    m0 = src_of.get('x')
    if not m0 or m0[0] not in attr or any(src_of.get(c) != (m0[0], c) for c in 'xyz'): return {'pos_attr': -1}
    if last.get('w') == '1.0': w = 'one'
    elif src_of.get('w') == (m0[0], 'w'): w = 'attr'
    else: return {'pos_attr': -1}
    # uv pass-through (used for the "this-pixel addressing" proof in merge B step 2): the .x / .y of interpolated output locn K are taken unchanged from the .x / .y of vertex attribute A → uv_from_attr[K] = A
    ost = re.search(r'struct main0_out\s*\{(.*?)\};', src, re.S)
    locn = {m.group(1): int(m.group(2)) for m in re.finditer(r'(m_\d+) \[\[user\(locn(\d+)\)\]\]', ost.group(1) if ost else '')}
    olast = {}
    for m in re.finditer(r'out\.(m_\d+)\.([xyzw])\s*=\s*([^;]+);', b): olast[(m.group(1), m.group(2))] = m.group(3).strip()
    uv, aff = {}, {}
    for mem, k in locn.items():
        sx, sy = var.get(olast.get((mem, 'x'), '')), var.get(olast.get((mem, 'y'), ''))
        if sx and sy and sx[0] == sy[0] and sx[0] in attr and sx[1] == 'x' and sy[1] == 'y': uv[str(k)] = attr[sx[0]]; continue
        # Affine: component = fma(attribute component, uniform[slot].vector[i].component, uniform[slot].vector[j].component) (vp_cN is uniform slot N − 3)
        def fm(v_):
            m = re.search(r'float %s = fma\((_\w+), \(\*spvDescriptorSet0\.vp_c(\d+)\)\._m0\[(\d+)\]\.([xyzw]), \(\*spvDescriptorSet0\.vp_c(\d+)\)\._m0\[(\d+)\]\.([xyzw])\);' % re.escape(v_), b)
            if not m or m.group(1) not in var: return None
            return var[m.group(1)], [int(m.group(2)) - 3, int(m.group(3)), 'xyzw'.index(m.group(4))], [int(m.group(5)) - 3, int(m.group(6)), 'xyzw'.index(m.group(7))]
        fx, fy = fm(olast.get((mem, 'x'), '')), fm(olast.get((mem, 'y'), ''))
        if fx and fy and fx[0][0] == fy[0][0] and fx[0][0] in attr and fx[0][1] == 'x' and fy[0][1] == 'y' and min(fx[1][0], fx[2][0], fy[1][0], fy[2][0]) >= 0:
            aff[str(k)] = {'attr': attr[fx[0][0]], 'sx': fx[1], 'ox': fx[2], 'sy': fy[1], 'oy': fy[2]}
    return {'pos_attr': attr[m0[0]], 'pos_w': w, 'uv_from_attr': uv, 'uv_affine': aff}

def decode_tex(name):
    """fp_t_cb{slots}_{handle} / fp_t_tcb_{handle}: texture slot, sampler slot (Ryujinx TextureHandle encoding; c[2] handle table)"""
    m = re.match(r'[a-z]+_[ti]\w*?_(?:cb(\d+)|tcb)_([0-9A-F]+)', name)
    if not m: return None
    slots = int(m.group(1)) if m.group(1) else -1; h = int(m.group(2), 16)
    tb = (slots & 0xffff) if slots >= 0 else 2; sb = ((slots >> 16) - 1) if slots >= 0 and (slots >> 16) else tb
    o0, o1, ty = h & 0x3fff, (h >> 14) & 0x3fff, h >> 28
    def slot(word, base, cb): return (word * 4 - base) // 8 if cb == 2 and word * 4 >= base and (word * 4 - base) % 8 == 0 else None
    words = [o0] + ([o1] if ty else [])
    if re.match(r'[a-z]+_i', name):   # image
        return next((slot(x, IMG_BASE, tb) for x in words if IMG_BASE <= x * 4 < IMG_END and slot(x, IMG_BASE, tb) is not None), None), None, ty
    t = next((slot(x, TEX_BASE, tb) for x in words if slot(x, TEX_BASE, tb) is not None and x * 4 < TEX_END), None)
    s = next((slot(x, SAMP_BASE, sb) for x in words if SAMP_BASE <= x * 4 < SAMP_END and slot(x, SAMP_BASE, sb) is not None), None)
    return t, s, ty

def img_size_queries(src):
    """
    Size queries on an image handle: a TXQ immediate handle falling in c[2]'s image table (IMG_BASE..IMG_END) gets its own texture argument <seg>_t_tcb_<handle> (plus a ...Smplr sampler) from Ryujinx,
    and the host has no "image as texture" binding. They query the same descriptor, so when this shader also accesses the image through the same handle (<seg>_i_cb2_<handle>_...), point the queries at that image argument and drop the extra texture / sampler members.
    Only texture arguments used solely for get_width / get_height / get_depth / get_num_mip_levels are changed; returns (new MSL, list of changed member names)"""
    body = re.search(r'struct spvDescriptorSetBuffer0\s*\{(.*?)\};', src, re.S)
    if not body: return src, []
    members = re.findall(r'^\s*.+?\s+(\w+)\s*\[\[id\(\d+\)\]\];', body.group(1), re.M)
    done = []
    for nm in members:
        m = re.fullmatch(r'([a-z]+)_t_tcb_([0-9A-F]+)', nm)
        if not m or not (IMG_BASE <= int(m.group(2), 16) * 4 < IMG_END): continue
        img = [x for x in members if re.fullmatch(r'%s_i_cb2_%s_\w*' % (m.group(1), m.group(2)), x)]
        uses = re.findall(r'spvDescriptorSet0\.%s\b(\.\w+\()?' % nm, src)
        if len(img) != 1 or not uses or any(u not in ('.get_width(', '.get_height(', '.get_depth(', '.get_num_mip_levels(') for u in uses): continue
        if re.search(r'spvDescriptorSet0\.%sSmplr\b' % nm, src): continue
        src = re.sub(r'spvDescriptorSet0\.%s\.' % nm, 'spvDescriptorSet0.%s.' % img[0], src)
        src = re.sub(r'^\s*.+?\s+%s(Smplr)?\s*\[\[id\(\d+\)\]\];\n' % nm, '', src, flags=re.M)
        done.append(nm)
    return src, done

def scale_index(nm, res):
    """Ryujinx's resolution-scale index of a texture or image argument (plan item 320f; Translation/Transforms/TexturePass.cs: render_scale[1 + i]
    and render_scale_y[1 + i], i the texture's place among the shader's texture descriptors, an image's after the textures that are not arrays):
    the argument's constant buffer slot and handle (ResourceManager.cs names them <stage>_t[...]_tcb_<handle> or _cb<slot>_<handle>, tcb = slot -1)
    found once in the translator's lists (res: Program.cs, the order of ShaderProgramInfo), else None"""
    m = re.match(r'[a-z]+_([ti])\w*?_(?:cb(\d+)|tcb)_([0-9A-F]+)', nm)
    if not m: return None
    cb = int(m.group(2)) if m.group(2) else -1; h = int(m.group(3), 16)
    lst = res['images'] if m.group(1) == 'i' else res['textures']
    hits = [i for i, t in enumerate(lst) if t['cbuf_slot'] == cb and t['handle'] == h]
    if len(hits) != 1: return None
    return hits[0] + (sum(1 for t in res['textures'] if t['array'] <= 1) if m.group(1) == 'i' else 0)

def cb_slots(nm, msl):
    """The 16-byte slots a uniform binding's shader reads (plan item 320g: the widening scans only these): the constant indices of its
    (*spvDescriptorSet0.<nm>)._m0[i] reads as up to 8 ranges [lo, hi] (the nearest gaps closed first), or None when an index is not a constant or
    the binding is used another way (the whole block then)"""
    uses = re.findall(r'\b%s\b' % re.escape(nm), msl)
    reads = re.findall(r'\(\*spvDescriptorSet0\.%s\)\._m0\[([^\]]+)\]' % re.escape(nm), msl)
    decl = len(re.findall(r'^\s*constant \w+\* %s \[\[id\(\d+\)\]\];' % re.escape(nm), msl, re.M))
    if not reads or len(reads) + decl != len(uses) or any(not r.isdigit() for r in reads): return None
    sl = sorted(set(int(r) for r in reads)); rg = [[sl[0], sl[0]]]
    for x in sl[1:]:
        if x == rg[-1][1] + 1: rg[-1][1] = x
        else: rg.append([x, x])
    while len(rg) > 8:   # the nearest gap closed
        i = min(range(len(rg) - 1), key=lambda j: rg[j + 1][0] - rg[j][1]); rg[i][1] = rg[i + 1][1]; del rg[i + 1]
    return rg

def reflect(key, stage, msl, res):
    """MSL argument buffer struct member → replay reflection: kind (same as nvn_refl.h) + NVN slot number + argument buffer member index"""
    body = re.search(r'struct spvDescriptorSetBuffer0\s*\{(.*?)\};', msl, re.S)
    sb_cb = {b['binding']: b for b in res['sbufs']}
    binds = []
    for m in re.finditer(r'^\s*(.+?)\s+(\w+)\s*\[\[id\((\d+)\)\]\];', body.group(1) if body else '', re.M):
        ty, nm, aid = m.group(1).strip(), m.group(2), int(m.group(3))
        e = {'msl_name': nm, 'msl_type': ty, 'arg_id': aid}
        if nm == 'support_buffer': e.update(kind='support')
        elif re.fullmatch(r'[a-z]+_c(\d+)', nm):
            n = int(re.fullmatch(r'[a-z]+_c(\d+)', nm).group(1))
            e.update(kind='cbuf', reg=n - 3, cb_slots=cb_slots(nm, msl)) if 3 <= n <= 16 else e.update(kind='c1' if n == 1 else 'drvcb', bank=n)
        elif re.fullmatch(r'[a-z]+_s(\d+)', nm):
            e.update(kind='sbuf', index=int(re.fullmatch(r'[a-z]+_s(\d+)', nm).group(1)))
        elif nm.endswith('Smplr'):   # handle type 0 = texture and sampler combined in one handle: the sampler index is taken from the high bits of that texture slot's handle
            t, s, ty = decode_tex(nm[:-5]); e.update(kind='samp', reg=s) if ty else e.update(kind='samp_tex', reg=t)
        else:
            d = decode_tex(nm)
            if not d: e.update(kind='?')
            else:
                e.update(kind='img' if re.match(r'[a-z]+_i', nm) else ('tbuf' if 'texture_buffer' in ty else 'tex'), reg=d[0])
                if e['kind'] in ('tex', 'img'): e['scale_ix'] = scale_index(nm, res)   # plan item 320f (None: not found once)
        binds.append(e)
    # Storage buffers: SPIR-V binding number → c0 offset → NVN storage buffer slot (c0 base: vertex 0x110, compute 0x310, pixel 0x510, 0x10 per slot)
    base = {'vs': 0x110, 'cs': 0x310, 'ps': 0x510}.get(stage)   # storage buffer base of the hull / domain stages is not measured: a storage buffer there gets no slot
    sb_sorted = sorted(res['sbufs'], key=lambda b: b['binding'])
    for e in binds:
        if e['kind'] == 'sbuf':
            b = sb_sorted[e['index']] if e['index'] < len(sb_sorted) else None
            off = b['cb_offset'] * 4 if b and b['cb_slot'] == 0 else None
            if stage == 'cs' and off is not None and 0x210 <= off < 0x310 and (off - 0x210) % 16 == 0:
                e.update(kind='ubo_ptr', reg=(off - 0x210) // 16)   # compute-stage uniform slot r: address at c0[0x210 + 0x10 r], read through global memory (checked against cs_lut_combine_c reading cb10[66])
            elif b and 3 <= b['cb_slot'] <= 16:
                # The address (and size) is read from a uniform buffer the game binds: cb slot n = NVN uniform slot n − 3, at byte cb_offset × 4
                # (MacNativeGames' "sbuf_ubo", ly05010419/MacNativeGames, MIT; 21 of Tears of the Kingdom's vertex shaders); the host reads it at bind time
                e.update(kind='sbuf_ubo', reg=b['cb_slot'] - 3, off=b['cb_offset'] * 4)
            else: e['reg'] = (off - base) // 16 if off is not None and base is not None and 0 <= off - base < 0x100 and (off - base) % 16 == 0 else None
            e['rw'] = bool(b and b['flags'] & 1)   # Ryujinx BufferUsageFlags.Write = 1 << 0 (this tested bit 1, so no storage buffer was ever writable)
    return binds

# What the replay layer fills of the driver segments, per stage (nvn_mtl.m drv_fill). c0: the storage buffer table (16 slots × 0x10) at 0x110 (vertex),
# 0x310 (compute), 0x510 (pixel); compute also the dispatch size at 0x200 and the uniform buffer table at 0x210; the graphics stages the largest point
# size at 0x610. c2: the texture and sampler handle tables (nvn_c2)
DRV_FILL = {'vs': {0: [(0x110, 0x210), (0x610, 0x620)]}, 'cs': {0: [(0x200, 0x410)]}, 'ps': {0: [(0x510, 0x620)]}}   # whole vec4s (the rest of a vec4 reads 0)
for _st in DRV_FILL: DRV_FILL[_st][2] = [(TEX_BASE, TEX_END), (SAMP_BASE, SAMP_END)]
DRV_PREFIX = {'vs': 'vp', 'ps': 'fp', 'cs': 'cp'}   # spirv-cross names the segments <stage>p_c0 / <stage>p_c2

def drv_reads(src, stage):
    """Reads of driver segments c0 / c2 in the MSL of a shader of this stage: constant index → byte range; non-constant indices are recorded as dynamic.
    (This scanned fragment-shader names only, fp_c0, so vertex and compute shaders reading c0 were given an all-zero segment: nxnative 9579030 /
    MacNativeGames' drv_scan; in Tears of the Kingdom's library 229 vertex and 146 compute shaders)
    words = offsets of the 4-byte words actually read: an index followed by components (.x / .yz ...) records only those components, without components the whole vec4
    (c2's handle tables use 8 bytes per slot, not vec4-aligned: with manosaba's layout _m0[22].z = 0x168 is texture slot 0, counting by vec4 would falsely report 0x160)"""
    out = {}
    for bank in (0, 2):
        offs, words, dyn = set(), set(), False
        for m in re.finditer(r'%s_c%d\)\._m0\[([^\]]+)\](\.[xyzw]{1,4}\b)?' % (DRV_PREFIX[stage], bank), src):
            idx = m.group(1).rstrip('u')
            if not idx.isdigit(): dyn = True; continue
            o = int(idx) * 16; offs.add(o)
            words.update(o + 4 * 'xyzw'.index(c) for c in (m.group(2)[1:] if m.group(2) else 'xyzw'))
        if offs or dyn: out[str(bank)] = {'vec4_offsets': sorted(offs), 'words': sorted(words), 'dynamic': dyn}
    return out

def unfillable(dr, stage):
    bad = []
    for bank, d in dr.items():
        if d['dynamic']: bad.append(f'c{bank} dynamic index')
        for o in d['words']:
            if not any(a <= o and o + 4 <= b for a, b in DRV_FILL[stage][int(bank)]): bad.append(f'c{bank}+{o:#x}')
    return bad

def vs_init(src):
    """Every float4 output of a vertex shader starts as (0, 0, 0, 1) before the translated body: on the hardware that is what an unwritten component
    reads. The translator's source starts from out = {}, all zeros; an output written only in xyz (a world position) that the pixel shader divides by its
    w then divided by 0 (seedds/nxnative e81d66f, MIT: Link's Awakening was in shadow everywhere outdoors). Components the body writes are overwritten"""
    i = src.find('struct main0_out\n{\n'); j = src.find('\n};', i); k = src.find('main0_out out = {};')
    if i < 0 or j < 0 or k < 0: return src
    outs = re.findall(r'\n    float4 (\w+) \[\[user\(locn\d+\)\]\];', src[i:j])
    k += len('main0_out out = {};')
    return src[:k] + ''.join(f'\n    out.{n} = float4(0.0, 0.0, 0.0, 1.0);' for n in outs) + src[k:]

RULES = []   # MSL rewrite rules attached by shader name (function, applicability test); the former dark-range remap toe has been removed (back to the original Switch brightness)
# The first-stage code fingerprint does not include dmsh_fetch.py; readable-tile variants get a separate second-stage cache keyed on the generated MSL.
CODE_FP = hashlib.sha256(source_without_rule_bodies().encode() + b''.join(b'\0' + open(m.__file__, 'rb').read() for m in (dmsh_chain,) + ((ios_metal,) if IOSOUT else ()))).hexdigest()
TOOL_FP = tool_fingerprint().encode()

# Integer vertex inputs. The translated vertex shaders declare every input as float and bit-cast where the original used it as an integer (Maxwell hands
# integer attributes over unconverted). When the game's vertex format is an integer one, Metal refuses that float input on iOS ("Cannot convert
# attribute"; macOS converts the value instead, which breaks the bit-cast). For vertex shaders that bit-cast an input component (as_type<int / uint>),
# a second entry main0_vi is appended whose such inputs can be declared uint / int instead, each chosen by a function constant (VI_U + location /
# VI_S + location); it hands the shader the same bits again with as_type<float>. main0 stays exactly as before; the host specializes main0_vi only
# for an integer format (nvn_mtl.m pso_desc). Returns (source with the variant, sorted locations) or (source, []).
VI_U, VI_S = 128, 144
def vi_variant(src):
    st = re.search(r'struct main0_in\n\{\n(.*?)\n\};\n', src, re.S)
    fn = re.search(r'^vertex (\w+) main0\(main0_in in \[\[stage_in\]\](.*?)\)\n\{\n(.*?)\n\}\n', src, re.S | re.M)
    if not st or not fn: return src, []
    members = {m.group(2): (m.group(1), int(m.group(3))) for m in re.finditer(r'^\s*(float[234]?) (m_\w+) \[\[attribute\((\d+)\)\]\];$', st.group(1), re.M)}
    body = fn.group(3)
    var = {v: mem for v, mem in re.findall(r'\bfloat[234]? (_\d+) = in\.(m_\w+)(?:\.[xyzw]+)?;', body)}
    used = set()
    for x in re.findall(r'as_type<u?int[234]?>\((_\d+|in\.m_\w+)', body):
        mem = x[3:] if x.startswith('in.') else var.get(x)
        if mem in members: used.add(mem)
    if not used: return src, []
    locs = sorted(members[n][1] for n in used)
    out = ['', '// Integer vertex inputs (dmsh_build.py vi_variant): specialized by the host only for integer vertex formats']
    for L in locs:
        out += [f'constant bool dm_vi{L}_u [[function_constant({VI_U + L})]];', f'constant bool dm_vi{L}_s [[function_constant({VI_S + L})]];',
                f'constant bool dm_vi{L}_un = is_function_constant_defined(dm_vi{L}_u) && dm_vi{L}_u;',
                f'constant bool dm_vi{L}_sn = is_function_constant_defined(dm_vi{L}_s) && dm_vi{L}_s;',
                f'constant bool dm_vi{L}_f = !dm_vi{L}_un && !dm_vi{L}_sn;']
    lines = []
    for ln in st.group(1).split('\n'):
        m = re.match(r'^(\s*)(float([234]?)) (m_\w+) \[\[attribute\((\d+)\)\]\];$', ln)
        if m and m.group(4) in used:
            ind, t, n, nm, L = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
            lines += [f'{ind}{t} {nm} [[attribute({L}), function_constant(dm_vi{L}_f)]];', f'{ind}uint{n} {nm}_u [[attribute({L}), function_constant(dm_vi{L}_un)]];',
                      f'{ind}int{n} {nm}_s [[attribute({L}), function_constant(dm_vi{L}_sn)]];']
        else: lines.append(ln)
    out += ['struct main0_in_vi', '{', *lines, '};', '']
    pro = ''.join(f'    {members[n][0]} dm_in_{n} = dm_vi{members[n][1]}_un ? as_type<{members[n][0]}>(in.{n}_u) : dm_vi{members[n][1]}_sn ? as_type<{members[n][0]}>(in.{n}_s) : in.{n};\n'
                  for n in sorted(used, key=lambda n: members[n][1]))
    body_vi = re.sub(r'\bin\.(m_\w+)\b', lambda m: 'dm_in_' + m.group(1) if m.group(1) in used else m.group(0), body)
    out.append(f'vertex {fn.group(1)} main0_vi(main0_in_vi in [[stage_in]]{fn.group(2)})\n{{\n{pro}{body_vi}\n}}\n')
    return src + '\n'.join(out), locs

# Vertex / pixel pairs the game put in one program: <capture dir>/pairs.tsv, written by the host while capturing (DM_SHDUMP). Vertex key → pixel keys
PAIRS = collections.defaultdict(set)
if os.path.exists(os.path.join(cap_dir, 'pairs.tsv')):
    for _ln in open(os.path.join(cap_dir, 'pairs.tsv')):
        _f = _ln.split()
        if len(_f) == 2 and not _ln.startswith('#'): PAIRS[_f[0]].add(_f[1])

# Integer render targets: <capture dir>/fout.tsv, "<pixel key> <target> uint|sint" per line, written by the host when a pipeline failed because the
# shader writes float to an integer colour attachment (nvn_mtl.m; DM_SHDUMP). The translator then declares that output integer and stores the
# register bits unchanged (Ryujinx answers QueryGraphicsState's fragment output types from the render target formats the same way). Tears of the
# Kingdom's BC4 encoder writes RGBA16UI. Part of the shader's cache key
# The game profile's games/<DM_GAME>/fout.tsv is read too (the same format): outputs known without a run that failed (plan item 100: Tears of the
# Kingdom's Ultrahand mask shader, from nxnative PR #11's shader_fout.tsv), so a new workspace builds them right the first time
# Such outputs go to a variant <key>_i beside the float library (plan item 162): one pixel shader can be bound to float targets and to integer ones
# (MacNativeGames' shared list, MIT, keys as data: 23 of Tears of the Kingdom's keys with both; two that our fout.tsv had made integer only drew
# nothing on float targets instead). The host takes <key>_i_b for a pipeline with an integer format where the shader writes (nvn_mtl.m fout_fn)
FOUT = collections.defaultdict(dict)
for _fp in (os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'games', GAME_ID, 'fout.tsv'), os.path.join(cap_dir, 'fout.tsv')):
    if not os.path.exists(_fp): continue
    for _ln in open(_fp):
        _f = _ln.split()
        if _ln.startswith('#') or len(_f) != 3: continue
        if not _f[1].isdigit() or not 0 <= int(_f[1]) < 8 or _f[2] not in ('uint', 'sint'): sys.exit(f'[red] {_fp}: bad line {_ln.strip()!r}')
        if FOUT[_f[0]].get(_f[1], _f[2]) != _f[2]: sys.exit(f'[red] {_fp}: {_f[0]} target {_f[1]} is both uint and sint')
        FOUT[_f[0]][_f[1]] = _f[2]

def vs_pad_locs(key):
    """Locations the paired pixel shaders of a vertex shader read, from their built MSL (the vertex shader's own outputs are subtracted in vs_pad,
    once its MSL exists). Part of the vertex shader's cache key"""
    want = set()
    for ps in sorted(PAIRS.get(key, ())):
        fn = os.path.join(out, ps + '_b.metal')
        if os.path.exists(fn): want |= {int(x) for x in re.findall(r'\[\[user\(locn(\d+)\)', open(fn).read())}
    return sorted(want)

def vs_pad(src, want):
    """Give a vertex shader the outputs its paired pixel shaders read but it does not write, with the value (0, 0, 0, 1). Maxwell attributes are untyped
    32-bit slots and a pixel shader reading a location the vertex shader did not write gets a default (Ryujinx fills (0, 0, 0, 1) when it links
    stages); every shader here is translated on its own, and Metal refuses to link a pixel input no vertex output provides (19 of Tears of the
    Kingdom's pipelines failed so; their draws were skipped). From nxnative's dmsh_build.py vs_pad (seedds, MIT). Returns (source, padded locations)"""
    miss = sorted(set(want) - {int(x) for x in re.findall(r'\[\[user\(locn(\d+)\)', src)})
    i = src.find('struct main0_out\n{\n'); j = src.find('\n};', i); k = src.find('main0_out out = {};')
    if not miss or i < 0 or j < 0 or k < 0: return src, []
    src = src[:j] + ''.join(f'\n    float4 dm_pad{l} [[user(locn{l})]];' for l in miss) + src[j:]
    k = src.find('main0_out out = {};') + len('main0_out out = {};')
    return src[:k] + ''.join(f'\n    out.dm_pad{l} = float4(0.0, 0.0, 0.0, 1.0);' for l in miss) + src[k:], miss

ALPHA_DECL = ('constant uint dm_alpha_func_c [[function_constant(160)]];\n'
              'constant float dm_alpha_ref_c [[function_constant(161)]];\n'
              'constant uint dm_alpha_func = is_function_constant_defined(dm_alpha_func_c) ? dm_alpha_func_c : 8u;\n'
              'constant float dm_alpha_ref = is_function_constant_defined(dm_alpha_ref_c) ? dm_alpha_ref_c : 0.0f;\n\n')
def alpha_test(src, output_alpha):
    """The NVN alpha test (Metal has none), after lordran-native's dmsh_build.py alpha_test (a63dd76, MIT): before each return of the fragment entry,
    colour 0's alpha is compared with the reference under the NVN comparison (1 never, 2 less, 3 equal, 4 lequal, 5 greater, 6 notequal, 7 gequal,
    8 always), function constants 160 and 161: the host specializes every such shader with ALWAYS at load (Metal refuses an unspecialized function
    for a pipeline), which folds the comparison and its discard away (a fragment function that may discard loses hidden surface removal on Apple
    GPUs, so the values are not read from a buffer), and a draw with an alpha test gets its own specialization (nvn_mtl.m alpha_fn). The first
    specialization of a shader costs 2-18 ms on an M5 (the system's shader cache keeps it), so the game profile opts in (game.json alpha_test "on";
    neither of our games calls ColorStateSetAlphaTest). A shader that exports no colour 0 alpha (SPH word 18 bit 3 clear, or no float colour 0) is
    discarded only by NEVER. Each block replaces the return with a braced statement, so it stays inside an unbraced if. Checked by
    gfx/tests/t_alpha.m"""
    entry = re.search(r'\bfragment\s+\w+\s+\w+\s*\(', src)
    if not entry: raise ValueError('alpha test: no fragment entry')
    member = re.search(r'\bfloat4\s+(\w+)\s*\[\[color\(0\)\]\]', src)
    keep = 'dm_alpha_func != 1u'
    if output_alpha and member:
        a = f'float(out.{member[1]}.w)'
        keep = (f'dm_alpha_func == 2u ? {a} < dm_alpha_ref : dm_alpha_func == 3u ? {a} == dm_alpha_ref : dm_alpha_func == 4u ? {a} <= dm_alpha_ref : '
                f'dm_alpha_func == 5u ? {a} > dm_alpha_ref : dm_alpha_func == 6u ? {a} != dm_alpha_ref : dm_alpha_func == 7u ? {a} >= dm_alpha_ref : {keep}')
    test = f'if (dm_alpha_func != 8u && !({keep})) discard_fragment(); '
    head, body = src[:entry.start()], src[entry.start():]
    if re.search(r'\breturn\s+out\s*;', body): body = re.sub(r'\breturn\s+out\s*;', lambda _: '{ ' + test + 'return out; }', body)
    else:
        body = re.sub(r'\breturn\s*;', lambda _: '{ ' + test + 'return; }', body)
        end = body.rfind('}'); body = body[:end] + '    ' + test + '\n' + body[end:]
    return head + ALPHA_DECL + body

def finish(key, w, j, c1):
    res = json.load(open(os.path.join(w, key + '.res.json')))
    msl = os.path.join(w, key + '.metal'); air = msl[:-6] + '.air'; lib = os.path.join(out, key + '_b.metallib')
    r = subprocess.run(['spirv-cross', *XC, os.path.join(w, key + '.spv'), '--output', msl], capture_output=True, text=True)
    if r.returncode: return 'MSL failed', r.stderr.strip()[:160], None
    src, iq = img_size_queries(open(msl).read())
    if iq: res['notes'].append('image size queries moved to the image argument: ' + ' '.join(iq))
    inl = msl_helpers.inline_helpers(src)   # the NoContraction helpers inlined, not called (msl_helpers.py)
    if inl != src: res['notes'].append('NoContraction helpers inlined')
    inl, nprec = msl_helpers.precise_minmax(inl)   # min / max / clamp with Maxwell's results for a NaN operand (msl_helpers.py, plan item 260)
    if nprec: res['notes'].append(f'fast:: min / max / clamp as precise:: ({nprec})')
    inl, nwide = msl_helpers.wide_loads(inl)   # the translator's split storage words read whole (msl_helpers.py, plan item 311b)
    if nwide: res['notes'].append(f'storage words read whole ({nwide})')
    if iq or inl != src: src = inl; open(msl, 'w').write(src)
    pad = []
    if j['stage'] == 'vs' and j.get('pad_want'):   # outputs the paired pixel shaders read but this one does not write get (0, 0, 0, 1) (see vs_pad)
        src, pad = vs_pad(src, j['pad_want'])
        if pad: open(msl, 'w').write(src); res['notes'].append('outputs padded for the paired pixel shaders: locations ' + ' '.join(map(str, pad)))
    if j['stage'] == 'vs':   # outputs start as (0, 0, 0, 1) (vs_init)
        src = vs_init(src); open(msl, 'w').write(src)
    vi_locs = []
    if j['stage'] == 'vs':   # the variant goes into the compiled source only; everything below analyzes the original (src)
        vsrc, vi_locs = vi_variant(src)
        if vi_locs: open(msl, 'w').write(vsrc); res['notes'].append('integer vertex input variant main0_vi: locations ' + ' '.join(map(str, vi_locs)))
    if j['stage'] == 'ps' and ALPHA:   # the alpha test goes into the compiled source only (alpha_test; nvn_mtl.m alpha_fn)
        open(msl, 'w').write(alpha_test(src, j.get('alpha_output', True)))
    try: MTL.compile('mac', msl, lib)   # an MSL compiled before takes that library (dmsh_mtlcache.py)
    except subprocess.CalledProcessError as e:
        return 'metal failed', str(e)[:160], None
    clone(msl, os.path.join(out, key + '_b.metal'))
    if IOSOUT:   # iOS: the same MSL goes through iOS's single compile convention (ios_metal.py) to produce an iphoneos metallib
        try: MTL.compile('ios', msl, os.path.join(IOSOUT, key + '_b.metallib'))
        except subprocess.CalledProcessError as e: return 'iOS metal failed', str(e)[:160], None
    entry = re.search(r'(vertex|fragment|kernel)\s+\S+\s+(\w+)\s*\(', src)
    dr = drv_reads(src, j['stage'])
    refl = {'key': key + '_b', 'route': 'B', 'chain': CHAIN, 'dm_ctl': key, 'source': 'capture', 'stage': j['stage'],
            'entry': entry.group(2) if entry else 'main0', 'msl_entry_kind': entry.group(1) if entry else None,
            'threadgroup_size': j.get('local'), 'c1': c1.hex(), 'c2': C2, 'depth_mode': DEPTH, 'notes': res['notes'], 'drv_reads': dr, 'drv_unfillable': unfillable(dr, j['stage']),
            'bindings': reflect(key, j['stage'], src, res),
            'vertex_inputs': [{'location': int(m.group(2)), 'msl_type': m.group(1)} for m in re.finditer(r'(\w+)\s+\w+\s*\[\[attribute\((\d+)\)\]\]', src)],
            'raster': raster_facts(j['stage'], src), 'vs_pad': pad, 'alpha_test': j['stage'] == 'ps' and ALPHA,
            'frag_coord': bool(res.get('uses_frag_coord'))}   # plan item 320f: the shader reads the fragment coordinate (Ryujinx's UsesFragCoord)
    if vi_locs: refl['vi_entry'] = 'main0_vi'; refl['vi_int'] = vi_locs
    if j['stage'] == 'vs': refl['screen_locn'] = dmsh_fetch.screen_locn(src)   # outputs holding the projected screen position (the vertex side of a projected fetch proof)
    write_refl(key, refl)
    fetch_finish(key, src, refl)
    return 'ok', '', refl

keys = sorted(want) if want else sorted(CAP_RECS)
FOUT_KEYS = sorted(k for k in FOUT if stage_of.get(k) == 'ps' and k in keys)   # the integer-output variants (see FOUT): the same capture as another key
for _k in FOUT_KEYS: CAP_RECS[_k + '_i'] = CAP_RECS[_k]; stage_of[_k + '_i'] = 'ps'
keys += [_k + '_i' for _k in FOUT_KEYS]
NJ = int(os.environ.get('J', '8'))
os.makedirs(out, exist_ok=True)
w = tempfile.mkdtemp(prefix='dmsh_', dir=os.environ.get('WORK'))
MTL = dmsh_mtlcache.Cache(os.path.join(CAS, 'metallib'), {'mac': ('macosx', MAC_RECORD, mac_compile), **({'ios': ('iphoneos', ios_metal.RECORD, ios_metal.compile)} if IOSOUT else {})}, w)
# First take the libraries earlier chain versions left on disk: the b-*/ folders beside the output (iOS: the folder of the same name) and the roots
# DMSH_PREFILL lists (<mac root>[=<iOS root>], separated by ':')
for _spec in [cache_root + ('=' + os.path.dirname(IOSOUT) if IOSOUT else '')] + [x for x in os.environ.get('DMSH_PREFILL', '').split(os.pathsep) if x]:
    _mac, _, _ios = _spec.partition('=')
    for _b in sorted(os.listdir(_mac)) if os.path.isdir(_mac) else []:
        if _b.startswith('b-') and os.path.realpath(os.path.join(_mac, _b)) != os.path.realpath(out):
            for _ln in MTL.fill(os.path.join(_mac, _b), os.path.join(_ios, _b) if _ios else None): print(_ln, flush=True)
jobs, c1s = {}, {}
digests, rule_fps, cache_reasons, cache_hits = {}, {}, {}, set()
XL_STATS = collections.Counter()
xerr = {}
def wave(wkeys):
    """Look up, translate and finish one set of keys. Vertex shaders go in a second wave, after the pixel shaders they are padded for (vs_pad)
    have their MSL; the padded locations are part of their cache key"""
    for key in wkeys:
        if key not in CAP_RECS: sys.exit(f'[red] {key} is not in the capture directory {cap_dir}')
        rec = CAP_RECS[key]
        pw = vs_pad_locs(key) if stage_of[key] == 'vs' else []
        digests[key] = shader_key(key, 'capture', rec) if not pw else hashlib.sha256((shader_key(key, 'capture', rec) + '\0pad\0' + ' '.join(map(str, pw))).encode()).hexdigest()
        fo = FOUT.get(key[:-2]) if key.endswith('_i') else None   # the float library of a FOUT key stays float; its variant <key>_i takes the integer outputs
        if fo: digests[key] = hashlib.sha256((digests[key] + '\0fout\0' + ' '.join(f'{t}:{fo[t]}' for t in sorted(fo))).encode()).hexdigest()
        rule_fps[key] = hashlib.sha256(shader_rule_source('capture')).hexdigest()
        hit, cache_reasons[key] = cache_lookup(key, digests[key], rule_fps[key], CODE_FP)
        if hit: cache_hits.add(key)
        else:
            jobs[key], c1s[key] = job_for(key, rec, w)
            if pw: jobs[key]['pad_want'] = pw
            if fo: jobs[key]['fout'] = dict(fo)
    misses = [key for key in wkeys if key not in cache_hits]
    tl, spread = xlate_share(misses, jobs, w, xerr)   # one translation per identity (xlate_identity); each key is finished on its own
    chunks = [tl[i::NJ] for i in range(NJ)]
    def xl(ch):
        jp = os.path.join(w, f'job_{abs(hash(ch[0]))}.json'); json.dump([{k_: v for k_, v in jobs[k].items() if k_ != 'pad_want'} for k in ch], open(jp, 'w'))
        return subprocess.run([XLATE, jp, w], capture_output=True, text=True)
    with cf.ThreadPoolExecutor(NJ) as ex: xr = list(ex.map(xl, [c for c in chunks if c]))
    for r in xr:
        for ln in r.stderr.splitlines():
            m = re.match(r'\[red\] (\w+): (.*)', ln)
            if m: xerr[m.group(1)] = m.group(2)[:160]
    XL_STATS['shared'] += spread(); XL_STATS['translated'] += len(tl)
    def fin(key):
        if key in cache_hits:
            refl = json.load(open(os.path.join(out, key + '_b.json')))
            src = open(os.path.join(out, key + '_b.metal')).read() if refl['stage'] == 'ps' else ''
            return key, 'ok', '', fetch_finish(key, src, refl)
        if key in xerr or not os.path.exists(os.path.join(w, key + '.spv')): return key, 'translation failed', xerr.get(key, 'no spv'), None
        result = (key,) + finish(key, w, jobs[key], c1s[key])
        if result[1] == 'ok': publish_cache(key, digests[key], rule_fps[key], CODE_FP)
        return result
    with cf.ThreadPoolExecutor(NJ) as ex: return list(ex.map(fin, wkeys))
res = wave([k for k in keys if stage_of[k] != 'vs']) + wave([k for k in keys if stage_of[k] == 'vs'])
print(f'vertex shaders padded for their pixel shaders: {sum(1 for r in res if r[1] == "ok" and r[3] and r[3].get("vs_pad"))} (pairs file: {sum(len(v) for v in PAIRS.values())} pairs)')
shutil.rmtree(w, ignore_errors=True)
st = collections.Counter(r[1] for r in res)
print(f'shaders {len(res)}: ok {st["ok"]}' + ''.join(f', {k} {v}' for k, v in st.items() if k != 'ok'))
print(MTL.summary())
print(f'shader cache: {PRE_STATS["hit"]} hits, {PRE_STATS["miss"]} rebuilt; translated {XL_STATS["translated"]}, {XL_STATS["shared"]} taking a twin\'s translation (xlate_identity)')
for reason in ('new key', 'rule changed', 'code fingerprint changed: everything rebuilt'):
    if PRE_STATS[reason]: print(f'  {reason} {PRE_STATS[reason]}')
print(f'variant cache: {BF_STATS["generated"]} generated, {BF_STATS["hit"]} hits, {BF_STATS["miss"]} rebuilt, {BF_STATS["compile"]} compiled')
for lvl in [k for k in st if k != 'ok']:
    for e, n in collections.Counter(r[2] for r in res if r[1] == lvl).most_common(10): print(f'  [{lvl}] {n:5d}  {e}')
ok = [r[3] for r in res if r[1] == 'ok']
rd = [x for x in ok if x['drv_reads']]
print(f'shaders reading driver banks at run time: {len(rd)} (c0 {sum("0" in x["drv_reads"] for x in rd)}, c2 {sum("2" in x["drv_reads"] for x in rd)}); reading what the replay layer cannot fill: {sum(bool(x["drv_unfillable"]) for x in ok)}')
for e, n in collections.Counter(u for x in ok for u in x['drv_unfillable']).most_common(15): print(f'  unfillable {n:5d}  {e}')
SLOTK = {'cbuf', 'samp', 'samp_tex', 'tex', 'img', 'tbuf', 'sbuf', 'ubo_ptr', 'sbuf_ubo'}
noreg = [x for x in ok if any(b['kind'] in SLOTK and b.get('reg') is None for b in x['bindings'])]
unk = [x for x in ok if any(b['kind'] == '?' for b in x['bindings'])]
print(f'shaders with a binding without a slot: {len(noreg)}, with an unknown binding kind: {len(unk)} (loading them reports red)')
for e, n in collections.Counter(f"{b['kind']} {b['msl_name'][:28]}" for x in noreg + unk for b in x['bindings'] if b['kind'] == '?' or (b['kind'] in SLOTK and b.get('reg') is None)).most_common(8): print(f'  {n:5d}  {e}')
nt = collections.Counter(nn.split(' (')[0] for x in ok for nn in x['notes'])
for e, n in nt.most_common(8): print(f'  translator note {n:5d}  {e}')
json.dump([{'key': r[0], 'st': r[1], 'err': r[2]} for r in res], open(os.path.join(out, '_b_build.json'), 'w'), ensure_ascii=False)
