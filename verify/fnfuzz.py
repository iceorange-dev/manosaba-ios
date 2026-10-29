# Function-level differential fuzzing bench: for each function containing a rewrite site, the original Switch image run as a whole function in Unicorn = ground truth, the rewritten image run as a whole function with the same input = under test; final states are compared.
# Usage: fnfuzz.py <rewrite build output dir> <module> <NSO> [--seed 1] [--trials 6] [--budget 200000] [--jobs N] [--mutate 200] [--funcs a,b,..] [--tsv file]
# Loading: both images are loaded at the same address with the same RELATIVE/ABS64/GLOB_DAT/JUMP_SLOT relocations applied; imported symbols point at a trap page (16 bytes per symbol).
# A trial stops on one of these events: return sentinel (ret), call to an import (import:name), host gate (gate: where the original image executes svc / reads cntpct, the rewritten image enters the KGATE sentinel), memory access fault, instruction budget exhausted.
# Only when both sides stop on the same event (ret / same-named import / same gate original address) is the trial "effective"; then compare: x0–x30 (x18 against the slot value), sp, nzcv, v0–v31, tpidr,
# the final values of all written addresses (dead stack below the current sp and the TSD are not compared). The extra x29/x30 the rewritten image pushes at a gate are restored per the template before comparing.
# Non-equivalence is reported as DIFF; functions with 0 effective trials are listed as NOEFF (a coverage gap, not a failure but must be reported). If sp is outside the stack area when either side stops, it is recorded as "sp_oob" (sp out of bounds) and not compared (unrealistic input).
# The android target (patch_nso.py DM_TARGET, plan item 301b): the rewritten side's tpidr_el0 points at a bionic TLS area whose slot 2 (+16) holds the slot
# block, and before every instruction of the patch area the 256 bytes below the current sp are overwritten (a signal frame: Linux has no red zone), except
# in functions with a site listed in belowsp.tsv (the red-zone form). A byte overwritten so and not written since is left out of the memory comparison: it was
# dead stack then, and what the original holds there is as stale (a callee's frame, later under another callee's frame that never wrote it).
# Mutation self-check: draw mutate ordinary rewrite blocks and change the replaced instruction to nop, then count how many of those "actually executed" are judged divergent.
import sys, os, struct, random, argparse, bisect, multiprocessing as mp
from unicorn import *
from unicorn.arm64_const import *
from capstone import *
sys.path.insert(0, os.path.dirname(__file__))
import nsoimg
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True

ap = argparse.ArgumentParser()
ap.add_argument('dir'); ap.add_argument('mod'); ap.add_argument('nso')
ap.add_argument('--seed', type=int, default=1); ap.add_argument('--trials', type=int, default=6)
ap.add_argument('--budget', type=int, default=200000); ap.add_argument('--jobs', type=int, default=os.cpu_count())
ap.add_argument('--mutate', type=int, default=200); ap.add_argument('--funcs'); ap.add_argument('--tsv')
A = ap.parse_args()

IB = 0x100000000
ARENA, ARENA_SZ = 0x10000000, 0x1000000      # Random heap: half of the 8-byte words are pointers back into the heap
STK, STK_SZ = 0x20000000, 0x100000; SP0 = STK + STK_SZ - 0x10000   # 64 KB kept above sp for stack-passed arguments
TSD = 0x30000000
TRAP = 0x300000000; RET = 0x400000000; GATE = 0x500000000
hdr = nsoimg.header(A.dir, A.mod)
PO, TE = hdr['PATCH_OFF'], hdr['TEXT_END']
KEY_IDX = {k[4:-4]: v for k, v in hdr.items() if k.startswith('KEY_') and k.endswith('_IDX')}
KEY_SLOT = {k: 300 + i for i, k in enumerate(sorted(KEY_IDX, key=KEY_IDX.get))}
K18, KTLS, KTLR, KGATE = (KEY_SLOT[k] for k in ('K18', 'KTLS', 'KTLR', 'KGATE'))
KEY_PAGE_OFF = hdr['KEY_PAGE_OFF']
ANDROID = hdr.get('TARGET_ANDROID') == 1
BELOW = {int(l, 16) for l in open(f'{A.dir}/{A.mod}.belowsp.tsv') if not l.startswith('#')} if ANDROID else set()
TLSB = 0x31000000; POISON = bytes([0xA5]) * 256; ONES = bytes([1]) * 256
orig = bytearray(nsoimg.load(A.nso))
pat = bytearray(open(f'{A.dir}/{A.mod}.patched.bin', 'rb').read())

# ---- Relocations (identical for both images) ----
imports = {}
for off, typ, name, val, add in nsoimg.relocs(bytes(orig)):
    if typ == 1027: v = IB + add
    elif val is not None: v = IB + val + (add if typ == 257 else 0)
    else: v = TRAP + 16 * imports.setdefault(name, len(imports)) + (add if typ == 257 else 0)
    for b in (orig, pat): struct.pack_into('<Q', b, off, v & (2**64 - 1))
iname = {i: n for n, i in imports.items()}
TRAP_SZ = (16 * len(imports) + 0xFFFF) & ~0xFFFF or 0x10000

# ---- Rewrite sites and functions ----
sites = {}
for l in open(f'{A.dir}/{A.mod}.sites.tsv'):
    f = l.split()
    if f[1] != 'J': sites[int(f[0], 16)] = f[1]
gsites = {a for a, k in sites.items() if k in ('svc', 'cntpct', 'cntvct')}
# Cache type reads are replaced by a constant in place (an intended value change, structck ⑫): the original image gets the same word, so both sides read it
for a, k in sites.items():
    if k == 'ctr': struct.pack_into('<I', orig, a, struct.unpack_from('<I', pat, a)[0])
starts = nsoimg.func_starts(bytes(orig), TE)
def func_of(a): return starts[bisect.bisect_right(starts, a) - 1]
funcs = sorted({func_of(a) for a in sites})
# Code caves (caves.tsv, plan item 139) change what their function does on purpose: those functions are the game's tests' to check, not compared here
_cp = f'{A.dir}/{A.mod}.caves.tsv'
cave_funcs = {func_of(int(l.split()[0], 16)) for l in open(_cp) if not l.startswith('#')} if os.path.exists(_cp) else set()
funcs = [f for f in funcs if f not in cave_funcs]
below_funcs = {func_of(a) for a in BELOW}   # android: functions with a red-zone site, run without the signal frames
if A.funcs: funcs = [int(x, 16) for x in A.funcs.split(',')]

def btarget(b, o):
    w = struct.unpack_from('<I', b, o)[0]; imm = w & 0x3FFFFFF
    if imm & 0x2000000: imm -= 0x4000000
    return o + 4 * imm
def plain_subst(a):
    s = btarget(pat, a)
    ins = list(md.disasm(bytes(pat[s:s + 28]), s))
    if len(ins) < 5 or ins[0].mnemonic != 'stp' or ins[1].mnemonic != 'mrs' or ('tpidr_el0' if ANDROID else 'tpidrro_el0') not in ins[1].op_str: return None
    if ANDROID: ins = ins[:2] + ins[3:]   # (android: ldr xT, [xT, #0x10] after the mrs)
    if ins[2].mnemonic != 'adrp' or ins[2].operands[1].imm != KEY_PAGE_OFF: return None
    if ins[3].mnemonic != 'ldr' or ins[3].operands[1].mem.base != ins[2].operands[0].reg: return None
    if ins[4].mnemonic != 'add' or ins[4].operands[0].reg != ins[1].operands[0].reg or ins[4].operands[1].reg != ins[1].operands[0].reg: return None
    if len(ins) > 5 and ins[5].mnemonic in ('ldr', 'str') and ins[5].operands[1].mem.base == ins[4].operands[0].reg: return ins[5].address + 4
    return ins[4].address
mut = {}
if A.mutate:
    cand = [a for a, k in sites.items() if k == 'x18' and plain_subst(a)]
    for a in random.Random(A.seed).sample(cand, min(A.mutate, len(cand))): mut[a] = plain_subst(a)
    pat_mut = bytearray(pat)
    for a, s in mut.items(): struct.pack_into('<I', pat_mut, s, 0xD503201F)

GP = [getattr(sys.modules['unicorn.arm64_const'], f'UC_ARM64_REG_X{i}') for i in range(29)] + [UC_ARM64_REG_X29, UC_ARM64_REG_X30]
VR = [getattr(sys.modules['unicorn.arm64_const'], f'UC_ARM64_REG_Q{i}') for i in range(32)]
PAGE = lambda n: (n + 0xFFFF) & ~0xFFFF

class Emu:
    def __init__(self, image, patched, arena0):
        self.patched, self.image, self.arena0 = patched, bytes(image), arena0
        u = self.u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        u.mem_map(IB, PO); u.mem_write(IB, self.image[:PO])
        u.mem_map(IB + PO, PAGE(len(pat)) - PO, UC_PROT_EXEC)     # Patch area: same address and same permissions on both sides (all zeros in the original image), data reads and writes both fault
        if patched: u.mem_write(IB + PO, self.image[PO:])
        RW = UC_PROT_READ | UC_PROT_WRITE                      # Heap / stack / TSD are non-executable: when an indirect call lands in random data, both sides take the same instruction fetch fault
        u.mem_map(ARENA, ARENA_SZ, RW); u.mem_write(ARENA, arena0)
        u.mem_map(STK, STK_SZ, RW); u.mem_map(TSD, 0x10000, RW)
        self.poison, self.clobber = [], False
        self.pmask = bytearray(STK_SZ) if ANDROID and patched else None   # stack bytes overwritten by a signal frame and not written since
        if ANDROID and patched:
            u.mem_map(TLSB, 0x1000, RW)
            def clobber(uc, addr, size, d):   # a signal frame below the current sp before each patch-area instruction
                if not self.clobber: return
                sp = uc.reg_read(UC_ARM64_REG_SP)
                if STK + 256 <= sp <= STK + STK_SZ: uc.mem_write(sp - 256, POISON); self.poison.append(sp - 256); self.pmask[sp - 256 - STK:sp - STK] = ONES
            u.hook_add(UC_HOOK_CODE, clobber, begin=IB + PO, end=IB + PAGE(len(pat)) - 1)
        for base, sz in ((TRAP, TRAP_SZ), (RET, 0x10000), (GATE, 0x10000)): u.mem_map(base, sz, UC_PROT_EXEC | UC_PROT_READ)
        self.w = []; self.fault = None; self.stop = None; self.hit = set()
        self.wpatch, self.wcode = set(), set()   # bytes written from the patch area (rewrite blocks' red-zone scratch) / from anywhere else
        def wr(uc, acc, addr, size, val, d):
            self.w.append((addr, size))
            if self.pmask is not None and STK <= addr < STK + STK_SZ: self.pmask[addr - STK:addr - STK + size] = bytes(size)
            if self.patched: (self.wpatch if uc.reg_read(UC_ARM64_REG_PC) >= IB + PO else self.wcode).update(range(addr, addr + size))
        u.hook_add(UC_HOOK_MEM_WRITE, wr)
        def bad(uc, acc, addr, size, val, d): self.fault = (acc, addr); return False
        u.hook_add(UC_HOOK_MEM_INVALID, bad)
        def at(reason):
            def h(uc, addr, size, d): self.stop = reason(addr); uc.emu_stop()
            return h
        u.hook_add(UC_HOOK_CODE, at(lambda a: ('import', iname.get((a - TRAP) // 16))), begin=TRAP, end=TRAP + TRAP_SZ - 1)
        u.hook_add(UC_HOOK_CODE, at(lambda a: ('ret',)), begin=RET, end=RET + 0xFFFF)
        if patched:
            u.hook_add(UC_HOOK_CODE, at(lambda a: ('gate', self.gate_site())), begin=GATE, end=GATE + 0xFFFF)
            for a, s in mut.items():           # Whether the mutated block was executed
                u.hook_add(UC_HOOK_CODE, lambda uc, addr, size, d: self.hit.add(addr), begin=IB + s, end=IB + s)
        else:
            for g in gsites: u.hook_add(UC_HOOK_CODE, at(lambda a: ('gate', a - IB)), begin=IB + g, end=IB + g)
    def gate_site(self):
        # Gate block: stp x29,x30,[sp,#-16]! ; fetch KGATE via key page ; ldr x29,[sp] ; blr x30 ← return point, block ends with b original address+4
        r = self.u.reg_read(UC_ARM64_REG_X30) - IB
        return btarget(pat, r + 4) - 4
    def restore(self):
        for a, s in self.w:
            if ARENA <= a < ARENA + ARENA_SZ: self.u.mem_write(a, self.arena0[a - ARENA:a - ARENA + s])
            elif IB <= a < IB + PO: self.u.mem_write(a, self.image[a - IB:a - IB + s])
            elif STK <= a < STK + STK_SZ: self.u.mem_write(a, bytes(s))
        for a in self.poison: self.u.mem_write(a, bytes(256))
        if self.poison: self.pmask[:] = bytes(STK_SZ)
        self.poison.clear()
        self.w.clear(); self.wpatch.clear(); self.wcode.clear(); self.fault = None; self.stop = None
    def run(self, fn, init, budget):
        u = self.u; self.restore()
        for r, v in zip(GP, init['g']): u.reg_write(r, v)
        for r, v in zip(VR, init['v']): u.reg_write(r, v)
        u.reg_write(UC_ARM64_REG_X30, RET)
        u.reg_write(UC_ARM64_REG_SP, SP0); u.reg_write(UC_ARM64_REG_NZCV, init['nzcv'])
        u.reg_write(UC_ARM64_REG_FPCR, 0); u.reg_write(UC_ARM64_REG_FPSR, 0)   # Every trial starts from the same floating-point control / status, no leakage across functions
        u.mem_write(SP0, init['stk'])
        if self.patched:
            u.reg_write(UC_ARM64_REG_TPIDRRO_EL0, 0 if ANDROID else TSD)
            if ANDROID: u.mem_write(TLSB + 16, struct.pack('<Q', TSD)); self.clobber = fn not in below_funcs
            for key, idx in KEY_IDX.items():
                u.mem_write(IB + KEY_PAGE_OFF + 8 * idx, struct.pack('<Q', KEY_SLOT[key] * 8))
            for k, v in ((K18, init['g'][18]), (KTLS, init['tls']), (KTLR, init['tlr']), (KGATE, GATE)): u.mem_write(TSD + 8 * k, struct.pack('<Q', v))
            u.reg_write(UC_ARM64_REG_X18, 0xDEADBEEFDEADBEEF); u.reg_write(UC_ARM64_REG_TPIDR_EL0, TLSB if ANDROID else 0)
        else:
            u.reg_write(UC_ARM64_REG_TPIDRRO_EL0, init['tlr']); u.reg_write(UC_ARM64_REG_TPIDR_EL0, init['tls'])
        try: u.emu_start(IB + fn, 0, count=budget)
        except UcError: pass
        if self.stop is None: self.stop = ('fault', self.fault) if self.fault else ('budget',)
        return self.state()
    def state(self):
        u = self.u
        g = [u.reg_read(r) for r in GP]; sp = u.reg_read(UC_ARM64_REG_SP)
        tp = u.reg_read(UC_ARM64_REG_TPIDR_EL0)
        if self.patched:
            rq = lambda a: struct.unpack('<Q', u.mem_read(a, 8))[0]
            g[18] = rq(TSD + 8 * K18); tp = rq(TSD + 8 * KTLS)
            if self.stop[0] == 'gate': sp += 16   # The gate block has already restored x29 before blr (r26), live registers are compared directly; x30 is the gate return point and is cleared below
        if self.stop[0] in ('gate', 'import'): g[30] = None   # x30 at a gate / import is each side's own return point, not comparable
        if self.stop[0] == 'gate' and not self.patched: g[30] = None
        return {'stop': self.stop, 'g': g, 'sp': sp, 'nzcv': u.reg_read(UC_ARM64_REG_NZCV), 'tp': tp,
                'v': [u.reg_read(r) for r in VR]}

def mem_diff(eo, ep, sp):
    addrs, worig = set(), set()
    for a, s in eo.w: worig.update(range(a, a + s))
    for a, s in eo.w + ep.w:
        for x in range(a, a + s):
            if not (STK <= x < sp) and not (TSD <= x < TSD + 0x10000): addrs.add(x)
    # Red-zone scratch of the rewrite blocks: below the entry sp the stack holds nothing at entry, so a byte the original never wrote and the
    # rewritten image wrote only from the patch area is leftover scratch that a later frame (after sp moved down) has not initialized yet
    addrs -= {x for x in ep.wpatch - ep.wcode - worig if STK <= x < SP0}
    if ep.pmask is not None: addrs = {x for x in addrs if not (STK <= x < STK + STK_SZ and ep.pmask[x - STK])}
    for x in sorted(addrs):
        if eo.u.mem_read(x, 1) != ep.u.mem_read(x, 1): return x
    return None

def init_state(rng):
    def val():
        r = rng.random()
        if r < 0.4: return ARENA + (rng.randrange(ARENA_SZ - 0x10000) & ~7)
        if r < 0.7: return rng.randrange(0, 256)
        if r < 0.85: return rng.randrange(0, 1 << 32)
        return rng.getrandbits(64)
    return {'g': [val() for _ in range(31)], 'v': [rng.getrandbits(128) for _ in range(32)], 'nzcv': rng.randrange(16) << 28,
            'tls': ARENA + (rng.randrange(ARENA_SZ - 0x10000) & ~15), 'tlr': ARENA + (rng.randrange(ARENA_SZ - 0x10000) & ~15),
            'stk': b''.join(struct.pack('<Q', val()) for _ in range(64))}

def make_arena(rng):
    q = []
    for _ in range(ARENA_SZ // 8):
        q.append(ARENA + (rng.getrandbits(24) & ~7) if rng.getrandbits(1) else rng.getrandbits(rng.choice((8, 32, 64))))
    return struct.pack(f'<{len(q)}Q', *q)

ARENA0 = make_arena(random.Random(A.seed))
def work(job):
    idx, chunk, mutated = job
    eo = Emu(orig, False, ARENA0); ep = Emu(pat_mut if mutated else pat, True, ARENA0)
    out = []
    for fn in chunk:
        rng = random.Random(A.seed * 1000003 + fn)
        st = {'n': 0, 'eff': 0, 'stops': {}, 'diffs': [], 'hit': set()}
        for t in range(A.trials):
            init = init_state(rng)
            so = eo.run(fn, init, A.budget); ep.hit.clear()
            sp_ = ep.run(fn, init, A.budget * 4)          # The rewritten image runs a few more instructions per rewrite site
            st['n'] += 1; st['hit'] |= ep.hit
            k = so['stop'][0]; st['stops'][k] = st['stops'].get(k, 0) + 1
            if so['stop'][0] in ('budget',) or sp_['stop'][0] == 'budget': continue
            if not (STK <= so['sp'] <= STK + STK_SZ and STK <= sp_['sp'] <= STK + STK_SZ):
                st['stops']['sp_oob'] = st['stops'].get('sp_oob', 0) + 1; continue   # A variable-length stack allocation of random length pushes sp out of the stack area: cannot happen in a real run; the rewrite block uses the red zone below sp (Apple ABI, 128 bytes) and would fault first here
            if so['stop'][0] == 'fault' and sp_['stop'][0] == 'fault':
                if so['stop'][1][1] != sp_['stop'][1][1]: st['diffs'].append((t, 'fault_addr', so['stop'], sp_['stop']))
                continue
            if so['stop'] != sp_['stop']: st['diffs'].append((t, 'stop', so['stop'], sp_['stop'])); continue
            st['eff'] += 1
            fl = [k for k in ('g', 'sp', 'nzcv', 'tp', 'v') if so[k] != sp_[k]]
            if 'g' in fl: fl[fl.index('g')] = 'x' + ','.join(str(i) for i in range(31) if so['g'][i] != sp_['g'][i])
            m = mem_diff(eo, ep, so['sp'])
            if m is not None: fl.append(f'mem@{m:#x}')
            if fl: st['diffs'].append((t, 'final_state', so['stop'], fl))
        out.append((fn, st))
    return out

def runall(lst, mutated):
    k = 64
    jobs = [(i, lst[i::k], mutated) for i in range(k) if lst[i::k]]
    with mp.get_context('fork').Pool(A.jobs) as p:
        return sorted(r for part in p.imap_unordered(work, jobs) for r in part)

if __name__ == '__main__':
    res = runall(funcs, 0)
    n = sum(s['n'] for _, s in res); eff = sum(s['eff'] for _, s in res)
    stops = {}
    for _, s in res:
        for k, v in s['stops'].items(): stops[k] = stops.get(k, 0) + v
    bad = [(f, s) for f, s in res if s['diffs']]; noeff = [f for f, s in res if s['eff'] == 0]
    for f, s in bad:
        for d in s['diffs'][:3]: print(f'DIFF fn={f:#x} trial={d[0]} {d[1]} {d[2]} {d[3]}', flush=True)
    if A.tsv:
        with open(A.tsv, 'w') as fo:
            fo.write('func\ttrials\teff\tdiffs\tstops\n')
            for f, s in res: fo.write(f"{f:#x}\t{s['n']}\t{s['eff']}\t{len(s['diffs'])}\t{s['stops']}\n")
    hit = caught = 0
    if mut:
        mf = sorted({func_of(a) for a in mut})
        mres = dict(runall(mf, 1))
        for a, sa in mut.items():
            s = mres[func_of(a)]
            if IB + sa in s['hit']:
                hit += 1; caught += bool(s['diffs'])
    ok = not bad
    print(f'FNFUZZ {A.mod} {"OK" if ok else "FAIL"} funcs={len(res)} total_trials={n} eff_trials={eff} stops={stops} funcs_eff_ge1={len(res) - len(noeff)} '
          f'zero_eff_funcs={len(noeff)} diff_funcs={len(bad)} mutations={len(mut)} executed={hit} caught={caught} seed={A.seed} trials={A.trials}', flush=True)
    sys.exit(0 if ok else 1)
