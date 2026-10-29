# Generic NSO rewriter: Switch aarch64 module → an image that can execute natively on Apple systems.
# Usage: patch_nso.py <NSO file> <output dir> <module name> [--rules games/<id>/rules.py]
# Rewrites (the Apple kernel modifies x18 and tpidr_el0; Horizon's thread area, svc and timers have no counterpart on Apple):
#   x18 read/write      → thread slot K18 (TSD, via tpidrro_el0)
#   mrs tpidr_el0       → read thread slot KTLS; msr tpidr_el0 → write KTLS
#   mrs tpidrro_el0     → read thread slot KTLR (Horizon ThreadLocalRegion pointer)
#   svc #n, mrs cntpct_el0 → host gate (gate address taken from thread slot KGATE; once in C, gates.tsv is looked up by return point)
#   mrs xN, ctr_el0     → movz xN, #4, lsl #16 in place (cache type; Apple does not let user mode read it: SIGILL)
#   ldxr / ldxp / ldxrb / ldxrh → ldaxr / ldaxp / ldaxrb / ldaxrh in place (Apple's cores reorder the loads after a plain one; listed in <name>.acq.tsv)
#   The game's rules (rules.py, by NSO build ID) — cuts (CUTS): before rewriting, the original bytes are asserted one by one and any mismatch aborts; results are listed in <name>.cuts.tsv
#   observation gates (OBS): one instruction at the entry is replaced with "enter the host observation callback → execute the original instruction → jump back", so diagnostics modify zero code at runtime; listed in <name>.obs.tsv
#   parameter slots (PARAMS): a constant built by two instructions is read from a host thread slot instead
# The key page only stores "actual key number × 8" at the end of the data segment, and patch code fetches thread slots indirectly through the key page: the loader may use any key number.
# Output: <name>.patched.bin (key page and patch area appended after the image at PATCH_OFF), <name>.fixups.tsv (layout and key page index header), <name>.gates.tsv, <name>.sites.tsv, <name>.acq.tsv, <name>.cuts.tsv, <name>.options.tsv, <name>.obs.tsv, <name>.params.tsv
import struct, re, sys, os
from itertools import combinations
import lz4.block
from capstone import *

src, outdir, NAME = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(src, 'rb').read()
assert d[:4] == b'NSO0'
flags = struct.unpack_from('<I', d, 0xc)[0]
segs = [struct.unpack_from('<III', d, 0x10 + i * 0x10) for i in range(3)]
bss = struct.unpack_from('<I', d, 0x3c)[0]
csz = struct.unpack_from('<III', d, 0x60)
IMG = bytearray(segs[2][1] + segs[2][2] + bss)
for i, (fo, mo, sz) in enumerate(segs):
    raw = d[fo:fo + csz[i]]
    if flags >> i & 1: raw = lz4.block.decompress(raw, uncompressed_size=sz)
    IMG[mo:mo + sz] = raw
TEXT_END = segs[0][2]
RO_OFF, DATA_OFF = segs[1][1], segs[2][1]
KEY_PAGE_OFF = (len(IMG) + 0x3FFF) & ~0x3FFF
IMG.extend(bytes(0x4000))

# ---- Game rules: games/<id>/rules.py (--rules <file> or DM_RULES), keyed by the NSO build ID of each module ----
#   CUTS     {build ID: [(offset, original bytes hex, new bytes hex, reason)]}: byte replacements; the original bytes are asserted first
#   OBS      {build ID: [(offset, gate number, original instruction word hex, reason)]}: observation gates at function entries (host: gate_obs_set)
#   PARAMS   {build ID: [(offset A, word A hex, offset B, word B hex, register, key, reason)]}: parameter slots (see the PARAMS section below)
#   HOSTREFS {build ID: [(offset, original instruction word hex, (kind, ...), reason)]}: instructions that address the host page, a 16 KiB page of
#            zeros right after the key page that stays writable and that the host fills (dm_host_page): ('adrp', d) = adrp xd, host page;
#            ('ldr_s', t, n, byte offset) = ldr st, [xn, #byte offset]. They become cuts (asserted, listed in cuts.tsv, checked as cuts); a module with
#            HOSTREFS gets the page and HOST_PAGE_OFF in its fixups header, the others keep their layout
#   OPTIONS  {build ID: {option name: [entries as in CUTS, or as in HOSTREFS when the third item is a (kind, ...) form]}}: changes the host applies
#            at load to the options DM_OPTIONS or the product's options.default names (host/runtime/loader.c, plan item 57: one product for 30 and 60 fps).
#            Asserted against the original here but not written into the image; listed in <name>.options.tsv with their final bytes. They must not
#            overlap a cut and must stay outside the rewritten code (asserted after the rewrite: the host writes them where the original still is).
#            A host page form gives the module the page as HOSTREFS does
#   CAVES    {build ID: [(offset, original instruction word hex, routine, reason[, option])]}: code caves (plan item 139; totk-mobile's routines appended to
#            the patch area, written anew): the word at offset (asserted) becomes `b <routine>`, the routine is emitted into the patch area and ends
#            with `b <offset + 4>` unless its last form already leaves. Routine forms: an int (an instruction word, which must not be PC-relative: it
#            no longer sits where it was assembled), ('orig',) (the replaced word, same rule), ('adrp_host', d) (adrp xd, host page: the module gets
#            the page as HOSTREFS gives it), ('b', target) / ('bl', target) (to a text address). A routine may borrow registers through the red zone
#            (stur / ldur below sp) as the rewrite blocks do. Listed in <name>.caves.tsv with the words emitted; structck checks them word by word and
#            their exits, fnfuzz leaves their functions out (an intended change of behaviour, checked by the game's own tests). With an option
#            name (plan item 163) the routine is emitted all the same but the word at offset stays: the branch to the routine is a row of that
#            option in <name>.options.tsv, so the host writes it at load only when the option is on (and checks the original word first)
# A module whose build ID has no entry gets the generic rewrite only.
def rules_path():
    path = os.environ.get('DM_RULES')
    if '--rules' in sys.argv: path = sys.argv[sys.argv.index('--rules') + 1]
    return path
def load_rules():
    path = rules_path()
    if not path: return {}, {}, {}, {}, {}, {}
    import importlib.util
    sys.dont_write_bytecode = True   # no __pycache__ next to the game's rules
    spec = importlib.util.spec_from_file_location('game_rules', path); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return getattr(m, 'CUTS', {}), getattr(m, 'OBS', {}), getattr(m, 'PARAMS', {}), getattr(m, 'HOSTREFS', {}), getattr(m, 'OPTIONS', {}), getattr(m, 'CAVES', {})
CUTS, OBS, PARAMS, HOSTREFS, OPTIONS, CAVES = load_rules()
BUILD_ID = d[0x40:0x50].hex()
cuts = list(CUTS.get(BUILD_ID, []))
hostrefs = HOSTREFS.get(BUILD_ID, [])
options = OPTIONS.get(BUILD_ID, {})
caves = CAVES.get(BUILD_ID, [])
HOST_PAGE_OFF = 0
def hostref_word(off, ow, form):   # the instruction a host page reference becomes (the original word asserted)
    w = struct.unpack_from('<I', IMG, off)[0]
    assert f'{w:08x}' == ow, f'host page reference {off:#x}: original instruction differs ({w:08x})'
    if form[0] == 'adrp':
        imm = (HOST_PAGE_OFF >> 12) - (off >> 12); assert -(1 << 20) <= imm < (1 << 20)
        return w, 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | form[1]
    if form[0] == 'ldr_s':
        t, n, bo = form[1:]; assert bo % 4 == 0 and 0 <= bo < 0x4000
        return w, 0xBD400000 | ((bo // 4) << 10) | (n << 5) | t
    raise AssertionError(f'host page reference {off:#x}: unknown form {form}')
if hostrefs or any(isinstance(e[2], tuple) for rows in options.values() for e in rows) or any(isinstance(f, tuple) and f[0] == 'adrp_host' for c in caves for f in c[2]):
    HOST_PAGE_OFF = KEY_PAGE_OFF + 0x4000
    IMG.extend(bytes(max(0, HOST_PAGE_OFF + 0x4000 - len(IMG))))
    for off, ow, form, why in hostrefs:
        w, nw = hostref_word(off, ow, form)
        cuts.append((off, struct.pack('<I', w).hex(), struct.pack('<I', nw).hex(), f'{why} [host page +{HOST_PAGE_OFF:#x}]'))
opts = []   # (option, offset, original bytes hex, new bytes hex, reason): checked against the original, not written
for name, rows in options.items():
    for off, o, n, why in rows:
        if isinstance(n, tuple): w, nw = hostref_word(off, o, n); ob, nb = struct.pack('<I', w), struct.pack('<I', nw); why = f'{why} [host page +{HOST_PAGE_OFF:#x}]'
        else: ob, nb = bytes.fromhex(o), bytes.fromhex(n); assert len(ob) == len(nb) and IMG[off:off + len(ob)] == ob, f'option {name} {off:#x}: original bytes differ ({bytes(IMG[off:off + len(ob)]).hex()})'
        assert not any(c[0] < off + len(ob) and off < c[0] + len(bytes.fromhex(c[1])) for c in cuts), f'option {name} {off:#x} overlaps a cut'
        opts.append((name, off, ob.hex(), nb.hex(), why))
PATCH_OFF = (len(IMG) + 0xFFFF) & ~0xFFFF
for off, ob, nb, why in cuts:
    ob, nb = bytes.fromhex(ob), bytes.fromhex(nb)
    assert len(ob) == len(nb) and IMG[off:off + len(ob)] == ob, f'cut {off:#x}: original bytes differ ({bytes(IMG[off:off + len(ob)]).hex()})'
    IMG[off:off + len(nb)] = nb
# Key page indices are fixed; the key page content is written by the loader as "actual key number × 8".
# KPARAM0..3: the four parameter slots of PARAMS (host: g_keyparam[0..3])
KEY_INDEX = {k: i for i, k in enumerate(('K18', 'KTLS', 'KTLR', 'KGATE', 'KPARAM0', 'KPARAM1', 'KPARAM2', 'KPARAM3'))}

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
patch = bytearray(); fix = []; gates = []; stats = {}
def stat(k): stats[k] = stats.get(k, 0) + 1
def here(): return PATCH_OFF + len(patch)
def emit(w, kind=None):
    if kind: fix.append((len(patch), kind))
    patch.extend(struct.pack('<I', w & 0xFFFFFFFF))

def emit_key_addr(reg, kind):
    fix.append((len(patch), kind)); emit(ADRP(reg, here(), KEY_PAGE_OFF)); emit(LDRX(reg, reg, KEY_INDEX[kind]))

def emit_slot_addr(base, key_reg, kind):
    emit_key_addr(key_reg, kind); emit(ADDR(base, base, key_reg))

# ---- Encoders ----
def B(frm, to):
    off = to - frm; assert -(1 << 27) <= off < (1 << 27) and off % 4 == 0
    return 0x14000000 | ((off >> 2) & 0x3FFFFFF)
def STP(t, t2, n, off): return 0xA9000000 | (((off // 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t
def LDP(t, t2, n, off): return 0xA9400000 | (((off // 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t
def STP_PRE(t, t2, n, off): return 0xA9800000 | (((off // 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t
def LDP_POST(t, t2, n, off): return 0xA8C00000 | (((off // 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t
def STUR(t, n, off): return 0xF8000000 | ((off & 0x1FF) << 12) | (n << 5) | t   # stur xt,[xn,#off] (off may be negative)
def LDUR(t, n, off): return 0xF8400000 | ((off & 0x1FF) << 12) | (n << 5) | t   # ldur xt,[xn,#off]
def LDRX(t, n, imm12=0): return 0xF9400000 | (imm12 << 10) | (n << 5) | t
def STRX(t, n, imm12=0): return 0xF9000000 | (imm12 << 10) | (n << 5) | t
def MRS_TPIDRRO(t): return 0xD53BD060 | t
def MRS_TPIDR(t): return 0xD53BD040 | t
def STR_PRE(t, n, off): return 0xF8000C00 | ((off & 0x1FF) << 12) | (n << 5) | t    # str xt, [xn, #off]!
def LDR_POST(t, n, off): return 0xF8400400 | ((off & 0x1FF) << 12) | (n << 5) | t   # ldr xt, [xn], #off
def ADRP(dd, pc, target):
    imm = ((target & ~0xFFF) - (pc & ~0xFFF)) >> 12; assert -(1 << 20) <= imm < (1 << 20)
    return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | dd
def ADDI(dd, n, imm12, sh=0): return 0x91000000 | (sh << 22) | (imm12 << 10) | (n << 5) | dd
def ADDR(dd, n, m): return 0x8B000000 | (m << 16) | (n << 5) | dd
def SUBI(dd, n, imm12, sh=0): return 0xD1000000 | (sh << 22) | (imm12 << 10) | (n << 5) | dd
def BR(n): return 0xD61F0000 | (n << 5)
def BLR(n): return 0xD63F0000 | (n << 5)
def BL(frm, to):
    off = to - frm; assert -(1 << 27) <= off < (1 << 27) and off % 4 == 0
    return 0x94000000 | ((off >> 2) & 0x3FFFFFF)
def SUBR(dd, n, m): return 0xCB000000 | (m << 16) | (n << 5) | dd   # sub xd, xn, xm (flags untouched)
def CBZ(t, frm, to):
    off = to - frm; assert -(1 << 20) <= off < (1 << 20) and off % 4 == 0
    return 0xB4000000 | (((off >> 2) & 0x7FFFF) << 5) | t
def BRK(imm): return 0xD4200000 | ((imm & 0xFFFF) << 5)
NOP = 0xD503201F

def dis(w, addr):
    l = list(md.disasm(struct.pack('<I', w), addr)); assert len(l) == 1, hex(w)
    return l[0]

# ---- Where a thread's slots are and where a block keeps the registers it borrows (DM_TARGET, plan item 301b) ----
# apple (the default): Darwin's TSD, a slot at tpidrro_el0 + its key page entry; a block borrows registers through Apple's 128-byte red zone below sp.
# android: tpidrro_el0 reads 0 in a 64-bit Linux task and bionic owns tpidr_el0, so the host keeps each guest thread's slot block in bionic's
# TLS_SLOT_APP (tpidr_el0 + 16; bionic's tls_defines.h: "available for use by apps in API level 29 and later") and the key page holds offsets into
# that block: one load more per slot access (after shirai91/nx2android's DM_TARGET, MIT). Linux has no red zone: a signal whose handler lacks
# SA_ONSTACK (libcore's, heapprofd's, ART's) builds its frame right below sp, over whatever a block saved there. So on android a block moves sp down
# before it saves a register and back as it restores it, an instruction that addresses sp gets its offset moved by as much (sp_moved), and a site
# that cannot be encoded so keeps the red-zone form, counted as below_sp and listed in <name>.belowsp.tsv (after jhaizhou-ops/d2r-mobile's NOREDZONE
# / sp_frame, MIT, written anew). DM_TEST_NRZ_OFF=1 (negative control of t_rewriter_synth only): android's slots with the red-zone forms
TARGET = os.environ.get('DM_TARGET', 'apple'); assert TARGET in ('apple', 'android'), f'DM_TARGET {TARGET}: apple or android'
NRZ = TARGET == 'android' and not os.environ.get('DM_TEST_NRZ_OFF')
TLS_SLOT_APP = 2
below_sp = []
def emit_tp(t):
    """xt = the thread's slot block (the base the key page entries are offsets from)"""
    if TARGET == 'android': emit(MRS_TPIDR(t)); emit(LDRX(t, t, TLS_SLOT_APP))
    else: emit(MRS_TPIDRRO(t))
def uses_sp(ins): return re.search(r'\b[w]?sp\b', ins.op_str) is not None
def sp_moved(ins, w, dlt):
    """An instruction w (ins after the x18 substitution) for a block that has moved sp down by F before it: (F, words) with the same effect on memory and
    registers, sp's write-back (dlt) left to the block's restore, or None when it cannot be encoded. F is 16 unless the instruction writes below the
    incoming sp (a pre-index push), when the saved pair goes below what it writes. Each new word is checked by disassembly: the same mnemonic and
    operands but for the sp offset"""
    if not uses_sp(ins): return (16, [w]) if dlt == 0 else None
    rn, rd = (w >> 5) & 31, w & 31
    o = dis(w, ins.address)   # the instruction as the block runs it (x18 already xS)
    def check(nw, off):
        d = dis(nw, ins.address)
        strip = lambda t: re.sub(r'\[sp(, #-?(0x)?[0-9a-f]+)?\](!|, #-?(0x)?[0-9a-f]+)?', '[sp]', t)
        return d.mnemonic == o.mnemonic and strip(d.op_str) == strip(o.op_str) and f'[sp, #{off:#x}]' in d.op_str
    if (w >> 27) & 7 == 0b101 and not (w >> 26) & 1 and rn == 31:          # ldp / stp / ldpsw on sp
        opc, mode = w >> 30, (w >> 23) & 3
        if opc == 3 or opc == 1 and not (w >> 22) & 1 or mode == 0: return None   # (stgp, the no-allocate pairs)
        sc = 8 if opc == 2 else 4
        imm = ((((w >> 15) & 0x7F) ^ 0x40) - 0x40) * sc
        if mode == 2:                                                       # signed offset: [sp, #imm]
            if imm < 0 or dlt: return None
            F, at = 16, imm
        elif mode == 1: F, at = 16, 0                                       # post-index: at sp, then sp += dlt
        else:                                                               # pre-index: at sp + dlt, then sp = sp + dlt
            F, at = 16, dlt
            while 16 - F > at: F += 16
        off = at + F
        if off % sc or off < 0 or (F + dlt) % 8 or not -512 <= F + dlt <= 504: return None
        if off // sc > 63:   # past the pair's reach (252 / 504): two single-register ldr / str at unsigned offsets (ldpsw → ldrsw), each checked
            L = (w >> 22) & 1; size, sopc = (3 if opc == 2 else 2), ((2 if opc == 1 else 1) if L else 0)
            regs = o.op_str.split(', [')[0].split(', '); out = []
            for k, rt in enumerate((w & 31, (w >> 10) & 31)):
                nw = 0x39000000 | (size << 30) | (sopc << 22) | (((off + k * sc) // sc) << 10) | (31 << 5) | rt
                d = dis(nw, ins.address)
                if not d.mnemonic.startswith(('ldr', 'str')) or d.op_str != f'{regs[k]}, [sp, #{off + k * sc:#x}]': return None
                out.append(nw)
            return F, out
        nw = (w & ~(0x7F << 15) & ~(3 << 23)) | ((off // sc) << 15) | (2 << 23)
        return (F, [nw]) if check(nw, off) else None
    if dlt: return None
    if (w >> 27) & 7 == 0b111 and not (w >> 26) & 1 and rn == 31:          # single-register ldr / str on sp, no write-back
        size = w >> 30; sc = 1 << size
        if (w >> 24) & 3 == 1:                                              # unsigned offset
            if (w >> 22) & 3 == 2 and size == 3: return None                # prfm
            off = ((w >> 10) & 0xFFF) * sc + 16
            if off // sc > 0xFFF: return None
            nw = (w & ~(0xFFF << 10)) | ((off // sc) << 10)
            return (16, [nw]) if check(nw, off) else None
        if (w >> 24) & 3 == 0 and not (w >> 21) & 1 and (w >> 10) & 3 == 0:   # unscaled (ldur / stur)
            off = ((((w >> 12) & 0x1FF) ^ 0x100) - 0x100) + 16
            if not 16 <= off <= 255: return None                            # an original offset below sp is refused
            nw = (w & ~(0x1FF << 12)) | ((off & 0x1FF) << 12)
            return (16, [nw]) if check(nw, off) else None
        return None
    if w & 0xFF000000 in (0x91000000, 0xD1000000) and rn == 31 and rd != 31:   # add / sub xd, sp, #imm (no flags): then xd += 16
        return 16, [w, ADDI(rd, rd, 16)]
    if w & 0xFFE00000 in (0x8B200000, 0xCB200000) and rn == 31 and rd != 31:   # add / sub xd, sp, xm (extended register): then xd += 16
        return 16, [w, ADDI(rd, rd, 16)]
    return None
class Data:
    """A word Capstone cannot decode (inline data in the text): no registers, no branch, and control never falls through it (decode_words)"""
    mnemonic, operands = '.word', ()
    def __init__(self, w, a): self.address, self.bytes, self.op_str = a, struct.pack('<I', w), f'{w:#010x}'
    def regs_access(self): return [], []
def decode_words(buf, fa, fb):
    """The instructions of buf[fa:fb], one entry per word. Capstone's stream decoding stops at the first word it cannot decode, so everything after
    inline data was hidden from the jump-table, exclusive-section and function-pointer analysis (an exclusive section after it got plain sites; a
    census of a 4 MB code dump in va11mobile's verify/INPUT-DIAG.zh-CN.md read 0 tpidrro reads that way and 3,116 with skipdata); such a word becomes
    a Data entry and decoding goes on after it (plan item 129)"""
    if os.environ.get('DM_TEST_DISASM_STREAM'): return list(md.disasm(bytes(buf[fa:fb]), fa))   # negative control of t_rewriter_synth only: the old stream decoding
    out, a = [], fa
    while a < fb:
        n0 = len(out); out.extend(md.disasm(bytes(buf[a:fb]), a)); a += 4 * (len(out) - n0)
        if a < fb: out.append(Data(struct.unpack_from('<I', buf, a)[0], a)); a += 4
    return out
def regset(ins):
    r, w = ins.regs_access()
    return {ins.reg_name(x) for x in r}, {ins.reg_name(x) for x in w}
def num(name):
    m = re.fullmatch(r'[xw](\d+)', name or ''); return int(m.group(1)) if m else None
def has18(ins): return re.search(r'\b[xw]18\b', ins.op_str) is not None
def regs_in(ins):
    s = {int(n) for n in re.findall(r'\b[xw](\d+)\b', ins.op_str)}
    try:
        rd, wr = regset(ins); s |= {num(n) for n in rd | wr} - {None}
    except Exception: pass
    return s
def pick(avoid, n=2):
    out = [r for r in range(0, 16) if r not in avoid and r != 18][:n]; assert len(out) == n; return out

def subst(ins, S):
    """x18/w18 → xS/wS: only the 4 register field positions are changed, verified against the disassembly text."""
    w = struct.unpack('<I', ins.bytes)[0]
    want = re.sub(r'\b([xw])18\b', lambda m: f'{m.group(1)}{S}', ins.op_str)
    pos = [p for p in (0, 5, 10, 16) if (w >> p) & 31 == 18]
    for k in range(len(pos), 0, -1):
        for c in combinations(pos, k):
            nw = w
            for p in c: nw = (nw & ~(31 << p)) | (S << p)
            try: dd = dis(nw, ins.address)
            except AssertionError: continue
            if dd.mnemonic == ins.mnemonic and dd.op_str == want: return nw
    raise RuntimeError(f'subst fail {ins.address:x} {ins.mnemonic} {ins.op_str}')

# ---- Rewrite blocks of each kind ----
def sp_delta(ins):
    """The amount an instruction writes back to sp (pre-index [sp, #i]! / post-index [sp], #i); returns 0 if sp is not written back."""
    m = re.search(r'\[sp, #(-?(?:0x)?[0-9a-f]+)\]!', ins.op_str)
    if m: return int(m.group(1), 0)
    m = re.search(r'\[sp\], #(-?(?:0x)?[0-9a-f]+)$', ins.op_str)
    if m: return int(m.group(1), 0)
    assert not re.match(r'(sp|wsp),', ins.op_str), f'writes sp: {ins.address:x} {ins.op_str}'
    return 0

def site_plain(ins):
    S, T = pick(regs_in(ins))
    dlt = sp_delta(ins)
    mv = sp_moved(ins, subst(ins, S), dlt) if NRZ else None
    if mv:                                    # android: sp down F, the pair at the new sp, the instruction re-addressed, sp back with its write-back
        F, words = mv; start = here()
        emit(STP_PRE(S, T, 31, -F)); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
        for x in words: emit(x)
        emit(STRX(S, T))
        emit(LDP_POST(S, T, 31, F + dlt)); emit(B(here(), ins.address + 4))
        return start
    if NRZ: below_sp.append(ins.address); stat('below_sp')
    save = -16 if dlt == 0 else -112          # Instructions that modify sp write below sp, so the scratch save is placed deep in the reserved area
    assert dlt == 0 or dlt >= -96, f'sp delta {dlt} at {ins.address:x}'
    start = here()
    emit(STP(S, T, 31, save)); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
    emit(subst(ins, S))
    emit(STRX(S, T))                 # Always write back: capstone can miss reporting a write (the adds w18 instance)
    emit(LDP(S, T, 31, save - dlt)); emit(B(here(), ins.address + 4))
    return start

def site_cond(ins):
    w = struct.unpack('<I', ins.bytes)[0]
    S, T = pick(regs_in(ins))
    tgt = ins.operands[-1].imm
    start = here()
    emit(STP_PRE(S, T, 31, -16) if NRZ else STP(S, T, 31, -16)); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
    m = ins.mnemonic
    taken = here() + 4 * 3
    off = (taken - here()) >> 2
    nw = ((w & 0xFF000000) | ((off & 0x7FFFF) << 5) | S) if m in ('cbz', 'cbnz') else ((w & 0xFFF80000) | ((off & 0x3FFF) << 5) | S)
    pc = here(); emit(nw)
    dd = dis(nw, pc); assert dd.mnemonic == m and dd.operands[-1].imm == taken
    emit(LDP_POST(S, T, 31, 16) if NRZ else LDP(S, T, 31, -16)); emit(B(here(), ins.address + 4))
    assert here() == taken
    emit(LDP_POST(S, T, 31, 16) if NRZ else LDP(S, T, 31, -16)); emit(B(here(), tgt))
    return start

def site_pcrel(ins):
    tgt = ins.operands[1].imm
    S, T = pick(set())
    start = here()
    emit(STP_PRE(S, T, 31, -16) if NRZ else STP(S, T, 31, -16)); emit_tp(T); emit_slot_addr(T, S, 'K18')   # S first serves as a temporary for the key page offset; the next instruction then loads the target value
    pc = here(); emit(ADRP(S, pc, tgt))
    if ins.mnemonic == 'adr': emit(ADDI(S, S, tgt & 0xFFF))
    emit(STRX(S, T))
    emit(LDP_POST(S, T, 31, 16) if NRZ else LDP(S, T, 31, -16)); emit(B(here(), ins.address + 4))
    return start

def site_br(ins, base, count):
    """Jump table br x18: target = base + 4i. Stash x16 in the red zone → equally spaced landing pads → restore x16 → original target."""
    start = here()
    if NRZ: emit(STP_PRE(16, 17, 31, -16))   # android: x16 at [sp], x17 at [sp, #8] after sp moves down 16
    else: emit(STUR(16, 31, -16)); emit(STUR(17, 31, -24))   # x16 / x17 are temporary registers that can stay live within a function; save them to the red zone before borrowing
    emit_tp(16); emit_slot_addr(16, 17, 'K18'); emit(LDRX(16, 16)); emit(LDRX(17, 31, 1) if NRZ else LDUR(17, 31, -24))
    padbase = start + 4 * 24
    delta = padbase - base
    op = ADDI if delta >= 0 else SUBI; dl = abs(delta)
    ops = []; hi, lo = dl >> 12, dl & 0xFFF
    while hi: h = min(hi, 0xFFF); ops.append(op(16, 16, h, 1)); hi -= h
    if lo: ops.append(op(16, 16, lo, 0))
    assert len(ops) <= 12
    for o in ops: emit(o)
    emit(BR(16))
    while here() < padbase: emit(NOP)
    tails = padbase + 4 * count
    for i in range(count): emit(B(here(), tails + 8 * i))
    for i in range(count):
        emit(LDR_POST(16, 31, 16) if NRZ else 0xF85F0000 | (31 << 5) | 16)      # ldr x16, [sp], #16 / ldur x16,[sp,#-16]
        emit(B(here(), base + 4 * i))
    return start

def site_br_tab(ins, targets):
    """Jump table br x18 whose targets are not equally spaced (anchor + signed word, see jump_tab): the table is constant, so all targets are read at
    rewrite time. x16 = guest x18, compared with each target in turn (adrp + add put the target in x17, sub + cbz compare without touching NZCV,
    which br x18 leaves alone); on a match x16 / x17 are restored and the block jumps to that target, which is exactly where br x18 would go;
    no match → brk #0x18 (an index the analysis did not foresee stops loudly). After nxnative's site_br_tab (seedds, MIT), which compares with cmp."""
    start = here()
    if NRZ: emit(STP_PRE(16, 17, 31, -16))
    else: emit(STUR(16, 31, -16)); emit(STUR(17, 31, -24))   # as in site_br: save x16 / x17 to the red zone before borrowing
    emit_tp(16); emit_slot_addr(16, 17, 'K18'); emit(LDRX(16, 16))
    uniq = sorted(set(targets))
    pads = here() + 4 * (4 * len(uniq) + 1)
    for k, t in enumerate(uniq):
        emit(ADRP(17, here(), t)); emit(ADDI(17, 17, t & 0xFFF)); emit(SUBR(17, 17, 16)); emit(CBZ(17, here(), pads + 12 * k))
    emit(BRK(0x18))
    for t in uniq:
        if NRZ: emit(LDP_POST(16, 17, 31, 16)); emit(NOP); emit(B(here(), t))   # (three words, as the pads are spaced)
        else: emit(LDUR(16, 31, -16)); emit(LDUR(17, 31, -24)); emit(B(here(), t))
    return start

def site_xbranch(ins, link):
    """br / blr x18 through a function pointer (not a jump table): IL2CPP invokers use x18 as the only free scratch register.
    The target goes to x17 (IP1: a linker veneer may clobber it on any call, so the callee cannot rely on it at entry); x16 is borrowed through the red zone.
    blr: x30 = original site + 4, so the callee returns straight into the original code and unwinding sees the original FDE, not the patch area."""
    start = here()
    emit(STR_PRE(16, 31, -16) if NRZ else STUR(16, 31, -16))
    emit_tp(17); emit_slot_addr(17, 16, 'K18'); emit(LDRX(17, 17)); emit(LDR_POST(16, 31, 16) if NRZ else LDUR(16, 31, -16))
    if link:
        ret = ins.address + 4
        emit(ADRP(30, here(), ret)); emit(ADDI(30, 30, ret & 0xFFF))
    emit(BR(17))
    return start

MOV_X17_SP, MOV_SP_X16 = 0x910003F1, 0x9100021F    # add x17, sp, #0 / add sp, x16, #0
def site_mov_sp(ins):
    """mov sp, x18 after `mov xA, sp; sub x18, xA, xB` (IL2CPP stackalloc: x18 holds the new, lower sp). x16 / x17 are borrowed through the
    red zone of the old sp and reloaded through x17 = old sp after sp moves: the old red zone now lies inside the allocation, above the new sp."""
    start = here()
    if NRZ:   # android: the pair at the old sp - 16 (sp moved down onto it), reloaded through x17 once sp is the allocation's: inside the allocation from 16 bytes up
        emit(STP_PRE(16, 17, 31, -16)); emit_tp(16); emit_slot_addr(16, 17, 'K18'); emit(LDRX(16, 16))
        emit(MOV_X17_SP); emit(MOV_SP_X16)
        emit(LDRX(16, 17)); emit(LDRX(17, 17, 1))
        emit(B(here(), ins.address + 4))
        return start
    emit(STUR(16, 31, -16)); emit(STUR(17, 31, -24))
    emit_tp(16); emit_slot_addr(16, 17, 'K18'); emit(LDRX(16, 16))
    emit(MOV_X17_SP); emit(MOV_SP_X16)
    emit(LDUR(16, 17, -16)); emit(LDUR(17, 17, -24))
    emit(B(here(), ins.address + 4))
    return start

def alloca_shape(a):
    """True when the x18 that `mov sp, x18` at a installs was computed as (a register copied from sp) minus something, in the same straight-line block."""
    insl = decode_words(IMG0, max(0, a - 0x40), a)
    sub_src = None
    for p in reversed(insl):
        if p.mnemonic in ('b', 'bl', 'br', 'blr', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz', '.word') or p.mnemonic.startswith('b.'): return False
        _, wr = regset(p)
        if sub_src is None:
            if wr & {'x18', 'w18'}:
                m = re.fullmatch(r'x18, (x\d+), x\d+', p.op_str)
                if p.mnemonic != 'sub' or not m: return False
                sub_src = m.group(1)
        elif sub_src in wr:
            return p.mnemonic == 'mov' and p.op_str == f'{sub_src}, sp'
    return False

def site_sysreg_read(ins, kind):
    """mrs xN, tpidr_el0 / tpidrro_el0 → xN = thread slot value."""
    N = ins.bytes[0] & 31
    tmp = 16 if N != 16 else 17
    start = here()
    emit(STR_PRE(tmp, 31, -16) if NRZ else STUR(tmp, 31, -16)); emit_tp(N); emit_slot_addr(N, tmp, kind); emit(LDRX(N, N))
    emit(LDR_POST(tmp, 31, 16) if NRZ else LDUR(tmp, 31, -16))   # tmp is saved by borrowing the red zone (android: below a moved sp)
    emit(B(here(), ins.address + 4))
    return start

def site_tpidr_write(ins):
    N = struct.unpack('<I', ins.bytes)[0] & 31
    (T,) = pick({N}, 1)
    start = here()
    tmp = next(x for x in (16, 17) if x not in (N, T))
    if NRZ:
        emit(STP_PRE(T, tmp, 31, -16)); emit_tp(T); emit_slot_addr(T, tmp, 'KTLS'); emit(STRX(N, T)); emit(LDP_POST(T, tmp, 31, 16))
        emit(B(here(), ins.address + 4))
        return start
    emit(0xF81F0000 | (31 << 5) | T)           # stur xT,[sp,#-16]
    emit(STUR(tmp, 31, -24)); emit_tp(T); emit_slot_addr(T, tmp, 'KTLS'); emit(STRX(N, T)); emit(LDUR(tmp, 31, -24))
    emit(0xF85F0000 | (31 << 5) | T)           # ldur xT,[sp,#-16]
    emit(B(here(), ins.address + 4))
    return start

def emit_gate_call():
    """Common section of a gate block (right after stp x29,x30,[sp,#-16]! and before blr x30): x30 = the gate address in thread slot KGATE.
    Fetching the slot through the key page borrows a second register; x29 is borrowed and restored from the stack right away: the x29 that _dm_gate
    saves must be the guest's own value (host code may identify a caller by [x29+8], and crash backtraces walk the x29 chain)."""
    emit_tp(30); emit_slot_addr(30, 29, 'KGATE'); emit(LDRX(30, 30)); emit(LDRX(29, 31))

def site_gate(ins, kind, arg):
    """svc / mrs cntpct_el0 → host gate. The gate address is taken from thread slot KGATE; the gate looks up gates.tsv by return point (the address of this block's ldp)."""
    start = here()
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call()
    emit(BLR(30))
    ret = here()
    emit(LDP_POST(29, 30, 31, 16))
    emit(B(here(), ins.address + 4))
    gates.append((ret, kind, arg, ins.address))
    return start

def site_obs(off, oid, word):
    """Observation gate: original entry instruction → enter the host gate (arg = observation number) → restore x29/x30 → execute the original instruction → jump back to off+4.
        The first half of the gate block is word-for-word isomorphic with site_gate; the host only reads registers and does not modify them (the x0–x8 written back by dm_gate are the original values)."""
    ins = dis(word, off)
    assert ins.mnemonic not in ('adr', 'adrp', 'b', 'bl', 'br', 'blr', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz') and not ins.mnemonic.startswith('b.') \
        and not (ins.mnemonic.startswith('ldr') and '[' not in ins.op_str) and not has18(ins), f'observation gate {off:#x}: the original instruction cannot be moved ({ins.mnemonic} {ins.op_str})'
    start = here()
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call()
    emit(BLR(30))
    ret = here()
    emit(LDP_POST(29, 30, 31, 16))
    emit(word)
    emit(B(here(), off + 4))
    gates.append((ret, 'obs', oid, off))
    return start

# ---- Exclusive windows ----
# Between an exclusive load and its store, x18 cannot go through the slot (the slot access is a memory access inside the exclusive section, and the
# section may then never succeed), so the whole section is copied with x18 replaced by a scratch register S loaded from the slot before it.
# A section is found by control flow (exclusive_regions): every instruction on a path from an exclusive load to an exclusive store, plus the cbz /
# cbnz on the store's status. A contiguous section entered only at its load is a window (site_window); TotK's compiler also moves the retry of a
# compare-and-swap into a cold block (main 0x18598: ldxr; cmp; b.eq → the stxr at 0x181fc of the main path), giving a section in several pieces
# (site_region).
EXCL_LD = ('ldxr', 'ldaxr', 'ldxp', 'ldaxp', 'ldxrb', 'ldaxrb', 'ldxrh', 'ldaxrh')
EXCL_ST = ('stxr', 'stlxr', 'stxp', 'stlxp', 'stxrb', 'stlxrb', 'stxrh', 'stlxrh')
EXCL_STOP = ('bl', 'blr', 'br', 'ret', 'svc', 'brk', 'hlt', 'udf', 'clrex', '.word')   # a path through these is not one exclusive section
def exclusive_regions(insl):
    """Exclusive sections of one function → sorted lists of instruction indices. From each exclusive load L, walk forward without passing another
    exclusive load or EXCL_STOP and without going past an exclusive store (its status cbz / cbnz is kept); the section of L is what can reach one of
    those stores walking back inside that set, again without passing an exclusive load (a failed compare that loops back to L is not part of it).
    Sections that share an instruction are one section."""
    idx = {x.address: i for i, x in enumerate(insl)}
    def succ(i):
        x = insl[i]
        if x.mnemonic in EXCL_STOP: return []
        out = [i + 1] if x.mnemonic not in ('b', '.word') and i + 1 < len(insl) else []
        t = dtarget(x)
        if t is not None and t in idx: out.append(idx[t])
        return out
    def status(k):
        s = insl[k].op_str.split(',')[0]
        return k + 1 if k + 1 < len(insl) and insl[k + 1].mnemonic in ('cbz', 'cbnz') and insl[k + 1].op_str.split(',')[0] == s else None
    out = []
    for L, x in enumerate(insl):
        if x.mnemonic not in EXCL_LD: continue
        F, st, stores = {L}, [L], set()
        while st:
            i = st.pop()
            if insl[i].mnemonic in EXCL_ST: stores.add(i); continue
            for j in succ(i):
                if j not in F and insl[j].mnemonic not in EXCL_LD: F.add(j); st.append(j)
        if not stores: continue
        back = {}
        for i in F:
            if insl[i].mnemonic not in EXCL_ST:
                for j in succ(i):
                    if j in F: back.setdefault(j, []).append(i)
        R, st = set(stores), list(stores)
        while st:
            i = st.pop()
            if insl[i].mnemonic in EXCL_LD: continue
            for p in back.get(i, []):
                if p not in R: R.add(p); st.append(p)
        R |= {status(k) for k in stores} - {None}
        for M in [M for M in out if M & R]: out.remove(M); R |= M
        out.append(R)
    return sorted((sorted(R) for R in out), key=lambda r: r[0])

def region_entries(insl, R, fa, extra=()):
    """Instructions of section R that can be entered from outside it: a branch (b, bl, conditional) or fall-through from outside, a jump table
    target (extra: (from, to) edges), the function entry, or no visible predecessor at all."""
    inR = {insl[i].address for i in R}
    preds = {a: [] for a in inR}
    for i, x in enumerate(insl):
        if x.mnemonic not in ('b', 'br', 'ret', '.word') and i + 1 < len(insl) and insl[i + 1].address in preds: preds[insl[i + 1].address].append(x.address)
        t = dtarget(x) if x.mnemonic != 'bl' else x.operands[0].imm
        if t in preds: preds[t].append(x.address)
    for frm, t in extra:
        if t in preds: preds[t].append(frm)
    return sorted(a for a in inR if a == fa or not preds[a] or any(p not in inR for p in preds[a]))

def site_window(seg):
    used = set()
    for x in seg: used |= regs_in(x)
    assert all(sp_delta(x) == 0 for x in seg), f'window {seg[0].address:#x} moves sp'   # the exits reload S / T from [sp, #-16]
    S, T = pick(used)
    lo, hi = seg[0].address, seg[-1].address + 4
    nrz = NRZ and not any(uses_sp(x) for x in seg)   # (an instruction of the window addressing sp keeps the red-zone form)
    if NRZ and not nrz: below_sp.append(lo); stat('below_sp')
    start = here()
    emit(STP_PRE(S, T, 31, -16) if nrz else STP(S, T, 31, -16)); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
    copy0 = here(); n = len(seg)
    exits = copy0 + 4 * n
    ex_targets = []; words = []
    for k, x in enumerate(seg):
        w = struct.unpack('<I', x.bytes)[0]
        if has18(x): w = subst(x, S)
        m = x.mnemonic
        if m in ('cbz', 'cbnz', 'tbz', 'tbnz') or m.startswith('b.'):
            tgt = x.operands[-1].imm
            if lo <= tgt < hi: newt = copy0 + (tgt - lo)
            else: ex_targets.append(tgt); newt = exits + 16 * len(ex_targets)   # Exit stub 0 is reserved for the sequential fall-out (see below); targets outside the window start from stub 1
            pc = copy0 + 4 * k; off = (newt - pc) >> 2
            if m in ('tbz', 'tbnz'): w = (w & 0xFFF8001F) | ((off & 0x3FFF) << 5)
            else: w = (w & 0xFF00001F) | ((off & 0x7FFFF) << 5)
            dd = dis(w, pc); assert dd.operands[-1].imm == newt and dd.mnemonic == m
        else:
            assert m not in ('adr', 'adrp', 'b', 'bl', 'br', 'blr', 'ret') and not (m == 'ldr' and '[' not in x.op_str), hex(x.address)
        words.append(w)
    for w in words: emit(w)
    # Order of the exit stubs: when the last instruction of the copy has executed, control "falls out sequentially" into exit stub 0, which immediately follows the copy -- so stub 0 must be the fall-out exit back to hi, and jump targets outside the window come after it.
    # Up to r40 the fall-out exit was placed last: whenever a window contained a conditional jump to outside the window (the "compare not equal" exit of a compare-and-swap), a successful write fell into the first external exit = handled as a failed compare
    # (one operation done twice / a lock-free list popped more than once). The game has 6 such windows (main 0x1848eb8 / 0x1848f28 / 0x1848f84, sdk 0x2ea0fc / 0x2ea4cc / 0x2ea644);
    # windows whose only branch is a cbnz back into the window are not affected. The equivalence check used to never draw the "compare equal" direction at random, so it stayed green (equiv.py now forces both directions of window branches).
    for t in [hi] + ex_targets:
        emit(STRX(S, T)); emit(LDP_POST(S, T, 31, 16) if nrz else LDP(S, T, 31, -16)); emit(B(here(), t)); emit(NOP)
    return start, lo, hi

def site_region(seg, entry):
    """Exclusive section in several pieces, entered only at the exclusive load `entry`: the same prologue and exits as site_window, the pieces copied
    one after another in address order. A branch to an instruction of the section goes to its copy (also an unconditional b, e.g. a cold retry
    block's way back); a piece whose last instruction falls through ends with a b to the exit that returns to the instruction after it.
    Layout: prologue; [b to the entry's copy unless it comes first]; piece copies (+ b to its fall-through exit); exits. Returns the start and the
    pieces as (original lo, original hi, copy address)."""
    used = set()
    for x in seg: used |= regs_in(x)
    assert all(sp_delta(x) == 0 for x in seg), f'exclusive section {entry:#x} moves sp'
    S, T = pick(used)
    inR = {x.address for x in seg}
    nrz = NRZ and not any(uses_sp(x) for x in seg)
    if NRZ and not nrz: below_sp.append(entry); stat('below_sp')
    runs = []
    for k, x in enumerate(seg):
        if k and x.address == seg[k - 1].address + 4: runs[-1][1] = k + 1
        else: runs.append([k, k + 1])
    start = here()
    emit(STP_PRE(S, T, 31, -16) if nrz else STP(S, T, 31, -16)); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
    pos, pieces, p = {}, [], here() + (4 if entry != seg[0].address else 0)
    for a, b in runs:
        pieces.append((seg[a].address, seg[b - 1].address + 4, p))
        for k in range(a, b): pos[seg[k].address] = p; p += 4
        if seg[b - 1].mnemonic != 'b': p += 4
    exits0, exits = p, []
    def exit_to(t): exits.append(t); return exits0 + 16 * (len(exits) - 1)
    if entry != seg[0].address: emit(B(here(), pos[entry]))
    for (a, b), (lo, hi, c0) in zip(runs, pieces):
        assert here() == c0
        for k in range(a, b):
            x = seg[k]; pc = here(); m = x.mnemonic
            w = subst(x, S) if has18(x) else struct.unpack('<I', x.bytes)[0]
            if m in ('b', 'cbz', 'cbnz', 'tbz', 'tbnz') or m.startswith('b.'):
                tgt = x.operands[-1].imm
                newt = pos[tgt] if tgt in inR else exit_to(tgt)
                off = (newt - pc) >> 2
                if m == 'b': w = B(pc, newt)
                elif m in ('tbz', 'tbnz'): w = (w & 0xFFF8001F) | ((off & 0x3FFF) << 5)
                else: w = (w & 0xFF00001F) | ((off & 0x7FFFF) << 5)
                dd = dis(w, pc); assert dd.operands[-1].imm == newt and dd.mnemonic == m
            else:
                assert m not in ('adr', 'adrp', 'bl', 'br', 'blr', 'ret') and not (m == 'ldr' and '[' not in x.op_str), hex(x.address)
            emit(w)
        if seg[b - 1].mnemonic != 'b': emit(B(here(), exit_to(hi)))
    assert here() == exits0
    for t in exits:
        emit(STRX(S, T)); emit(LDP_POST(S, T, 31, 16) if nrz else LDP(S, T, 31, -16)); emit(B(here(), t)); emit(NOP)
    return start, pieces

# ---- Function boundaries (eh_frame_hdr) ----
m0 = struct.unpack_from('<I', IMG, 4)[0]
assert IMG[m0:m0 + 4] == b'MOD0'
ehs = m0 + struct.unpack_from('<i', IMG, m0 + 16)[0]
def rd(o, enc):
    fmt, app = enc & 0xF, enc & 0x70
    v, sz = {0x3: (struct.unpack_from('<I', IMG, o)[0], 4), 0xB: (struct.unpack_from('<i', IMG, o)[0], 4),
             0x4: (struct.unpack_from('<Q', IMG, o)[0], 8)}[fmt]
    if app == 0x10: v += o
    if app == 0x30: v += ehs
    return v, sz
_, penc, cenc, tenc = IMG[ehs:ehs + 4]
o = ehs + 4
_, s = rd(o, penc); o += s
nfde, s = rd(o, cenc); o += s
starts = set()
for _ in range(nfde):
    a, s = rd(o, tenc); o += s
    _, s = rd(o, tenc); o += s
    starts.add(a)
starts = sorted(x for x in starts if x < TEXT_END)

# ---- Linear scan to find all rewrite points (not relying on register read/write reports) ----
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sysreg
kinds = {}
# Load-exclusives as acquire loads (plan item 232, after nxport ce8f1c3, MIT): Apple cores reorder the loads after a plain ldxr that the console's
# cores keep in order, so a spinlock built on ldxr / stxr could read the data it protects early (nxport: Breath of the Wild's jobs ran twice and
# crashed until this; Tears of the Kingdom's walking speed the same). Every LDXR / LDXP / LDXRB / LDXRH (o2 = 0, L = 1, o0 = 0) the disassembler
# confirms gets bit 15 (o0): LDAXR / LDAXP / LDAXRB / LDAXRH, in place, before the sites are found (an exclusive section copied to the patch area
# copies the acquire form). The addresses go to <name>.acq.tsv (verify/structck.py ⑭ checks them against llvm-objdump's reading; equiv.py's deexcl
# and fnfuzz.py's single-threaded runs read both forms alike). The words of cuts and options stay as the rules give them (their original bytes are
# checked again later, and by the host for options)
ACQ_MASK, ACQ_VAL, ACQ_PLAIN = 0x3FC08000, 0x08400000, ('ldxr', 'ldxp', 'ldxrb', 'ldxrh')
acq_keep = {(c[0] + k) & ~3 for c in cuts for k in range(len(bytes.fromhex(c[1])))} | {(o[1] + k) & ~3 for o in opts for k in range(len(bytes.fromhex(o[2])))}
acq = []
for o in range(0, TEXT_END, 4):
    w = struct.unpack_from('<I', IMG, o)[0]
    if w & ACQ_MASK == ACQ_VAL and o not in acq_keep and not os.environ.get('DM_TEST_ACQ_OFF'):   # (the env: negative control of t_rewriter_synth only)
        if any(x.mnemonic in ACQ_PLAIN for x in md.disasm(IMG[o:o + 4], o)): w |= 0x8000; struct.pack_into('<I', IMG, o, w); acq.append(o)
    if w & 0xFFE0001F == 0xD4000001: kinds[o] = ('svc', (w >> 5) & 0xFFFF); continue
    if w & 0xFFFFFFE0 == 0xD53BD040: kinds[o] = ('tpidr_r', None); continue
    if w & 0xFFFFFFE0 == 0xD51BD040: kinds[o] = ('tpidr_w', None); continue
    if w & 0xFFFFFFE0 == 0xD53BD060: kinds[o] = ('tpidrro_r', None); continue
    if w & 0xFFFFFFE0 == 0xD53BE020: kinds[o] = ('cntpct', w & 31); continue
    if w & 0xFFFFFFE0 == 0xD53BE040: kinds[o] = ('cntvct', w & 31); continue
    # mrs xN, ctr_el0 (cache type): SIGILL in user mode on Apple systems; SDK 20's nn::os::FlushDataCache reads it for the data cache line size (TotK sdk
    # 0x13bbb4, from AddI3dl2Reverb). After nxnative (seedds, MIT)
    if w & 0xFFFFFFE0 == 0xD53B0020: kinds[o] = ('ctr', w & 31); continue
    # any other system register: only those user mode reads alike on the Switch and here pass (tools/sysreg.py); the rest stops the rewrite
    if sysreg.is_sysreg_access(w):
        bad = sysreg.refused(w, md, o)
        assert not bad, f'{NAME} {o:#x}: {bad}: a system register the rewriter does not handle (SIGILL or another value on this host)'
        continue
    # x18 / w18 can only sit in a register field (Rd/Rt, Rn, Ra/Rt2, Rm/Rs); a word with 18 in none of them needs no disassembly
    # (upstream PR #55, n3crosis)
    if not any((w >> p) & 31 == 18 for p in (0, 5, 10, 16)): continue
    for ins in md.disasm(IMG[o:o + 4], o):
        if has18(ins): kinds[o] = ('x18', None)

# Take the instruction table per function, find the windows and jump tables
import bisect
def func_of(a):
    i = bisect.bisect_right(starts, a) - 1
    return (starts[i], starts[i + 1] if i + 1 < len(starts) else TEXT_END) if i >= 0 else (a, a + 4)

# ---- Control flow inside one function (jump tables whose targets are not equally spaced) ----
# Edges: fall-through (not after b / br / ret; bl and blr fall through), direct branch targets inside the function, and the targets of the jump
# tables found so far. A backward walk that reaches an instruction with no known predecessor (the function entry, or a target of a jump table
# not recognised) cannot see every definition.
def dtarget(ins):
    m = ins.mnemonic
    return ins.operands[-1].imm if m in ('b', 'cbz', 'cbnz', 'tbz', 'tbnz') or m.startswith('b.') else None
def preds_of(insl, extra=()):
    """Predecessor edges per instruction index: (predecessor index, 'fall' | 'taken' | 'jt')."""
    idx = {x.address: i for i, x in enumerate(insl)}
    P = [[] for _ in insl]
    for i, x in enumerate(insl):
        if x.mnemonic not in ('b', 'br', 'ret', '.word') and i + 1 < len(insl): P[i + 1].append((i, 'fall'))
        t = dtarget(x)
        if t is not None and t in idx: P[idx[t]].append((i, 'taken'))
    for frm, t in extra:
        if t in idx: P[idx[t]].append((idx[frm], 'jt'))
    return P
CALL_CLOBBER = {f'x{i}' for i in range(19)} | {'x30'}
def wnames(ins):
    """Registers an instruction writes, as x names; a call writes every register the callee may clobber."""
    _, w = ins.regs_access()
    s = {re.sub(r'^w', 'x', ins.reg_name(r)) for r in w}
    if ins.mnemonic in ('bl', 'blr'): s |= CALL_CLOBBER
    return s
class OpenPath(Exception): pass
def reaching(insl, P, i, reg, strict):
    """Instructions that may define reg (x name) for the use at insl[i], over every backward path. strict: a path that reaches an instruction with
    no known predecessor raises OpenPath; otherwise that path contributes nothing (the optimistic rounds of jump_tables)."""
    defs, seen, st = set(), set(), [p for p, _ in P[i]]
    if not P[i] and strict: raise OpenPath(f'{insl[i].address:x}')
    while st:
        k = st.pop()
        if k in seen: continue
        seen.add(k)
        if len(seen) > 6000: raise OpenPath('walk limit')
        if reg in wnames(insl[k]): defs.add(k); continue
        if not P[k]:
            if strict: raise OpenPath(f'{insl[k].address:x}')
            continue
        st.extend(p for p, _ in P[k])
    return defs
def const_of(insl, P, i, reg, strict):
    """The address in reg at insl[i] when every reaching definition gives the same one (adrp + add, or adr); otherwise None."""
    vals = set()
    for k in reaching(insl, P, i, reg, strict):
        x = insl[k]
        if x.mnemonic == 'adr' and x.op_str.startswith(reg + ','): vals.add(x.operands[1].imm); continue
        m = re.fullmatch(rf'{reg}, {reg}, #(0x[0-9a-f]+|\d+)', x.op_str) if x.mnemonic == 'add' else None
        if not m: return None
        pg = reaching(insl, P, k, reg, strict)
        if len(pg) != 1: return None
        y = insl[next(iter(pg))]
        if y.mnemonic != 'adrp' or not y.op_str.startswith(reg + ','): return None
        vals.add(y.operands[1].imm + int(m.group(1), 0))
    return vals.pop() if len(vals) == 1 else None
INV_COND = {'hi': 'ls', 'ls': 'hi', 'hs': 'lo', 'lo': 'hs'}
def bound(insl, P, i, reg, strict):
    """Number of table entries for the index reg at insl[i]: every backward path (following register copies into the index) must meet
    `cmp wA, #N` + a conditional branch whose edge on that path means wA <= N (b.ls taken / b.hi not taken) or wA < N (b.lo taken / b.hs not
    taken); the largest bound over the paths. None when a path redefines the index otherwise."""
    best, seen = 0, set()
    st = [(p, e, frozenset({reg})) for p, e in P[i]]
    if not st and strict: raise OpenPath(f'{insl[i].address:x}')
    while st:
        k, e, al = st.pop()
        if (k, e, al) in seen: continue
        seen.add((k, e, al))
        if len(seen) > 6000: raise OpenPath('walk limit')
        x = insl[k]
        c = x.mnemonic[2:] if x.mnemonic.startswith('b.') else None
        if c in INV_COND and k > 0:
            cmp = insl[k - 1]
            m = re.fullmatch(r'w(\d+), #(0x[0-9a-f]+|\d+)', cmp.op_str) if cmp.mnemonic == 'cmp' else None
            if m and f'x{m.group(1)}' in al:
                eff = c if e == 'taken' else INV_COND[c] if e == 'fall' else None
                n = int(m.group(2), 0)
                if eff == 'ls': best = max(best, n + 1); continue
                if eff == 'lo': best = max(best, n); continue
        hit = wnames(x) & al
        if hit:
            mm = re.fullmatch(r'[wx]\d+, [wx](\d+)', x.op_str) if x.mnemonic == 'mov' else None
            if not mm or len(hit) != 1: return None
            al = (al - hit) | {f'x{mm.group(1)}'}
        if not P[k]:
            if strict: raise OpenPath(f'{x.address:x}')
            continue
        st.extend((p, e2, al) for p, e2 in P[k])
    return best or None
TAB_ELEM = {'ldrsw': (4, '<i'), 'ldrh': (2, '<H'), 'ldrb': (1, '<B'), 'ldrsh': (2, '<h'), 'ldrsb': (1, '<b')}
def jump_tab(insl, P, j, fa, fb, strict):
    """br xD at insl[j] after `adr xA, #anchor; ldr{sw,h,b,sh,sb} xT, [xB, xI{, lsl #s}]; add xD, xA, xT{, lsl #k}` (TotK main 0x393bb0:
    adr x18; ldrsw x1, [x17, x0, lsl #2]; add x18, x18, x1; br x18), with the table base xB from adrp + add and the index bounded by a compare
    (0x680528: sub w12, w18, #1; cmp w12, #0x2d; b.ls → table block) → (all targets, description) or (None, reason).
    Targets = anchor + (entry << k), read from the constant table; all must lie inside the function."""
    rD = insl[j].op_str
    ds = reaching(insl, P, j, rD, strict)
    if len(ds) != 1: return None, 'register defined by several instructions'
    ka = next(iter(ds)); a = insl[ka]
    m = re.fullmatch(rf'{rD}, (x\d+), x(\d+)(?:, lsl #(\d))?', a.op_str) if a.mnemonic == 'add' else None
    if not m: return None, f'defined by {a.address:x} {a.mnemonic} {a.op_str}'
    rA, rT, sh = m.group(1), f'x{m.group(2)}', int(m.group(3) or 0)
    anc = const_of(insl, P, ka, rA, strict); order = ''
    if anc is None and not sh and not os.environ.get('DM_TEST_JT_NO_SWAP'):   # (the env: negative control of t_rewriter_synth only)
        # add is commutative: offsets relative to the table itself, `ldrsw x18, [xB, xI, lsl #2]; add x18, x18, xB; br x18` (Link's Awakening's
        # subsdk in nxnative's bring-up notes, va11mobile's site_br_rel, smo's selftest), put the loaded entry first and the anchor second. Same
        # targets, anchor + entry; not with a shift, which would scale the anchor (plan item 129)
        rA, rT = rT, rA; anc = const_of(insl, P, ka, rA, strict); order = ' (operands swapped)'
    if anc is None: return None, 'anchor not a constant'
    ld = reaching(insl, P, ka, rT, strict)
    if len(ld) != 1: return None, 'offset defined by several instructions'
    kl = next(iter(ld)); L = insl[kl]
    mm = re.fullmatch(rf'[xw]{rT[1:]}, \[x(\d+), x(\d+)(?:, lsl #(\d))?\]', L.op_str)
    if not mm or L.mnemonic not in TAB_ELEM: return None, f'offset from {L.address:x} {L.mnemonic} {L.op_str}'
    size, fmt = TAB_ELEM[L.mnemonic]
    if (1 << int(mm.group(3) or 0)) != size: return None, 'index scale differs from the element size'
    tb = const_of(insl, P, kl, f'x{mm.group(1)}', strict)
    if tb is None: return None, 'table base not a constant'
    if not RO_OFF <= tb < DATA_OFF: return None, f'table {tb:#x} outside the read-only segment'
    n = bound(insl, P, kl, f'x{mm.group(2)}', strict)
    if n is None: return None, 'index not bounded'
    tg = [anc + (struct.unpack_from(fmt, IMG, tb + size * i)[0] << sh) for i in range(n)]
    if any(not (fa <= t < fb) or t % 4 for t in tg): return None, 'a target outside the function'
    return tg, f'anchor {anc:#x} table {tb:#x} {n} x {L.mnemonic} << {sh}{order}'
def jump_tables(insl, fa, fb):
    """All jump tables of one function → ({br index: targets}, {br index: description or reason}). A table's targets are edges that other tables'
    walks need (a table's own targets can loop back to it), so rounds that ignore paths with an unknown predecessor run to a fixpoint; then every
    table is derived again strictly with all edges found, and only tables that pass the strict round count."""
    brs = [j for j, x in enumerate(insl) if x.mnemonic == 'br']
    edges = lambda tabs: [(insl[j].address, t) for j, tg in tabs.items() for t in tg]
    known = {}
    for _ in range(10):
        P = preds_of(insl, edges(known)); new = {}
        for j in brs:
            try: tg, _w = jump_tab(insl, P, j, fa, fb, False)
            except OpenPath: tg = None
            if tg: new[j] = tg
        if new == known: break
        known = new
    P = preds_of(insl, edges(known)); out, why = {}, {}
    for j in brs:
        try: tg, why[j] = jump_tab(insl, P, j, fa, fb, True)
        except OpenPath as e: tg, why[j] = None, f'a path reaches {e} with no known predecessor'
        if tg: out[j] = tg
    return out, why

done = set(); windows = []; jumps = {}; tabjumps = {}; xbranch = {}
def x18_fnptr(insl, j):
    """The instruction that last wrote x18 before insl[j], within the same straight-line block: must be `ldr x18, [...]` (a function pointer)."""
    for p in reversed(insl[:j]):
        if p.mnemonic in ('b', 'bl', 'br', 'blr', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz', '.word') or p.mnemonic.startswith('b.'): return False
        _, wr = regset(p)
        if wr & {'x18', 'w18'}: return p.mnemonic == 'ldr' and p.op_str.startswith('x18, [')
    return False
funcs = sorted({func_of(a) for a, (k, _) in kinds.items() if k == 'x18'})
regions = []
for fa, fb in funcs:
    insl = decode_words(IMG, fa, fb)
    tabs = None
    for j, ins in enumerate(insl):
        if ins.mnemonic == 'blr' and ins.op_str == 'x18':
            # The block reads the K18 slot, which holds the guest's x18 wherever it was loaded: TotK main 0xc25f0 loads it in an earlier block
            # (nxnative's blr rewrite has no such check either)
            xbranch[ins.address] = 1
        if ins.mnemonic == 'br' and ins.op_str == 'x18':
            if not any(p.mnemonic == 'adr' and p.op_str.startswith('x18,') for p in insl[max(0, j - 8):j]) and x18_fnptr(insl, j):
                xbranch[ins.address] = 0; continue
            base = width = None
            ctx = insl[max(0, j - 8):j]
            for p in reversed(ctx):
                if p.mnemonic == 'adr' and p.op_str.startswith('x18,'): base = p.operands[1].imm; break
            for p in reversed(ctx):
                if p.mnemonic == 'add' and p.op_str.startswith('x18, x18,') and 'lsl #2' in p.op_str:
                    idx = num(p.reg_name(p.operands[2].reg))
                    for q in reversed(ctx):
                        if q.mnemonic in ('ldrb', 'ldrsb') and num(q.reg_name(q.operands[0].reg)) == idx: width = 256
                        if q.mnemonic in ('ldrh', 'ldrsh') and num(q.reg_name(q.operands[0].reg)) == idx: width = 65536
                        if width: break
                    break
            if base and width: jumps[ins.address] = (base, width); continue
            if tabs is None: tabs = jump_tables(insl, fa, fb)
            assert j in tabs[0], f'jump table {ins.address:x}: {tabs[1][j]}'
            tabjumps[ins.address] = tabs[0][j]
    # Exclusive sections that use x18 may be entered only at their exclusive load: an entry further in would run part of the section natively with
    # the real x18, or start the copy after its load (the jump-table targets of site_br_tab count as entries; site_br's equally spaced tables are
    # not resolved to targets, as before)
    tedges = [(a_, t) for a_, tg in tabjumps.items() if fa <= a_ < fb for t in tg]
    for R in exclusive_regions(insl):
        seg = [insl[i] for i in R]
        if not any(has18(x) for x in seg): continue
        ents = region_entries(insl, R, fa, tedges)
        assert len(ents) == 1 and next(x for x in seg if x.address == ents[0]).mnemonic in EXCL_LD, \
            f'exclusive section {seg[0].address:x}: entered at {[hex(e) for e in ents]} (supported: one entry, at its exclusive load)'
        if ents[0] == seg[0].address and seg[-1].address - seg[0].address == 4 * (len(seg) - 1): windows.append(seg)
        else: regions.append((seg, ents[0]))
        done |= {x.address for x in seg}

IMG0 = bytes(IMG)   # text before any site is replaced by a branch (alloca_shape looks back across earlier sites)
def put_branch(at, to): struct.pack_into('<I', IMG, at, B(at, to))
for wseg in windows:
    st, lo, hi = site_window(wseg); stat('window'); put_branch(lo, st)
region_rows = []
for seg, e in regions:
    st, pieces = site_region(seg, e); stat('region'); put_branch(e, st); region_rows.append((e, pieces))
for a in sorted(kinds):
    if a in done: continue
    k, arg = kinds[a]
    ins = dis(struct.unpack_from('<I', IMG, a)[0], a)
    if k == 'x18':
        m = ins.mnemonic
        if a in xbranch: st = site_xbranch(ins, xbranch[a]); stat('blr_fnptr' if xbranch[a] else 'br_fnptr')
        elif m == 'br' and a in tabjumps: st = site_br_tab(ins, tabjumps[a]); stat('br_tab')
        elif m == 'br': st = site_br(ins, *jumps[a]); stat('br')
        elif m in ('cbz', 'cbnz', 'tbz', 'tbnz'): st = site_cond(ins); stat('cond')
        elif m in ('adr', 'adrp'): st = site_pcrel(ins); stat('pcrel')
        elif m == 'mov' and ins.op_str == 'sp, x18':
            assert alloca_shape(a), f'mov sp, x18 that is not a stackalloc {a:x}'
            st = site_mov_sp(ins); stat('mov_sp')
        elif m in ('blr', 'ret', 'b', 'bl') or (m == 'ldr' and '[' not in ins.op_str): raise RuntimeError(f'{a:x} {m} {ins.op_str}')
        else: st = site_plain(ins); stat('plain')
    elif k == 'tpidr_r': st = site_sysreg_read(ins, 'KTLS'); stat(k)
    elif k == 'tpidrro_r': st = site_sysreg_read(ins, 'KTLR'); stat(k)
    elif k == 'tpidr_w': st = site_tpidr_write(ins); stat(k)
    elif k == 'svc': st = site_gate(ins, 'svc', arg); stat(k)
    elif k in ('cntpct', 'cntvct'): st = site_gate(ins, k, arg); stat(k)
    elif k == 'ctr':   # in place: DminLine = 4 (64-byte lines). The value only has to be at most the real line size (128 bytes on Apple cores): a smaller
        # step cleans every line, some twice. IminLine = 0 (4 bytes) is the smallest, so instruction cache loops cover every line too
        assert arg != 18, f'{a:x} mrs x18, ctr_el0'
        struct.pack_into('<I', IMG, a, 0xD2A00080 | arg); stat(k); continue
    put_branch(a, st)

obs = list(OBS.get(BUILD_ID, []))
for off, oid, ow, why in obs:
    w = struct.unpack_from('<I', IMG, off)[0]
    assert f'{w:08x}' == ow, f'observation gate {off:#x}: original instruction differs ({w:08x})'
    # Function entries only: dm_gate writes back only x0–x8 / x30, and host C code may modify x9–x17; at entry these carry no values under AAPCS64 (arguments are only in x0–x7 / x8)
    assert off in set(starts), f'observation gate {off:#x} is not a function entry'
    assert off < TEXT_END and off not in kinds and off not in done, f'observation gate {off:#x} overlaps a rewrite site'   # done: every instruction of a window / section
    put_branch(off, site_obs(off, oid, w)); stat('obs')

# ---- Parameter slots (host only): {NSO build ID: [(offset A, original word A, offset B, original word B, register, key, reason)]}.
# The two constant-building instructions at the original site are changed so that the first jumps to a parameter trampoline in the patch area and the second is a nop; the trampoline fetches the key page value indirectly and jumps back to just after the first (A+4): when A and B are not adjacent, the original instructions in between execute as usual
# (jumping back to B+4 would skip whatever was between A and B; structck checks the trampoline exit).
# xR must not be read or written between A and B (asserted here). The game's hook code sets the slot's value (dm.h g_keyparam) ----
params = PARAMS.get(BUILD_ID, [])
for oa, wa, ob_, wb, r, key, why in params:
    for o, w0 in ((oa, wa), (ob_, wb)):
        w = struct.unpack_from('<I', IMG, o)[0]
        assert f'{w:08x}' == w0, f'parameter slot {o:#x}: original instruction differs ({w:08x})'
        assert o not in kinds and o not in done, f'parameter slot {o:#x} overlaps a rewrite site'
    for o in range(oa + 4, ob_, 4):
        ins = dis(struct.unpack_from('<I', IMG, o)[0], o)
        assert r not in regs_in(ins), f'parameter slot: x{r} is used between A and B at {o:#x} ({ins.mnemonic} {ins.op_str})'
    start = here()
    tmp = 16 if r != 16 else 17
    emit(STR_PRE(tmp, 31, -16) if NRZ else STUR(tmp, 31, -16)); emit_tp(r); emit_slot_addr(r, tmp, key); emit(LDRX(r, r)); emit(LDR_POST(tmp, 31, 16) if NRZ else LDUR(tmp, 31, -16)); emit(B(here(), oa + 4))
    struct.pack_into('<I', IMG, oa, B(oa, start)); struct.pack_into('<I', IMG, ob_, NOP); stat('param')

# ---- Code caves (CAVES, see the rules section at the top): one instruction becomes a branch to a routine in the patch area ----
PCREL = ('adr', 'adrp', 'b', 'bl', 'cbz', 'cbnz', 'tbz', 'tbnz')
def cave_word_ok(ins):   # a word that may run in the patch area as it is: not PC-relative, nothing the rewrite would have to change
    m = ins.mnemonic
    return not (m in PCREL or m.startswith('b.') or (m.startswith('ldr') or m == 'prfm') and '[' not in ins.op_str or has18(ins)
                or m in ('svc', 'mrs', 'msr', 'br', 'blr', 'ret'))
busy_cave = set(kinds) | done | {o for o, *_ in obs} | {o for row in params for o in (row[0], row[2])}
busy_cave |= {(c[0] + k) & ~3 for c in cuts for k in range(len(bytes.fromhex(c[1])))} | {(o[1] + k) & ~3 for o in opts for k in range(len(bytes.fromhex(o[2])))}
cave_rows = []
for off, ow, routine, why, *gate in caves:
    w = struct.unpack_from('<I', IMG, off)[0]; gate = gate[0] if gate else ''
    assert not gate or re.fullmatch(r'[A-Za-z0-9_]{1,31}', gate), f'code cave {off:#x}: option name {gate!r}'
    assert f'{w:08x}' == ow, f'code cave {off:#x}: original instruction differs ({w:08x})'
    assert off < TEXT_END and off % 4 == 0 and off not in busy_cave and why.strip(), f'code cave {off:#x}: outside text, on another change, or no reason'
    busy_cave.add(off)
    start = here(); exits, left = set(), False
    for f in routine:
        if isinstance(f, int) or f == ('orig',):
            x = w if f == ('orig',) else f; ins = dis(x, here())
            assert cave_word_ok(ins), f'code cave {off:#x}: {ins.mnemonic} {ins.op_str} cannot run in the patch area (PC-relative, x18, svc, a system register or a branch: use a form)'
            emit(x); left = False
            if NRZ and re.search(r'\[sp, #-', ins.op_str) and off not in below_sp: below_sp.append(off); stat('below_sp')   # a routine that keeps registers below sp
        elif f[0] == 'adrp_host': emit(ADRP(f[1], here(), HOST_PAGE_OFF)); left = False
        elif f[0] in ('b', 'bl'):
            assert 0 <= f[1] < TEXT_END and f[1] % 4 == 0, f'code cave {off:#x}: {f[0]} to {f[1]:#x} outside text'
            if f[0] == 'b': emit(B(here(), f[1])); exits.add(f[1]); left = True
            else: emit(BL(here(), f[1])); left = False
        else: raise AssertionError(f'code cave {off:#x}: unknown form {f}')
    if not left and not os.environ.get('DM_TEST_CAVE_NO_BACK'):   # (the env: negative control of t_rewriter_synth only)
        emit(B(here(), off + 4)); exits.add(off + 4)
    if gate and not os.environ.get('DM_TEST_CAVE_GATE_WRITTEN'):   # (the env: negative control of t_rewriter_synth only: the branch written as well)
        opts.append((gate, off, struct.pack('<I', w).hex(), struct.pack('<I', B(off, start)).hex(), f'{why} [code cave: b {start:#x}]'))
    else: put_branch(off, start)
    stat('cave')
    cave_rows.append((off, ow, start, bytes(patch[start - PATCH_OFF:here() - PATCH_OFF]), sorted(exits), gate, why))

os.makedirs(outdir, exist_ok=True)
P = lambda ext: os.path.join(outdir, f'{NAME}.{ext}')
for name, off, ob, nb, why in opts:   # the host writes an option where the original still is: the rewrite must not have touched it
    assert IMG[off:off + len(ob) // 2].hex() == ob, f'option {name} {off:#x}: rewritten here ({IMG[off:off + len(ob) // 2].hex()}), cannot be applied at load'
open(P('patched.bin'), 'wb').write(IMG + bytearray(PATCH_OFF - len(IMG)) + patch)
import hashlib
with open(os.path.join(outdir, 'build_src.sha256'), 'w') as f:
    f.write(hashlib.sha256(open(os.path.abspath(__file__), 'rb').read()).hexdigest() + '  patch_nso.py\n')
with open(P('fixups.tsv'), 'w') as f:
    f.write(f'# PATCH_OFF\t{PATCH_OFF:#x}\n# KEY_PAGE_OFF\t{KEY_PAGE_OFF:#x}\n# KEY_PAGE_SIZE\t0x4000\n# TEXT_END\t{TEXT_END:#x}\n# RO_OFF\t{RO_OFF:#x}\n# DATA_OFF\t{DATA_OFF:#x}\n')
    if TARGET == 'android': f.write(f'# TARGET_ANDROID\t1\n# NRZ\t{int(NRZ)}\n')   # (the apple header is as before)
    if HOST_PAGE_OFF: f.write(f'# HOST_PAGE_OFF\t{HOST_PAGE_OFF:#x}\n')
    for k in KEY_INDEX: f.write(f'# KEY_{k}_IDX\t{KEY_INDEX[k]}\n')
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import rewriter_stamp   # this rewriter + the rules: the host compares it (loader.c)
    f.write(f'# REWRITER\t0x{rewriter_stamp.stamp(rules_path())}\n')
with open(P('gates.tsv'), 'w') as f:
    for ret, k, arg, orig in gates: f.write(f'{ret:#x}\t{k}\t{arg}\t{orig:#x}\n')
with open(P('sites.tsv'), 'w') as f:
    for a in sorted(kinds):
        if a not in done: f.write(f'{a:#x}\t{kinds[a][0]}\n')
    for wseg in windows: f.write(f'{wseg[0].address:#x}\tW\t{wseg[-1].address + 4:#x}\n')
    for e, pieces in region_rows: f.write(f'{e:#x}\tR\t' + ' '.join(f'{lo:#x}-{hi:#x}@{c:#x}' for lo, hi, c in pieces) + '\n')   # exclusive section in pieces: entry, then each piece [lo, hi) and the address of its copy
    for a, (b, c) in jumps.items(): f.write(f'{a:#x}\tJ\t{b:#x}\t{c}\n')
    for a, tg in sorted(tabjumps.items()): f.write(f'{a:#x}\tJT\t' + ' '.join(f'{t:#x}' for t in tg) + '\n')   # jump table with explicit targets, one per entry (site_br_tab)
    for a, link in sorted(xbranch.items()): f.write(f'{a:#x}\tT\t{link}\n')   # br / blr x18 through a function pointer: 1 = blr
with open(P('acq.tsv'), 'w') as f:   # load-exclusives turned into acquire loads in place (plan item 232): offset, original word
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for a in acq: f.write(f'{a:#x}\t{struct.unpack_from("<I", IMG0, a)[0] & ~0x8000:08x}\n')
with open(P('cuts.tsv'), 'w') as f:
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for off, ob, nb, why in cuts: f.write(f'{off:#x}\t{ob}\t{nb}\t{why}\n')   # columns: offset, original bytes, new bytes, reason
with open(P('options.tsv'), 'w') as f:   # columns: option, offset, original bytes, new bytes, reason (host/runtime/loader.c)
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for name, off, ob, nb, why in opts: f.write(f'{name}\t{off:#x}\t{ob}\t{nb}\t{why}\n')
with open(P('params.tsv'), 'w') as f:
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for oa, wa, ob_, wb, r, key, why in params: f.write(f'{oa:#x}\t{ob_:#x}\tx{r}\t{key}\t{why}\n')
with open(P('caves.tsv'), 'w') as f:   # columns: offset, original word, routine start, its words, its exits back to text, option (empty: always), reason (structck ⑬)
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for off, ow, start, wb, ex, gate, why in cave_rows:
        f.write(f'{off:#x}\t{ow}\t{start:#x}\t' + ' '.join(f'{v:08x}' for v in struct.unpack(f'<{len(wb) // 4}I', wb)) + '\t' + ','.join(f'{e:#x}' for e in ex) + f'\t{gate}\t{why}\n')
with open(P('obs.tsv'), 'w') as f:
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for off, oid, ow, why in obs: f.write(f'{off:#x}\t{oid}\t{ow}\t{why}\n')
if TARGET == 'android':   # the sites whose blocks still keep registers below sp (see DM_TARGET): unsafe under a signal without SA_ONSTACK
    with open(P('belowsp.tsv'), 'w') as f:
        f.write(f'# BUILD_ID\t{BUILD_ID}\n')
        for a in sorted(below_sp): f.write(f'{a:#x}\n')
print(NAME, stats, 'acquire', len(acq), 'cuts', len(cuts), 'options', len(opts), 'caves', len(cave_rows), 'obs', len(obs), 'patch', len(patch), 'PATCH_OFF', hex(PATCH_OFF), 'slot_refs', len(fix), 'gates', len(gates))
