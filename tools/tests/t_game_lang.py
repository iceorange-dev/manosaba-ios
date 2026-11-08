#!/usr/bin/env python3
"""The game's languages (plan item 170): tools/gen_game.py takes game.json "languages" (nn::settings codes, the profile's language among them) into
the profile (.languages) and gives Apple's localizations for them (--get apple_localizations: en-US → en, zh-CN and zh-Hans → zh-Hans once); a code
outside nn::settings, or a list without the profile's language, is refused; without the field the profile's language alone. ios/tools/
project_lang.py puts them into project.yml as CFBundleLocalizations, right after CFBundleDisplayName, and refuses a malformed tag. Both games'
profiles: manosaba zh-Hans and zh-Hant (its StreamingAssets/config.json), Tears of the Kingdom 14. The check of codes left out
(T_GAME_LANG_NO_CHECK=1, the broken variant): 2 fail."""
import json, os, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
GEN, PL = os.path.join(ROOT, 'tools', 'gen_game.py'), os.path.join(ROOT, 'ios', 'tools', 'project_lang.py')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
with tempfile.TemporaryDirectory() as d:
    gen = GEN
    if os.environ.get('T_GAME_LANG_NO_CHECK'):   # the broken variant: languages not checked
        src = open(GEN).read().replace("    if not isinstance(langs, list) or not langs or any(l not in LANG_CODES for l in langs) or g['language'] not in langs:", "    if False:")
        gen = os.path.join(d, 'gen_game_broken.py'); open(gen, 'w').write(src)
    base = json.load(open(os.path.join(ROOT, 'games', 'manosaba', 'game.json')))
    def run(langs, *args):
        g = dict(base); g.pop('languages', None)
        if langs is not None: g['languages'] = langs
        p = os.path.join(d, 'game.json'); json.dump(g, open(p, 'w'))
        return subprocess.run([sys.executable, '-I', gen, p, *args], capture_output=True, text=True)
    r = run(['zh-Hans', 'en-US', 'zh-CN', 'es-419'], '--get', 'apple_localizations')
    check('apple_localizations: en-US → en, zh-CN and zh-Hans → zh-Hans once, es-419 as it is', r.returncode == 0 and r.stdout.split() == ['zh-Hans', 'en', 'es-419'], r.stdout + r.stderr)
    r = run(['zh-Hans', 'zh-Hant'], os.path.join(d, 'p.c'))
    check('the profile gets .languages', r.returncode == 0 and '.languages = "zh-Hans zh-Hant"' in open(os.path.join(d, 'p.c')).read())
    r = run(None, '--get', 'apple_localizations')
    check('no "languages": the profile\'s language alone', r.returncode == 0 and r.stdout.split() == ['zh-Hans'], r.stdout + r.stderr)
    r = run(['zh-Hans', 'sv'], '--get', 'apple_localizations')
    check('a code outside nn::settings (sv): refused', r.returncode != 0 and 'languages must list' in r.stderr, r.stdout + r.stderr)
    r = run(['ja', 'en-US'], '--get', 'apple_localizations')
    check('a list without the profile\'s language (zh-Hans): refused', r.returncode != 0 and 'languages must list' in r.stderr, r.stdout + r.stderr)
    out = os.path.join(d, 'p.yml')
    r = subprocess.run([sys.executable, '-I', PL, os.path.join(ROOT, 'ios', 'project.yml'), out, 'zh-Hans', 'zh-Hant'], capture_output=True, text=True)
    y = open(out).read() if r.returncode == 0 else ''
    check('project_lang.py: CFBundleLocalizations after CFBundleDisplayName', r.returncode == 0 and '        CFBundleDisplayName: $(DM_GAME_NAME)   # the game\'s id in ASCII (sideloading tools build the App ID from it); the title: Gen/Names\n        CFBundleLocalizations: ["zh-Hans", "zh-Hant"]\n' in y, r.stderr)
    r = subprocess.run([sys.executable, '-I', PL, os.path.join(ROOT, 'ios', 'project.yml'), out, 'zh Hans'], capture_output=True, text=True)
    check('... a malformed tag: refused', r.returncode != 0)
    langs = {g: json.load(open(os.path.join(ROOT, 'games', g, 'game.json'))).get('languages') for g in ('manosaba', 'totk')}
    check('the games: manosaba zh-Hans zh-Hant, Tears of the Kingdom 14 with its zh-Hant', langs['manosaba'] == ['zh-Hans', 'zh-Hant'] and len(langs['totk']) == 14 and 'zh-Hant' in langs['totk'], str(langs))
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
