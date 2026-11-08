#!/usr/bin/env python3
"""Every CoreHaptics engine of host/front/gamepad.m plays haptics only (plan item 314e): `playsHapticsOnly = YES` is set on each engine before it
is started (the SDK's CHHapticEngine.h: it takes effect at the next start), the phone's as the controllers'. The check run on a copy of the file
with the phone engine's line removed must fail (the broken variant, checked here)."""
import os, re, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}')
def engines_ok(src):   # (engines made, engines set haptics-only before their first start)
    made = [m.start() for m in re.finditer(r'\[\[CHHapticEngine alloc\] initAndReturnError|createEngineWithLocality', src)]
    ok = 0
    for at in made:   # between the creation and the next start in the file (the controllers' engines are set in one loop after both are made)
        s = src.find('startAndReturnError', at); h = src.find('playsHapticsOnly = YES', at)
        ok += h >= 0 and (s < 0 or h < s)
    return len(made), ok
src = open(os.path.join(ROOT, 'host', 'front', 'gamepad.m'), encoding='utf-8').read()
n, ok = engines_ok(src)
check(f'gamepad.m: each of its {n} engine creations sets playsHapticsOnly before the engine starts', n >= 3 and ok == n)
broken = src.replace(' eng.playsHapticsOnly = YES;', '', 1)
n2, ok2 = engines_ok(broken)
check('the broken variant (the phone engine\'s line removed) is caught', broken != src and ok2 < n2)
print(f'{total} checks, {fails} failed')
sys.exit(1 if fails else 0)
