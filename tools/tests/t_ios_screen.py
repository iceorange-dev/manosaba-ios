#!/usr/bin/env python3
"""The screen the iOS app reads (plan item 302, iPhone Duo: two displays, the outer and the inner one; UIScreen.mainScreen does not say which
the game's window is on): no code in ios/Sources or gfx/nvn reads UIScreen's main screen (comments left out); mtl_display_hz's iOS branch
(gfx/nvn/mtl/texdump.inc) returns front_ios_screen_hz(), which ios/Sources/front_ios.m defines from the screen the front end notes on the main
thread, at the view's creation (the scene's screen) and on every layout of the black container (its window scene's screen); the frame rate hint
asks the game view's window scene's screen for 120 Hz. The old lines put back (T_IOS_SCREEN_OLD=1, the broken variant): 3 fail."""
import glob, os, re, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))
def code(text):   # without // comments (none of these files has // inside a string)
    return '\n'.join(line.split('//', 1)[0] for line in text.split('\n'))
src = {p: open(p, encoding='utf-8').read() for p in sorted(glob.glob(os.path.join(ROOT, 'ios', 'Sources', '*.m')) + glob.glob(os.path.join(ROOT, 'gfx', 'nvn', '**', '*.m'), recursive=True)
                                                            + glob.glob(os.path.join(ROOT, 'gfx', 'nvn', '**', '*.inc'), recursive=True))}
FRONT, TEXDUMP = os.path.join(ROOT, 'ios', 'Sources', 'front_ios.m'), os.path.join(ROOT, 'gfx', 'nvn', 'mtl', 'texdump.inc')
if os.environ.get('T_IOS_SCREEN_OLD'):   # the broken variant: the lines before plan item 302
    src[TEXDUMP] = src[TEXDUMP].replace('    return front_ios_screen_hz();',
        '    Class c = NSClassFromString(@"UIScreen"); id s = c ? [c valueForKey:@"mainScreen"] : nil;\n    return s ? [[s valueForKey:@"maximumFramesPerSecond"] intValue] : 0;')
    src[FRONT] = src[FRONT].replace('g_view.window.windowScene.screen.maximumFramesPerSecond >= 120', 'UIScreen.mainScreen.maximumFramesPerSecond >= 120')
main = re.compile(r'UIScreen\.mainScreen|\[\s*UIScreen\s+mainScreen\s*\]|@"mainScreen"|UIScreen\.main\b')
hits = [f'{os.path.relpath(p, ROOT)}: {m.group(0)}' for p, t in src.items() for m in main.finditer(code(t))]
check('no code in ios/Sources or gfx/nvn reads UIScreen\'s main screen', not hits, '; '.join(hits))
td, fr = code(src[TEXDUMP]), code(src[FRONT])
ios_branch = td.split('int mtl_display_hz(void) {', 1)[-1].split('#else', 1)[-1].split('#endif', 1)[0]
check('mtl_display_hz on iOS returns front_ios_screen_hz(), which front_ios.m defines; noted at the view\'s creation and on each layout',
      'return front_ios_screen_hz();' in ios_branch and re.search(r'int front_ios_screen_hz\(void\) \{ return atomic_load\(&g_screen_hz\); \}', fr) is not None
      and 'screen_note(screen);' in fr and 'screen_note(self.window.windowScene.screen);' in fr, ios_branch.strip()[:200])
check('the frame rate hint asks the game view\'s window scene\'s screen for 120 Hz', 'g_view.window.windowScene.screen.maximumFramesPerSecond >= 120' in fr)
print(f'{total} checks, {fails} failed')
sys.exit(1 if fails else 0)
