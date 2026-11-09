#!/usr/bin/env python3
"""The built app signed again with a fresh development profile instead of built again (plan item 319: a free account's profile lasts 7 days; S2Arm's
dev_sign.py, Apache-2.0, written anew):
  resign.py <app> <bundle ID> <team ID> [<device UDID>]
① the profile: of ~/Library/MobileDevice/Provisioning Profiles and ~/Library/Developer/Xcode/UserData/Provisioning Profiles (each read with
  `security cms -D`), a development one (get-task-allow) for <team>.<bundle ID>, listing the device when one is given, with more than a day left;
  the one lasting longest;
② the identity: the signing identity of this Mac's keychain (`security find-identity -v -p codesigning`) whose certificate (the SHA-1 of its DER) the
  profile holds, never a distribution one;
③ the entitlements: the app's own (`codesign -d --entitlements`) with application-identifier, team-identifier and get-task-allow set for the team,
  and each kept only when the profile allows it (a free team's leaves out iCloud and the like: named);
④ the profile embedded, the frameworks and libraries inside signed first, then the app with the entitlements, then checked (`codesign -v`).
Exit 0 signed; 3 no such profile or identity (ios/run.sh resign then asks Xcode for a profile and tries again); 1 another failure."""
import datetime, glob, hashlib, os, plistlib, re, shutil, subprocess, sys, tempfile

def utcnow(): return datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)   # plistlib's dates are naive UTC
PROFILE_DIRS = ['~/Library/MobileDevice/Provisioning Profiles', '~/Library/Developer/Xcode/UserData/Provisioning Profiles']

def choose_profile(profiles, team, bid, udid, now):
    """profiles: [(path, decoded dict)]; the development profile for team.bid (with udid when given) lasting longest, more than a day left; or None"""
    best = None
    for path, p in profiles:
        e = p.get('Entitlements') or {}; exp = p.get('ExpirationDate')
        if os.environ.get('T_RESIGN_ANY_PROFILE') != '1':   # (the broken variant of tools/tests/t_resign.py: any profile of the app)
            if e.get('get-task-allow') is not True or not isinstance(exp, datetime.datetime) or exp - now < datetime.timedelta(days=1): continue
            if udid and udid not in (p.get('ProvisionedDevices') or []): continue
        if e.get('application-identifier') != f'{team}.{bid}': continue
        if best is None or (exp or now) > (best[1].get('ExpirationDate') or now): best = (path, p)
    return best

def choose_identity(find_identity_out, profile):
    """the SHA-1 of the keychain identity whose certificate the profile holds (never a distribution one), or None"""
    certs = {hashlib.sha1(c).hexdigest().upper() for c in profile.get('DeveloperCertificates') or []}
    for line in find_identity_out.splitlines():
        m = re.match(r'\s*\d+\)\s+([0-9A-F]{40})\s+"(.*)"', line)
        if m and m.group(1) in certs and 'Distribution' not in m.group(2): return m.group(1)
    return None

def merge_entitlements(app_ents, allowed, team, bid):
    """(entitlements to sign with, keys left out as the profile does not allow them)"""
    e = dict(app_ents); e.update({'application-identifier': f'{team}.{bid}', 'com.apple.developer.team-identifier': team, 'get-task-allow': True})
    out = [k for k in e if k not in allowed]
    for k in out: del e[k]
    return e, out

def decode(path):
    r = subprocess.run(['security', 'cms', '-D', '-i', path], capture_output=True)
    try: return plistlib.loads(r.stdout) if r.returncode == 0 else None
    except Exception: return None

def main(a):
    if len(a) not in (3, 4): print(__doc__, file=sys.stderr); return 2
    app, bid, team = a[:3]; udid = a[3] if len(a) == 4 else ''
    profiles, unread = [], 0
    for d in PROFILE_DIRS:
        for f in glob.glob(os.path.join(os.path.expanduser(d), '*.mobileprovision')):
            p = decode(f)
            if p is None: unread += 1
            else: profiles.append((f, p))
    best = choose_profile(profiles, team, bid, udid, utcnow())
    if not best:
        print(f'no development profile for this app{" and this phone" if udid else ""} with more than a day left'
              + (f' ({unread} profiles could not be read: security cms failed)' if unread else ''), file=sys.stderr); return 3
    path, prof = best
    ids = subprocess.run(['security', 'find-identity', '-v', '-p', 'codesigning'], capture_output=True, text=True).stdout
    ident = choose_identity(ids, prof)
    if not ident: print('[red] the keychain holds no signing identity whose certificate the profile names', file=sys.stderr); return 3
    r = subprocess.run(['codesign', '-d', '--entitlements', '-', '--xml', app], capture_output=True)
    try: app_ents = plistlib.loads(r.stdout) if r.stdout else {}
    except Exception: app_ents = {}
    ents, out = merge_entitlements(app_ents, prof.get('Entitlements') or {}, team, bid)
    if out: print(f'left out, as the profile does not allow them: {", ".join(sorted(out))}', file=sys.stderr)
    shutil.copyfile(path, os.path.join(app, 'embedded.mobileprovision'))
    def sign(p, extra=()):
        r = subprocess.run(['codesign', '--force', '--sign', ident, '--timestamp=none', *extra, p], capture_output=True, text=True)
        if r.returncode: print(f'[red] codesign {os.path.basename(p)}: {r.stderr.strip()[-300:]}', file=sys.stderr)
        return r.returncode == 0
    nested = sorted(glob.glob(os.path.join(app, 'Frameworks', '*.dylib')) + glob.glob(os.path.join(app, 'Frameworks', '*.framework')))
    if not all(sign(n) for n in nested): return 1
    with tempfile.NamedTemporaryFile(suffix='.plist', delete=False) as tf: plistlib.dump(ents, tf); ep = tf.name
    try:
        if not sign(app, ['--entitlements', ep]): return 1
    finally: os.unlink(ep)
    v = subprocess.run(['codesign', '-v', app], capture_output=True, text=True)
    if v.returncode: print(f'[red] the signature does not verify: {v.stderr.strip()[-300:]}', file=sys.stderr); return 1
    left = prof['ExpirationDate'] - utcnow()
    iml = ents.get('com.apple.developer.kernel.increased-memory-limit') is True
    print(f'signed again with the profile {os.path.basename(path)}, valid {left.days} days {left.seconds // 3600} hours; the increased memory limit: {"yes" if iml else "no"}')
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
