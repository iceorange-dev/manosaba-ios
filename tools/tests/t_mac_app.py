#!/usr/bin/env python3
"""The Mac app's Info.plist (tools/mac_app.sh, plan item 303h): the heredoc parses as a property list with the shell's variables filled in, keeps
the games category, and turns Game Mode off (LSSupportsGameMode false; seedds/nxnative cb59a67: in fullscreen Game Mode macOS held the GPU at its
lowest clock). The key left out (T_MAC_APP_OLD=1, the broken variant, as before): 1 fails."""
import os, re, sys, plistlib
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
src = open(os.path.join(ROOT, 'tools', 'mac_app.sh'), encoding='utf-8').read()
if os.environ.get('T_MAC_APP_OLD'): src = src.replace('<key>LSSupportsGameMode</key><false/>\n', '')
m = re.search(r'cat > "\$APP/Contents/Info.plist" <<PL\n(.*?)\nPL\n', src, re.S)
check('mac_app.sh writes Info.plist from a heredoc', m is not None)
if m:
    text = re.sub(r'\$[A-Z_]+', 'X', m.group(1))
    try: pl = plistlib.loads(text.encode()); err = ''
    except Exception as e: pl, err = {}, str(e)
    check('the heredoc is a property list', bool(pl), err)
    check('the games category kept', pl.get('LSApplicationCategoryType') == 'public.app-category.games')
    check('Game Mode off: LSSupportsGameMode false [T_MAC_APP_OLD]', pl.get('LSSupportsGameMode') is False, repr(pl.get('LSSupportsGameMode')))
print(f'{total} checks, {fails} failed')
sys.exit(1 if fails else 0)
