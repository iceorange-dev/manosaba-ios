#!/usr/bin/env python3
# Tears of the Kingdom's code options in games/totk/rules.py. The fixed resolution (option fixedres, plan item 276): the dynamic resolution controller's epilogue run in
# Unicorn with the tier's scale 0.8 in w23 and bit 33 in x8 (the tier did not change): with the option the result's low word is 1.0 and bit 33 is
# kept; the option's original word is the epilogue's `mov w9, w23` and, when the workspace's product is here, the word main holds at 0x5848a8.
# The interval held (option hold60, plan item 285): `cmp w10, w9` and the option's word in Unicorn with desired 2 above the current interval 1: the
# original b.gt goes to the raising (0x2db2b4), the option falls through; its original word is the workspace's at 0x2db214.
# Negative controls: T_FIXEDRES_ORIGINAL=1 runs the original word: 1 fails; T_HOLD60_ORIGINAL=1 likewise: 1 fails.
import os, struct, sys, importlib.util
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
total = fails = 0
def check(name, ok):
    global total, fails
    total += 1; fails += not ok
    print(('✓ ' if ok else '✗ ') + name)
spec = importlib.util.spec_from_file_location('totk_rules', os.path.join(ROOT, 'games', 'totk', 'rules.py'))
rules = importlib.util.module_from_spec(spec); spec.loader.exec_module(rules)
(addr, orig, repl, _), = rules.OPTIONS['277178b7dba1b6d4949a84778d4abc58']['fixedres']
import capstone
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
dis = lambda h, at=None: ' '.join(f'{i.mnemonic} {i.op_str}'.strip() for i in md.disasm(bytes.fromhex(h), addr if at is None else at))
check(f'the option is one word at {addr:#x}: {dis(orig)} → {dis(repl)}', addr == 0x5848a8 and dis(orig) == 'mov w9, w23' and dis(repl) == 'mov w9, #0x3f800000')
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X8, UC_ARM64_REG_X23
word = orig if os.environ.get('T_FIXEDRES_ORIGINAL') == '1' else repl
code = bytes.fromhex(word) + bytes.fromhex('000109aa')   # then the epilogue's orr x0, x8, x9
uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM); uc.mem_map(0x100000, 0x1000); uc.mem_write(0x100000, code)
uc.reg_write(UC_ARM64_REG_X23, struct.unpack('<I', struct.pack('<f', 0.8))[0]); uc.reg_write(UC_ARM64_REG_X8, 1 << 33)
uc.emu_start(0x100000, 0x100000 + len(code))
x0 = uc.reg_read(UC_ARM64_REG_X0); scale = struct.unpack('<f', struct.pack('<I', x0 & 0xffffffff))[0]
check(f'a tier of 0.8: the result scales by {scale:g}, bit 33 kept [T_FIXEDRES_ORIGINAL=1]', scale == 1.0 and x0 >> 32 == 2)
img = os.path.join(ROOT, 'work', 'build', 'totk', 'product60', 'main.patched.bin')
if os.path.exists(img):
    with open(img, 'rb') as f: f.seek(addr); have = f.read(4).hex()
    check(f'the workspace\'s main holds the original word there ({have})', have == orig)
(haddr, horig, hrepl, _), = rules.OPTIONS['277178b7dba1b6d4949a84778d4abc58']['hold60']
check(f'hold60 is one word at {haddr:#x}: {dis(horig, haddr)} → {dis(hrepl, haddr)}', haddr == 0x2db214 and dis(horig, haddr) == 'b.gt #0x2db2b4' and dis(hrepl, haddr) == 'nop')
from unicorn.arm64_const import UC_ARM64_REG_W9, UC_ARM64_REG_W10, UC_ARM64_REG_PC
hw = horig if os.environ.get('T_HOLD60_ORIGINAL') == '1' else hrepl
base = 0x2db210; code = bytes.fromhex('5f01096b') + bytes.fromhex(hw)   # cmp w10, w9; the word
uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM); uc.mem_map(0x2db000, 0x1000); uc.mem_write(base, code + bytes.fromhex('1f2003d5') * 64)
uc.reg_write(UC_ARM64_REG_W10, 2); uc.reg_write(UC_ARM64_REG_W9, 1)
uc.emu_start(base, base + 8, count=2)
pc = uc.reg_read(UC_ARM64_REG_PC)
check(f'desired 2 above the interval 1: next {pc:#x}, not the raising 0x2db2b4 [T_HOLD60_ORIGINAL=1]', pc == base + 8)
if os.path.exists(img):
    with open(img, 'rb') as f: f.seek(haddr); have = f.read(4).hex()
    check(f'the workspace\'s main holds hold60\'s original word there ({have})', have == horig)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
