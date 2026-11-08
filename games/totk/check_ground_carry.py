#!/usr/bin/env python3
# Runs Tears of the Kingdom 1.4.3's check on the velocity Link keeps from moving ground (main 0x562a58-0x562a74, rules.py CAVES, plan item 304;
# nxport 1bf994a's finding, MIT) in Unicorn: the time object's +0x28 holds 30 / (step × time scale), +0x38 the step (1 at 30 fps), and the
# check keeps a tenth of the ground's velocity (b.gt to 0x562dac) when its value is over 100. At 20 to 240 fps and time scales 1, 0.5, 0.35 and
# 0.25: with the option realtime the branch is taken only for time slowed below 0.3, whatever the frame rate; the registers it leaves for the code
# after (x22, x25, sp) stay. The original main and the product without realtime: taken at normal speed above 100 fps (the slide this fixes).
# Broken variant: a product from before plan item 304 (no cave): 2 fail. Needs the venv's unicorn and lz4.
# Usage: check_ground_carry.py <product dir> <original main NSO>
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'verify'))
import nsoimg
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X22, UC_ARM64_REG_X25

SITE, START, BRANCH, TAKEN, NEXT = 0x562a64, 0x562a58, 0x562a74, 0x562dac, 0x562a78
BASE, DATA, STACK = 0x10000000, 0x80000000, 0x90000000
f = lambda v: struct.pack('<f', v)

def slides(img, hz, scale):
    """1 when the check keeps a tenth of the ground's velocity (the branch taken), and whether x22 / x25 / sp stayed"""
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, (len(img) + 0xFFFF) & ~0xFFFF); uc.mem_write(BASE, bytes(img))
    uc.mem_map(DATA, 0x10000); uc.mem_map(STACK - 0x10000, 0x20000)
    step = 30 / hz; t = DATA + 0x1000
    uc.mem_write(DATA + 0x100, struct.pack('<Q', DATA + 0x200))            # x25 → the owner, whose +0xc8 is the time object
    uc.mem_write(DATA + 0x200 + 0xc8, struct.pack('<Q', t))
    uc.mem_write(t + 0x28, f(30 / (step * scale))); uc.mem_write(t + 0x38, f(step))
    uc.reg_write(UC_ARM64_REG_X22, DATA + 0x800); uc.reg_write(UC_ARM64_REG_X25, DATA + 0x100); uc.reg_write(UC_ARM64_REG_SP, STACK)
    uc.emu_start(BASE + START, BASE + BRANCH, count=40)       # up to the b.gt (the cave's routine and its branch back included)
    at_branch = uc.reg_read(UC_ARM64_REG_PC) == BASE + BRANCH
    uc.emu_start(BASE + BRANCH, 0, count=1)                  # the b.gt alone: then at its target or the next instruction (the target's code later
    pc = uc.reg_read(UC_ARM64_REG_PC) - BASE                 # reaches 0x562a78 too, so no stop address there)
    kept = uc.reg_read(UC_ARM64_REG_X22) == DATA + 0x800 and uc.reg_read(UC_ARM64_REG_X25) == DATA + 0x100 and uc.reg_read(UC_ARM64_REG_SP) == STACK
    assert at_branch and pc in (TAKEN, NEXT), hex(pc)
    return pc == TAKEN, kept

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
expect('the product keeps the word at 0x562a64; realtime makes it a branch into the patch area',
       raw[SITE:SITE + 4] == orig[SITE:SITE + 4] and prod[SITE:SITE + 4] != orig[SITE:SITE + 4] and struct.unpack_from('<I', prod, SITE)[0] >> 26 == 5)
bad = []; cases = 0
for hz in (20, 30, 60, 90, 100, 120, 144, 240):
    for scale in (1, .5, .35, .25):   # (not 0.3 itself: 30 / 0.3 is 100 only up to float rounding)
        s, kept = slides(prod, hz, scale); cases += 1
        if s != (scale < .3 - 1e-6) or not kept: bad.append(f'{hz} fps × {scale}: {"slides" if s else "holds"}{"" if kept else ", registers changed"}')
expect(f'realtime: only time slowed below 0.3 keeps a tenth, at every frame rate ({cases} cases)', not bad, '; '.join(bad[:4]))
expect('the original main at 120 fps, normal speed: keeps a tenth (the slide)', slides(orig, 120, 1)[0])
expect('... and at 90 fps it holds (under 100)', not slides(orig, 90, 1)[0])
expect('the product without realtime at 120 fps: the game\'s own check, the slide', slides(off, 120, 1)[0])
print(f'== {n} checks, {fail} failed ==')
sys.exit(1 if fail else 0)
