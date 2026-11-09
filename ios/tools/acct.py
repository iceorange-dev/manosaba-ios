#!/usr/bin/env python3
"""The signing team and the phone without local.env (plan item 319; S2Arm's install flow, Apache-2.0, written anew):
  acct.py team                      the team Xcode is signed in with (Xcode's own preferences: `defaults export com.apple.dt.Xcode -`): a paid one
                                    first, else the free personal team; prints "<team ID> <1 if free else 0>", or a reason on stderr and exit 1
  acct.py device <devicectl json>   the one phone to use from `xcrun devicectl list devices --json-output`: a physical iPhone / iPad, wired (or its
                                    tunnel connected; wired ones first), paired, developer mode on; prints "<devicectl identifier> <hardware UDID>",
                                    or what to do on stderr and exit 1
  acct.py bundle <game> <team ID>   a bundle ID of the player's own when local.env names none: dm.<game>.<team ID in lower case>
The keys read are those this Mac's Xcode 27 and devicectl write (IDEProvisioningTeamByIdentifier: teamID, isFreeProvisioningTeam;
DVTDeveloperAccountManagerAppleIDLists: identifier; hardwareProperties: reality, platform, udid; connectionProperties: transportType, tunnelState,
pairingState; deviceProperties: developerModeStatus). Nothing is written."""
import json, os, plistlib, subprocess, sys

def teams(prefs):
    """[(team ID, free)] from Xcode's preferences, each once"""
    out, seen = [], set()
    for ts in (prefs.get('IDEProvisioningTeamByIdentifier') or {}).values():
        for t in ts or []:
            tid = t.get('teamID') if isinstance(t, dict) else None
            if tid and tid not in seen: seen.add(tid); out.append((tid, bool(t.get('isFreeProvisioningTeam'))))
    return out

def signed_in(prefs):
    acc = prefs.get('DVTDeveloperAccountManagerAppleIDLists') or {}
    return isinstance(acc, dict) and any(isinstance(xs, list) and any(isinstance(a, dict) and a.get('identifier') for a in xs) for xs in acc.values())

def pick_team(prefs):
    """(team ID, free) or (None, reason)"""
    ts = teams(prefs)
    if ts:
        paid = [t for t in ts if not t[1]]
        return (paid or ts)[0]
    if signed_in(prefs): return None, 'Xcode is signed in but lists no team yet: open Xcode → Settings → Accounts once and wait for the team to show, then run again'
    return None, 'Xcode is not signed in: Xcode → Settings → Accounts → + → Apple ID (a free account is enough), then run again'

def pick_device(doc):
    """(identifier, UDID) or (None, what to do)"""
    ds = []
    for d in (doc.get('result') or {}).get('devices') or []:
        hw = d.get('hardwareProperties') or (d.get('properties') or {}).get('hardware') or {}; cp = d.get('connectionProperties') or {}
        if hw.get('reality') != 'physical' or hw.get('platform') not in ('iOS', 'iPadOS'): continue
        if cp.get('transportType') == 'wired' or cp.get('tunnelState') == 'connected': ds.append(d)
    if os.environ.get('T_ACCT_ANY_DEVICE') != '1':   # (the broken variant of tools/tests/t_acct.py: no preference for the wired one)
        wired = [d for d in ds if (d.get('connectionProperties') or {}).get('transportType') == 'wired']
        ds = wired or ds
    if not ds: return None, 'no iPhone connected: connect it with a cable and unlock it'
    if len(ds) > 1: return None, f'{len(ds)} iPhones / iPads connected: leave only the one to install on (or set DM_DEVICE in local.env)'
    d = ds[0]; hw = d.get('hardwareProperties') or (d.get('properties') or {}).get('hardware') or {}
    if (d.get('connectionProperties') or {}).get('pairingState') != 'paired':
        return None, 'the iPhone does not trust this Mac yet: tap Trust on the phone and enter its passcode'
    if (d.get('deviceProperties') or {}).get('developerModeStatus') != 'enabled':
        return None, 'developer mode is off on the iPhone: Settings → Privacy & Security → Developer Mode, restart, unlock'
    return d.get('identifier'), hw.get('udid')

def bundle_id(game, team): return f'dm.{game}.{team.lower()}'

def main(a):
    if len(a) == 1 and a[0] == 'team':
        r = subprocess.run(['defaults', 'export', 'com.apple.dt.Xcode', '-'], capture_output=True)
        try: prefs = plistlib.loads(r.stdout) if r.returncode == 0 else {}
        except Exception: prefs = {}
        tid, x = pick_team(prefs)
        if not tid: print(f'[red] {x}', file=sys.stderr); return 1
        print(tid, 1 if x else 0); return 0
    if len(a) == 2 and a[0] == 'device':
        try: doc = json.load(open(a[1]))
        except Exception: doc = {}
        ident, x = pick_device(doc)
        if not ident: print(f'[red] {x}', file=sys.stderr); return 1
        print(ident, x); return 0
    if len(a) == 3 and a[0] == 'bundle': print(bundle_id(a[1], a[2])); return 0
    print(__doc__, file=sys.stderr); return 2

if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
