# Per-site equivalence verification: original instruction (original image, x18 is a real register) vs patch block (rewritten image, x18 lives in a TSD slot).
# Several random states per site; compares x0-x30 (x18 compared against the slot value), sp, nzcv, v0-v31, all memory writes, final PC, memory access faults.
# Usage: equiv.py <rewrite build output dir> <module> <NSO> [--seed 1] [--jobs N] [--min-eff 2] [--max-extra 16] [--mutate 300] [--tsv file]
# Coverage: a trial where both sides take a memory access fault only compares the fault address and does not count as "effective"; each site needs at least min-eff effective trials, extra draws are made if short (≤max-extra), and sites still short are listed by name.
# A trial whose original instruction reads the 128 bytes below sp is not compared and not effective (redzone_reads): the rewrite blocks keep their scratch
# registers there (Apple's red zone), and real code never reads below sp (TotK main 0x5cc0ac ldr x18, [x17] drew x17 = sp - 14 and read the saved x0 / x1).
# A patched side that touches memory more than 128 bytes below the CURRENT sp (Apple's red zone; an exception may write below it) where the original
# did not differs ("redzone"): a block's scratch save past the red zone after the instruction moved sp (a post-index pop larger than 16 with the
# save at -112; upstream 373b402 / 4f0603f count the Android form; plan item 201). The start sp's 128 bytes alone used to be watched.
# Mutation self-check: draw mutate ordinary rewrite blocks; in half the replaced instruction is changed to nop, in half the target of the trampoline exit b gets +4; all must be judged divergent (a negative control showing the criterion can discriminate).
# The android target (patch_nso.py DM_TARGET, plan item 301b): the patched side's tpidr_el0 points at a bionic TLS area whose slot 2 (+16) holds the slot block,
# and Linux has no red zone: before every instruction of a block the 256 bytes below the current sp are overwritten (a signal frame, which a handler without
# SA_ONSTACK puts there at any moment), and a patched access below the current sp that the original did not make differs ("redzone"). The sites listed in
# belowsp.tsv keep the red-zone form and are checked as on apple (counted in the summary). DM_TEST_NRZ_OFF products (red-zone forms under android's slots) fail here.
# Host gate (svc / cntpct / cntvct) semantics are defined by the host and are not run here, structck.py does the structural check; registered cuts (cuts.tsv) are intentional semantic changes and likewise only have their bytes checked by structck.
import sys, os, struct, random, argparse, multiprocessing as mp
from unicorn import *
from unicorn.arm64_const import *
from capstone import *
sys.path.insert(0, os.path.dirname(__file__))
import nsoimg

ap = argparse.ArgumentParser()
ap.add_argument('dir'); ap.add_argument('mod'); ap.add_argument('nso')
ap.add_argument('--seed', type=int, default=1); ap.add_argument('--jobs', type=int, default=os.cpu_count())
ap.add_argument('--min-eff', type=int, default=2); ap.add_argument('--max-extra', type=int, default=16)
ap.add_argument('--mutate', type=int, default=300); ap.add_argument('--tsv')
A = ap.parse_args()

IB = 0x100000000
ARENA = 0x0; ARENA_SZ = 0x1000000          # Low 16 MB readable/writable random area
TSD = 0x2000000
SP0 = 0x800000
DIR, MOD = A.dir, A.mod
hdr = nsoimg.header(DIR, MOD)                  # Header: segment boundaries and the thread slot numbers hard-coded into instructions
PATCH_OFF = hdr['PATCH_OFF']
KEY_IDX = {k[4:-4]: v for k, v in hdr.items() if k.startswith('KEY_') and k.endswith('_IDX')}
KEY_SLOT = {k: 300 + i for i, k in enumerate(sorted(KEY_IDX, key=KEY_IDX.get))}
SLOT18, SLOTTLS, SLOTTLR = (KEY_SLOT[k] for k in ('K18', 'KTLS', 'KTLR'))
KEY_PAGE_OFF = hdr['KEY_PAGE_OFF']
ANDROID = hdr.get('TARGET_ANDROID') == 1
BELOW = {int(l, 16) for l in open(f'{DIR}/{MOD}.belowsp.tsv') if not l.startswith('#')} if ANDROID else set()
TLSB = 0x2100000   # android: bionic's TLS slots (tpidr_el0); slot 2 holds the slot block's address
PRO = 28 if ANDROID else 24   # a window's prologue: stp; the slot base (one instruction, android two); adrp; ldr; add; ldr
pat = bytearray(open(f'{DIR}/{MOD}.patched.bin', 'rb').read())
orig = nsoimg.load(A.nso)
ncut = sum(1 for l in open(f'{DIR}/{MOD}.cuts.tsv') if not l.startswith('#'))
nobs = sum(1 for l in open(f'{DIR}/{MOD}.obs.tsv') if not l.startswith('#')) if os.path.exists(f'{DIR}/{MOD}.obs.tsv') else 0
def deexcl(b):
    # Test only: all exclusive loads/stores (every width, single / pair) → plain ldr/str/ldp/stp; the status test right after a store takes the path of
    # a store that succeeded (status 0): cbnz → nop, cbz → b to its target. Both images are processed the same way. (cbz became a nop as well until plan
    # item 129, so a section that tests the status with cbz never left by its success exit: 5 of the 134 x18 sections of Tears of the Kingdom's main;
    # bayonetta3-mobile's copy of blade2-mobile's dm/verify/equiv.py, MIT, does the same). Plain and acquire forms alike, so the rewriter's acquire loads
    # (plan item 232, acq.tsv, checked by structck ⑭) compare equal here
    LD = {0: 0x39400000, 1: 0x79400000, 2: 0xB9400000, 3: 0xF9400000}
    ST = {0: 0x39000000, 1: 0x79000000, 2: 0xB9000000, 3: 0xF9000000}
    b = bytearray(b); n = 0
    for o in range(0, len(b) - 4, 4):
        w = struct.unpack_from('<I', b, o)[0]
        if w & 0x3F800000 != 0x08000000: continue
        size, L, pair = w >> 30, (w >> 22) & 1, (w >> 21) & 1
        rs, rt2, rn, rt = (w >> 16) & 31, (w >> 10) & 31, (w >> 5) & 31, w & 31
        if pair:
            if size < 2: continue
            nw = ((0xA9400000 if L else 0xA9000000) if size == 3 else (0x29400000 if L else 0x29000000)) | (rt2 << 10) | (rn << 5) | rt
        else:
            nw = (LD if L else ST)[size] | (rn << 5) | rt
        struct.pack_into('<I', b, o, nw); n += 1
        if not L:
            nx = struct.unpack_from('<I', b, o + 4)[0]
            if nx & 0x7E000000 == 0x34000000 and nx & 31 == rs:
                cbz = not nx >> 24 & 1 and not os.environ.get('DM_TEST_EQUIV_CBZ_NOP')   # (the env: negative control of t_rewriter_synth only)
                imm = (nx >> 5) & 0x7FFFF; imm -= (imm & 0x40000) << 1
                struct.pack_into('<I', b, o + 4, 0x14000000 | (imm & 0x3FFFFFF) if cbz else 0xD503201F)
    return bytes(b), n
orig, _ = deexcl(orig); pat, _ = deexcl(pat)

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True
sites = []; winend = {}; brinfo = {}; brtab = {}; fnptr = {}; ngate = 0
secs = {}   # exclusive section in pieces (R row): entry → [(lo, hi, copy address)]
for l in open(f'{DIR}/{MOD}.sites.tsv'):
    f = l.split(); a = int(f[0], 16)
    if f[1] == 'W': winend[a] = int(f[2], 16); sites.append(a)
    elif f[1] == 'R': secs[a] = [tuple(int(v, 16) for v in p.replace('@', '-').split('-')) for p in f[2:]]; sites.append(a)
    elif f[1] == 'J': brinfo[a] = (int(f[2], 16), int(f[3]))
    elif f[1] == 'JT': brtab[a] = [int(t, 16) for t in f[2:]]       # jump table with explicit targets (the site itself has its own x18 row)
    elif f[1] == 'T': fnptr[a] = int(f[2])                   # br / blr x18 through a function pointer (the site itself has its own x18 row)
    elif f[1] in ('svc', 'cntpct', 'cntvct'): ngate += 1      # Host gate: semantics are defined by the host, only a structural check is done
    elif f[1] == 'ctr': ngate += 1                            # cache type read replaced by a constant in place: an intended value change, checked word for word by structck ⑫
    else: sites.append(a)

def btarget(b, o):
    w = struct.unpack_from('<I', b, o)[0]; imm = w & 0x3FFFFFF
    if imm & 0x2000000: imm -= 0x4000000
    return o + 4 * imm
def plain_block(a):
    """Ordinary rewrite block (stp; indirect slot read via key page; replaced instruction; indirect slot write via key page; ...) → offset of the replaced instruction."""
    s = btarget(pat, a); w0 = struct.unpack_from('<I', pat, s)[0]
    if w0 & 0xFFC003E0 not in (0xA90003E0, 0xA98003E0): return None          # stp xS, xT, [sp, #off] (android: [sp, #-F]!)
    ins = list(md.disasm(bytes(pat[s:s + 40]), s)); k = 1 if ANDROID else 0   # (android: ldr xT, [xT, #0x10] after the mrs)
    if len(ins) < 8 + k or ins[1].mnemonic != 'mrs' or ('tpidr_el0' if ANDROID else 'tpidrro_el0') not in ins[1].op_str:
        return None
    if ins[2 + k].mnemonic != 'adrp' or ins[2 + k].operands[1].imm != KEY_PAGE_OFF: return None
    if ins[3 + k].mnemonic != 'ldr' or ins[3 + k].operands[1].mem.base != ins[2 + k].operands[0].reg: return None
    T = ins[1].operands[0].reg
    if ins[4 + k].mnemonic != 'add' or ins[4 + k].operands[0].reg != T or ins[4 + k].operands[1].reg != T: return None
    if ins[5 + k].mnemonic != 'ldr' or ins[5 + k].operands[1].mem.base != T: return None              # ldr xS,[xT]: read the slot
    if ins[7 + k].mnemonic != 'str' or ins[7 + k].operands[1].mem.base != T: return None              # str xS,[xT]: write back to the slot - only ordinary blocks have it, conditional blocks do not (same convention as the old version)
    return ins[6 + k].address
GP = [getattr(sys.modules['unicorn.arm64_const'], f'UC_ARM64_REG_X{i}') for i in range(29)] + [UC_ARM64_REG_X29, UC_ARM64_REG_X30]
VR = [getattr(sys.modules['unicorn.arm64_const'], f'UC_ARM64_REG_Q{i}') for i in range(32)]
# Conditional branches inside an exclusive window (the status cbz/cbnz has already been turned into nop by deexcl and is not included): a random state almost never draws the "compare equal" direction (2^-32),
# so each branch is forced through both directions once -- in both images the same branch is changed to nop (not taken) / an unconditional b (taken) at the same time, and the rest is compared as usual.
# Background: up to r40 the rewriter placed the "sequential fall-out" exit of the window copy after the external exits, so a compare-and-swap window fell into the "compare not equal" exit after a successful write; the random trials were all green (6 windows in the game: main 0x1848eb8 / 0x1848f28 / 0x1848f84, sdk 0x2ea0fc / 0x2ea4cc / 0x2ea644).
def cond_target(w, pc):
    if w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000: imm = (w >> 5) & 0x7FFFF; imm -= (imm & 0x40000) << 1; return pc + 4 * imm
    if w & 0x7E000000 == 0x36000000: imm = (w >> 5) & 0x3FFF; imm -= (imm & 0x2000) << 1; return pc + 4 * imm
    return None
# Exclusive sections: a window [lo, hi) is copied right after its prologue; a section in pieces lists each piece's copy (structck ⑪ checks the copies
# word by word). The original run of a section ends when pc leaves its instructions (inside); a piece of a section in pieces that is not entered from
# outside (TotK main 0x18598, the cold retry block, reached only from the status cbnz that deexcl turns into nop) is run on its own as well: the
# rewritten side starts at its copy with the state the prologue leaves (S = x18, T = slot address, S / T saved at [sp, #-16]).
win_br = {}; inside = {}; piece_start = {}
for a0, hi0 in list(winend.items()) + [(e, None) for e in secs]:
    gate = btarget(pat, a0); pre = list(md.disasm(bytes(pat[gate:gate + PRO]), gate))
    assert [x.mnemonic for x in pre] == ['stp', 'mrs'] + (['ldr'] if ANDROID else []) + ['adrp', 'ldr', 'add', 'ldr'], f'window block prologue does not have the expected shape {a0:#x}'
    nrz_sec = pre[0].op_str.endswith('!')   # android: the pair at the moved sp
    pieces = [(a0, hi0, gate + PRO)] if hi0 is not None else secs[a0]
    inside[a0] = {IB + c for lo, hi, cp in pieces for c in range(lo, hi, 4)}
    S, T = (GP[int(pre[0].reg_name(pre[0].operands[k].reg)[1:])] for k in (0, 1))   # stp xS, xT, [sp, #-16]
    for lo, hi, cp in pieces:
        if lo != a0: piece_start[lo] = (a0, cp, S, T, nrz_sec); inside[lo] = inside[a0]
    lst = []
    for lo, hi, copy0 in pieces:
        for c in range(lo, hi, 4):
            wo = struct.unpack_from('<I', orig, c)[0]; to = cond_target(wo, c)
            if to is None: continue
            cp = copy0 + (c - lo); wp = struct.unpack_from('<I', pat, cp)[0]; tp_ = cond_target(wp, cp)
            assert tp_ is not None, f'no matching conditional branch in the window copy {c:#x}'
            lst.append((c, wo, to, cp, wp, tp_))
    if lst:
        win_br[a0] = lst
        for lo, hi, cp in pieces:
            if lo != a0: win_br[lo] = lst
sites += sorted(piece_start)
def enc_b(pc, tgt): return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)
mut_sites = []
if A.mutate:
    cand = [a for a in sites if a not in inside and a not in brinfo and a not in brtab and plain_block(a)]
    mut_sites = sorted(random.Random(A.seed).sample(cand, min(A.mutate, len(cand))))
    pat_mut = bytearray(pat)
    def exit_b(a):   # The b by which a rewrite block returns to text (the first unconditional b in the block whose target lands back in text)
        w = struct.unpack_from('<I', pat, a)[0]; o = a + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000)
        for k in range(o, o + 4 * 24, 4):
            w = struct.unpack_from('<I', pat, k)[0]
            if w & 0xFC000000 == 0x14000000 and k + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000) < PATCH_OFF: return k, w
        return None
    # Two kinds of mutation, half each: even index = the replaced instruction is changed to nop; odd index = target of the trampoline exit b +4 (the class where a parameter-gate trampoline jumps back out of bounds: skips one original instruction)
    mut_kind = {}
    for i, a in enumerate(mut_sites):
        eb = exit_b(a) if i % 2 else None
        if eb: k, w = eb; struct.pack_into('<I', pat_mut, k, (w & 0xFC000000) | ((w + 1) & 0x3FFFFFF)); mut_kind[a] = 'exit+4'
        else: struct.pack_into('<I', pat_mut, plain_block(a), 0xD503201F); mut_kind[a] = 'nop'
    pat_mut = bytes(pat_mut)


RZ_CURRENT = not os.environ.get('DM_TEST_EQUIV_RZ_START')
def pre_sp(uc):
    """The write-back of the instruction at pc when it is a pre-index load / store on sp (pair or single register), else 0"""
    w = struct.unpack('<I', uc.mem_read(uc.reg_read(UC_ARM64_REG_PC), 4))[0]
    if (w >> 5) & 31 != 31: return 0
    if (w >> 27) & 7 == 0b101 and (w >> 23) & 3 == 3:                       # ldp / stp, pre-index
        sc = 8 if w >> 30 == 2 else 4 if w >> 30 in (0, 1) else 16
        return ((((w >> 15) & 0x7F) ^ 0x40) - 0x40) * sc
    if (w >> 27) & 7 == 0b111 and (w >> 24) & 3 == 0 and not (w >> 21) & 1 and (w >> 10) & 3 == 3:   # ldr / str, pre-index
        return (((w >> 12) & 0x1FF) ^ 0x100) - 0x100
    return 0   # negative control of t_rewriter_synth only: no check against the current sp, as before
def mk(image, arena0, patched=False):
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    sz = (len(image) + 0xFFFF) & ~0xFFFF
    u.mem_map(IB, sz, UC_PROT_ALL); u.mem_write(IB, bytes(image))
    u.mem_map(ARENA, ARENA_SZ); u.mem_write(ARENA, arena0)
    u.mem_map(TSD, 0x10000)
    u.reg_write(UC_ARM64_REG_TPIDRRO_EL0, TSD)
    if ANDROID and patched: u.mem_map(TLSB, 0x1000); u.mem_write(TLSB + 16, struct.pack('<Q', TSD)); u.reg_write(UC_ARM64_REG_TPIDRRO_EL0, 0)
    st = {'w': [], 'fault': None, 'arena': arena0, 'rz': False, 'r': [], 'deep': [], 'site': None, 'poison': []}
    def below(uc, addr):   # patched side: more than 128 bytes below the current sp (android: any byte), within its 4 KiB
        if patched and RZ_CURRENT:
            sp = uc.reg_read(UC_ARM64_REG_SP); rz = 0 if ANDROID and st['site'] not in BELOW else 128
            if rz == 0: sp += pre_sp(uc)   # a pre-index access on sp moves sp with it: the sp it leaves is the one a signal sees
            if sp - 0x1000 <= addr < sp - rz: st['deep'].append(addr)
    if ANDROID and patched:
        def clobber(uc, addr, size, d):   # a signal frame below the current sp before each instruction of a block
            if st['site'] in BELOW: return
            sp = uc.reg_read(UC_ARM64_REG_SP)
            if ARENA + 256 <= sp <= ARENA + ARENA_SZ: uc.mem_write(sp - 256, POISON); st['poison'].append(sp - 256)
        u.hook_add(UC_HOOK_CODE, clobber, begin=IB + PATCH_OFF, end=IB + sz - 1)
    def wr(uc, acc, addr, size, val, d): st['w'].append((addr, size, val & ((1 << (8 * size)) - 1))); below(uc, addr)
    def bad(uc, acc, addr, size, val, d): st['fault'] = (acc, addr); return False
    def rd(uc, acc, addr, size, val, d):
        if addr < SP0 and addr + size > SP0 - 128: st['rz'] = True
        if not patched: st['r'].append(addr)
        below(uc, addr)
    u.hook_add(UC_HOOK_MEM_WRITE, wr)
    u.hook_add(UC_HOOK_MEM_READ, rd)
    u.hook_add(UC_HOOK_MEM_INVALID, bad)
    return u, st

POISON = bytes([0xA5]) * 256
def restore(u, st):
    for a, s, v in st['w']:
        if ARENA <= a < ARENA + ARENA_SZ: u.mem_write(a, st['arena'][a:a + s])
    for a in st['poison']: u.mem_write(a, st['arena'][a:a + 256])
    st['w'].clear(); st['fault'] = None; st['rz'] = False; st['r'].clear(); st['deep'].clear(); st['poison'].clear()

def state(u, st, patched):
    g = [u.reg_read(r) for r in GP]
    tp = u.reg_read(UC_ARM64_REG_TPIDR_EL0)
    if patched: g[18] = struct.unpack('<Q', u.mem_read(TSD + 8 * SLOT18, 8))[0]; tp = struct.unpack('<Q', u.mem_read(TSD + 8 * SLOTTLS, 8))[0]
    g = g + [tp]
    w = list(st['w'])   # patched-side slot writes are dropped in trial(), only where the original wrote nothing (a random address can land in the TSD area)
    return g, u.reg_read(UC_ARM64_REG_SP), u.reg_read(UC_ARM64_REG_NZCV), [u.reg_read(r) for r in VR], w, st['fault']

def run(u, st, start, patched, init):
    restore(u, st)
    for r, v in zip(GP, init['g']): u.reg_write(r, v)
    for r, v in zip(VR, init['v']): u.reg_write(r, v)
    u.reg_write(UC_ARM64_REG_SP, SP0); u.reg_write(UC_ARM64_REG_NZCV, init['nzcv'])
    u.reg_write(UC_ARM64_REG_TPIDR_EL0, TLSB if ANDROID and patched else init['tls'])
    if not patched: u.reg_write(UC_ARM64_REG_TPIDRRO_EL0, init['tlr'])
    st['site'] = start - IB
    if patched:
        for key, idx in KEY_IDX.items():
            u.mem_write(IB + KEY_PAGE_OFF + 8 * idx, struct.pack('<Q', KEY_SLOT[key] * 8))
        u.mem_write(TSD + 8 * SLOT18, struct.pack('<Q', init['g'][18]))
        u.mem_write(TSD + 8 * SLOTTLS, struct.pack('<Q', init['tls']))
        u.mem_write(TSD + 8 * SLOTTLR, struct.pack('<Q', init['tlr']))
        u.reg_write(GP[18], 0xDEADBEEFDEADBEEF)
        if start - IB in piece_start:                                  # what the section's prologue leaves: S / T saved below sp, S = x18, T = slot address
            a0_, cp, S, T, nrz_ = piece_start[start - IB]; st['site'] = a0_
            u.mem_write(SP0 - 16, struct.pack('<QQ', u.reg_read(S), u.reg_read(T))); st['w'].append((SP0 - 16, 16, 0))   # the record lets restore() put the arena back
            if nrz_: u.reg_write(UC_ARM64_REG_SP, SP0 - 16)           # (android: the pair at the moved sp)
            u.reg_write(S, init['g'][18]); u.reg_write(T, TSD + 8 * SLOT18)
            start = IB + cp
    end = {'pc': None}
    if patched:
        seen = {'patch': False}
        def hk(uc, addr, size, d):
            if addr >= IB + PATCH_OFF: seen['patch'] = True
            elif seen['patch']: end['pc'] = addr; uc.emu_stop()
        if start >= IB + PATCH_OFF: seen['patch'] = True
        h = u.hook_add(UC_HOOK_CODE, hk)
        try: u.emu_start(start, IB + 0x7FFFFFFF, count=400)
        except UcError: pass
        u.hook_del(h)
        if end['pc'] is None and st['fault'] is None: end['pc'] = 'NOEXIT'
    elif start - IB in inside:
        ins_ = inside[start - IB]
        def hk2(uc, addr, size, d):
            if addr not in ins_: end['pc'] = addr; uc.emu_stop()
        h = u.hook_add(UC_HOOK_CODE, hk2)
        try: u.emu_start(start, IB + 0x7FFFFFFF, count=400)
        except UcError: pass
        u.hook_del(h)
        if end['pc'] is None and st['fault'] is None: end['pc'] = 'NOEXIT'
    else:
        try: u.emu_start(start, IB + 0x7FFFFFFF, count=1)
        except UcError: pass
        end['pc'] = u.reg_read(UC_ARM64_REG_PC) if st['fault'] is None else None
    s = state(u, st, patched)
    return s + (end['pc'],)

TEXT_END = hdr['TEXT_END']
def init_state(rng, kind, site_br=None, fn_target=False, tab=None):
    g = []
    for i in range(31):
        if kind == 0: g.append(rng.randrange(0, 0x10000) & ~7)
        elif kind == 1: g.append(rng.randrange(0, ARENA_SZ - 0x10000))
        else: g.append(rng.getrandbits(64))
    if site_br: g[18] = IB + site_br[0] + 4 * rng.randrange(site_br[1])
    if fn_target: g[18] = IB + 4 * rng.randrange(TEXT_END // 4)    # function pointer: any text address, so both sides land on mapped code
    if tab: g[18] = IB + rng.choice(tab)                             # explicit jump table: one of its targets (any other value stops at the block's brk)
    return {'g': g, 'v': [rng.getrandbits(128) for _ in range(32)], 'nzcv': rng.randrange(16) << 28, 'tls': rng.randrange(0, 0x100000) & ~15, 'tlr': rng.randrange(0x100000, 0x200000) & ~15}

# The patched side's own writes: its scratch registers in the red zone, and one 8-byte store into a slot a rewrite block writes (x18's, the thread
# pointer's; the other keys' slots are only read). Any other write into the slot area is the block's and is compared: a block that also stored into
# a wrong slot passed while the filter took all of [TSD, TSD + 64 KiB) (plan item 129; dr3-mobile's third_party/d2r-verify/equiv.py, MIT, drops
# only 8-byte writes to key slots; here the slot three past x18's, the review's fixture, is the gate key's, so all key slots would still pass it)
OWN_SLOT_ADDR = {TSD + 8 * SLOT18, TSD + 8 * SLOTTLS}
def tsd_slot_write(x):
    if os.environ.get('DM_TEST_EQUIV_TSD_WIDE'): return TSD <= x[0] < TSD + 0x10000   # negative control of t_rewriter_synth only: the old filter
    return x[1] == 8 and x[0] in OWN_SLOT_ADDR
def trial(UO, SO, UP, SP, a, init):
    so = run(UO, SO, IB + a, False, init)
    if SO['rz']: return True, False, [], True       # the original read below sp: not a state real code can be in
    sp_ = run(UP, SP, IB + a, True, init)
    if a in fnptr:   # the function pointer block leaves the target in x17 (IP1, not preserved across a call / tail call by AAPCS64)
        g = list(sp_[0]); g[17] = so[0][17]; sp_ = (g,) + sp_[1:]
    ow = {x[0] for x in so[4]}
    own = 512 if ANDROID else 128   # the block's saved pair (android: below a pre-index push it re-addressed)
    sp_ = sp_[:4] + ([x for x in sp_[4] if not ((SP0 - own <= x[0] < SP0 or tsd_slot_write(x)) and x[0] not in ow)],) + sp_[5:]
    both = bool(so[5] and sp_[5])
    same = (so[5] == sp_[5]) if (so[5] or sp_[5]) else (so == sp_)
    deep = [x for x in SP['deep'] if x not in ow and x not in SO['r']]
    if deep and not both: return False, both, [k for k, (x, y) in enumerate(zip(so, sp_)) if x != y] + ['redzone'], False
    return same, both, [k for k, (x, y) in enumerate(zip(so, sp_)) if x != y], False

def work(job):
    idx, chunk, mutated = job
    rng = random.Random(A.seed * 1000003 + idx * 2 + mutated)
    arena0 = rng.randbytes(ARENA_SZ)
    UO, SO = mk(orig, arena0); UP, SP = mk(pat_mut if mutated else pat, arena0, True)
    out = []
    for a in chunk:
        n = eff = nrz = 0; diffs = []
        kinds = [0, 1] + ([] if a in brinfo else [2])
        extra = 0
        while kinds or (eff < A.min_eff and extra < A.max_extra):
            if kinds: kind = kinds.pop(0)
            else: kind = (1, 0)[extra % 2]; extra += 1
            same, both, fl, rz = trial(UO, SO, UP, SP, a, init_state(rng, kind, brinfo.get(a), a in fnptr, brtab.get(a)))
            n += 1; eff += not (both or rz); nrz += rz
            if not same: diffs.append((kind, fl))
            if mutated and diffs: break
        if not mutated:
            for c, wo, to, cp, wp, tp_ in win_br.get(a, ()):
                for mode in ('not-taken', 'taken'):
                    UO.mem_write(IB + c, struct.pack('<I', 0xD503201F if mode == 'not-taken' else enc_b(c, to)))
                    UP.mem_write(IB + cp, struct.pack('<I', 0xD503201F if mode == 'not-taken' else enc_b(cp, tp_)))
                    UO.ctl_remove_cache(IB + c, IB + c + 4); UP.ctl_remove_cache(IB + cp, IB + cp + 4)
                    for kind in (0, 1, 0, 1):
                        same, both, fl, rz = trial(UO, SO, UP, SP, a, init_state(rng, kind))
                        n += 1; eff += not (both or rz); nrz += rz
                        if not same: diffs.append((f'window-branch {c:#x} forced-{mode}', fl))
                    UO.mem_write(IB + c, struct.pack('<I', wo)); UP.mem_write(IB + cp, struct.pack('<I', wp))
                    UO.ctl_remove_cache(IB + c, IB + c + 4); UP.ctl_remove_cache(IB + cp, IB + cp + 4)
        out.append((a, n, eff, diffs, nrz))
    return out

def runall(lst, mutated):
    k = 64                                   # Chunk count is fixed, so the result varies only with seed, not with jobs
    jobs = [(i, lst[i::k], mutated) for i in range(k) if lst[i::k]]
    with mp.get_context('fork').Pool(A.jobs) as p:
        return [r for part in p.imap_unordered(work, jobs) for r in part]

def ins_of(a): return next(md.disasm(orig[a:a + 4], a))
if __name__ == '__main__':
    res = sorted(runall(sites, 0))
    T = sum(r[1] for r in res); E = sum(r[2] for r in res); RZ = sum(r[4] for r in res)
    zero = [r for r in res if r[2] == 0]; low = [r for r in res if r[2] < A.min_eff]
    bad = [r for r in res if r[3]]
    for a, n, eff, diffs, _ in bad:
        i = ins_of(a)
        for kind, fl in diffs: print(f'DIFF {a:#x} {i.mnemonic} {i.op_str} kind={kind} fields={fl}', flush=True)
    for a, n, eff, _, _ in zero:
        i = ins_of(a); print(f'NOEFF {a:#x} {i.mnemonic} {i.op_str} trials={n}')
    caught = miss = 0; mut_by = '-'
    if mut_sites:
        mres = runall(mut_sites, 1)
        caught = sum(1 for r in mres if r[3])
        for a, n, eff, d, _ in mres:
            if not d: i = ins_of(a); print(f'MUTMISS {a:#x} {mut_kind[a]} {i.mnemonic} {i.op_str} trials={n} eff={eff}')
        miss = len(mres) - caught
        mk_ = {}
        for a, n, eff, d, _ in mres: c = mk_.setdefault(mut_kind[a], [0, 0]); c[0] += 1; c[1] += bool(d)
        mut_by = ' '.join(f'{k}:{v[1]}/{v[0]}' for k, v in sorted(mk_.items()))
    if A.tsv:
        with open(A.tsv, 'w') as f:
            f.write('site\ttrials\teff\tdiffs\n')
            for a, n, eff, d, _ in res: f.write(f'{a:#x}\t{n}\t{eff}\t{len(d)}\n')
    ok = not bad and not zero and not miss   # A missed mutation = the criterion cannot tell a bad rewrite apart, so fail (earlier a miss was only printed, not judged)
    if ANDROID: print(f'ANDROID {MOD} signal frames below sp before every block instruction; red-zone sites (belowsp.tsv) {len(BELOW)}')
    print(f'EQUIV {MOD} {"OK" if ok else "FAIL"} sites={len(res)} trials={T} eff_trials={E} both_fault={T - E - RZ} redzone_reads={RZ} '
          f'sites_eff_ge1={len(res) - len(zero)} sites_eff_ge{A.min_eff}={len(res) - len(low)} zero_eff={len(zero)} diff_sites={len(bad)} '
          f'windows={len(winend)} pieces_sections={len(secs)}(piece_starts={len(piece_start)}) explicit_jump_tables={len(brtab)} cond_windows={len(set(win_br) - set(piece_start))}(forced_branches={sum(len(v) for a, v in win_br.items() if a not in piece_start)}x2) gates(structck)={ngate} cuts(structck)={ncut} obs_gates(structck)={nobs} mutations={len(mut_sites)}({mut_by}) caught={caught} missed={miss} seed={A.seed}', flush=True)
    sys.exit(0 if ok else 1)
