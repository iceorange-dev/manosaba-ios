#!/usr/bin/env python3
# project.yml with CFBundleLocalizations (plan item 170): iOS then offers a per-app Language setting (Settings > Apps > <game> > Language) among the
# game's languages (tools/gen_game.py --get apple_localizations), and NSLocale.preferredLanguages starts with the one picked (ios/Sources/main.m).
# Usage: project_lang.py <in yml> <out yml> <localization>...
import re, sys
src, dst, locs = sys.argv[1], sys.argv[2], sys.argv[3:]
if not locs or any(not re.fullmatch(r'[a-z]{2,3}(-[A-Za-z0-9]{2,4})?', l) for l in locs): sys.exit(f'project_lang.py: localizations like en, en-GB, es-419, zh-Hant: {locs}')
s = open(src, encoding='utf-8').read()
anchor = '        CFBundleDisplayName: $(DM_GAME_NAME)   # the game\'s id in ASCII (sideloading tools build the App ID from it); the title: Gen/Names\n'
if s.count(anchor) != 1: sys.exit(f'project_lang.py: "{anchor.strip()}" should occur exactly once, found {s.count(anchor)}')
s = s.replace(anchor, anchor + '        CFBundleLocalizations: [' + ', '.join(f'"{l}"' for l in locs) + ']\n')
open(dst, 'w', encoding='utf-8').write(s)
