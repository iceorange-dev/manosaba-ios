#!/usr/bin/env python3
"""The app's name (plan item 314d, after S2Arm 4c0bec5, Apache-2.0, written anew): Info.plist's CFBundleDisplayName is the game's id, in ASCII,
because sideloading tools build the App ID from it and refuse other characters (S2Arm: AltServer for Windows 1.8.1 answered "The name for this
app is invalid" for a Chinese one); the title the player sees goes into <loc>.lproj/InfoPlist.strings for each of the game's localizations and
en, the development language iOS falls back to, so every phone shows the title as before.
Usage: app_names.py <out dir> <ascii name> <title> <localization>...   writes <out dir>/<loc>.lproj/InfoPlist.strings (UTF-8); exit 1 when the
name is not printable ASCII. T_APP_NAMES_BROKEN=noen (tools/tests/t_app_names.py): no en.lproj."""
import os, re, sys

def strings_value(s):   # a .strings literal: backslash and double quote escaped, newlines as \n
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

def write(out, name, title, locs):
    if not name or not re.fullmatch(r'[ -~]+', name): sys.exit(f'app_names.py: the app name must be printable ASCII: {name!r}')
    if any(not re.fullmatch(r'[a-z]{2,3}(-[A-Za-z0-9]{2,4})?', l) for l in locs): sys.exit(f'app_names.py: localizations like en, zh-Hant: {locs}')
    want = list(dict.fromkeys(locs + ([] if os.environ.get('T_APP_NAMES_BROKEN') == 'noen' else ['en'])))
    for l in want:
        d = os.path.join(out, f'{l}.lproj'); os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'InfoPlist.strings'), 'w', encoding='utf-8') as f:
            f.write(f'/* the title shown on the home screen (ios/tools/app_names.py) */\n"CFBundleDisplayName" = {strings_value(title)};\n')
    return want

if __name__ == '__main__':
    if len(sys.argv) < 4: print(__doc__); sys.exit(2)
    print(' '.join(write(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:])))
