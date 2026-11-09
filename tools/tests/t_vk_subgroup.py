#!/usr/bin/env python3
"""The Vulkan bundle's subgroup size (plan item 311; nx2android 44eca7e, MIT): gfx/tools/dmsh_vk.py translates for 128-lane subgroups (Adreno's waves
are 64 or 128), and the translator keeps Maxwell's 32-lane warps inside the wave: a shuffle's source lane is the lane's own 32-lane group
(SubgroupLocalInvocationId & 0x60) or'ed with the lane within it. manosaba's 0086622fe9de813d (a quad shuffle by lane, then FSWZADD) translated
alone: its module has the & 96 on the subgroup invocation id and passes spirv-val. Translated for 32 (DM_TEST_VK_SUBGROUP=32, the broken variant,
as before): the source lane loses the wave's upper lanes, 1 fails. Needs the manosaba captures (DM_ROOT/shextract), the translator and the NDK."""
import os, re, struct, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
DM_ROOT = os.environ.get('DM_ROOT') or os.path.join(ROOT, 'work')
NDK = os.environ.get('NDK', '/opt/homebrew/share/android-ndk'); DIS = os.path.join(NDK, 'shader-tools', 'darwin-x86_64', 'spirv-dis')
KEY = '0086622fe9de813d'
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
if not os.path.isdir(os.path.join(DM_ROOT, 'shextract')):   # the game's captures (CI's runner has none)
    print(f'(no manosaba captures at {os.path.join(DM_ROOT, "shextract")}: skipped)'); sys.exit(0)
with tempfile.TemporaryDirectory() as w:
    out = os.path.join(w, 't.dmvk'); env = dict(os.environ, DM_GAME='manosaba', DM_ROOT=DM_ROOT, J='1')
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'gfx', 'tools', 'dmsh_vk.py'), os.path.join(DM_ROOT, 'shextract'), out, KEY], capture_output=True, text=True, env=env)
    data = open(out, 'rb').read() if os.path.exists(out) else b''
    mod = b''
    if data[:4] == b'DMVK':
        _, n = struct.unpack_from('<II', data, 4)
        for i in range(n):
            key, st, so, sl, ro, rl = struct.unpack_from('<QIIIII', data, 12 + 28 * i)
            if '%016x' % key == KEY: mod = data[so:so + sl]
    check(f'{KEY} translated into a bundle of its own', bool(mod), (r.stdout + r.stderr)[-300:])
    spv = os.path.join(w, 'm.spv'); open(spv, 'wb').write(mod)
    dis = subprocess.run([DIS, spv], capture_output=True, text=True).stdout if mod else ''
# The lane id reaches the mask through bitcasts and a temporary (the translator's helper function): the shuffle and an & with the constant 96 in the
# module are the 128-lane form; at 32 the helper uses the lane id as it is and no 96 appears
ids = set(re.findall(r'OpDecorate (%\w+) BuiltIn SubgroupLocalInvocationId', dis))
c96 = set(m.group(1) for m in re.finditer(r'(%\w+) = OpConstant %\w+ 96\b', dis))
anded = any(m.group(1) in c96 or m.group(2) in c96 for m in re.finditer(r'%\w+ = OpBitwiseAnd %\w+ (%\w+) (%\w+)', dis))
shuffles = len(re.findall(r'OpGroupNonUniformShuffle\b', dis))
check('its shuffle keeps the lane\'s own 32-lane group: (SubgroupLocalInvocationId & 96) | lane [DM_TEST_VK_SUBGROUP=32]', bool(ids) and shuffles > 0 and anded,
      f'{len(ids)} lane id variables, {shuffles} shuffles, {len(c96)} constants 96')
print(f'{total} checks, {fails} failed')
sys.exit(1 if fails else 0)
