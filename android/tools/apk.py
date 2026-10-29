#!/usr/bin/env python3
"""The APK writer and signer of the Android host (plan item 301d): Python's standard library and the openssl command, no Android SDK and no JDK
(after shirai91/nx2android's mkapk.py, MIT, written anew).

  apk.py --lib libdmhost.so --out game.apk --package P --label L [--version-code N] [--version-name V] [--icon icon.png]
         [--asset NAME=FILE ...] [--product DIR --modules "main sdk ..."] [--key key.pem] [--release]

The APK holds
  AndroidManifest.xml   binary XML (frameworks/base libs/androidfw ResourceTypes.h): a NativeActivity without code (hasCode false) that loads
                        lib/arm64-v8a/lib<name>.so (meta-data android.app.lib_name), landscape (sensorLandscape), its configuration changes
                        handled by the app so that a rotation or a keyboard does not recreate the activity, a game (appCategory game)
  resources.arsc        with --icon only: one drawable, icon (0x7f010000), at no density (res/drawable-nodpi-v4/icon.png)
  lib/arm64-v8a/        the host library, stored and at a 16 KB boundary of the file (extractNativeLibs false: the system maps it from the APK)
  assets/<NAME>         each --asset, deflated (the app reads them with AAssetManager); --product: assets/product/ holds each module's
                        <module>.patched.bin and the files ios/tools/product_files.py lists for it (the loader's fixups and gates, options)
signed with APK Signature Scheme v2 (source.android.com/docs/security/features/apksigning/v2): RSASSA-PKCS1-v1_5 with SHA2-256 (0x0103) by a
self-signed RSA-2048 certificate. The key (--key, default ~/.config/dm-android/apk-key.pem, mode 600) is made on first use and kept: an APK signed
by another key does not install over this one without uninstalling it, which deletes the app's saves. The attribute resource IDs and enum values
are AOSP's (core/res/res/values/public-final.xml, attrs_manifest.xml). android/tests/t_apk.py checks the output with a reader of its own."""
import argparse, hashlib, os, struct, subprocess, sys, tempfile, zlib

ANDROID_NS = 'http://schemas.android.com/apk/res/android'
ATTR = {   # android: attributes (public-final.xml)
    'label': 0x01010001, 'icon': 0x01010002, 'name': 0x01010003, 'hasCode': 0x0101000c, 'debuggable': 0x0101000f, 'exported': 0x01010010,
    'launchMode': 0x0101001d, 'screenOrientation': 0x0101001e, 'configChanges': 0x0101001f, 'value': 0x01010024, 'minSdkVersion': 0x0101020c,
    'versionCode': 0x0101021b, 'versionName': 0x0101021c, 'targetSdkVersion': 0x01010270, 'allowBackup': 0x01010280,
    'extractNativeLibs': 0x010104ea, 'resizeableActivity': 0x010104f6, 'appCategory': 0x01010545}
# configChanges flags (attrs_manifest.xml): keyboard, keyboardHidden, navigation, orientation, screenLayout, uiMode, screenSize, smallestScreenSize, density
CONFIG_CHANGES = 0x0010 | 0x0020 | 0x0040 | 0x0080 | 0x0100 | 0x0200 | 0x0400 | 0x0800 | 0x1000
SENSOR_LANDSCAPE, SINGLE_TASK, CATEGORY_GAME = 6, 2, 0
MIN_SDK = TARGET_SDK = 35
ICON_ID = 0x7f010000   # package 0x7f, type 1 (drawable), entry 0
ICON_PATH = 'res/drawable-nodpi-v4/icon.png'
# Res_value data types and chunk types (ResourceTypes.h)
T_REFERENCE, T_STRING, T_INT_DEC, T_INT_HEX, T_BOOL = 0x01, 0x03, 0x10, 0x11, 0x12
RES_STRING_POOL, RES_TABLE, RES_XML = 0x0001, 0x0002, 0x0003
XML_START_NS, XML_END_NS, XML_START_EL, XML_END_EL, XML_RES_MAP = 0x0100, 0x0101, 0x0102, 0x0103, 0x0180
TABLE_PACKAGE, TABLE_TYPE, TABLE_TYPE_SPEC = 0x0200, 0x0201, 0x0202
DENSITY_NONE = 0xffff   # ACONFIGURATION_DENSITY_NONE (the NDK's android/configuration.h)
NONE = 0xffffffff

def manifest(pkg, label, version_code, version_name, lib, icon, debuggable):
    """The manifest as (tag, [(android namespace?, name, value)], children); a value is a str, bool, int, ('hex', n) or ('ref', resource id)"""
    app = [(True, 'label', label)] + ([(True, 'icon', ('ref', ICON_ID))] if icon else []) + [
        (True, 'hasCode', False), (True, 'debuggable', debuggable), (True, 'allowBackup', False), (True, 'extractNativeLibs', False),
        (True, 'appCategory', CATEGORY_GAME)]
    act = [(True, 'name', 'android.app.NativeActivity'), (True, 'exported', True), (True, 'launchMode', SINGLE_TASK),
           (True, 'screenOrientation', SENSOR_LANDSCAPE), (True, 'configChanges', ('hex', CONFIG_CHANGES)), (True, 'resizeableActivity', False)]
    return ('manifest', [(False, 'package', pkg), (True, 'versionCode', version_code), (True, 'versionName', version_name)], [
        ('uses-sdk', [(True, 'minSdkVersion', MIN_SDK), (True, 'targetSdkVersion', TARGET_SDK)], []),
        ('application', app, [
            ('activity', act, [
                ('meta-data', [(True, 'name', 'android.app.lib_name'), (True, 'value', lib)], []),
                ('intent-filter', [], [
                    ('action', [(True, 'name', 'android.intent.action.MAIN')], []),
                    ('category', [(True, 'name', 'android.intent.category.LAUNCHER')], [])])])])])

def chunk(kind, header, body=b''):   # ResChunk_header {type, headerSize, size} + the rest of the header + the body
    return struct.pack('<HHI', kind, 8 + len(header), 8 + len(header) + len(body)) + header + body

def string_pool(strings):   # UTF-16 (no UTF8_FLAG), no styles
    offs, data = [], b''
    for s in strings:
        u = s.encode('utf-16-le'); n = len(u) // 2
        if n >= 0x8000: raise ValueError(f'string too long for the pool: {s[:40]}')
        offs.append(len(data)); data += struct.pack('<H', n) + u + b'\0\0'
    data += b'\0' * (-len(data) % 4)
    return chunk(RES_STRING_POOL, struct.pack('<IIIII', len(strings), 0, 0, 28 + 4 * len(strings), 0), b''.join(struct.pack('<I', o) for o in offs) + data)

def attrs_of(node):
    yield from node[1]
    for k in node[2]: yield from attrs_of(k)

def axml(root):
    """Binary XML: the string pool (the attribute names that have resource IDs first, in the resource map's order), the resource map, the android
    namespace around the elements; each element's attributes in ascending resource ID, the one without (package) last"""
    named = sorted({n for ns, n, _ in attrs_of(root) if ns}, key=lambda n: ATTR[n])
    strings = list(named)
    def s(x):
        if x not in strings: strings.append(x)
        return strings.index(x)
    prefix, uri = s('android'), s(ANDROID_NS)
    body, line = [], [1]
    def element(n):
        tag, attrs, kids = n
        enc = []
        for ns, name, v in attrs:
            if isinstance(v, bool): raw, t, d = NONE, T_BOOL, NONE if v else 0
            elif isinstance(v, int): raw, t, d = NONE, T_INT_DEC, v
            elif isinstance(v, tuple): raw, t, d = NONE, {'hex': T_INT_HEX, 'ref': T_REFERENCE}[v[0]], v[1]
            else: raw, t, d = s(v), T_STRING, s(v)
            enc.append((ATTR[name] if ns else NONE, struct.pack('<IIIHBBI', uri if ns else NONE, s(name), raw, 8, 0, t, d)))
        enc.sort(key=lambda e: e[0])
        node = struct.pack('<II', line[0], NONE)   # ResXMLTree_node: lineNumber, comment
        body.append(chunk(XML_START_EL, node, struct.pack('<IIHHHHHH', NONE, s(tag), 20, 20, len(enc), 0, 0, 0) + b''.join(e for _, e in enc)))
        line[0] += 1
        for k in kids: element(k)
        body.append(chunk(XML_END_EL, struct.pack('<II', line[0], NONE), struct.pack('<II', NONE, s(tag)))); line[0] += 1
    element(root)
    ns = struct.pack('<II', prefix, uri)
    nodes = chunk(XML_START_NS, struct.pack('<II', 1, NONE), ns) + b''.join(body) + chunk(XML_END_NS, struct.pack('<II', line[0], NONE), ns)
    return chunk(RES_XML, b'', string_pool(strings) + chunk(XML_RES_MAP, b'', b''.join(struct.pack('<I', ATTR[n]) for n in named)) + nodes)

def arsc_icon(pkg):
    """resources.arsc with one resource: drawable/icon → ICON_PATH, at no density (ResTable_config density 0xffff, sdkVersion 4: drawable-nodpi-v4)"""
    config = struct.pack('<I', 64) + bytes(4 + 4) + struct.pack('<BBH', 0, 0, DENSITY_NONE) + bytes(4 + 4) + struct.pack('<HH', 4, 0) + bytes(64 - 28)
    spec = chunk(TABLE_TYPE_SPEC, struct.pack('<BBHI', 1, 0, 0, 1), struct.pack('<I', 0x0100))   # the entry varies by density (ACONFIGURATION_DENSITY)
    entry = struct.pack('<HHI', 8, 0, 0) + struct.pack('<HBBI', 8, 0, T_STRING, 0)   # ResTable_entry {size, flags, key "icon"} + Res_value → string 0
    typ = chunk(TABLE_TYPE, struct.pack('<BBHII', 1, 0, 0, 1, 8 + 12 + 64 + 4) + config, struct.pack('<I', 0) + entry)
    name = pkg.encode('utf-16-le')[:254].ljust(256, b'\0')
    types, keys = string_pool(['drawable']), string_pool(['icon'])
    head = struct.pack('<I', 0x7f) + name + struct.pack('<IIIII', 288, 1, 288 + len(types), 1, 0)   # typeStrings / keyStrings offsets from the chunk's start
    package = chunk(TABLE_PACKAGE, head, types + keys + spec + typ)
    return chunk(RES_TABLE, struct.pack('<I', 1), string_pool([ICON_PATH]) + package)

DOS_DATE = (2026 - 1980) << 9 | 1 << 5 | 1   # 2026-01-01 00:00 for every entry: the same inputs give the same APK
def build_zip(entries):
    """entries: (name, data, deflate, align). Returns (the entries' section, central directory, end record). An aligned entry is stored and its data
    starts at a multiple of align in the file, padded by an extra field (id 0xd935, the one zipalign writes)"""
    out, cd = bytearray(), bytearray()
    for name, data, deflate, align in entries:
        nb = name.encode(); crc = zlib.crc32(data) & NONE
        if deflate: c = zlib.compressobj(9, zlib.DEFLATED, -15); packed = c.compress(data) + c.flush(); method, ver = 8, 20
        else: packed, method, ver = data, 0, 10
        extra = b''
        if align > 1 and os.environ.get('T_APK_BROKEN') != 'align':   # (the broken variant of t_apk.py: none aligned)
            assert not deflate, f'{name}: an aligned entry is stored'
            pad = -(len(out) + 30 + len(nb)) % align
            if pad and pad < 4: pad += align   # an extra field takes at least its 4-byte header
            if pad: extra = struct.pack('<HH', 0xd935, pad - 4) + bytes(pad - 4)
        off = len(out)
        out += struct.pack('<IHHHHHIIIHH', 0x04034b50, ver, 0, method, 0, DOS_DATE, crc, len(packed), len(data), len(nb), len(extra)) + nb + extra + packed
        cd += struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, ver, ver, 0, method, 0, DOS_DATE, crc, len(packed), len(data), len(nb), 0, 0, 0, 0, 0, off) + nb
    if len(out) + len(cd) >= 1 << 32: raise ValueError('APK past 4 GB (no ZIP64 here)')
    eocd = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(entries), len(entries), len(cd), len(out), 0)
    return bytes(out), bytes(cd), eocd

def lp(b): return struct.pack('<I', len(b)) + b   # uint32 length prefix
def v2_digest(sections):   # 1 MB chunks: 0xa5 + length + chunk each, then 0x5a + count + the chunk digests
    ds = []
    for sec in sections:
        for i in range(0, len(sec), 1 << 20):
            c = sec[i:i + (1 << 20)]; ds.append(hashlib.sha256(b'\xa5' + struct.pack('<I', len(c)) + c).digest())
    return hashlib.sha256(b'\x5a' + struct.pack('<I', len(ds)) + b''.join(ds)).digest()
ALG_RSA_PKCS1_SHA256 = 0x0103
V2_BLOCK_ID = 0x7109871a
def v2_sign(body, cd, eocd, sign, cert, spki):
    """The APK with its signing block between the entries and the central directory. The digest covers the entries, the central directory and the
    end record with its central directory offset taken as the signing block's (= len(body), what eocd already holds)"""
    digest = v2_digest([body, eocd] if os.environ.get('T_APK_BROKEN') == 'digest' else [body, cd, eocd])   # (the broken variant of t_apk.py: no directory)
    signed = lp(lp(struct.pack('<I', ALG_RSA_PKCS1_SHA256) + lp(digest))) + lp(lp(cert)) + lp(b'')
    signer = lp(signed) + lp(lp(struct.pack('<I', ALG_RSA_PKCS1_SHA256) + lp(sign(signed)))) + lp(spki)
    pair = struct.pack('<QI', 4 + len(lp(lp(signer))), V2_BLOCK_ID) + lp(lp(signer))
    size = len(pair) + 8 + 16
    block = struct.pack('<Q', size) + pair + struct.pack('<Q', size) + b'APK Sig Block 42'
    return body + block + cd + eocd[:16] + struct.pack('<I', len(body) + len(block)) + eocd[20:]

def openssl(*args, data=None):
    r = subprocess.run(['openssl', *args], input=data, capture_output=True)
    if r.returncode: raise RuntimeError(f'openssl {args[0]}: {r.stderr.decode(errors="replace").strip()}')
    return r.stdout
def signing_key(path):
    """(sign(bytes) → signature, certificate DER, SubjectPublicKeyInfo DER) of the key at path, made there with its certificate first"""
    crt = path + '.crt.der'
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path) or '.', mode=0o700, exist_ok=True)
        old = os.umask(0o077)
        try: openssl('genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048', '-out', path)
        finally: os.umask(old)
        print(f'apk: a new signing key {path}', file=sys.stderr)
    if not os.path.exists(crt):
        openssl('req', '-new', '-x509', '-key', path, '-sha256', '-days', '10000', '-subj', '/CN=dm android', '-outform', 'DER', '-out', crt)
    return (lambda b: openssl('dgst', '-sha256', '-sign', path, data=b)), open(crt, 'rb').read(), openssl('pkey', '-in', path, '-pubout', '-outform', 'DER')

def build(lib, lib_name, pkg, label, version_code, version_name, icon, assets, key, debuggable):
    sign, cert, spki = signing_key(key)
    entries = [('AndroidManifest.xml', axml(manifest(pkg, label, version_code, version_name, lib_name, bool(icon), debuggable)), True, 1)]
    if icon: entries += [('resources.arsc', arsc_icon(pkg), False, 4), (ICON_PATH, open(icon, 'rb').read(), False, 4)]
    entries += [(f'lib/arm64-v8a/lib{lib_name}.so', open(lib, 'rb').read(), False, 16384)]
    entries += [(f'assets/{n}', open(f, 'rb').read(), True, 1) for n, f in assets]
    return v2_sign(*build_zip(entries), sign, cert, spki)

def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--lib', required=True); p.add_argument('--out', required=True); p.add_argument('--package', required=True); p.add_argument('--label', required=True)
    p.add_argument('--lib-name', default='dmhost'); p.add_argument('--version-code', type=int, default=1); p.add_argument('--version-name', default='1')
    p.add_argument('--icon'); p.add_argument('--asset', action='append', default=[], metavar='NAME=FILE')
    p.add_argument('--product'); p.add_argument('--modules', default='')
    p.add_argument('--key', default=os.path.expanduser('~/.config/dm-android/apk-key.pem')); p.add_argument('--release', action='store_true', help='not debuggable')
    a = p.parse_args()
    assets = [tuple(x.split('=', 1)) for x in a.asset]
    if a.product:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'ios', 'tools')); import product_files
        mods = a.modules.split(); paths, bad = product_files.files(a.product, mods)
        if bad: sys.exit('apk: ' + ', '.join(f'missing {b}' for b in bad))
        assets += [(f'product/{os.path.basename(f)}', f) for f in [os.path.join(a.product, f'{m}.patched.bin') for m in mods] + paths]
    apk = build(a.lib, a.lib_name, a.package, a.label, a.version_code, a.version_name, a.icon, assets, a.key, not a.release)
    tmp = a.out + '.tmp'; open(tmp, 'wb').write(apk); os.replace(tmp, a.out)
    print(f'apk: {a.out} {len(apk) / 1e6:.1f} MB, {a.package} {a.version_name} ({a.version_code}), {len(assets)} assets')

if __name__ == '__main__': main()
