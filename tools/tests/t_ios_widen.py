#!/usr/bin/env python3
"""The setting "Wide screen" on iOS (plan item 320g; the widening, gfx/nvn/core/widen.h): the Settings app's page (ios/Settings.bundle/Root.plist)
has a toggle dm.widen, off by default, and the in-game panel (ios/Sources/front_ios.m) the same key; ios/Sources/main.m sets DM_WIDEN=1 from it
at launch, after env.txt is applied (so env.txt's DM_WIDEN, either way, wins) and only when env.txt said nothing; the front end and the graphics
settings read DM_WIDEN, not the setting (a change waits for the next launch, as the widened storage is made when the game starts).
The line in main.m left out (T_IOS_WIDEN_OLD=1, the broken variant): 1 fails."""
import os, plistlib, re, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
pl = plistlib.load(open(os.path.join(ROOT, 'ios', 'Settings.bundle', 'Root.plist'), 'rb'))
w = [s for s in pl.get('PreferenceSpecifiers', []) if s.get('Key') == 'dm.widen']
check('the Settings app has the toggle dm.widen, off by default', len(w) == 1 and w[0].get('Type') == 'PSToggleSwitchSpecifier' and w[0].get('DefaultValue') is False, str(w))
front = open(os.path.join(ROOT, 'ios', 'Sources', 'front_ios.m'), encoding='utf-8').read()
check('the in-game panel has the same key', 'panel_switch(@"dm.widen")' in front)
main = open(os.path.join(ROOT, 'ios', 'Sources', 'main.m'), encoding='utf-8').read()
if os.environ.get('T_IOS_WIDEN_OLD'): main = re.sub(r'\n    if \(!getenv\("DM_WIDEN"\) && \[NSUserDefaults\.standardUserDefaults boolForKey:@"dm\.widen"\]\).*', '', main)
env_at = main.find('dm_env_file_apply(d.fileSystemRepresentation'); set_at = main.find('if (!getenv("DM_WIDEN") && [NSUserDefaults.standardUserDefaults boolForKey:@"dm.widen"]) { setenv("DM_WIDEN", "1", 0);')
check('main.m sets DM_WIDEN=1 from it after env.txt, only when env.txt said nothing [T_IOS_WIDEN_OLD]', env_at > 0 and set_at > env_at)
check('the front end and the graphics settings read DM_WIDEN, not the setting', 'boolForKey:@"dm.widen"' not in front and main.count('boolForKey:@"dm.widen"') <= 1 and 'getenv("DM_WIDEN")' in front)
print(f'{total} checks, {fails} failed')
sys.exit(1 if fails else 0)
