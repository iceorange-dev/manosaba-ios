#!/usr/bin/env python3
# Version of the shader translation chain (the build result must be determined by the sources alone).
# V = the first 12 hex digits of sha256 over the contents of these files plus the translation options: this script, dmsh_build.py, dmsh_fetch.py,
# the dmsh_xlate sources (Program.cs, dmsh_xlate.csproj), the iOS compile convention ios_metal.py, the MSL rewrite msl_helpers.py and the tool lock dmsh_chain.lock (fingerprints of
# the translator, spirv-cross and the Metal compilers). dmsh_build.py writes into <cache root>/b-<V>/ and records chain = V in every reflection
# JSON; the host takes V from nvn/dmsh_chain.h and loads only from b-<V>/.
# Usage: dmsh_chain.py                 print V (default options)
#        dmsh_chain.py --header        rewrite nvn/dmsh_chain.h (after changing any file above)
#        dmsh_chain.py --check-tools   check the installed tools against the lock file; exit 1 on a mismatch
#        dmsh_chain.py --update-lock   rewrite the lock file from the installed tools (after changing Ryujinx / spirv-cross / Xcode; commit it)
# Environment: XLATE_DEBUG / XLATE_REDUCED (as in dmsh_build.py, default 1 / 1; other options give another V, which the host does not load).
# XLATE_DEBUG=0 is refused (plan items 200 and 64g): the translator's optimizer then loses call arguments and else-if bodies (Program.cs)
import os, sys, hashlib, subprocess
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # Workspace root (game files, build outputs and run artifacts are all under it)
HERE = os.path.dirname(os.path.abspath(__file__)); GFX = os.path.dirname(HERE); HOME = os.path.expanduser('~')
FILES = ['tools/dmsh_chain.py', 'tools/dmsh_build.py', 'tools/dmsh_fetch.py', 'tools/dmsh_xlate/Program.cs', 'tools/dmsh_xlate/dmsh_xlate.csproj',
         'tools/ios_metal.py', 'tools/dmsh_chain.lock', 'tools/msl_helpers.py']
LOCK = os.path.join(HERE, 'dmsh_chain.lock'); HEADER = os.path.join(GFX, 'nvn/dmsh_chain.h')
XLATE_DIR = os.path.dirname(os.environ.get('XLATE', f'{DM_ROOT}/build/gfx/dmsh_xlate/dmsh_xlate'))

XLATE_DEBUG_REFUSED = ('[red] XLATE_DEBUG=0 is refused: without the translator\'s debug mode its optimizer gives Tears of the Kingdom 306 failures '
                       '(call arguments) and 4,175 modules with unreachable else-if bodies (plan item 200)')

def options():
    if os.environ.get('XLATE_DEBUG', '1') == '0': sys.exit(XLATE_DEBUG_REFUSED)
    return 'debug=%s reduced=%s' % (os.environ.get('XLATE_DEBUG', '1') != '0', os.environ.get('XLATE_REDUCED', '1') == '1')

def version():
    h = hashlib.sha256()
    for f in FILES:
        h.update(f.encode() + b'\0' + open(os.path.join(GFX, f), 'rb').read() + b'\0')
    h.update(options().encode())
    return h.hexdigest()[:12]

def sha16(p): return hashlib.sha256(open(p, 'rb').read()).hexdigest()[:16]

def tools_now():
    """Actual tool fingerprints: translator dll (including the Ryujinx shader library), spirv-cross, metal compiler (macOS / iOS)"""
    t = {}
    for dll in ('dmsh_xlate.dll', 'Ryujinx.Graphics.Shader.dll', 'Ryujinx.Common.dll'): t[dll] = sha16(os.path.join(XLATE_DIR, dll))
    t['xlate_source'] = subprocess.run([os.path.join(XLATE_DIR, 'dmsh_xlate'), '--source-sha'], capture_output=True, text=True).stdout.strip()
    sc = subprocess.run(['which', 'spirv-cross'], capture_output=True, text=True).stdout.strip()
    t['spirv-cross'] = sha16(sc) if sc else 'missing'
    for sdk in ('macosx', 'iphoneos'):
        t['metal_' + sdk] = subprocess.run(['xcrun', '-sdk', sdk, 'metal', '--version'], capture_output=True, text=True).stdout.splitlines()[0:1] or ['missing']
        t['metal_' + sdk] = t['metal_' + sdk][0]
    return t

def xlate_source_sha():   # same convention as dmsh_xlate --source-sha: the embedded Program.cs and csproj concatenated in that order
    return hashlib.sha256(b''.join(open(os.path.join(HERE, 'dmsh_xlate', f), 'rb').read() for f in ('Program.cs', 'dmsh_xlate.csproj'))).hexdigest()[:16]

def check_tools():
    want = dict(l.rstrip('\n').split('\t', 1) for l in open(LOCK) if '\t' in l and not l.startswith('#'))
    now = tools_now(); bad = [f'{k}: lock {want.get(k)} / installed {v}' for k, v in now.items() if want.get(k) != v]
    if now['xlate_source'] != xlate_source_sha(): bad.append(f'the translator was not built from these sources: embedded {now["xlate_source"]} / here {xlate_source_sha()} (rebuild dmsh_xlate)')
    return bad

if __name__ == '__main__':
    a = sys.argv[1:]
    if a == ['--update-lock']:
        t = tools_now()
        if t['xlate_source'] != xlate_source_sha(): sys.exit(f'[red] the translator was not built from these sources (embedded {t["xlate_source"]} / here {xlate_source_sha()}): rebuild dmsh_xlate first')
        open(LOCK, 'w').write('# Translation chain tool fingerprints (dmsh_chain.py --update-lock; regenerate after changing a tool and commit)\n' + ''.join(f'{k}\t{v}\n' for k, v in t.items()))
        print('wrote', LOCK)
    elif a == ['--check-tools']:
        bad = check_tools()
        for b in bad: print('[red] translation chain tool differs from the lock file: ' + b)
        sys.exit(1 if bad else 0)
    elif a == ['--header']:
        open(HEADER, 'w').write('// Generated by gfx/tools/dmsh_chain.py --header: the shader translation chain version this tree expects; the host loads only <library dir>/b-<V>/\n'
                                f'#define DMSH_CHAIN "{version()}"\n')
        print(version())
    elif not a: print(version())
    else: sys.exit('usage: see the head of this file')
