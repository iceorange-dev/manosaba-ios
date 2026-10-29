#!/usr/bin/env python3
"""The SDK's address-region node area cut (plan item 289) in every game's rules (games/*/rules.py): for each build, the cuts whose reason names the
AddressRegionManager constructor come as a pair (the limit, then the area registered as occupied, less than 0x100 further on), each original word a
`mov wN, #0x3fe00000` and its replacement the same register's `mov wN, #0x3e00000`. With the game's files in the work folder (DM_ROOT, default
work/; games/<id>/game.json workspace.exefs), the module's own words at those offsets are the originals, in the constructor's shape (the frame set
up just before the limit, `add x8, x1, x8` within four words after it); without them that part is skipped. T_NODE_AREA_BROKEN=1 leaves the first
cut's limit as it was (a cut that changes nothing): fails 1. Run in the venv (capstone, lz4)."""
import glob, importlib.util, json, os, struct, sys
import lz4.block
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
ROOT = os.environ.get('DM_ROOT', os.path.join(REPO, 'work'))
BROKEN = os.environ.get('T_NODE_AREA_BROKEN') == '1'
MD = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}')
def ins(word):
    x = next(MD.disasm(word, 0), None)
    return (x.mnemonic, x.op_str) if x else (None, None)
def image(path):
    """An NSO's memory image (tools/nso.py) and its build ID"""
    d = open(path, 'rb').read()
    flags = struct.unpack_from('<I', d, 0xc)[0]; segs = [struct.unpack_from('<III', d, 0x10 + i * 0x10) for i in range(3)]; csz = struct.unpack_from('<III', d, 0x60)
    img = bytearray(segs[2][1] + segs[2][2])
    for i, (fo, mo, sz) in enumerate(segs):
        raw = d[fo:fo + csz[i]]
        img[mo:mo + sz] = lz4.block.decompress(raw, uncompressed_size=sz) if flags >> i & 1 else raw
    return bytes(img), d[0x40:0x50].hex()
def mov_imm(word, imm):
    m, ops = ins(word)
    return m == 'mov' and ops.endswith(f', #{imm:#x}'), ops.split(',')[0] if ops else None

for rules_py in sorted(glob.glob(os.path.join(REPO, 'games', '*', 'rules.py'))):
    gid = os.path.basename(os.path.dirname(rules_py))
    spec = importlib.util.spec_from_file_location('rules_' + gid, rules_py); rules = importlib.util.module_from_spec(spec); spec.loader.exec_module(rules)
    gj = json.load(open(os.path.join(os.path.dirname(rules_py), 'game.json')))
    exefs = os.path.join(ROOT, gj.get('workspace', {}).get('exefs', ''))
    mods = {}
    for m in gj.get('modules', []):
        p = os.path.join(exefs, m)
        if os.path.isfile(p): img, bid = image(p); mods[bid] = (m, img)
    for bid, cuts in getattr(rules, 'CUTS', {}).items():
        node = [c for c in cuts if 'AddressRegionManager constructor' in c[3]]
        if not node: continue
        if BROKEN and total == 0: node[0] = (node[0][0], node[0][1], node[0][1], node[0][3])
        check(f'{gid} {bid[:12]}: a pair, the limit then the occupied area (+{node[-1][0] - node[0][0]:#x})',
              len(node) == 2 and 0 < node[1][0] - node[0][0] < 0x100)
        for off, old, new, _ in node:
            (o_ok, o_reg), (n_ok, n_reg) = mov_imm(bytes.fromhex(old), 0x3fe00000), mov_imm(bytes.fromhex(new), 0x3e00000)
            check(f'{gid} {bid[:12]} +{off:#x}: mov {o_reg}, #0x3fe00000 → mov {n_reg}, #0x3e00000', o_ok and n_ok and o_reg == n_reg)
        if bid not in mods: print(f'{gid} {bid[:12]}: no module of this build under {exefs}: its words not read'); continue
        name, img = mods[bid]; lim = node[0][0]
        words_ok = all(img[off:off + 4] == bytes.fromhex(old) for off, old, _, _ in node)
        shape_ok = ins(img[lim - 4:lim]) == ('mov', 'x29, sp') and any(ins(img[lim + 4 * k:lim + 4 * k + 4]) == ('add', 'x8, x1, x8') for k in range(1, 5))
        check(f'{gid} {name} {bid[:12]}: the module\'s words are the originals, in the constructor\'s shape', words_ok and shape_ok)

print(f't_node_area: {total} checks, {fails} failed')
sys.exit(1 if fails or not total else 0)
