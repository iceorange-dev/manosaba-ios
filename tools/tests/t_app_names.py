#!/usr/bin/env python3
"""ios/tools/app_names.py, the app's name (plan item 314d): Info.plist's name is the game's id in ASCII (project.yml), the title is in
<loc>.lproj/InfoPlist.strings for each of the game's localizations and en (the fallback), a strings file plutil reads back to the title (a
quote and a backslash in it escaped), a name that is not ASCII refused; ios/run.sh writes them into Gen/Names, which project.yml packs.
T_APP_NAMES_BROKEN=noen (no en.lproj: a phone in another language shows the id): 1 fails."""
import json, os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
AN = os.path.join(ROOT, 'ios', 'tools', 'app_names.py')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
def read_strings(p):   # plutil's reading of a .strings file
    r = subprocess.run(['plutil', '-convert', 'json', '-o', '-', p], capture_output=True, text=True)
    return json.loads(r.stdout) if r.returncode == 0 else {'error': r.stderr.strip()}

with tempfile.TemporaryDirectory() as w:
    title = '魔法少女ノ魔女裁判'
    r = subprocess.run([sys.executable, AN, w, 'manosaba', title, 'zh-Hans', 'zh-Hant'], capture_output=True, text=True)
    got = sorted(n for n in os.listdir(w) if n.endswith('.lproj'))
    check('the game\'s localizations and en, each with InfoPlist.strings [T_APP_NAMES_BROKEN=noen]', r.returncode == 0 and got == ['en.lproj', 'zh-Hans.lproj', 'zh-Hant.lproj'], f'{got} {r.stderr}')
    vals = {l: read_strings(os.path.join(w, l, 'InfoPlist.strings')).get('CFBundleDisplayName') for l in got}
    check('each read back by plutil as the title', vals and all(v == title for v in vals.values()), f'{vals}')
    odd = 'A "quoted" \\ title'
    r = subprocess.run([sys.executable, AN, os.path.join(w, 'odd'), 'totk', odd, 'en'], capture_output=True, text=True)
    check('a quote and a backslash in the title escaped', r.returncode == 0 and read_strings(os.path.join(w, 'odd', 'en.lproj', 'InfoPlist.strings')).get('CFBundleDisplayName') == odd)
    r = subprocess.run([sys.executable, AN, os.path.join(w, 'bad'), '魔法', title, 'en'], capture_output=True, text=True)
    check('a name that is not ASCII: refused, nothing written', r.returncode != 0 and not os.path.exists(os.path.join(w, 'bad')), r.stderr)
y = open(os.path.join(ROOT, 'ios', 'project.yml'), encoding='utf-8').read()
check('project.yml: the display name is the ASCII id ($(DM_GAME_NAME)) and Gen/Names is packed as resources',
      'CFBundleDisplayName: $(DM_GAME_NAME)' in y and '- path: Gen/Names' in y and 'DM_GAME_TITLE' not in y)
rs = open(os.path.join(ROOT, 'ios', 'run.sh'), encoding='utf-8').read()
check('ios/run.sh: DM_GAME_NAME is the game id and app_names.py writes Gen/Names', 'echo "DM_GAME_NAME = $GAME"' in rs and 'tools/app_names.py Gen/Names "$GAME" "$TITLE"' in rs)
print(f'{total} checks, {fails} failed')
sys.exit(1 if fails else 0)
