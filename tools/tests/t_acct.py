#!/usr/bin/env python3
"""ios/tools/acct.py (plan item 319): a paid team is taken before the free personal one, a free one alone is taken as free; signed in with no team
yet and not signed in give their own reasons; of the phones, the wired one is taken before one whose tunnel is connected, a phone over the network
only is not taken, two wired ones are refused, an untrusted one and one with developer mode off say what to do, the older JSON shape (properties
.hardware) reads too; the bundle ID is dm.<game>.<team in lower case>. No preference for the wired phone (T_ACCT_ANY_DEVICE=1, the broken
variant): 1 fails."""
import os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'ios', 'tools')); import acct
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
T = lambda tid, free: {'teamID': tid, 'teamName': 'n', 'isFreeProvisioningTeam': free, 'teamType': 'x'}
prefs = {'IDEProvisioningTeamByIdentifier': {'a': [T('FREE1', True), T('PAID1', False)], 'b': [T('PAID1', False)]}}
check('a paid team before the free personal one', acct.pick_team(prefs) == ('PAID1', False), str(acct.pick_team(prefs)))
check('a free one alone: taken, free', acct.pick_team({'IDEProvisioningTeamByIdentifier': {'a': [T('FREE1', True)]}}) == ('FREE1', True))
r1 = acct.pick_team({'DVTDeveloperAccountManagerAppleIDLists': {'IDE.Identifiers.Prod': [{'identifier': 'someone'}]}}); r2 = acct.pick_team({})
check('signed in, no team yet / not signed in: each its reason', r1[0] is None and 'lists no team' in r1[1] and r2[0] is None and 'not signed in' in r2[1], f'{r1} {r2}')
def dev(i, udid, transport='wired', tunnel='connected', paired='paired', mode='enabled', reality='physical', old=False):
    hw = {'reality': reality, 'platform': 'iOS', 'udid': udid}
    d = {'identifier': i, 'connectionProperties': {'transportType': transport, 'tunnelState': tunnel, 'pairingState': paired}, 'deviceProperties': {'developerModeStatus': mode}}
    d.update({'properties': {'hardware': hw}} if old else {'hardwareProperties': hw}); return d
doc = lambda *ds: {'result': {'devices': list(ds)}}
check('the wired phone before one whose tunnel is connected; one over the network only and a simulator not counted [T_ACCT_ANY_DEVICE=1: two]',
      acct.pick_device(doc(dev('W', 'UW'), dev('N', 'UN', 'localNetwork', 'connected'), dev('L', 'UL', 'localNetwork', 'disconnected'), dev('S', 'US', 'sameMachine', 'disconnected', reality='simulated'))) == ('W', 'UW'))
r = acct.pick_device(doc(dev('W1', 'U1'), dev('W2', 'U2')))
check('two wired: refused', r[0] is None and '2 iPhones' in r[1], str(r))
r = acct.pick_device(doc(dev('W', 'U', paired='unpaired'))); s = acct.pick_device(doc(dev('W', 'U', mode='disabled')))
check('untrusted / developer mode off: what to do', r[0] is None and 'Trust' in r[1] and s[0] is None and 'Developer Mode' in s[1], f'{r} {s}')
check('none: connect it', acct.pick_device(doc())[0] is None and 'connect it' in acct.pick_device(doc())[1])
check('the older JSON shape (properties.hardware)', acct.pick_device(doc(dev('W', 'UO', old=True))) == ('W', 'UO'))
check('the bundle ID: dm.<game>.<team in lower case>', acct.bundle_id('totk', 'AB12CD') == 'dm.totk.ab12cd')
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
