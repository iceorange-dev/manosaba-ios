#!/usr/bin/env python3
"""Instruction semantics of the shader translator, run on the GPU: each case is a tiny Maxwell fragment program (MOV loads the inputs from constant
bank 3, so no compiler can fold them; then the instruction under test; R0 is colour target 0 as uint) translated as the game's shaders are
(gfx/tools/dmsh_xlate, then spirv-cross with dmsh_build.py's MSL options, minus argument buffers so the bank binds as a plain buffer), drawn by
t_xlate_run.m and compared with the value worked out from the instruction's definition.
  FMUL / FFMA .FMZ: a zero factor gives a zero product, also against infinity and NaN (D3D9 rules; yuzu and tomodachi-native a0aa6c2 do the same);
  without .FMZ, and with .FTZ alone, IEEE (NaN).
  I2I and SHF: LASRecomp d4c2ff6's vectors (MIT; I2I .SAT compares in the source's signedness, narrowing without .SAT keeps the low bits
  zero-extended; SHF .W wraps the shift, otherwise it is clamped to the width, 32 or 64), plus shifts of 0 and of exactly 32.
  LDC.64 with the address register among its destinations (Rd or Rd+1; LASRecomp a179452's cases, MIT; plan item 111; 2,069 of Tears of the
  Kingdom's shaders use the form): both words come from the address as it was. Bank 3 holds 8, 0x11111111, 0x22222222, 4, 0x44444444 at words 0-4,
  so c[3][8 + 4] is 4 and the high word 0x44444444; a translation that overwrote the address first would read the high word at 4 + 4 + 4: 4 again.
  DM_TEST_LDC_BUG=1 runs that wrong order instead (two 32-bit loads, the second from the address register after the first wrote it): 1 fails.
  HSETP2 .H_AND: LASRecomp d4c2ff6's five vectors (MIT; yuzu's rule, plan item 64g): each half is combined with the predicate, then the two are
  ANDed, the second result its complement; R0 is 1 where the predicate read is set. The translator before batch 150 (the halves ANDed first, the
  predicate after): 2 fail.
Usage: t_xlate_ops.py [work dir]; XLATE = the translator (default $DM_ROOT/build/gfx/dmsh_xlate/dmsh_xlate; DOTNET_ROOT defaults to Homebrew's),
needs spirv-cross, clang and a Metal GPU."""
import os, re, struct, subprocess, sys, tempfile, json

HERE = os.path.dirname(os.path.abspath(__file__))
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')
XLATE = os.environ.get('XLATE', f'{DM_ROOT}/build/gfx/dmsh_xlate/dmsh_xlate')
MSL = ['--msl', '--msl-version', '20300', '--msl-texture-buffer-native', '--msl-pad-fragment-output']   # dmsh_build.py XC without --msl-argument-buffers

PT = 7 << 16   # predicate guard PT
def mov_c(d, word): return 0x4C98000000000000 | 0xF << 39 | 3 << 34 | word << 20 | PT | d   # MOV Rd, c[3][4 * word]
EXIT = 0xE30000000007000F
def mov_r(d, b): return 0x5C98000000000000 | 0xF << 39 | b << 20 | PT | d   # MOV Rd, Rb
def ldc(d, a, idx, off, size): return 0xEF90000000000000 | PT | d | a << 8 | (off & 0xFFFF) << 20 | idx << 36 | size << 48   # size 4: 32-bit, 5: .64 (Rd, Rd+1)
def ldc64(d, a, idx, off):   # LDC.64, or with DM_TEST_LDC_BUG=1 the wrong order: the low word written before the high word's address is read
    return [ldc(d, a, idx, off, 4), ldc(d + 1, a, idx, off + 4, 4)] if os.environ.get('DM_TEST_LDC_BUG') == '1' else [ldc(d, a, idx, off, 5)]
def fmul(d, a, b, fmz=0, sat=False): return 0x5C68000000000000 | PT | d | a << 8 | b << 20 | fmz << 44 | sat << 50
def ffma(d, a, b, c, fmz=0, sat=False): return 0x5980000000000000 | PT | d | a << 8 | b << 20 | c << 39 | sat << 50 | fmz << 53
def fmul32i(d, a, imm, fmz=0): return 0x1E00000000000000 | PT | d | a << 8 | imm << 20 | fmz << 53
def ffma32i(d, a, imm, fmz=0): return 0x0C00000000000000 | PT | d | a << 8 | imm << 20 | fmz << 53   # C is Rd
def i2i(d, b, dsz, dsg, ssz, ssg, sat=False, abs_=False, neg=False):
    return 0x5CE0000000000000 | PT | d | dsz << 8 | ssz << 10 | dsg << 12 | ssg << 13 | b << 20 | neg << 45 | abs_ << 49 | sat << 50
def hsetp2(pa, pb, a, b, cmp, p, pneg, bop, hand):   # HSETP2 Pa, Pb, Ra, Rb (both halves), compare cmp, combined with predicate p (pneg: !p) by bop
    return 0x5D20000000000000 | PT | pb | pa << 3 | a << 8 | b << 20 | cmp << 35 | p << 39 | pneg << 42 | bop << 45 | hand << 49
def mov_if(d, b, p): return 0x5C98000000000000 | 0xF << 39 | b << 20 | p << 16 | d   # @Pp MOV Rd, Rb
GT, LT, AND, OR = 4, 1, 0, 1
def shf(right, d, a, sreg, imm, c, width, wrap):   # SHF.{L,R} Rd, Ra (low), shift (Rs or imm6), Rc (high); width 0 32-bit, 2 .U64, 3 .S64; wrap = .W
    w = ((0x5CF8 if right else 0x5BF8) << 48 | sreg << 20) if sreg >= 0 else ((0x38F8 if right else 0x36F8) << 48 | (imm & 63) << 20)
    return w | PT | d | a << 8 | width << 37 | c << 39 | wrap << 50
FMZ, FTZ = 2, 1
ZERO, NEG0, INF, NAN, ONE, TWO, THREE, FIVE, SIX, SEVEN = 0, 0x80000000, 0x7F800000, 0x7FC00000, 0x3F800000, 0x40000000, 0x40400000, 0x40A00000, 0x40C00000, 0x40E00000
LO, HI = 0x89ABCDEF, 0x01234567
HI0 = 0x76543210   # shares no bit with LO: a funnel shift of 0 that ORs both words in gives 0xffffffff
LDCB = [8, 0x11111111, 0x22222222, 4, 0x44444444]   # bank 3 for the LDC cases (R1-R5 too)

def is_zero(v): return v & 0x7FFFFFFF == 0
def is_nan(v): return v & 0x7F800000 == 0x7F800000 and v & 0x7FFFFF != 0
# (name, inputs loaded into R1, R2, R3, ..., instructions, check): check is a value (exact) or a function of the result
CASES = [
    ('FMUL 2 x 3', [TWO, THREE], [fmul(0, 1, 2)], SIX),
    ('FMUL 0 x Inf (IEEE: NaN)', [ZERO, INF], [fmul(0, 1, 2)], is_nan),
    ('FMUL.FTZ 0 x Inf (NaN: FTZ only flushes denormals)', [ZERO, INF], [fmul(0, 1, 2, FTZ)], is_nan),
    ('FMUL.FMZ 0 x Inf', [ZERO, INF], [fmul(0, 1, 2, FMZ)], is_zero),
    ('FMUL.FMZ Inf x -0', [INF, NEG0], [fmul(0, 1, 2, FMZ)], is_zero),
    ('FMUL.FMZ 0 x NaN', [ZERO, NAN], [fmul(0, 1, 2, FMZ)], is_zero),
    ('FMUL.FMZ.SAT 0 x Inf', [ZERO, INF], [fmul(0, 1, 2, FMZ, True)], is_zero),
    ('FMUL.FMZ 2 x 3', [TWO, THREE], [fmul(0, 1, 2, FMZ)], SIX),
    ('FMUL32I.FMZ Inf x 0', [INF], [fmul32i(0, 1, ZERO, FMZ)], is_zero),
    ('FFMA 0 x Inf + 5 (IEEE: NaN)', [ZERO, INF, FIVE], [ffma(0, 1, 2, 3)], is_nan),
    ('FFMA.FMZ 0 x Inf + 5', [ZERO, INF, FIVE], [ffma(0, 1, 2, 3, FMZ)], FIVE),
    ('FFMA.FMZ NaN x -0 + 1', [NAN, NEG0, ONE], [ffma(0, 1, 2, 3, FMZ)], ONE),
    ('FFMA.FMZ 2 x 3 + 1', [TWO, THREE, ONE], [ffma(0, 1, 2, 3, FMZ)], SEVEN),
    ('FFMA32I.FMZ Inf x 0 + 5', [INF, FIVE], [mov_c(0, 1), ffma32i(0, 1, ZERO, FMZ)], FIVE),
    ('I2I.U8.U32.SAT 0xffffffff', [0xFFFFFFFF], [i2i(0, 1, 0, 0, 2, 0, True)], 0xFF),
    ('I2I.U16.U32.SAT 0x12345', [0x12345], [i2i(0, 1, 1, 0, 2, 0, True)], 0xFFFF),
    ('I2I.S16.U32.SAT 0x80000000', [0x80000000], [i2i(0, 1, 1, 1, 2, 0, True)], 0x7FFF),
    ('I2I.S32.U32.SAT 0x80000000', [0x80000000], [i2i(0, 1, 2, 1, 2, 0, True)], 0x7FFFFFFF),
    ('I2I.U32.S32.SAT -1', [0xFFFFFFFF], [i2i(0, 1, 2, 0, 2, 1, True)], 0),
    ('I2I.U32.S32.SAT 5', [5], [i2i(0, 1, 2, 0, 2, 1, True)], 5),
    ('I2I.S8.S32.SAT -200', [(-200) & 0xFFFFFFFF], [i2i(0, 1, 0, 1, 2, 1, True)], 0xFFFFFF80),
    ('I2I.U16.S32.SAT 70000', [70000], [i2i(0, 1, 1, 0, 2, 1, True)], 0xFFFF),
    ('I2I.S8.S32 0x1ff (low byte, zero-extended)', [0x1FF], [i2i(0, 1, 0, 1, 2, 1)], 0xFF),
    ('I2I.S32.S32 |-5|', [(-5) & 0xFFFFFFFF], [i2i(0, 1, 2, 1, 2, 1, abs_=True)], 5),
    ('SHF.R.W 4', [LO, HI, 4], [shf(True, 0, 1, 3, 0, 2, 0, True)], 0x789ABCDE),
    ('SHF.R.W 36 (wraps to 4)', [LO, HI, 36], [shf(True, 0, 1, 3, 0, 2, 0, True)], 0x789ABCDE),
    ('SHF.R 40 (clamps to 32)', [LO, HI, 40], [shf(True, 0, 1, 3, 0, 2, 0, False)], HI),
    ('SHF.L 40 (clamps to 32)', [LO, HI, 40], [shf(False, 0, 1, 3, 0, 2, 0, False)], LO),
    ('SHF.L.W 8', [LO, HI, 8], [shf(False, 0, 1, 3, 0, 2, 0, True)], 0x23456789),
    ('SHF.R.U64 imm 36', [LO, HI], [shf(True, 0, 1, -1, 36, 2, 2, False)], 0x00123456),
    ('SHF.R.U64 imm 4', [LO, HI], [shf(True, 0, 1, -1, 4, 2, 2, False)], 0x789ABCDE),
    ('SHF.R.U64 70 (clamps to 64)', [LO, HI, 70], [shf(True, 0, 1, 3, 0, 2, 2, False)], 0),
    ('SHF.R.S64 40', [0, 0xF0000000, 40], [shf(True, 0, 1, 3, 0, 2, 3, False)], 0xFFF00000),
    ('SHF.R.S64 70 (clamps to 64)', [0, 0xF0000000, 70], [shf(True, 0, 1, 3, 0, 2, 3, False)], 0xFFFFFFFF),
    ('SHF.L.U64 40', [LO, HI, 40], [shf(False, 0, 1, 3, 0, 2, 2, False)], 0xABCDEF00),
    ('SHF.L.U64 64 (clamps to 64)', [LO, HI, 64], [shf(False, 0, 1, 3, 0, 2, 2, False)], 0),
    ('SHF.L.U64.W 72 (wraps to 8)', [LO, HI, 72], [shf(False, 0, 1, 3, 0, 2, 2, True)], 0x23456789),
    ('SHF.L.U64 imm 40', [LO, HI], [shf(False, 0, 1, -1, 40, 2, 2, False)], 0xABCDEF00),
    ('SHF.R 0', [LO, HI0, 0], [shf(True, 0, 1, 3, 0, 2, 0, False)], LO),
    ('SHF.L 0', [LO, HI0, 0], [shf(False, 0, 1, 3, 0, 2, 0, False)], HI0),
    ('SHF.R.W 32 (wraps to 0)', [LO, HI0, 32], [shf(True, 0, 1, 3, 0, 2, 0, True)], LO),
    ('SHF.R 32', [LO, HI, 32], [shf(True, 0, 1, 3, 0, 2, 0, False)], HI),
    ('SHF.L 32', [LO, HI, 32], [shf(False, 0, 1, 3, 0, 2, 0, False)], LO),
    ('SHF.R.U64 0', [LO, HI0, 0], [shf(True, 0, 1, 3, 0, 2, 2, False)], LO),
    ('SHF.L.U64 0', [LO, HI0, 0], [shf(False, 0, 1, 3, 0, 2, 2, False)], HI0),
    ('SHF.R imm 0', [LO, HI0], [shf(True, 0, 1, -1, 0, 2, 0, False)], LO),
    ('SHF.L.W 32 (wraps to 0)', [LO, HI0, 32], [shf(False, 0, 1, 3, 0, 2, 0, True)], HI0),
    ('LDC.64 R0, c[3][R0+4] (low)', LDCB, [mov_r(0, 1), *ldc64(0, 0, 3, 4)], 4),
    ('LDC.64 R0, c[3][R0+4] (high, from the address as it was)', LDCB, [mov_r(0, 1), *ldc64(0, 0, 3, 4), mov_r(0, 1)], 0x44444444),
    ('LDC.64 R0, c[3][R1+4] (low)', LDCB, [*ldc64(0, 1, 3, 4)], 4),
    ('LDC.64 R0, c[3][R1+4] (high, address in Rd+1)', LDCB, [*ldc64(0, 1, 3, 4), mov_r(0, 1)], 0x44444444),
    ('LDC R0, c[3][R0+4]', LDCB, [mov_r(0, 1), ldc(0, 0, 3, 4, 4)], 4),
    ('HSETP2.GT.H_AND.AND !PT (first)', [0x3C003C00, 0, 1], [mov_r(0, 2), hsetp2(0, 1, 1, 2, GT, 7, True, AND, True), mov_if(0, 3, 0)], 0),
    ('HSETP2.GT.H_AND.AND !PT (second)', [0x3C003C00, 0, 1], [mov_r(0, 2), hsetp2(0, 1, 1, 2, GT, 7, True, AND, True), mov_if(0, 3, 1)], 1),
    ('HSETP2.GT.H_AND.OR PT (first)', [0x00003C00, 0, 1], [mov_r(0, 2), hsetp2(0, 1, 1, 2, GT, 7, False, OR, True), mov_if(0, 3, 0)], 1),
    ('HSETP2.GT.H_AND.OR PT (second)', [0x00003C00, 0, 1], [mov_r(0, 2), hsetp2(0, 1, 1, 2, GT, 7, False, OR, True), mov_if(0, 3, 1)], 0),
    ('HSETP2.LT.AND PT (per half)', [0xBC000000, 0, 1], [mov_r(0, 2), hsetp2(0, 1, 1, 2, LT, 7, False, AND, False), mov_if(0, 3, 1)], 1),
]

def program(inputs, insns):
    """0x50-byte SPH (pixel shader, version 3; colour target 0's red from R0) and the code: a scheduling word before every three instructions"""
    sph = [0] * 20; sph[0] = 2 | 3 << 5 | 5 << 10; sph[18] = 0x1
    body = [mov_c(1 + i, i) for i in range(len(inputs))] + insns + [EXIT]
    code = []
    for i in range(0, len(body), 3):
        grp = body[i:i + 3]; code += [0x001FC400FE2007F6] + grp + [0x50B0000000070F00] * (3 - len(grp))   # NOP pads the last group
    return struct.pack('<20I', *sph) + struct.pack(f'<{len(code)}Q', *code)

def main():
    if not os.path.exists(XLATE): print(f't_xlate_ops: skipped, no translator at {XLATE} (set DM_ROOT or XLATE)'); return 0
    work = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix='t_xlate_ops.')
    os.makedirs(work, exist_ok=True)
    jobs = []
    for k, (name, inputs, insns, _) in enumerate(CASES):
        p = os.path.join(work, f'c{k:02d}.bin'); open(p, 'wb').write(program(inputs, insns))
        jobs.append({'key': f'c{k:02d}', 'stage': 'ps', 'code': p, 'fout': {'0': 'uint'}, 'depth': 'near_is_zero',
                     'c2': {'tex': [0x20, 1], 'img': [0x120, 1]}, 'debug': False, 'reduced': False})
    jp = os.path.join(work, 'jobs.json'); json.dump(jobs, open(jp, 'w'))
    env = dict(os.environ)
    if 'DOTNET_ROOT' not in env:   # as tools/game_extract.sh: the Homebrew .NET
        env['DOTNET_ROOT'] = subprocess.run(['brew', '--prefix', 'dotnet'], capture_output=True, text=True).stdout.strip() + '/libexec'
    r = subprocess.run([XLATE, jp, work], capture_output=True, text=True, env=env)
    if r.returncode: print(r.stdout + r.stderr)
    lines = []
    for k, (name, inputs, _, _) in enumerate(CASES):
        spv, msl = os.path.join(work, f'c{k:02d}.spv'), os.path.join(work, f'c{k:02d}.metal')
        if not os.path.exists(spv): lines.append(f'{name}\t-\t-1\t'); continue
        subprocess.run(['spirv-cross', *MSL, spv, '--output', msl], check=True, capture_output=True)
        m = re.search(r'fp_c3\s*&?\s*\w*\s*\[\[buffer\((\d+)\)\]\]', open(msl).read())
        lines.append(f'{name}\t{msl}\t{m.group(1) if m else -1}\t' + ','.join(f'{v:x}' for v in inputs))
    lp = os.path.join(work, 'list.tsv'); open(lp, 'w').write('\n'.join(lines) + '\n')
    runner = os.path.join(work, 't_xlate_run')
    subprocess.run(['clang', '-fobjc-arc', '-O1', '-framework', 'Foundation', '-framework', 'Metal', '-o', runner, os.path.join(HERE, 't_xlate_run.m')], check=True)
    out = dict(l.split('\t', 1) for l in subprocess.run([runner, lp], capture_output=True, text=True, check=True).stdout.splitlines() if '\t' in l)
    fails = 0
    for name, _, _, want in CASES:
        got = out.get(name, 'missing')
        ok = not got.startswith(('error', 'missing')) and (want(int(got, 16)) if callable(want) else int(got, 16) == want)
        fails += not ok
        print(f'{name:<56} {"→ " + got:<14} {"" if callable(want) else f"(want {want:08x})":<16} {"✓" if ok else "✗"}')
    print(f'== {len(CASES)} checks, {fails} failed =='); return fails != 0

if __name__ == '__main__':
    sys.exit(main())
