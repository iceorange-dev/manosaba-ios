#!/usr/bin/env python3
"""ios/tools/resign.py (plan item 319): of the profiles, the development one for <team>.<bundle ID> listing the phone and lasting longest is taken;
one expiring within a day, a distribution one (no get-task-allow), one for another app and one without the phone are not; the identity is the one
whose certificate the profile holds, never a distribution one; the entitlements get the team's application-identifier, team-identifier and
get-task-allow, and lose what the profile does not allow (named). Any profile of the app taken (T_RESIGN_ANY_PROFILE=1, the broken variant): 1 fails."""
import datetime, hashlib, os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import resign
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
now = datetime.datetime(2026, 11, 9, 12, 0); day = datetime.timedelta(days=1)
def P(app, days, dev=True, devices=('U1',), cert=b'c1'):
    e = {'application-identifier': app}; e.update({'get-task-allow': True} if dev else {})
    return {'Entitlements': e, 'ExpirationDate': now + days * day, 'ProvisionedDevices': list(devices), 'DeveloperCertificates': [cert]}
ps = [('soon', P('T.b', 0.5)), ('dist', P('T.b', 300, dev=False)), ('other', P('T.x', 6)), ('nodev', P('T.b', 7, devices=('U2',))), ('good', P('T.b', 6)), ('old', P('T.b', 3))]
best = resign.choose_profile(ps, 'T', 'b', 'U1', now)
check('the development profile for the app listing the phone, lasting longest [T_RESIGN_ANY_PROFILE=1: the distribution one]', best and best[0] == 'good', str(best and best[0]))
check('... none when only an expiring one is left', resign.choose_profile([('soon', P('T.b', 0.5))], 'T', 'b', 'U1', now) is None or os.environ.get('T_RESIGN_ANY_PROFILE') == '1')
h1 = hashlib.sha1(b'c1').hexdigest().upper(); h2 = hashlib.sha1(b'c2').hexdigest().upper()
out = f'  1) {h2} "Apple Development: Someone (X)"\n  2) {h1} "iPhone Distribution: Someone Else (Y)"\n  3) {h1} "Apple Development: Someone (Z)"\n     3 valid identities found'
check('the identity whose certificate the profile holds, not a distribution one', resign.choose_identity(out, P('T.b', 6)) == h1)
check('... none when the keychain has none of its certificates', resign.choose_identity(f'  1) {h2} "Apple Development: A (X)"', P('T.b', 6)) is None)
ents, left = resign.merge_entitlements({'com.apple.developer.kernel.increased-memory-limit': True, 'com.apple.developer.icloud-services': ['CloudDocuments'], 'application-identifier': 'OLD.b'},
                                       {'application-identifier': 'T.b', 'com.apple.developer.team-identifier': 'T', 'get-task-allow': True, 'com.apple.developer.kernel.increased-memory-limit': True}, 'T', 'b')
check('entitlements: the team\'s identifiers, the memory limit kept, iCloud left out and named',
      ents == {'application-identifier': 'T.b', 'com.apple.developer.team-identifier': 'T', 'get-task-allow': True, 'com.apple.developer.kernel.increased-memory-limit': True}
      and left == ['com.apple.developer.icloud-services'], f'{ents} {left}')
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
