#!/usr/bin/env python3
"""The rewriter on a synthetic NSO (plan item 129; the idea and the first function's words are smo's tools/selftest.py, hyc1743/super-mario-odyssey-ios
6cd140f, MIT; the module layout and the rest are ours): every function exercises one thing the rewriter must handle, and the whole run needs no game
files, so CI runs tools/patch_nso.py, verify/structck.py and verify/equiv.py on it.
  0x100  svc #0x40, mrs x0, tpidr_el0, a w18 write, and smo's jump table with offsets relative to the table itself (`ldrsw x18, [x16, x18, lsl #2];
         add x18, x18, x16; br x18`: the loaded entry is the add's first operand)
  0x200  Tears of the Kingdom's jump table shape (adr x18, anchor; ldrsw x1, [x17, x0, lsl #2]; add x18, x18, x1; br x18)
  0x300  an exclusive section on x18 whose store status is tested with cbz (the success path leaves the section)
  0x400  a word Capstone cannot decode (inline data), then an exclusive section that reads x18
  0x500  a compare-and-swap whose store comes before its load (entered at the load, cbz status, x18 the pair load's second register), 0x600 an
         earlier store next to a section that must stay out of it, 0x700 `mov sp, x18` after `mov x9, sp; sub x18, x9, x1` (the words of these
         three are bayonetta3-mobile's tests/test_rewriter.py, enginefuture/bayonetta3-mobile e5ce116, MIT)
  0x800  `mov w0, #7; ret`, which a second run turns into a code cave (CAVES, plan item 139): x16 borrowed through the red zone, w0 loaded from
         the host page; run in Unicorn it must give the page's value with x16 and the flags kept, and structck must pass it. A third run puts
         the cave behind an option (plan item 163): 7 without it, the page's value with the option applied as the loader does
  0x900  plain load-exclusives (plan item 232): a spinlock's ldxr / stxr, then ldxrb, ldxrh, ldxp and a caspa (the same class bits, not a
         load-exclusive); 0xa00 an exclusive section on x18 that takes its value with a plain ldxr
  and a module with `mrs x0, cntfrq_el0`, which the rewriter must refuse.
Checked: the exact sites, both jump tables with their targets, the sections (0x300, 0x408 and 0x604 windows, 0x500 one piece [0x504, 0x520)
entered at 0x50c), the svc gate; STRUCT OK; EQUIV OK (with its own mutation
self-check); structck refuses the product with one site put back to the original word; equiv flags the 0x300 window when its success exit lands one
instruction late, and the w18 block when it also stores into a slot three past x18's. Plan item 232: acq.tsv lists the four plain load-exclusives
of 0x900 and 0xa00's ldxr (not the caspa), the image has their acquire forms, 0xa00's window copy has ldaxr, and structck refuses the product with
0x900 back to its plain ldxr.
Broken variants: DM_TEST_JT_NO_SWAP=1 (only the anchor first) stops the rewriter at 0x128: 1 fail, nothing else runs; DM_TEST_DISASM_STREAM=1
(stream decoding stops at the data word) leaves 0x400's section as plain sites: 2 fail (structck and equiv still pass then: each plain site is
right taken alone, only the exclusive monitor breaks); DM_TEST_EQUIV_CBZ_NOP=1 (cbz status as nop): 2 fail (the 0x300 exit is missed, and the clean run differs: 0x500's forced compare-equal path
loops back to its load until the step limit, which each side reaches at another instruction);
DM_TEST_EQUIV_TSD_WIDE=1 (every write into the slot area dropped): 1 fail; DM_TEST_CAVE_NO_BACK=1 (a routine without its branch back): 4 fail (both caves);
DM_TEST_CAVE_GATE_WRITTEN=1 (a cave behind an option written into the image as well): 3 fail; DM_TEST_ACQ_OFF=1 (the load-exclusives left plain):
7 fail (acq.tsv, the words, the copy, refusing the plain 0x900, and structck on each of the three products).
Plan item 201: a third module pops sp by 0x40 at an x18 site (the save past the red zone after the pop) and by 0x10; equiv must flag the first.
DM_TEST_EQUIV_RZ_START=1 (only the start sp's 128 bytes watched): 1 fail.
Plan item 301b, the android target (DM_TARGET=android): a fourth module, the first with 0x108 / 0x114 as the third's pops and 0xb00 a function of
x18 instructions on sp (ldr / str at unsigned offsets, a stp past the pair's reach, a pre-index push, a post-index pop, add x18, sp, #imm, ldur); the
header names the target, no site keeps the red-zone form, a plain block starts stp xS, xT, [sp, #-0x10]! and takes the slot block from
[tpidr_el0 + 16], the sp offsets moved by 16 (the far stp as two str), STRUCT OK, EQUIV OK with a signal frame below sp before every block
instruction (both pops equal too), and the same module rewritten with the red-zone forms (DM_TEST_NRZ_OFF=1) differs under it.
DM_TEST_NRZ_OFF=1 for the whole run: the fourth module's blocks take the red-zone forms: 6 fail (the four forms, structck's window prologues, equiv).
Needs capstone, lz4, unicorn (the venv: work/build/cache/dm/venv/bin/python) and llvm-objdump (Xcode)."""
import os, re, struct, subprocess, sys, tempfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
PATCH, STRUCTCK, EQUIV = (os.path.join(ROOT, *p) for p in (('tools', 'patch_nso.py'), ('verify', 'structck.py'), ('verify', 'equiv.py')))
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))

# ---- encoders (each word is checked against Capstone's text below) ----
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
def B(pc, to): return 0x14000000 | (((to - pc) >> 2) & 0x3FFFFFF)
def BCOND(pc, to, cond): return 0x54000000 | ((((to - pc) >> 2) & 0x7FFFF) << 5) | cond
def CB(pc, to, t, nz, sf=0): return (sf << 31) | 0x34000000 | (nz << 24) | ((((to - pc) >> 2) & 0x7FFFF) << 5) | t
def ADRP(d, pc, to): imm = (to >> 12) - (pc >> 12); return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | d
def ADR(d, pc, to): imm = to - pc; return 0x10000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | d
def ADDI(d, n, imm): return 0x91000000 | (imm << 10) | (n << 5) | d
def CMPW(n, imm): return 0x7100001F | (imm << 10) | (n << 5)
def MOVX(d, imm): return 0xD2800000 | (imm << 5) | d
def LDRSW_IDX(t, n, m): return 0xB8A07800 | (m << 16) | (n << 5) | t   # ldrsw xt, [xn, xm, lsl #2]
def ADDR(d, n, m): return 0x8B000000 | (m << 16) | (n << 5) | d
def LDAXR(t, n): return 0xC85FFC00 | (n << 5) | t
def STLXR(s, t, n): return 0xC800FC00 | (s << 16) | (n << 5) | t
def LDXR(t, n, sf=1): return 0x885F7C00 | (sf << 30) | (n << 5) | t
def STXR(s, t, n, sf=1): return 0x88007C00 | (sf << 30) | (s << 16) | (n << 5) | t
RET, NOP, HI = 0xD65F03C0, 0xD503201F, 8

TEXT, RO, DATA = 0x0, 0x1000, 0x2000
TAB0, TAB1, EHS = RO + 0x80, RO + 0xA0, RO
def text_words():
    W = {}
    def at(a, *ws):
        for k, w in enumerate(ws): W[a + 4 * k] = w
    # 0x100, smo's words (svc, tpidr_el0, w18 write, table relative to itself), our addresses
    at(0x100, 0xD4000801, 0xD53BD040, 0x528000B2, ADRP(16, 0x10C, TAB0), ADDI(16, 16, TAB0 & 0xFFF), 0x51000432, 0x71000E5F, BCOND(0x11C, 0x150, HI),
       0xB8B27A12, 0x8B100252, 0xD61F0240,
       MOVX(0, 0), RET, MOVX(0, 1), RET, MOVX(0, 2), RET, MOVX(0, 3), RET, NOP, RET)
    # 0x200, Tears of the Kingdom's shape
    at(0x200, ADR(18, 0x200, 0x220), ADRP(17, 0x204, TAB1), ADDI(17, 17, TAB1 & 0xFFF), CMPW(0, 2), BCOND(0x210, 0x23C, HI), LDRSW_IDX(1, 17, 0),
       ADDR(18, 18, 1), 0xD61F0240, MOVX(0, 10), RET, MOVX(0, 11), RET, MOVX(0, 12), RET, NOP, RET)
    # 0x300, an exclusive section on x18 with a cbz status
    at(0x300, LDAXR(18, 0), ADDI(18, 18, 1), STLXR(3, 18, 0), CB(0x30C, 0x314, 3, 0), B(0x310, 0x300), MOVX(0, 1), RET)
    # 0x400, inline data, then an exclusive section that reads x18
    at(0x400, B(0x400, 0x408), 0xFFFFFFFF, LDAXR(1, 0), ADDR(1, 1, 18), STLXR(3, 1, 0), CB(0x414, 0x408, 3, 1), RET)
    # 0x500 / 0x600 / 0x700, bayonetta3-mobile's words (relative branches, so they keep their meaning here), then our exits
    at(0x500, 0x14000003, 0xC831B5D0, 0x34000111, 0xC87FC9D1, 0xCA0F0252, 0xCA100231, 0xAA120231, 0xB4FFFF51, 0xD5033F5F, RET, MOVX(0, 1), RET)
    at(0x600, 0xC831B5D0, 0xC87FC9D1, 0xCA0F0252, 0xAA120231, 0xC831B5D0, 0x35FFFF91, RET)
    at(0x700, 0x910003E9, 0xCB010132, 0x9100025F, 0x9100013F, RET)
    at(0x800, 0x528000E0, RET)   # mov w0, #7: the code cave's site
    # 0x900, plain load-exclusives: a spinlock (ldxr; cbnz; stxr; cbnz; the protected read), ldxrb, ldxrh, ldxp, and caspa (o2 = 0, L = 1, o0 = 0 as well)
    at(0x900, LDXR(8, 0, 0), CB(0x904, 0x900, 8, 1), STXR(9, 1, 0, 0), CB(0x90C, 0x900, 9, 1), 0xB9400400, 0x085F7C02, 0x485F7C03, 0xC87F1404,
       0x48667C02, 0xD503305F, RET)
    # 0xa00, an exclusive section on x18 that loads with a plain ldxr
    at(0xA00, LDXR(18, 0), ADDI(18, 18, 1), STXR(3, 18, 0), CB(0xA0C, 0xA00, 3, 1), MOVX(0, 1), RET)
    return W
WANT = {0x100: 'svc #0x40', 0x104: 'mrs x0, tpidr_el0', 0x108: 'mov w18, #5', 0x10c: 'adrp x16, #0x1000', 0x110: 'add x16, x16, #0x80',
        0x114: 'sub w18, w1, #1', 0x118: 'cmp w18, #3', 0x11c: 'b.hi #0x150', 0x120: 'ldrsw x18, [x16, x18, lsl #2]', 0x124: 'add x18, x18, x16',
        0x128: 'br x18', 0x200: 'adr x18, #0x220', 0x204: 'adrp x17, #0x1000', 0x208: 'add x17, x17, #0xa0', 0x20c: 'cmp w0, #2', 0x210: 'b.hi #0x23c',
        0x214: 'ldrsw x1, [x17, x0, lsl #2]', 0x218: 'add x18, x18, x1', 0x21c: 'br x18', 0x300: 'ldaxr x18, [x0]', 0x304: 'add x18, x18, #1',
        0x308: 'stlxr w3, x18, [x0]', 0x30c: 'cbz w3, #0x314', 0x310: 'b #0x300', 0x400: 'b #0x408', 0x408: 'ldaxr x1, [x0]', 0x40c: 'add x1, x1, x18',
        0x410: 'stlxr w3, x1, [x0]', 0x414: 'cbnz w3, #0x408', 0x500: 'b #0x50c', 0x504: 'stlxp w17, x16, x13, [x14]', 0x508: 'cbz w17, #0x528',
        0x50c: 'ldaxp x17, x18, [x14]', 0x510: 'eor x18, x18, x15', 0x514: 'eor x17, x17, x16', 0x518: 'orr x17, x17, x18', 0x51c: 'cbz x17, #0x504',
        0x520: 'clrex', 0x600: 'stlxp w17, x16, x13, [x14]', 0x604: 'ldaxp x17, x18, [x14]', 0x608: 'eor x18, x18, x15', 0x60c: 'orr x17, x17, x18',
        0x610: 'stlxp w17, x16, x13, [x14]', 0x614: 'cbnz w17, #0x604', 0x700: 'mov x9, sp', 0x704: 'sub x18, x9, x1', 0x708: 'mov sp, x18',
        0x70c: 'mov sp, x9', 0x800: 'mov w0, #7', 0x900: 'ldxr w8, [x0]', 0x904: 'cbnz w8, #0x900', 0x908: 'stxr w9, w1, [x0]',
        0x90c: 'cbnz w9, #0x900', 0x910: 'ldr w0, [x0, #4]', 0x914: 'ldxrb w2, [x0]', 0x918: 'ldxrh w3, [x0]', 0x91c: 'ldxp x4, x5, [x0]',
        0x920: 'caspa x6, x7, x2, x3, [x0]', 0xa00: 'ldxr x18, [x0]', 0xa08: 'stxr w3, x18, [x0]', 0xa0c: 'cbnz w3, #0xa00'}
FUNCS = (0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800, 0x900, 0xA00)
ACQ = {0x900: 'ldaxr w8, [x0]', 0x914: 'ldaxrb w2, [x0]', 0x918: 'ldaxrh w3, [x0]', 0x91c: 'ldaxp x4, x5, [x0]', 0xa00: None}   # (0xa00: in a window)
T0, T1 = [0x12C, 0x134, 0x13C, 0x144], [0x220, 0x228, 0x230]

def build(words, funcs=None):
    funcs = funcs or FUNCS
    text = bytearray(0x1000)
    for a in range(0, 0x1000, 4): struct.pack_into('<I', text, a, NOP)
    for a, w in words.items(): struct.pack_into('<I', text, a, w)
    m0 = 8   # what an NSO's image starts with: a branch, then the MOD0 offset; MOD0 + 16 = eh_frame_hdr (relative to MOD0)
    struct.pack_into('<II', text, 0, NOP, m0); text[m0:m0 + 4] = b'MOD0'
    struct.pack_into('<iiiiii', text, m0 + 4, 0, 0, 0, EHS - m0, EHS + 12 + 8 * len(funcs) - m0, 0)
    ro = bytearray(0x200)
    # eh_frame_hdr: version 1, eh_frame_ptr udata4, fde_count udata4, table datarel sdata4: one (start, fde) row per function
    struct.pack_into('<BBBBII', ro, 0, 1, 0x03, 0x03, 0x3B, 0, len(funcs))
    for k, f in enumerate(funcs): struct.pack_into('<ii', ro, 12 + 8 * k, f - EHS, f - EHS)
    for k, t in enumerate(T0): struct.pack_into('<i', ro, TAB0 - RO + 4 * k, t - TAB0)   # relative to the table itself
    for k, t in enumerate(T1): struct.pack_into('<i', ro, TAB1 - RO + 4 * k, t - 0x220)  # relative to the adr anchor
    data = bytes(0x100)
    hdr = bytearray(0x100); hdr[0:4] = b'NSO0'
    off = 0x100
    for i, (mo, blob) in enumerate(((TEXT, text), (RO, ro), (DATA, data))):
        struct.pack_into('<III', hdr, 0x10 + 0x10 * i, off, mo, len(blob)); struct.pack_into('<I', hdr, 0x60 + 4 * i, len(blob)); off += len(blob)
    hdr[0x40:0x60] = bytes(range(0x20))
    return bytes(hdr) + bytes(text) + bytes(ro) + data

words = text_words()
got = {a: (lambda l: f'{l[0].mnemonic} {l[0].op_str}'.strip() if l else None)(list(md.disasm(struct.pack('<I', w), a))) for a, w in words.items() if a in WANT}
bad = {hex(a): (want, got.get(a)) for a, want in WANT.items() if got.get(a) != want}
check('the synthetic words decode as intended', not bad, str(bad))
check('0x404 is a word Capstone cannot decode', not list(md.disasm(struct.pack('<I', words[0x404]), 0x404)))

def run(*cmd, env=None):
    r = subprocess.run([sys.executable, *cmd], capture_output=True, text=True, env={**os.environ, **(env or {})}); return r.returncode, r.stdout + r.stderr
def rows(out, mod='m'):
    return [l.split() for l in open(os.path.join(out, f'{mod}.sites.tsv'))]
def btarget(b, o):
    w = struct.unpack_from('<I', b, o)[0]; return o + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000) if w & 0xFC000000 == 0x14000000 else None
def diffs(out): return {int(m.group(1), 16) for m in re.finditer(r'^DIFF (0x[0-9a-f]+)', out, re.M)}

with tempfile.TemporaryDirectory() as td:
    nso, out = os.path.join(td, 'm'), os.path.join(td, 'out')
    open(nso, 'wb').write(build(words))
    env = {'DM_VCACHE': os.path.join(td, 'vcache'), 'DM_RULES': ''}
    code, log = run('-I', PATCH, nso, out, 'm', env=env)
    check('the rewriter runs on the synthetic module', code == 0, log.strip().splitlines()[-1] if log.strip() else '')
    if code: print(f'== {total} checks, {fails} failed (the rest needs the rewriter\'s output) =='); sys.exit(1)
    R = rows(out)
    sites = {int(r[0], 16): r[1] for r in R if r[1] not in ('W', 'R', 'J', 'JT', 'T')}
    want_sites = {0x100: 'svc', 0x104: 'tpidr_r', **{a: 'x18' for a in (0x108, 0x114, 0x118, 0x120, 0x124, 0x128, 0x200, 0x218, 0x21c, 0x704, 0x708)}}
    check('sites: svc, tpidr_el0 and the x18 instructions outside the sections, nothing else', sites == want_sites,
          f'extra {sorted(hex(a) for a in set(sites) - set(want_sites))} missing {sorted(hex(a) for a in set(want_sites) - set(sites))}')
    jt = {int(r[0], 16): [int(t, 16) for t in r[2:]] for r in R if r[1] == 'JT'}
    check('0x128: the table relative to itself (operands swapped) has its four targets', jt.get(0x128) == T0, str(jt.get(0x128)))
    check('0x21c: Tears of the Kingdom\'s shape has its three targets', jt.get(0x21c) == T1, str(jt.get(0x21c)))
    wins = {int(r[0], 16): int(r[2], 16) for r in R if r[1] == 'W'}
    check('0x300: the section with a cbz status is a window [0x300, 0x310)', wins.get(0x300) == 0x310, str({hex(k): hex(v) for k, v in wins.items()}))
    check('0x408: the section after the undecodable word is a window [0x408, 0x418)', wins.get(0x408) == 0x418, str({hex(k): hex(v) for k, v in wins.items()}))
    check('0x604: the section is a window [0x604, 0x618), without the earlier store at 0x600', wins.get(0x604) == 0x618,
          str({hex(k): hex(v) for k, v in wins.items()}))
    check('0xa00: the section with a plain ldxr is a window [0xa00, 0xa10)', wins.get(0xa00) == 0xa10, str({hex(k): hex(v) for k, v in wins.items()}))
    secs = {int(r[0], 16): [tuple(int(v, 16) for v in p.replace('@', '-').split('-')[:2]) for p in r[2:]] for r in R if r[1] == 'R'}
    check('0x500: the store-first compare-and-swap is one piece [0x504, 0x520) entered at its load 0x50c', secs == {0x50c: [(0x504, 0x520)]},
          str({hex(k): v for k, v in secs.items()}))
    gates = open(os.path.join(out, 'm.gates.tsv')).read().split()
    check('the svc became a host gate', len(gates) == 4 and gates[1:4] == ['svc', '64', '0x100'], ' '.join(gates))

    code, so = run(STRUCTCK, out, 'm', nso, env=env)
    check('structck: STRUCT OK', code == 0 and re.search(r'^STRUCT m OK', so, re.M) is not None, so.strip().splitlines()[-1])
    code, eo = run(EQUIV, out, 'm', nso, '--jobs', '2', env=env)
    check('equiv: EQUIV OK with its mutation self-check (no DIFF, no NOEFF, no MUTMISS)', code == 0 and re.search(r'^EQUIV m OK', eo, re.M) is not None,
          eo.strip().splitlines()[-1])

    pat = bytearray(open(os.path.join(out, 'm.patched.bin'), 'rb').read())
    # Plan item 232: the plain load-exclusives became acquire loads in place, the caspa did not, and the window's copy took the acquire form
    arows = {int(l.split()[0], 16): int(l.split()[1], 16) for l in open(os.path.join(out, 'm.acq.tsv')) if not l.startswith('#')}
    check('232: acq.tsv lists 0x900, 0x914, 0x918, 0x91c and 0xa00 with their original words (not the caspa at 0x920)',
          arows == {a: words[a] for a in ACQ}, str({hex(a): hex(w) for a, w in arows.items()}))
    dis = lambda b, a: (lambda l: f'{l[0].mnemonic} {l[0].op_str}' if l else None)(list(md.disasm(bytes(b[a:a + 4]), a)))
    gotw = {a: dis(pat, a) for a in ACQ if ACQ[a]}
    check('... the image has ldaxr, ldaxrb, ldaxrh and ldaxp there, and the caspa as it was', gotw == {a: t for a, t in ACQ.items() if t}
          and dis(pat, 0x920) == WANT[0x920], f'{gotw} {dis(pat, 0x920)}')
    cw = [dis(pat, o) or '' for o in range(btarget(pat, 0xa00), btarget(pat, 0xa00) + 4 * 40, 4)]
    check('... 0xa00\'s window copy loads with ldaxr, no plain ldxr', any(t.startswith('ldaxr ') for t in cw) and not any(t.startswith('ldxr ') for t in cw),
          ' / '.join(t for t in cw if 'ld' in t and 'x' in t)[:200])
    def fixture(name, change):
        d = os.path.join(td, name); os.makedirs(d)
        for f in os.listdir(out):
            if f.startswith('m.'): open(os.path.join(d, f), 'wb').write(open(os.path.join(out, f), 'rb').read())
        p = bytearray(pat); change(p); open(os.path.join(d, 'm.patched.bin'), 'wb').write(p); return d
    # structck must refuse a product with one site back to the original word (0x108's B → mov w18, #5)
    d = fixture('orig_word', lambda p: struct.pack_into('<I', p, 0x108, words[0x108]))
    code, so = run(STRUCTCK, d, 'm', nso, env=env)
    check('structck refuses a product with one site left as the original word', code == 1 and re.search(r'^STRUCT m FAIL', so, re.M) is not None,
          so.strip().splitlines()[-1])
    d = fixture('plain_ldxr', lambda p: struct.pack_into('<I', p, 0x900, words[0x900]))
    code, so = run(STRUCTCK, d, 'm', nso, env=env)
    check('232: structck refuses a product with 0x900 back to its plain ldxr', code == 1 and re.search(r'^FAIL text changes do not match', so, re.M) is not None,
          so.strip().splitlines()[-1])
    # the success exit of a cbz status (the stub that goes back to 0x314 / 0x528) lands one instruction late
    def late_exit(p):
        for a, to in ((0x300, 0x314), (0x50c, 0x528)):
            s = btarget(p, a); k = next(o for o in range(s, s + 4 * 40, 4) if btarget(p, o) == to)
            struct.pack_into('<I', p, k, B(k, to + 4))
    code, eo = run(EQUIV, fixture('late_exit', late_exit), 'm', nso, '--jobs', '2', '--mutate', '0', env=env)
    check('equiv takes a cbz status\'s success exit: the 0x300 window whose exit lands one instruction late differs', 0x300 in diffs(eo),
          eo.strip().splitlines()[-1])
    check('... and so does the 0x500 section in one piece', 0x50c in diffs(eo) or 0x504 in diffs(eo), eo.strip().splitlines()[-1])
    # the w18 block (0x108) also stores x18's value into the slot three past x18's: str xS, [xT] → b to a stub that stores both
    def stray_store(p):
        s = btarget(p, 0x108); w = struct.unpack_from('<I', p, s + 28); S, T = w[0] & 31, (w[0] >> 5) & 31
        assert w[0] & 0xFFFFFC00 == 0xF9000000, hex(w[0])   # str xS, [xT]
        stub = (len(p) + 15) & ~15; p.extend(bytes(stub - len(p)))
        for o, x in enumerate((0xF9000000 | (T << 5) | S, 0xF9000C00 | (T << 5) | S, struct.unpack_from('<I', p, s + 32)[0], B(stub + 12, 0x10C))):
            p.extend(struct.pack('<I', x))
        struct.pack_into('<I', p, s + 28, B(s + 28, stub))
    code, eo = run(EQUIV, fixture('stray_store', stray_store), 'm', nso, '--jobs', '2', '--mutate', '0', env=env)
    check('equiv compares writes into the slot area: a block that also stores into a wrong slot differs', 0x108 in diffs(eo),
          eo.strip().splitlines()[-1])

    # A code cave (CAVES): 0x800's `mov w0, #7` becomes stur x16, [sp, #-16]; adrp x16, host page; ldr w0, [x16, #0x40]; ldur x16, [sp, #-16]; b 0x804
    STUR16, LDUR16, LDRW0 = 0xF81F03F0, 0xF85F03F0, 0xB9404200
    bid = bytes(range(16)).hex()
    def cave_run(name, routine, extra_env=None, gate=None):
        rp = os.path.join(td, name + '_rules.py'); g = f', {gate!r}' if gate else ''
        open(rp, 'w').write(f'CAVES = {{{bid!r}: [(0x800, "528000e0", {routine!r}, "test: w0 from the host page"{g})]}}\n')
        o = os.path.join(td, name); e = dict(env, DM_RULES=rp, **(extra_env or {}))
        return o, run('-I', PATCH, nso, o, 'm', env=e)
    co, (code, log) = cave_run('cave', [STUR16, ('adrp_host', 16), LDRW0, LDUR16])
    check('a code cave: the rewriter takes it', code == 0 and "'cave': 1" in log, log.strip().splitlines()[-1] if log.strip() else '')
    def cave_unicorn(outdir, img=None):
        from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UcError
        from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X16, UC_ARM64_REG_SP, UC_ARM64_REG_NZCV, UC_ARM64_REG_PC
        img = bytes(img) if img is not None else open(os.path.join(outdir, 'm.patched.bin'), 'rb').read()
        hp = next(int(l.split()[2], 16) for l in open(os.path.join(outdir, 'm.fixups.tsv')) if l.startswith('# HOST_PAGE_OFF'))
        u = Uc(UC_ARCH_ARM64, UC_MODE_ARM); size = (len(img) + 0xFFFF) & ~0xFFFF
        u.mem_map(0, size); u.mem_write(0, img); u.mem_write(hp + 0x40, struct.pack('<I', 0x1234))
        u.mem_map(0x4000000, 0x10000); u.reg_write(UC_ARM64_REG_SP, 0x4008000); u.reg_write(UC_ARM64_REG_X16, 0xAAAA); u.reg_write(UC_ARM64_REG_NZCV, 0x60000000)
        try: u.emu_start(0x800, 0x804, count=50)
        except UcError: pass
        return u.reg_read(UC_ARM64_REG_PC), u.reg_read(UC_ARM64_REG_X0) & 0xFFFFFFFF, u.reg_read(UC_ARM64_REG_X16), u.reg_read(UC_ARM64_REG_NZCV)
    if code == 0:
        pc, x0, x16, nzcv = cave_unicorn(co)
        check('... run in Unicorn: back at 0x804 with w0 = the host page value, x16 and the flags kept', pc == 0x804 and x0 == 0x1234 and x16 == 0xAAAA and nzcv == 0x60000000,
              f'pc {pc:#x} w0 {x0:#x} x16 {x16:#x} nzcv {nzcv:#x}')
        c2, so = run(STRUCTCK, co, 'm', nso, env=env)
        check('... structck passes it (⑬: words, branch back, exits)', c2 == 0 and 'CAVES m caves=1' in so, so.strip().splitlines()[-1])
    # The same cave behind an option (plan item 163): the word stays, options.tsv has the branch, and the image with the option applied as the
    # loader does (nsoimg.product_image) runs the routine
    go, (code, log) = cave_run('cave_gated', [STUR16, ('adrp_host', 16), LDRW0, LDUR16], gate='tst')
    check('a code cave behind an option: the rewriter takes it', code == 0 and "'cave': 1" in log, log.strip().splitlines()[-1] if log.strip() else '')
    if code == 0:
        sys.path.insert(0, os.path.join(ROOT, 'verify')); import nsoimg
        st = next(int(l.split('\t')[2], 16) for l in open(os.path.join(go, 'm.caves.tsv')) if not l.startswith('#'))
        orows = [l.rstrip('\n').split('\t')[:4] for l in open(os.path.join(go, 'm.options.tsv')) if not l.startswith('#')]
        pimg = open(os.path.join(go, 'm.patched.bin'), 'rb').read()
        check('... the image keeps 0x800 and options.tsv has one row of tst: the original word to b <routine>',
              struct.unpack_from('<I', pimg, 0x800)[0] == 0x528000e0 and orows == [['tst', '0x800', struct.pack('<I', 0x528000e0).hex(), struct.pack('<I', B(0x800, st)).hex()]],
              f'{struct.unpack_from("<I", pimg, 0x800)[0]:#x} {orows}')
        pc, x0, x16, nzcv = cave_unicorn(go)
        pc1, x01, x161, nzcv1 = cave_unicorn(go, nsoimg.product_image(go, 'm', ['tst']))
        check('... run in Unicorn: w0 = 7 without the option, the host page value with it (x16 and the flags kept)',
              pc == pc1 == 0x804 and x0 == 7 and x01 == 0x1234 and x161 == 0xAAAA and nzcv1 == 0x60000000, f'without {pc:#x} w0 {x0:#x}; with {pc1:#x} w0 {x01:#x}')
        c2, so = run(STRUCTCK, go, 'm', nso, env=env)
        check('... structck passes it (⑬: the original word kept, its option row, the routine\'s exits)', c2 == 0 and 'CAVES m caves=1 behind_options=1' in so,
              so.strip().splitlines()[-1])
    _, (code, log) = cave_run('cave_pcrel', [0x10000000])   # adr x0, #0
    check('a code cave with a PC-relative word: refused', code != 0 and 'cannot run in the patch area' in log, log.strip().splitlines()[-1] if log.strip() else '')
    w2 = dict(words); w2[0x104] = 0xD53BE000   # mrs x0, cntfrq_el0
    check('0x104 of the second module decodes as mrs x0, cntfrq_el0', next(md.disasm(struct.pack('<I', w2[0x104]), 0x104)).op_str == 'x0, cntfrq_el0')
    # Plan item 201: a pop by more than 16 (ldp x18, x19, [sp], #0x40) leaves the block's scratch save 176 bytes below the new sp, past Apple's
    # 128-byte red zone, while equiv watched only the start sp's: it must differ there; the pop by 16 (the games' sdk, 128 bytes) must not
    w3 = dict(words); w3[0x108] = 0xA8C44FF2; w3[0x114] = 0xA8C14FF2
    check('the third module: ldp x18, x19, [sp], #0x40 at 0x108 and ldp x18, x19, [sp], #0x10 at 0x114',
          [next(md.disasm(struct.pack('<I', w3[a]), a)).op_str for a in (0x108, 0x114)] == ['x18, x19, [sp], #0x40', 'x18, x19, [sp], #0x10'])
    nso3 = os.path.join(td, 'm3'); open(nso3, 'wb').write(build(w3)); out3 = os.path.join(td, 'out3')
    code, log = run('-I', PATCH, nso3, out3, 'm3', env=env)
    code, eo = run(EQUIV, out3, 'm3', nso3, '--jobs', '2', '--mutate', '0', env=env) if code == 0 else (code, log)
    check('201: equiv: the pop by 0x40 differs (redzone), the pop by 0x10 does not', 0x108 in diffs(eo) and 'redzone' in eo and 0x114 not in diffs(eo),
          eo.strip().splitlines()[-1] if eo.strip() else '')
    nso2 = os.path.join(td, 'm2'); open(nso2, 'wb').write(build(w2))
    code, log = run('-I', PATCH, nso2, os.path.join(td, 'out2'), 'm2', env=env)
    check('the rewriter refuses mrs x0, cntfrq_el0', code != 0 and 'cntfrq_el0' in log and 'a system register the rewriter does not handle' in log,
          log.strip().splitlines()[-1] if log.strip() else '')

    # Plan item 301b: the android target. 0xb00: x18 on sp in every form sp_moved re-addresses
    w4 = dict(w3)
    for k, w in enumerate((0xA9BE7BFD, 0xF94007F2, 0xB9001FF2, 0xA91F4BF1, 0xA9BD4FF2, 0xA8C34FF2, 0x910103F2, 0xF84043F2, 0xA8C27BFD, RET)): w4[0xB00 + 4 * k] = w
    W4 = ['stp x29, x30, [sp, #-0x20]!', 'ldr x18, [sp, #8]', 'str w18, [sp, #0x1c]', 'stp x17, x18, [sp, #0x1f0]', 'stp x18, x19, [sp, #-0x30]!',
          'ldp x18, x19, [sp], #0x30', 'add x18, sp, #0x40', 'ldur x18, [sp, #4]', 'ldp x29, x30, [sp], #0x20', 'ret']
    got4 = [(lambda l: f'{l[0].mnemonic} {l[0].op_str}'.strip())(list(md.disasm(struct.pack('<I', w4[0xB00 + 4 * k]), 0xB00 + 4 * k))) for k in range(len(W4))]
    check('the fourth module: 0xb00 decodes as intended', got4 == W4, str(got4))
    nso4 = os.path.join(td, 'm4'); open(nso4, 'wb').write(build(w4, FUNCS + (0xB00,))); out4 = os.path.join(td, 'out4')
    env4 = {**env, 'DM_TARGET': 'android'}
    code, log = run('-I', PATCH, nso4, out4, 'm4', env=env4)
    check('301b: the rewriter runs for android', code == 0, log.strip().splitlines()[-1] if log.strip() else '')
    if code == 0:
        hdr4 = open(os.path.join(out4, 'm4.fixups.tsv')).read(); bel = [l for l in open(os.path.join(out4, 'm4.belowsp.tsv')) if not l.startswith('#')]
        check('... the header names the target and no site keeps the red-zone form', '# TARGET_ANDROID\t1' in hdr4 and not bel, f'below sp: {bel}')
        p4 = open(os.path.join(out4, 'm4.patched.bin'), 'rb').read()
        def blk(a, n=12):
            s_ = btarget(p4, a); return [f'{x.mnemonic} {x.op_str}'.strip() for x in md.disasm(p4[s_:s_ + 4 * n], s_)] if s_ else []
        b1 = blk(0xB04)
        check('... a plain block: stp xS, xT, [sp, #-0x10]!, the slot block from [tpidr_el0 + 16]', len(b1) > 2 and re.fullmatch(r'stp x\d+, x(\d+), \[sp, #-0x10\]!', b1[0])
              and b1[1] == f'mrs x{re.match(r"stp x\d+, x(\d+)", b1[0]).group(1)}, tpidr_el0' and re.fullmatch(r'ldr x(\d+), \[x\1, #0x10\]', b1[2]) is not None, str(b1[:3]))
        S1 = re.match(r'stp x(\d+)', b1[0]).group(1) if b1 and re.match(r'stp x(\d+)', b1[0]) else '?'
        check('... sp offsets moved by 16: ldr x18, [sp, #8] → ldr xS, [sp, #0x18]; str w18, [sp, #0x1c] → str wS, [sp, #0x2c]',
              f'ldr x{S1}, [sp, #0x18]' in b1 and any(re.fullmatch(r'str w\d+, \[sp, #0x2c\]', t) for t in blk(0xB08)), str(b1))
        b3 = blk(0xB0C); S3 = re.match(r'stp x(\d+)', b3[0]).group(1) if b3 and re.match(r'stp x(\d+)', b3[0]) else '?'
        check('... the stp past the pair\'s reach (0x1f0 + 16) as str x17, [sp, #0x200]; str xS, [sp, #0x208]',
              [t for t in b3 if re.fullmatch(r'str x\d+, \[sp, #0x20[08]\]', t)] == ['str x17, [sp, #0x200]', f'str x{S3}, [sp, #0x208]'], str(b3))
        b5 = blk(0xB10); S5 = re.match(r'stp x(\d+)', b5[0]).group(1) if b5 and re.match(r'stp x(\d+)', b5[0]) else '?'
        check('... the push by 0x30: its pair saved below what it writes (sp down 0x40), the push at [sp, #0x10], sp back by 0x10',
              b5[:1] == [f'stp x{S5}, x{b5[0].split(", ")[1][1:]}, [sp, #-0x40]!'] and f'stp x{S5}, x19, [sp, #0x10]' in b5
              and any(re.fullmatch(rf'ldp x{S5}, x\d+, \[sp\], #0x10', t) for t in b5), str(b5))
        code, so = run(STRUCTCK, out4, 'm4', nso4, env=env)
        check('... structck: STRUCT OK', code == 0 and re.search(r'^STRUCT m4 OK', so, re.M) is not None, so.strip().splitlines()[-1])
        code, eo = run(EQUIV, out4, 'm4', nso4, '--jobs', '2', env=env)
        check('... equiv with a signal frame below sp before every block instruction: EQUIV OK, the pops by 0x40 and 0x10 too',
              code == 0 and re.search(r'^EQUIV m4 OK', eo, re.M) is not None and 'ANDROID m4' in eo, eo.strip().splitlines()[-1])
        out5 = os.path.join(td, 'out5')
        code, log = run('-I', PATCH, nso4, out5, 'm4', env={**env4, 'DM_TEST_NRZ_OFF': '1'})
        code, eo = run(EQUIV, out5, 'm4', nso4, '--jobs', '2', '--mutate', '0', env=env) if code == 0 else (code, log)
        check('... the red-zone forms under android\'s slots (DM_TEST_NRZ_OFF): equiv differs', code != 0 and len(diffs(eo)) > 10,
              eo.strip().splitlines()[-1] if eo.strip() else '')
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
