#!/usr/bin/env python3
# Runs Tears of the Kingdom 1.4.3's frame-rate controller (main 0x2db174) in Unicorn, from the product and from the original main, with frame
# times fed in the way the game measures them, and checks the interval it picks (rules.py CUTS, 60 fps). The original main is the negative control:
# it must never pick 1. Needs the venv's unicorn and lz4.
# Usage: check_frame_rate.py <product dir> <original main NSO>
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'verify'))
import nsoimg
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_SP, UC_ARM64_REG_LR, UC_ARM64_REG_PC

CTRL, TICKS_HZ_PTR, FREQ = 0x2db174, 0x3acceb8, 19200000   # controller; the tick frequency it divides by (adrp 0x3acc000 + 0xeb8)
BASE, SYS, OBJ, STACK, RET = 0x10000000, 0x80000000, 0x80010000, 0x81000000, 0x82000000

def run(img, frames):
    """frames: list of work times in ms (CPU = GPU); returns the interval (sys+0x88) and 60 / interval (sys+0x250) after each frame"""
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    size = (len(img) + 0xFFFF) & ~0xFFFF
    uc.mem_map(BASE, size); uc.mem_write(BASE, bytes(img))
    for a in (SYS, OBJ, STACK - 0x10000, RET): uc.mem_map(a, 0x10000)
    uc.mem_write(BASE + TICKS_HZ_PTR, struct.pack('<Q', FREQ))
    ctl = SYS + 0x200
    w = lambda a, fmt, *v: uc.mem_write(a, struct.pack(fmt, *v))
    w(ctl + 0x00, '<ii', 2, 3)              # minimum, maximum interval
    w(ctl + 0x08, '<ff', 0.0, 0.0)          # biases (CPU, GPU)
    w(ctl + 0x10, '<ii', 4, 10)             # frames over before raising, cooldown after a change
    w(ctl + 0x28, '<ii', 60, 0)
    w(ctl + 0x38, '<Q', 0)                  # no table (0x2db254)
    w(ctl + 0x40, '<iiii', 2, 2, 0, 0)      # current, stored lower candidate, frames over, cooldown left
    w(ctl + 0x58, '<B', 0)
    w(ctl + 0x68, '<QQ', ctl + 0x68, ctl + 0x68)   # empty listener list (0x2db308: first == sentinel)
    w(SYS + 0x150, '<Q', OBJ); w(OBJ + 0x70, '<Q', 0); w(OBJ + 0xa8, '<B', 2)   # window object: no SetPresentInterval call in the emulator
    out = []
    for ms in frames:
        t = int(ms * FREQ / 1000)
        w(SYS + 0x118, '<qqq', t, t, t)     # +0x118 > 0 runs the controller; +0x120 CPU, +0x128 GPU
        uc.reg_write(UC_ARM64_REG_X0, ctl); uc.reg_write(UC_ARM64_REG_X1, SYS)
        uc.reg_write(UC_ARM64_REG_SP, STACK - 0x100); uc.reg_write(UC_ARM64_REG_LR, RET)
        uc.emu_start(BASE + CTRL, RET, count=100000)
        if uc.reg_read(UC_ARM64_REG_PC) != RET: raise SystemExit('controller did not return')
        itv = struct.unpack('<i', uc.mem_read(SYS + 0x88, 4))[0]; fps = struct.unpack('<i', uc.mem_read(SYS + 0x250, 4))[0]
        out.append((itv, fps))
    return out

n = fail = 0
def expect(what, want, got):
    global n, fail
    n += 1; ok = want == got; fail += not ok
    print(f'{what:<88} expected {want!s:<12} got {got!s:<12} {"✓" if ok else "✗"}')

# Its options applied (rules.py OPTIONS) but hold60, which takes the raising branch out on purpose (plan item 285, off by default; its own check is
# tools/tests/t_totk_options.py): with every option the heavy frames below stayed at interval 1 and 3 checks failed (batch 118 to 121)
opts = sorted({l.split('\t')[0] for l in open(os.path.join(sys.argv[1], 'main.options.tsv')) if not l.startswith('#')})
prod = nsoimg.product_image(sys.argv[1], 'main', [o for o in opts if o != 'hold60'])
orig = nsoimg.load(sys.argv[2])
light, heavy, heavier = [10.0] * 40, [25.0] * 40, [40.0] * 40   # ms: desired = trunc(ms * 60 / 1000) + 1 = 1, 2, 3
seq = light + heavy + heavier + light + light
p, o = run(prod, seq), run(orig, seq)
expect('patched: light frames (10 ms) reach interval 1, 60 fps', (1, 60), p[39])
expect('patched: interval 1 reached within the cooldown and the candidate step (frames)', True, next(i for i, v in enumerate(p) if v[0] == 1) <= 25)
expect('patched: 25 ms frames raise it to 2 (30 fps)', (2, 30), p[79])
expect('patched: 40 ms frames raise it to 3 (20 fps)', (3, 20), p[119])
expect('patched: light frames again come back down to 1', (1, 60), p[-1])
expect('patched: never below 1, never above the maximum 3', True, all(1 <= v[0] <= 3 for v in p))
expect('patched: sys+0x250 is always 60 / interval', True, all(f == 60 // i for i, f in p))
expect('original (negative control): light frames stay at 2', (2, 30), o[39])
expect('original (negative control): never 1', True, all(v[0] >= 2 for v in o))
expect('original and patched agree on the heavy frames', (o[79], o[119]), (p[79], p[119]))

# Shrines (fps60's last five words, nxnative PR #11): the apply step 0xb00f3c (x0 = the owner: +0xa8 -> +0x20 = the system object, controller at
# +0x200) forces the fixed interval of +0x508 when bit 0 of +0x4f8 is set (shrines) by a tail call at 0xb00fd0; leaving a fixed interval (sys+0x258)
# re-arms the lowering (stored candidate ctl+0x44 = current - 1) only when the minimum ctl+0 is below the current interval. The same leaving path
# runs in the helper 0x2b3d24c (x0 = ctl, w1 = 0) and inlined in 0x1ff60cc (owner from the global at 0x3adbaa8, bit 0 cleared before the test)
OWN, OBJ2, GLOBAL_OWNER = 0x80020000, 0x80030000, 0x3adbaa8
def fixed(img, entry, shrine, x0_is_ctl=False):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    size = (len(img) + 0xFFFF) & ~0xFFFF
    uc.mem_map(BASE, size); uc.mem_write(BASE, bytes(img))
    for a in (SYS, OBJ, OWN, OBJ2, STACK - 0x10000, RET): uc.mem_map(a, 0x10000)
    w = lambda a, fmt, *v: uc.mem_write(a, struct.pack(fmt, *v))
    ctl = SYS + 0x200
    w(ctl + 0x00, '<ii', 2, 3); w(ctl + 0x14, '<i', 10); w(ctl + 0x28, '<i', 0)   # ctl+0x28 0: the fps check after re-arming returns at once
    w(ctl + 0x40, '<iiii', 2, 2, 0, 0); w(ctl + 0x50, '<i', 30); w(ctl + 0x58, '<B', 1)   # current 2, candidate 2, inside a fixed interval
    w(OWN + 0xa8, '<Q', OBJ); w(OBJ + 0x20, '<Q', SYS); w(OWN + 0x4f8, '<I', 1 if shrine else 0); w(OWN + 0x508, '<b', 2); w(OWN + 0x50a, '<B', 0)
    w(BASE + GLOBAL_OWNER, '<Q', OWN)
    hit = []
    uc.hook_add(UC_HOOK_CODE, lambda u, a, n, d: (hit.append(1), u.emu_stop()), begin=BASE + 0xb00fd0, end=BASE + 0xb00fd0)
    uc.reg_write(UC_ARM64_REG_X0, ctl if x0_is_ctl else (OBJ2 if entry == 0x1ff60cc else OWN)); uc.reg_write(UC_ARM64_REG_X1, 0)
    uc.reg_write(UC_ARM64_REG_SP, STACK - 0x100); uc.reg_write(UC_ARM64_REG_LR, RET)
    uc.emu_start(BASE + entry, RET, count=10000)
    if not hit and uc.reg_read(UC_ARM64_REG_PC) != RET: raise SystemExit(f'{entry:#x} did not return')
    return bool(hit), struct.unpack('<i', uc.mem_read(ctl + 0x44, 4))[0]
expect('patched: a shrine (owner+0x4f8 bit 0) no longer forces the fixed interval', False, fixed(prod, 0xb00f3c, 1)[0])
expect('original (negative control): a shrine forces it (0xb00fd0)', True, fixed(orig, 0xb00f3c, 1)[0])
expect('patched: leaving a fixed interval at 2 re-arms the lowering to 1 (apply step)', 1, fixed(prod, 0xb00f3c, 0)[1])
expect('original (negative control): from 2 it does not (the minimum is 2)', 2, fixed(orig, 0xb00f3c, 0)[1])
expect('patched: the helper 0x2b3d24c re-arms it to 1', 1, fixed(prod, 0x2b3d24c, 0, True)[1])
expect('original (negative control): the helper does not', 2, fixed(orig, 0x2b3d24c, 0, True)[1])
expect('patched: the copy inlined in 0x1ff60cc re-arms it to 1', 1, fixed(prod, 0x1ff60cc, 1)[1])
expect('original (negative control): the inlined copy does not', 2, fixed(orig, 0x1ff60cc, 1)[1])

# Real-time frame step (rules.py HOSTREFS): the two producers run from just after the controller, with sys+0x250 = 30 (interval 2) and the host
# page holding the step of a 90 fps frame; the original main computes 60 / 30 from the interval and ignores the page.
from unicorn.arm64_const import UC_ARM64_REG_X8, UC_ARM64_REG_X19, UC_ARM64_REG_X22, UC_ARM64_REG_S8, UC_ARM64_REG_S9
fx = {l.split()[1]: int(l.split()[2], 0) for l in open(os.path.join(sys.argv[1], 'main.fixups.tsv')) if l.startswith('# ')}
expect('the product has a host page after the key page', fx['KEY_PAGE_OFF'] + 0x4000, fx.get('HOST_PAGE_OFF'))
RATIO_G = 0x3a22128                                         # where 0x2daa7c stores the ratio
def producers(img, page, sel):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    size = (max(len(img), fx['HOST_PAGE_OFF'] + 0x4000) + 0xFFFF) & ~0xFFFF
    uc.mem_map(BASE, size); uc.mem_write(BASE, bytes(img)); uc.mem_map(SYS, 0x10000)
    uc.mem_write(BASE + fx['HOST_PAGE_OFF'], struct.pack('<ff', *page))
    uc.mem_write(SYS + 0x250, struct.pack('<i', 30)); uc.mem_write(SYS + 0x1da, bytes([sel]))
    uc.reg_write(UC_ARM64_REG_X19, SYS); uc.emu_start(BASE + 0x2daa54, BASE + 0x2daa80)
    ratio = struct.unpack('<f', uc.mem_read(BASE + RATIO_G, 4))[0]
    uc.reg_write(UC_ARM64_REG_X22, SYS); uc.reg_write(UC_ARM64_REG_X19, SYS); uc.reg_write(UC_ARM64_REG_X8, SYS)
    uc.reg_write(UC_ARM64_REG_S9, 0x3f800000)               # fmov s9, #1.0 at 0x3e5620, before the start
    uc.emu_start(BASE + 0x3e562c, BASE + 0x3e565c)
    s8 = struct.unpack('<f', struct.pack('<I', uc.reg_read(UC_ARM64_REG_S8) & 0xffffffff))[0]
    return round(ratio, 6), round(s8, 6)
page90 = (1 / 90 * 30, 1 / 90 * 60)
r = lambda x: round(struct.unpack('<f', struct.pack('<f', x))[0], 6)
expect('patched: ratio stored at 0x3a22128 = host page +0 (1/90 s x 30)', r(page90[0]), producers(prod, page90, 0)[0])
expect('patched: 0x3e5658 s8 = host page +4 (selector 1.0)', r(page90[1]), producers(prod, page90, 0)[1])
expect('patched: 0x3e5658 s8 = host page +4 x 0.5 (sys+0x1da bit 0)', r(page90[1] * 0.5), producers(prod, page90, 1)[1])
expect('original (negative control): ratio 60 / 30 x 0.5 = 1, page ignored', 1.0, producers(orig, page90, 0)[0])
expect('original (negative control): s8 = 60 / 30 = 2, page ignored', 2.0, producers(orig, page90, 0)[1])

# The host half (frame_step.c): built from the real source
import subprocess, tempfile
here = os.path.dirname(os.path.abspath(__file__))
with tempfile.TemporaryDirectory() as td:
    exe = os.path.join(td, 't_frame_step')
    subprocess.run(['clang', '-O1', '-Wall', '-I', os.path.join(here, '..', '..', 'host'), '-o', exe, os.path.join(here, 'tests', 't_frame_step.c')], check=True)
    res = subprocess.run([exe], capture_output=True, text=True); sys.stdout.write(res.stdout)
    expect('t_frame_step (the mean of the present intervals, the page layout) passes', 0, res.returncode)
print(f'== {n} checks, {fail} failed ==')
sys.exit(fail != 0)
