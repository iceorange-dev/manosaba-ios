#!/usr/bin/env python3
# Runs Tears of the Kingdom 1.4.3's fade steps (main 0x0a7ac4, 0x95dd24: the distance / LOD mesh fades; 0x6155e4, 0x615660: the fades before an
# actor appears; rules.py CAVES, plan item 292; MacNativeGames 3e9e7f67 / PR #1112's sites, PR #1134's rule, MIT) in Unicorn: each converts
# 2.1 × ratio (ratio = the step, 1 at 30 fps) to the integer it adds to a byte counter. With realtime: at 20-240 fps and in slow motion every
# positive input gives at least 1 (0.525 at 120 fps: 1, where the game's trunc gave 0 and the counter stopped), 2 at 30 fps and 1 at 60 as the
# game's, 0 stays 0 (a paused frame), negative and NaN inputs convert as the game's; the registers around (x9, x20, s0, sp) stay. The original
# main at 120 fps: 0 (the stall this fixes); the product without realtime: the game's conversion. Broken variant: a product from before plan item
# 292 (no caves): 2 fail. Needs the venv's unicorn and lz4.
# Usage: check_fade_step.py <product dir> <original main NSO>
import math, struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'verify'))
import nsoimg
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X8, UC_ARM64_REG_X9, UC_ARM64_REG_X10, UC_ARM64_REG_X20, UC_ARM64_REG_S0

SITES = ((0x0a7ac4, UC_ARM64_REG_X8), (0x95dd24, UC_ARM64_REG_X8), (0x6155e4, UC_ARM64_REG_X10), (0x615660, UC_ARM64_REG_X10))
BASE, STACK = 0x10000000, 0x90000000
f2b = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]

def step(img, site, dst, x):
    """The step a site makes of the input x (signed 32-bit), and whether x9 / x20 / s0 / sp stayed"""
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, (len(img) + 0xFFFF) & ~0xFFFF); uc.mem_write(BASE, bytes(img)); uc.mem_map(STACK - 0x10000, 0x20000)
    uc.reg_write(UC_ARM64_REG_SP, STACK); uc.reg_write(UC_ARM64_REG_X9, 0x1111); uc.reg_write(UC_ARM64_REG_X20, 0x2222); uc.reg_write(UC_ARM64_REG_S0, f2b(x))
    uc.reg_write(dst, 0xdeadbeef)
    uc.emu_start(BASE + site, BASE + site + 4, count=20)   # the conversion (or the cave's routine and its branch back)
    assert uc.reg_read(UC_ARM64_REG_PC) == BASE + site + 4
    r = uc.reg_read(dst) & 0xFFFFFFFF; r = r - (1 << 32) if r >> 31 else r
    s0 = uc.reg_read(UC_ARM64_REG_S0) & 0xFFFFFFFF
    kept = uc.reg_read(UC_ARM64_REG_X9) == 0x1111 and uc.reg_read(UC_ARM64_REG_X20) == 0x2222 and uc.reg_read(UC_ARM64_REG_SP) == STACK and s0 == f2b(x)
    return r, kept

def trunc(x):   # fcvtzs: toward zero, saturating, NaN → 0
    if math.isnan(x): return 0
    if x >= 2 ** 31: return 2 ** 31 - 1
    if x <= -2 ** 31: return -2 ** 31
    return int(x)
def want(x):     # PR #1134's rule
    t = trunc(x); return 1 if t == 0 and x > 0 else t

n = fail = 0
def expect(what, ok, detail=''):
    global n, fail
    n += 1; fail += not ok
    print(f'{what:<100} {"✓" if ok else "✗"}' + (f'  ({detail})' if detail and not ok else ''))

prod_dir = sys.argv[1]
opts = {l.split('\t')[0] for l in open(os.path.join(prod_dir, 'main.options.tsv')) if not l.startswith('#')}
prod = nsoimg.product_image(prod_dir, 'main', ['realtime'])
off = nsoimg.product_image(prod_dir, 'main', sorted(opts - {'realtime'}))
orig = nsoimg.load(sys.argv[2]); orig = orig[0] if isinstance(orig, tuple) else orig
raw = open(os.path.join(prod_dir, 'main.patched.bin'), 'rb').read()
expect('the product keeps the four words; realtime makes each a branch into the patch area',
       all(raw[a:a + 4] == orig[a:a + 4] and prod[a:a + 4] != orig[a:a + 4] and struct.unpack_from('<I', prod, a)[0] >> 26 == 5 for a, _ in SITES))
f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
inputs = [f32(2.1 * 30 / hz * sc) for hz in (20, 30, 40, 50, 60, 63, 64, 90, 100, 120, 144, 240) for sc in (1, .5, .25)] + [0.0, -0.0, -0.4, -1.5, float('nan'), 1e10, 0.999999]
bad = []; cases = 0
for a, dst in SITES:
    for x in inputs:
        r, kept = step(prod, a, dst, x); cases += 1
        if r != want(x) or not kept: bad.append(f'{a:#x} {x!r}: {r}{"" if kept else ", registers changed"}')
expect(f'realtime: a positive input gives at least 1, 0 / negative / NaN as the game converts them ({cases} cases)', not bad, '; '.join(bad[:4]))
expect('... 2 at 30 fps and 1 at 60, as the game\'s', all(step(prod, a, d, f32(2.1))[0] == 2 and step(prod, a, d, f32(1.05))[0] == 1 for a, d in SITES))
expect('the original main at 120 fps (0.525): 0, the counter stopped', all(step(orig, a, d, f32(0.525))[0] == 0 for a, d in SITES))
expect('the product without realtime at 120 fps: the game\'s 0', all(step(off, a, d, f32(0.525))[0] == 0 for a, d in SITES))
print(f'== {n} checks, {fail} failed ==')
sys.exit(1 if fail else 0)
