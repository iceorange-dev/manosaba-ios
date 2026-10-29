#!/usr/bin/env python3
"""android/tools/apk.py, the APK writer and signer (plan item 301d), read back by a reader of this test's own: the ZIP (Python's zipfile, every
entry's data and CRC), the library stored at a 16 KB boundary and resources.arsc / the icon stored at 4 bytes, the binary manifest (each android:
attribute's resource ID through the resource map as AOSP's public-final.xml gives it, attributes in ascending ID, the tree and its values),
resources.arsc (drawable/icon → its path at no density), and APK Signature Scheme v2 as source.android.com describes it (the signing block before
the central directory, the 1 MB chunk digest with the end record's directory offset taken as the block's, the signature by openssl against the
block's public key, the certificate's key the same). The same inputs give the same bytes. The phone checks what this cannot (android/tests/
run_on_phone.sh: it installs, and a damaged copy does not). T_APK_BROKEN=digest (the central directory left out of the digest): 1 fails;
T_APK_BROKEN=align (no alignment): 2 fail."""
import hashlib, io, os, struct, subprocess, sys, tempfile, zipfile, zlib
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'android', 'tools')); import apk
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<110} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))

# public-final.xml (frameworks/base core/res/res/values), as of 2026-10-30
PUBLIC = {'label': 0x01010001, 'icon': 0x01010002, 'name': 0x01010003, 'hasCode': 0x0101000c, 'debuggable': 0x0101000f, 'exported': 0x01010010,
          'launchMode': 0x0101001d, 'screenOrientation': 0x0101001e, 'configChanges': 0x0101001f, 'value': 0x01010024, 'minSdkVersion': 0x0101020c,
          'versionCode': 0x0101021b, 'versionName': 0x0101021c, 'targetSdkVersion': 0x01010270, 'allowBackup': 0x01010280,
          'extractNativeLibs': 0x010104ea, 'resizeableActivity': 0x010104f6, 'appCategory': 0x01010545}

def png(w, h):   # a small RGBA image
    raw = b''.join(b'\0' + b''.join(bytes([x * 40 % 256, y * 40 % 256, 200, 255]) for x in range(w)) for y in range(h))
    def c(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + c(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + c(b'IDAT', zlib.compress(raw)) + c(b'IEND', b'')

# ---- this test's reader ----
def pool(b):   # ResStringPool, UTF-16 or UTF-8
    t, hs, size = struct.unpack_from('<HHI', b); n, _, flags, start, _ = struct.unpack_from('<IIIII', b, 8)
    out = []
    for i in range(n):
        o = start + struct.unpack_from('<I', b, hs + 4 * i)[0]
        if flags & 0x100: k = b[o + 1]; out.append(b[o + 2:o + 2 + k].decode())   # (lengths under 0x80)
        else: k = struct.unpack_from('<H', b, o)[0]; out.append(b[o + 2:o + 2 + 2 * k].decode('utf-16-le'))
    return out, size
def read_axml(b):
    t, hs, size = struct.unpack_from('<HHI', b); assert t == 3 and size == len(b), (t, size, len(b))
    o = hs; strings = resmap = None; stack = [('', [], [])]; ns_uri = {}
    while o < size:
        t, hs, sz = struct.unpack_from('<HHI', b, o); c = b[o:o + sz]
        if t == 0x0001: strings, _ = pool(c)
        elif t == 0x0180: resmap = list(struct.unpack_from(f'<{(sz - hs) // 4}I', c, hs))
        elif t == 0x0100: p, u = struct.unpack_from('<II', c, hs); ns_uri[u] = strings[p]
        elif t == 0x0102:
            ns, name, ast, asz, n = struct.unpack_from('<IIHHH', c, hs); attrs = []
            for i in range(n):
                ans, an, raw, vs, _, dt, d = struct.unpack_from('<IIIHBBI', c, hs + ast + i * asz)
                v = strings[raw] if dt == 3 else d
                attrs.append((ns_uri.get(ans), strings[an], resmap[an] if resmap and an < len(resmap) else None, dt, v))
            el = (strings[name], attrs, []); stack[-1][2].append(el); stack.append(el)
        elif t == 0x0103: stack.pop()
        o += sz
    return stack[0][2][0]
def find(el, tag): return [k for k in el[2] if k[0] == tag]
def attr(el, name): return next(((dt, v) for _, n, _, dt, v in el[1] if n == name), None)

def read_v2(data):
    """(problem or None, the signer's certificate) by the v2 scheme"""
    e = data.rfind(b'PK\x05\x06'); cd_size, cd_off = struct.unpack_from('<II', data, e + 12)
    if data[cd_off - 16:cd_off] != b'APK Sig Block 42': return 'no signing block before the central directory', None
    size2 = struct.unpack_from('<Q', data, cd_off - 24)[0]; start = cd_off - size2 - 8
    if struct.unpack_from('<Q', data, start)[0] != size2: return 'the block\'s two sizes differ', None
    o, pairs = start + 8, {}
    while o < cd_off - 24:
        n = struct.unpack_from('<Q', data, o)[0]; pid = struct.unpack_from('<I', data, o + 8)[0]; pairs[pid] = data[o + 12:o + 8 + n]; o += 8 + n
    if 0x7109871a not in pairs: return 'no v2 block', None
    def seq(b):
        out, i = [], 0
        while i < len(b): n = struct.unpack_from('<I', b, i)[0]; out.append(b[i + 4:i + 4 + n]); i += 4 + n
        return out
    signer = seq(seq(pairs[0x7109871a])[0])[0]; signed, sigs, pub = seq(signer)
    digs, certs, _ = seq(signed)
    dg = seq(digs)[0]; alg = struct.unpack_from('<I', dg)[0]; dig = seq(dg[4:])[0]
    sg = seq(sigs)[0]; salg = struct.unpack_from('<I', sg)[0]; sig = seq(sg[4:])[0]
    if alg != 0x0103 or salg != 0x0103: return f'algorithms {alg:#x} / {salg:#x}, not RSASSA-PKCS1-v1_5 SHA2-256', None
    eocd = data[e:e + 16] + struct.pack('<I', start) + data[e + 20:]
    ds = []
    for sec in (data[:start], data[cd_off:e], eocd):
        for i in range(0, len(sec), 1 << 20): c = sec[i:i + (1 << 20)]; ds.append(hashlib.sha256(b'\xa5' + struct.pack('<I', len(c)) + c).digest())
    if hashlib.sha256(b'\x5a' + struct.pack('<I', len(ds)) + b''.join(ds)).digest() != dig: return 'the digest does not match the contents', None
    with tempfile.TemporaryDirectory() as d:
        open(f'{d}/pub.der', 'wb').write(pub); open(f'{d}/sig', 'wb').write(sig); open(f'{d}/signed', 'wb').write(signed)
        subprocess.run(['openssl', 'pkey', '-pubin', '-inform', 'DER', '-in', f'{d}/pub.der', '-out', f'{d}/pub.pem'], check=True, capture_output=True)
        r = subprocess.run(['openssl', 'dgst', '-sha256', '-verify', f'{d}/pub.pem', '-signature', f'{d}/sig', f'{d}/signed'], capture_output=True)
        if r.returncode: return 'the signature does not verify', None
        cert = seq(certs)[0]; open(f'{d}/c.der', 'wb').write(cert)
        cp = subprocess.run(['openssl', 'x509', '-inform', 'DER', '-in', f'{d}/c.der', '-pubkey', '-noout'], check=True, capture_output=True).stdout
        cpd = subprocess.run(['openssl', 'pkey', '-pubin', '-outform', 'DER'], input=cp, check=True, capture_output=True).stdout
        if cpd != pub: return 'the certificate holds another key', None
    return None, cert

with tempfile.TemporaryDirectory() as d:
    lib = os.urandom(70001); big = os.urandom(3 << 20)   # 3 MB: the digest's chunks
    open(f'{d}/lib.so', 'wb').write(lib); open(f'{d}/icon.png', 'wb').write(png(4, 4)); open(f'{d}/big.bin', 'wb').write(big); open(f'{d}/a.tsv', 'wb').write(b'x\ty\n')
    args = dict(lib=f'{d}/lib.so', lib_name='dmhost', pkg='org.example.dmtest', label='魔法少女ノ魔女裁判', version_code=125, version_name='b125',
                icon=f'{d}/icon.png', assets=[('product/main.patched.bin', f'{d}/big.bin'), ('product/main.gates.tsv', f'{d}/a.tsv')], key=f'{d}/k/key.pem', debuggable=True)
    data = apk.build(**args)
    check('a second build of the same inputs: the same bytes (and the key made once, kept)', apk.build(**args) == data and os.path.exists(f'{d}/k/key.pem'))
    open(f'{d}/t.apk', 'wb').write(data)
    z = zipfile.ZipFile(f'{d}/t.apk'); names = z.namelist()
    check('the ZIP reads: every entry, its CRC right', z.testzip() is None and names == ['AndroidManifest.xml', 'resources.arsc', apk.ICON_PATH,
          'lib/arm64-v8a/libdmhost.so', 'assets/product/main.patched.bin', 'assets/product/main.gates.tsv'], names)
    check('  the library, the assets and the icon as given', z.read('lib/arm64-v8a/libdmhost.so') == lib and z.read('assets/product/main.patched.bin') == big and
          z.read(apk.ICON_PATH) == open(f'{d}/icon.png', 'rb').read())
    def data_off(i): h = struct.unpack_from('<IHHHHHIIIHH', data, i.header_offset); return i.header_offset + 30 + h[9] + h[10]
    info = {i.filename: i for i in z.infolist()}
    so = info['lib/arm64-v8a/libdmhost.so']
    check('the library stored, its data at a 16 KB boundary of the file', so.compress_type == 0 and data_off(so) % 16384 == 0, (so.compress_type, data_off(so) % 16384))
    check('resources.arsc and the icon stored at a 4-byte boundary; the assets deflated', all(info[n].compress_type == 0 and data_off(info[n]) % 4 == 0 for n in ('resources.arsc', apk.ICON_PATH))
          and info['assets/product/main.patched.bin'].compress_type == 8)
    m = read_axml(z.read('AndroidManifest.xml'))
    def walk(el):
        yield el
        for k in el[2]: yield from walk(k)
    bad = [(el[0], n, rid) for el in walk(m) for ns, n, rid, _, _ in el[1] if ns == apk.ANDROID_NS and rid != PUBLIC.get(n)]
    check('each android: attribute names its public-final.xml resource ID through the resource map', not bad, bad)
    order = [el[0] for el in walk(m) if [a[2] if a[2] is not None else 1 << 32 for a in el[1]] != sorted(a[2] if a[2] is not None else 1 << 32 for a in el[1])]
    check('  in ascending ID in every element (the one without, package, last)', not order, order)
    app = find(m, 'application')[0]; act = find(app, 'activity')[0]; sdk = find(m, 'uses-sdk')[0]
    md = find(act, 'meta-data')[0]; filt = find(act, 'intent-filter')[0]
    want = [(attr(m, 'package'), (3, 'org.example.dmtest')), (attr(m, 'versionCode'), (0x10, 125)), (attr(m, 'versionName'), (3, 'b125')),
            (attr(sdk, 'minSdkVersion'), (0x10, 35)), (attr(sdk, 'targetSdkVersion'), (0x10, 35)),
            (attr(app, 'label'), (3, '魔法少女ノ魔女裁判')), (attr(app, 'icon'), (0x01, 0x7f010000)), (attr(app, 'hasCode'), (0x12, 0)),
            (attr(app, 'debuggable'), (0x12, 0xffffffff)), (attr(app, 'extractNativeLibs'), (0x12, 0)), (attr(app, 'appCategory'), (0x10, 0)),
            (attr(act, 'name'), (3, 'android.app.NativeActivity')), (attr(act, 'screenOrientation'), (0x10, 6)), (attr(act, 'launchMode'), (0x10, 2)),
            (attr(act, 'configChanges'), (0x11, 0x1ff0)), (attr(md, 'name'), (3, 'android.app.lib_name')), (attr(md, 'value'), (3, 'dmhost')),
            (attr(find(filt, 'action')[0], 'name'), (3, 'android.intent.action.MAIN')), (attr(find(filt, 'category')[0], 'name'), (3, 'android.intent.category.LAUNCHER'))]
    check('the manifest: package, versions, SDK 35, label, icon, no code, debuggable, the library mapped, a game; NativeActivity landscape, launcher',
          all(g == w for g, w in want), [(g, w) for g, w in want if g != w])
    r = z.read('resources.arsc'); t, hs, size = struct.unpack_from('<HHI', r); gp, gsz = pool(r[hs:])
    po = hs + gsz; pt, phs, psz = struct.unpack_from('<HHI', r, po); pid = struct.unpack_from('<I', r, po + 8)[0]
    typestr, _ = pool(r[po + struct.unpack_from('<I', r, po + 8 + 4 + 256)[0]:]); keystr, _ = pool(r[po + struct.unpack_from('<I', r, po + 8 + 4 + 256 + 8)[0]:])
    o, typ = po + phs, None
    while o < po + psz:
        ct, chs, csz = struct.unpack_from('<HHI', r, o)
        if ct == 0x0201: typ = r[o:o + csz]; break
        o += csz
    ok = t == 2 and size == len(r) and gp == [apk.ICON_PATH] and pt == 0x0200 and pid == 0x7f and typestr == ['drawable'] and keystr == ['icon'] and typ is not None
    if ok:
        tid, _, _, cnt, est = struct.unpack_from('<BBHII', typ, 8); csize = struct.unpack_from('<I', typ, 20)[0]; dens = struct.unpack_from('<H', typ, 20 + 14)[0]
        sdkv = struct.unpack_from('<H', typ, 20 + 24)[0]; e0 = struct.unpack_from('<I', typ, struct.unpack_from('<H', typ, 2)[0])[0]
        esz, efl, key, vs, _, vt, vd = struct.unpack_from('<HHIHBBI', typ, est + e0)
        ok = tid == 1 and cnt == 1 and csize == 64 and dens == 0xffff and sdkv == 4 and keystr[key] == 'icon' and vt == 3 and gp[vd] == apk.ICON_PATH
    check('resources.arsc: package 0x7f, drawable/icon (0x7f010000) → its PNG at no density (nodpi-v4)', ok)
    problem, cert = read_v2(data)
    check('APK Signature Scheme v2: the block, the digest of contents, directory and end record, the signature and the certificate', problem is None, problem)
    bad = bytearray(data); i = data_off(so) + 1000; bad[i] ^= 1
    check('  a byte of the library changed: the digest no longer matches', read_v2(bytes(bad))[0] == 'the digest does not match the contents')
    args['icon'] = None; z2 = zipfile.ZipFile(io.BytesIO(apk.build(**args))); m2 = read_axml(z2.read('AndroidManifest.xml'))
    check('without an icon: no icon attribute, no resources.arsc', attr(find(m2, 'application')[0], 'icon') is None and 'resources.arsc' not in z2.namelist())
print(f'== {total} checks, {fails} failed ==')
sys.exit(fails != 0)
