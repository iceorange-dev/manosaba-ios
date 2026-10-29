# Rewrite structural check: no code is run, the rewriter's build output is checked word by word. Denominators are recounted with an independent
# disassembler (llvm-objdump; Xcode's by default, LLVM_BIN overrides), not relying on the rewriter's own capstone scan.
# Usage: structck.py <build output dir> <module> <NSO>     last output line STRUCT <module> OK|FAIL ..., exit code 0/1
# Checks:
#  ⓪ Cuts (cuts.tsv): build ID matches, original image at that site == original bytes, rewritten image == new bytes, reason non-empty, no overlap with
#     rewrite sites / windows; host calls introduced by cuts (svc #N, N = 0x80–0xff) are rewritten into gates and have entry form (svc ; ret at a function
#     entry) or call-site form (a single bl replaced)
#  ① Non-text segments are byte-for-byte identical to the original image except for cuts; everything from the end of the image to the patch area is 0
#  ② Changed words in text == rewrite site heads (non-J rows of sites.tsv + window starts) ∪ cuts inside text; all rewrite site heads are a B into the patch area
#  ③ Independent denominator: llvm-objdump reads the original text; x18/w18 operands, tpidr_el0 reads/writes, tpidrro_el0 reads, svc, cntpct/cntvct
#     all fall within rewrite sites or windows; conversely, every rewrite site is in the independent set (except window starts)
#  ④ The patch area no longer contains x18/w18, tpidr_el0, svc, cntpct/cntvct
#  ⑤ Host gate (svc / timer) blocks equal the template word for word, and match gates.tsv one to one (return point, kind, svc number / destination register, original address)
#  ⑥ Thread slots: the key page indices in the header are the eight known keys, 0..7; every mrs xN, tpidrro_el0 in the patch area is followed (skipping
#     adrp/add that do not write xN) by the key page indirect slot sequence, and the slot access counts match the sites
#  ⑦ Observation gates (obs.tsv): original image at that site == the registered original instruction word; the original instruction is relocatable (not
#     PC-relative / not a branch / does not involve x18 / does not read or write system registers, judged by capstone); may be in the middle of a function
#     (the gate ABI saves and restores everything, checked by gatefn); no overlap with rewrite sites / windows / cuts; the gate block = the template word
#     for word (the same first half as an svc gate + original instruction + b back to off+4), gates.tsv records an obs row
#  ⑧ Parameter gates (params.tsv): the two original words build a constant with movz/movk, the two new words = trampoline branch / nop, the register is not
#     touched in between, no branch target in the range, key registered, a unique original value per key, no constant mask downstream
#  ⑨ Temporary registers: every non-original destination register in a patch block has a matching red-zone save/restore pair at the same offset
#  ⑩ Trampoline exits: for every b jumping into the patch area, the patch block's exits back to text may only be original site+4 / end of the absorbed
#     span / end of the window / a branch target of an original instruction within the span (cuts are let through and listed); an exclusive section in
#     pieces (R row) may leave only to an address outside it that follows one of its instructions; a jump table with explicit targets (JT row) only to them
#  ⑫ Cache type reads (ctr rows): the word is movz xN, #4, lsl #16 with the original destination register, in place (no trampoline)
#  ⑪ Exclusive sections and explicit jump tables, word by word: windows (W) and sections in pieces (R) are entered only at their head (no direct branch,
#     fall-through or JT target from outside lands on another of their instructions); the copy after the prologue is the original word for word, x18
#     replaced by the prologue's scratch register, branches retargeted to the copy of their target inside the section or to an exit that writes the
#     slot back, restores the scratch registers and jumps to the original target; JT blocks match the compare-chain template for exactly the listed
#     targets, the original instruction is br x18 and every target lies inside its function
#  ⑭ Acquire loads (acq.tsv, plan item 232): the rows are the plain load-exclusives (ldxr / ldxrb / ldxrh / ldxp) the independent disassembler reads in
#     the original text outside cuts and options, every one of them; the checks above compare against an original image with those words given bit 15
#     (ldaxr ...), so ② finds any listed word the image does not have in its acquire form and ⑪ wants the acquire form in the copies; the patch area has
#     no plain load-exclusive left (④)
import sys, os, re, struct, subprocess, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import nsoimg
DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # Workspace root (game files, build outputs and run artifacts are all under it)

def _llvm_bin():
    if os.environ.get('LLVM_BIN'): return os.environ['LLVM_BIN']
    try: return os.path.dirname(subprocess.run(['xcrun', '--find', 'llvm-objdump'], capture_output=True, text=True, check=True).stdout.strip())
    except (OSError, subprocess.CalledProcessError): sys.exit('structck: no llvm-objdump (install Xcode or set LLVM_BIN to a directory holding llvm-objdump)')
LLVM = _llvm_bin()
out_dir, mod, nso = sys.argv[1:4]
orig = nsoimg.load(nso)
h = nsoimg.header(out_dir, mod)
PO, TE = h['PATCH_OFF'], h['TEXT_END']
# The android target (patch_nso.py DM_TARGET, plan item 301b): the slot block is at [tpidr_el0 + 16] (mrs xN, tpidr_el0; ldr xN, [xN, #0x10]), and a block
# moves sp down before it saves a register (stp ..., [sp, #-16]! / ldp ..., [sp], #16); the sites that keep the red-zone form are listed in belowsp.tsv
AND = h.get('TARGET_ANDROID') == 1
BELOW = {int(l, 16) for l in open(f'{out_dir}/{mod}.belowsp.tsv') if not l.startswith('#')} if AND else set()
pat = open(f'{out_dir}/{mod}.patched.bin', 'rb').read()
W = lambda b, o: struct.unpack_from('<I', b, o)[0]
fails = []
def bad(msg):
    fails.append(msg)
    if len(fails) <= 30: print('FAIL', msg)

def elf_wrap(blob, path):
    """Same layout as `llvm-objcopy -I binary -O elf64-littleaarch64 --rename-section .data=.text,code`: one executable .text at address 0.
    Used when only Xcode's llvm-objdump is available (Xcode ships no llvm-objcopy)."""
    shstr = b'\0.text\0.shstrtab\0'
    text_off = 64; str_off = text_off + len(blob); sh_off = (str_off + len(shstr) + 7) & ~7
    ehdr = b'\x7fELF' + bytes([2, 1, 1]) + bytes(9) + struct.pack('<HHIQQQIHHHHHH', 1, 183, 1, 0, 0, sh_off, 0, 64, 0, 0, 64, 3, 2)
    sh = lambda name, typ, flags, off, size, align: struct.pack('<IIQQQQIIQQ', name, typ, flags, 0, off, size, 0, 0, align, 0)
    with open(path, 'wb') as f:
        f.write(ehdr + blob + shstr + bytes(sh_off - str_off - len(shstr)))
        f.write(bytes(64) + sh(1, 1, 6, text_off, len(blob), 4) + sh(7, 3, 0, str_off, len(shstr), 1))
def llvm_dis(blob, base):
    """Disassemble from raw bytes and return the relevant lines {offset: lowercase instruction text}."""
    with tempfile.TemporaryDirectory() as td:
        b, e = os.path.join(td, 'x.bin'), os.path.join(td, 'x.elf')
        open(b, 'wb').write(blob)
        if os.path.exists(f'{LLVM}/llvm-objcopy'):
            subprocess.run([f'{LLVM}/llvm-objcopy', '-I', 'binary', '-O', 'elf64-littleaarch64',
                            '--rename-section', '.data=.text,code', b, e], check=True)
        else: elf_wrap(blob, e)
        # Keep only potentially relevant lines (x18/w18, tpidr, svc, cnt?ct, ctr_el0); filtering millions of lines is left to grep (DIS_FILTER is part of the cache key)
        od = subprocess.Popen([f'{LLVM}/llvm-objdump', '-d', '--no-show-raw-insn', e], stdout=subprocess.PIPE)
        r = subprocess.run(['/usr/bin/grep', '-iE', DIS_FILTER], stdin=od.stdout, capture_output=True, text=True)
        od.stdout.close(); assert od.wait() == 0
    out = {}
    for line in r.stdout.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+(.*)$', line)
        if m: out[base + int(m.group(1), 16)] = m.group(2).split('//')[0].split('<')[0].strip().lower()
    return out

RX18 = re.compile(r'\b[xw]18\b')
DIS_FILTER = r'[xw]18|tpidr|svc|cnt[pv]ct|ctr_el0|ldx[rp]'
RXPLAIN = re.compile(r'ldx[rp][bh]?\s')   # a plain load-exclusive (⑭)
def sysk(t):
    if re.match(r'svc\s', t): return 'svc'
    if re.match(r'mrs\s', t) and 'tpidr_el0' in t: return 'tpidr_r'
    if re.match(r'msr\s', t) and 'tpidr_el0' in t: return 'tpidr_w'
    if re.match(r'mrs\s', t) and 'tpidrro_el0' in t: return 'tpidrro_r'
    if re.match(r'mrs\s', t) and 'cntpct_el0' in t: return 'cntpct'
    if re.match(r'mrs\s', t) and 'cntvct_el0' in t: return 'cntvct'
    if re.match(r'mrs\s', t) and 'ctr_el0' in t: return 'ctr'
    return None

# ---- sites.tsv ----
sites = {}; wins = []; psecs = {}; tabs = {}; jumps = 0; fnptr = 0
for l in open(f'{out_dir}/{mod}.sites.tsv'):
    f = l.split(); a = int(f[0], 16)
    if f[1] == 'J': jumps += 1; continue
    if f[1] == 'JT': tabs[a] = [int(t, 16) for t in f[2:]]; continue   # jump table with explicit targets: annotation of the br x18 site's own row
    if f[1] == 'T': fnptr += 1; continue   # br / blr x18 through a function pointer: annotation of an x18 site, which has its own row
    if f[1] == 'W': wins.append((a, int(f[2], 16))); sites[a] = 'W'
    elif f[1] == 'R':                       # exclusive section in pieces: entry, then lo-hi@copy per piece
        psecs[a] = [tuple(int(v, 16) for v in re.fullmatch(r'(0x[0-9a-f]+)-(0x[0-9a-f]+)@(0x[0-9a-f]+)', p).groups()) for p in f[2:]]; sites[a] = 'R'
    else: sites[a] = f[1]
inwin = set(o for lo, hi in wins for o in range(lo, hi, 4)) | {o for ps in psecs.values() for lo, hi, c in ps for o in range(lo, hi, 4)}

# ⓪ Registered cuts
cuts = []; bid = None
for l in open(f'{out_dir}/{mod}.cuts.tsv'):
    if l.startswith('# BUILD_ID'): bid = l.split()[-1]; continue
    off, ob, nb, why = l.rstrip('\n').split('\t', 3)
    cuts.append((int(off, 16), bytes.fromhex(ob), bytes.fromhex(nb), why))
if bid != open(nso, 'rb').read(0x50)[0x40:0x50].hex(): bad(f'cuts.tsv build ID {bid} does not match the NSO')
cutb = set()
for off, ob, nb, why in cuts:
    if orig[off:off + len(ob)] != ob: bad(f'cut {off:#x}: original bytes do not match')
    for k in range(0, len(nb), 4):                               # The svc words in the new bytes are subsequently rewritten into gates (head is B) and checked as rewrite sites; skipped here
        if int.from_bytes(nb[k:k + 4], 'little') & 0xFFE0001F == 0xD4000001: continue
        if pat[off + k:off + k + len(nb[k:k + 4])] != nb[k:k + 4]: bad(f'cut {off:#x}+{k}: rewritten image is not the new bytes'); break
    if not why.strip() or len(ob) != len(nb): bad(f'cut {off:#x}: empty reason or unequal lengths')
    cutb |= set(range(off, off + len(ob)))
# Host-call gates produced by cuts: a word in the cut's new bytes that decodes to svc #N (N = 0x80–0xff; the handler is registered at run time by the
# game's hooks, host/hle.h dm_host_call_register). The form is read from the bytes:
#   entry form: new bytes = svc #N ; ret at an eh_frame function entry - the gate saves and restores x30, ret returns to the original caller;
#   call-site form: the original instruction is a bl and the cut replaces that single word with svc #N; the gate returns to off+4 (checked by the final b of the ⑤ template).
#   Register rationale for the call-site form: at a bl, per AAPCS the callee could clobber x9..x17, x30 and NZCV anyway, so the gate's side effects on
#   these do not change caller-visible behavior; the x0..x7 return values and x19..x28 / sp are preserved by the host gate contract (checked gate by gate by gatefn).
_hostcall = {}
def _bl(w, o): return o + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000) if w & 0xFC000000 == 0x94000000 else None
_fstarts = None
for off, ob, nb, why in cuts:
    for k in range(0, len(nb), 4):
        w = int.from_bytes(nb[k:k + 4], 'little')
        if w & 0xFFE0001F != 0xD4000001: continue
        n = (w >> 5) & 0xFFFF; a = off + k
        if not 0x80 <= n <= 0xFF: bad(f'cut {off:#x} introduces svc #{n:#x}: host calls are 0x80–0xff (below 0x80 is Horizon\'s range)'); continue
        if sites.get(a) != 'svc': bad(f'cut {off:#x}: svc #{n:#x} was not rewritten into a host gate (sites.tsv has no {a:#x} svc)')
        _hostcall[a] = n
        if nb[k + 4:k + 8] == (0xD65F03C0).to_bytes(4, 'little'):
            if _fstarts is None: _fstarts = set(nsoimg.func_starts(orig, TE))
            if a not in _fstarts: bad(f'host call svc #{n:#x} {a:#x}: entry form (svc ; ret) but not an eh_frame function entry')
        elif _bl(W(orig, a), a) is not None:
            if len(nb) != 4: bad(f'host call svc #{n:#x} {a:#x}: a call-site cut may replace only that one bl')
        else: bad(f'host call svc #{n:#x} {a:#x}: neither entry form (svc ; ret at a function entry) nor call-site form (replaces a bl)')
if cutb & {a + i for a in list(sites) + list(inwin) if a not in _hostcall for i in range(4)}: bad('cuts overlap rewrite sites / windows')
ncut_code = sum(1 for c in cuts if c[0] < TE); ncut_data = len(cuts) - ncut_code
obs = []
obs_p = f'{out_dir}/{mod}.obs.tsv'
if os.path.exists(obs_p):
    for l in open(obs_p, encoding='utf-8'):
        if l.startswith('# BUILD_ID'):
            if l.split()[-1] != open(nso, 'rb').read(0x50)[0x40:0x50].hex(): bad('obs.tsv build ID does not match the NSO')
            continue
        off, oid, ow, why = l.rstrip('\n').split('\t', 3); obs.append((int(off, 16), int(oid), int(ow, 16), why))
obs_heads = {o for o, *_ in obs}
params = []                                                          # Parameter gates (params.tsv, since r11): (A offset, B offset, register number, key name)
par_p = f'{out_dir}/{mod}.params.tsv'
if os.path.exists(par_p):
    for l in open(par_p, encoding='utf-8'):
        if l.startswith('# BUILD_ID'):
            if l.split()[-1] != open(nso, 'rb').read(0x50)[0x40:0x50].hex(): bad('params.tsv build ID does not match the NSO')
            continue
        a, b, r, k, why = l.rstrip('\n').split('\t', 4)
        if not why.strip(): bad(f'parameter gate {a}: empty reason')
        params.append((int(a, 16), int(b, 16), int(r.lstrip('x')), k))
par_words = {o for a, b, r, k in params for o in (a, b)}
caves = {}                                                           # Code caves (caves.tsv, plan item 139): offset → (original word, routine start, its words, its exits, option, reason)
cav_p = f'{out_dir}/{mod}.caves.tsv'
if os.path.exists(cav_p):
    for l in open(cav_p, encoding='utf-8'):
        if l.startswith('# BUILD_ID'):
            if l.split()[-1] != open(nso, 'rb').read(0x50)[0x40:0x50].hex(): bad('caves.tsv build ID does not match the NSO')
            continue
        off, ow, st, ws, ex, gate, why = l.rstrip('\n').split('\t', 6)
        caves[int(off, 16)] = (int(ow, 16), int(st, 16), [int(x, 16) for x in ws.split()], {int(e, 16) for e in ex.split(',') if e}, gate, why)
gated = {a for a, c in caves.items() if c[4]}                        # caves behind an option (plan item 163): the word stays, options.tsv has the branch
opt_rows = {}                                                        # options.tsv: offset → [(option, original bytes, new bytes)]
opt_p = f'{out_dir}/{mod}.options.tsv'
if os.path.exists(opt_p):
    for l in open(opt_p, encoding='utf-8'):
        if l.startswith('#'): continue
        name, off, ob, nb = l.rstrip('\n').split('\t')[:4]
        opt_rows.setdefault(int(off, 16), []).append((name, bytes.fromhex(ob), bytes.fromhex(nb)))

# ⑭ (first half) acq.tsv: each row is a plain load-exclusive in the original text; the original image the checks below use gets its acquire form
ACQ_MASK, ACQ_VAL = 0x3FC08000, 0x08400000
orig0 = orig; acq = {}
acq_p = f'{out_dir}/{mod}.acq.tsv'
if not os.path.exists(acq_p): bad('no acq.tsv (a product from before plan item 232: rebuild it)')
else:
    for l in open(acq_p):
        if l.startswith('# BUILD_ID'):
            if l.split()[-1] != open(nso, 'rb').read(0x50)[0x40:0x50].hex(): bad('acq.tsv build ID does not match the NSO')
            continue
        off, ow = l.split(); acq[int(off, 16)] = int(ow, 16)
    _o = bytearray(orig)
    for a, ow in acq.items():
        if not (a < TE and a % 4 == 0 and W(orig, a) == ow and ow & ACQ_MASK == ACQ_VAL): bad(f'acquire load {a:#x}: the original word is not the listed plain load-exclusive'); continue
        struct.pack_into('<I', _o, a, ow | 0x8000)
    orig = bytes(_o)

# ① Non-text segments: compare whole blocks first, then byte by byte where blocks differ; differences may only fall inside cuts (cut bytes were already checked against their expected values in ⓪)
nd = []
for blk in range(TE, len(orig), 4096):
    e = min(blk + 4096, len(orig))
    if pat[blk:e] != orig[blk:e]: nd += [o for o in range(blk, e) if pat[o] != orig[o] and o not in cutb]
if nd: bad(f'non-text segments differ from the original image (outside cuts): {len(nd)} bytes, e.g. {[hex(x) for x in nd[:5]]}')
if any(pat[len(orig):PO]): bad('non-zero bytes between the end of the image and the patch area')

# ② text changes == rewrite site heads
diff = set()
for blk in range(0, TE & ~3, 4096):                    # Compare whole blocks first, then word by word where blocks differ
    e = min(blk + 4096, TE & ~3)
    if orig[blk:e] != pat[blk:e]: diff |= {o for o in range(blk, e, 4) if W(orig, o) != W(pat, o)}
heads = set(sites)
cutw = {o & ~3 for o in cutb if o < TE}
_exp = (heads | obs_heads | par_words | (set(caves) - gated)) - cutw; _got = diff - cutw                # Host-call gate heads inside cuts are within the cut bytes and are checked as cuts
if _got != _exp: bad(f'text changes do not match rewrite sites: {len(_got - _exp)} extra, {len(_exp - _got)} missing, e.g. {[hex(x) for x in sorted(_got ^ _exp)[:5]]}')
def btarget(o):
    w = W(pat, o)
    if w & 0xFC000000 != 0x14000000: return None
    imm = w & 0x3FFFFFF
    if imm & 0x2000000: imm -= 0x4000000
    return o + 4 * imm
nb = [a for a in heads | obs_heads | (set(caves) - gated) if sites.get(a) != 'ctr' and not (btarget(a) is not None and PO <= btarget(a) < len(pat))]
# ⑫ ctr sites: replaced in place by a constant
_ctr = [a for a, k in sites.items() if k == 'ctr']
for a in _ctr:
    if W(orig, a) & 0xFFFFFFE0 != 0xD53B0020 or W(pat, a) != 0xD2A00080 | (W(orig, a) & 31): bad(f'ctr site {a:#x}: not mrs xN, ctr_el0 → movz xN, #4, lsl #16')
if nb: bad(f'rewrite site heads that are not a B into the patch area: {len(nb)}, e.g. {[hex(x) for x in nb[:5]]}')

# ③ Independent denominator
def cached_dis(blob):
    """The original image is fixed data: relevant lines are cached by sha (a new llvm version gets a new key), so the ten-odd seconds of disassembly are paid only the first time."""
    import hashlib, json
    cd = os.environ.get('DM_VCACHE', os.path.join(DM_ROOT, 'build/cache/dm/verify'))
    ver = hashlib.sha256(subprocess.run([f'{LLVM}/llvm-objdump', '--version'], capture_output=True).stdout).hexdigest()[:8]
    p = os.path.join(cd, f'{mod}.{hashlib.sha256(blob).hexdigest()[:16]}.{ver}.{hashlib.sha256(DIS_FILTER.encode()).hexdigest()[:6]}.json')
    if os.path.exists(p): return {int(k): v for k, v in json.load(open(p)).items()}
    d = llvm_dis(blob, 0)
    os.makedirs(cd, exist_ok=True); tmp = f'{p}.new.{os.getpid()}'; json.dump(d, open(tmp, 'w')); os.replace(tmp, p)
    return d
D = cached_dis(orig0[:TE])   # the original text as the game has it (⑭ reads its plain load-exclusives)
ind = {}
for o, t in D.items():
    if RX18.search(t): ind[o] = 'x18'
    k = sysk(t)
    if k: ind[o] = k
miss = sorted(o for o in ind if o not in sites and o not in inwin)
if miss: bad(f'in the independent disassembly but not covered by rewrite sites: {len(miss)}, e.g. {[(hex(x), D[x]) for x in miss[:5]]}')
extra = sorted(a for a, k in sites.items() if k not in ('W', 'R') and a not in ind and a not in _hostcall)   # The svc introduced by a cut is not in the original image
if extra: bad(f'rewrite sites not in the independent set: {len(extra)}, e.g. {[(hex(x), D.get(x)) for x in extra[:5]]}')
kmis = sorted(a for a, k in sites.items() if k not in ('W', 'R') and a in ind and ind[a] != k)
if kmis: bad(f'rewrite site kind differs from the independent reading: {len(kmis)}, e.g. {[(hex(x), sites[x], D[x]) for x in kmis[:5]]}')
nwin_ref = sum(1 for o in ind if o in inwin)

# ④ Patch area is clean
P = llvm_dis(pat[PO:], PO)
_slotbase = lambda o: AND and W(pat, o) & 0xFFFFFFE0 == 0xD53BD040 and W(pat, o + 4) == 0xF9400800 | (W(pat, o) & 31) << 5 | (W(pat, o) & 31)   # mrs xN, tpidr_el0; ldr xN, [xN, #0x10]
left = sorted(o for o, t in P.items() if RX18.search(t) or not (sysk(t) is None or (sysk(t) == 'tpidrro_r' and not AND) or (sysk(t) == 'tpidr_r' and _slotbase(o))))
if left: bad(f'patch area still contains x18/system registers/svc: {len(left)}, e.g. {[(hex(x), P[x]) for x in left[:5]]}')
pleft = sorted(o for o, t in P.items() if RXPLAIN.match(t))
if pleft: bad(f'patch area has plain load-exclusives (⑭): {len(pleft)}, e.g. {[(hex(x), P[x]) for x in pleft[:5]]}')

# ⑭ (second half) the rows against the independent reading: every plain load-exclusive outside cuts and options listed, nothing else
plain = {o for o, t in D.items() if RXPLAIN.match(t)}
keepw = cutw | {(o + k) & ~3 for o, rs in opt_rows.items() for _, ob, _ in rs for k in range(len(ob))}
_m, _x, _k = sorted(plain - keepw - set(acq)), sorted(set(acq) - plain), sorted(set(acq) & keepw)
if _m: bad(f'plain load-exclusives not made acquire loads: {len(_m)}, e.g. {[(hex(x), D[x]) for x in _m[:5]]}')
if _x: bad(f'acq.tsv rows the independent disassembler does not read as a plain load-exclusive: {len(_x)}, e.g. {[hex(x) for x in _x[:5]]}')
if _k: bad(f'acq.tsv rows inside a cut or an option: {len(_k)}, e.g. {[hex(x) for x in _k[:5]]}')

# ⑤ Host gates
KEY = {k[4:-4]: v for k, v in h.items() if k.startswith('KEY_') and k.endswith('_IDX')}
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
_md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); _md.detail = True
def keyseq(at):
    if AND:   # mrs xT, tpidr_el0; ldr xT, [xT, #0x10]; then the key page sequence as on apple
        q = list(_md.disasm(pat[at:at + 8], at))
        if len(q) < 2 or q[0].mnemonic != 'mrs' or 'tpidr_el0' not in q[0].op_str or f'{q[1].mnemonic} {q[1].op_str}' != f'ldr {q[0].op_str.split(",")[0]}, [{q[0].op_str.split(",")[0]}, #0x10]':
            return None
        r = _keyseq(at + 4, q[0].operands[0].reg)
        return r
    return _keyseq(at, None)
def _keyseq(at, treg):
    q = list(_md.disasm(pat[at:at + 20], at))
    if treg is not None:   # android: the key page sequence after ldr xT, [xT, #0x10] (q[0] is that ldr)
        if len(q) < 4 or q[1].mnemonic != 'adrp' or q[1].operands[1].imm != h['KEY_PAGE_OFF'] \
                or q[2].mnemonic != 'ldr' or q[2].operands[1].mem.base != q[1].operands[0].reg \
                or q[3].mnemonic != 'add' or q[3].operands[0].reg != treg or q[3].operands[1].reg != treg:
            return None
        d = q[2].operands[1].mem.disp
        return (q[3].address + 4, d // 8) if d % 8 == 0 and 0 <= d // 8 < 8 else None
    if len(q) < 5 or q[0].mnemonic != 'mrs' or 'tpidrro_el0' not in q[0].op_str \
            or q[1].mnemonic != 'adrp' or q[1].operands[1].imm != h['KEY_PAGE_OFF'] \
            or q[2].mnemonic != 'ldr' or q[2].operands[1].mem.base != q[1].operands[0].reg \
            or q[3].mnemonic != 'add' or q[3].operands[0].reg != q[0].operands[0].reg \
            or q[3].operands[1].reg != q[0].operands[0].reg:
        return None
    d = q[2].operands[1].mem.disp
    return (q[3].address + 4, d // 8) if d % 8 == 0 and 0 <= d // 8 < 8 else None
def paramseq(at, r):
    q = list(_md.disasm(pat[at:at + 44], at))
    if AND and len(q) > 2: q = q[:2] + q[3:]   # (the slot base load after the mrs)
    tmp = 17 if r == 16 else 16
    rn = lambda ins, i: ins.reg_name(ins.operands[i].reg)
    if len(q) < 8 or q[0].mnemonic not in ('stur', 'str') or rn(q[0], 0) != f'x{tmp}' \
            or q[0].operands[1].mem.base != capstone.arm64_const.ARM64_REG_SP or q[0].operands[1].mem.disp not in (-16, -24) \
            or q[1].mnemonic != 'mrs' or rn(q[1], 0) != f'x{r}' \
            or ('tpidr_el0' if AND else 'tpidrro_el0') not in q[1].op_str or q[2].mnemonic != 'adrp' \
            or q[2].operands[1].imm != h['KEY_PAGE_OFF'] or q[3].mnemonic != 'ldr' \
            or q[3].operands[1].mem.base != q[2].operands[0].reg or q[4].mnemonic != 'add' \
            or q[4].operands[0].reg != q[1].operands[0].reg or q[4].operands[1].reg != q[1].operands[0].reg \
            or q[5].mnemonic != 'ldr' or q[5].operands[0].reg != q[1].operands[0].reg \
            or q[5].operands[1].mem.base != q[4].operands[0].reg or q[6].mnemonic not in ('ldur', 'ldr') \
            or rn(q[6], 0) != f'x{tmp}' \
            or q[6].operands[1].mem.base != capstone.arm64_const.ARM64_REG_SP \
            or (q[6].operands[1].mem.disp != q[0].operands[1].mem.disp and not AND) or q[7].mnemonic != 'b':
        return None
    d = q[3].operands[1].mem.disp
    return (q[6].address + 4, d // 8)
gates = {}
for l in open(f'{out_dir}/{mod}.gates.tsv'):
    r, k, arg, oa = l.split(); gates[int(r, 16)] = (k, int(arg), int(oa, 16))
def B(frm, to): return 0x14000000 | (((to - frm) >> 2) & 0x3FFFFFF)
# The gate block must have ldr x29,[sp] before blr: the x29 borrowed for fetching the slot via the key page is restored to the guest's original value, so the x29 the host reads is the real frame pointer (r26, see patch_nso emit_gate_call)
LDR_X29_SP = 0xF94003FD
gsites = sorted(a for a, k in sites.items() if k in ('svc', 'cntpct', 'cntvct'))
seen = set()
for a in gsites:
    s = btarget(a); ow = W(orig, a) if a not in _hostcall else (0xD4000001 | _hostcall[a] << 5)
    ks = keyseq(s + 4)
    if W(pat, s) != 0xA9BF7BFD or not ks or ks[1] != KEY.get('KGATE') \
            or W(pat, ks[0]) & 0xFFC00000 != 0xF9400000 or W(pat, ks[0] + 4) != LDR_X29_SP or W(pat, ks[0] + 8) != 0xD63F03C0 \
            or W(pat, ks[0] + 12) != 0xA8C17BFD or W(pat, ks[0] + 16) != B(ks[0] + 16, a + 4):
        bad(f'gate block does not match the key page template {a:#x}'); continue
    arg = (ow >> 5) & 0xFFFF if sites[a] == 'svc' else ow & 31
    g = gates.get(ks[0] + 12)
    if g != (sites[a], arg, a): bad(f'gates.tsv mismatch {a:#x}: table {g} expected {(sites[a], arg, a)}')
    seen.add(ks[0] + 12)
# ⑦ Observation gates
import bisect as _bs
_starts = set(nsoimg.func_starts(orig, TE))
_busy = {a + i for a in list(sites) + list(inwin) for i in range(4)} | cutb
for off, oid, ow, why in obs:
    if W(orig, off) != ow: bad(f'observation gate {off:#x}: original instruction word mismatch: image {W(orig, off):08x} registered {ow:08x}'); continue
    ins = next(_md.disasm(struct.pack('<I', ow), off), None)
    m = ins.mnemonic if ins else '?'
    if ins is None or m in ('adr', 'adrp', 'b', 'bl', 'br', 'blr', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz', 'svc', 'mrs', 'msr', 'eret') or m.startswith('b.') \
       or (m.startswith('ldr') and '[' not in ins.op_str) or RX18.search(ins.op_str or ''):
        bad(f'observation gate {off:#x}: original instruction is not relocatable: {m} {ins.op_str if ins else ""}')
    # Function entry is no longer required: _dm_gate saves and restores x0–x17, x19–x30, q0–q31 and NZCV/FPSR/FPCR, so live registers in the middle of a function are unaffected;
    # this ABI is checked functionally by gatefn (the stub deliberately clobbers x9–x17 and NZCV, and full restoration is checked), not approximated here by position
    if set(range(off, off + 4)) & _busy: bad(f'observation gate {off:#x} overlaps rewrite sites / windows / cuts')
    s_ = btarget(off)
    if s_ is None or not (PO <= s_ < len(pat)): bad(f'observation gate head {off:#x} is not a B into the patch area'); continue
    ks = keyseq(s_ + 4)
    if W(pat, s_) != 0xA9BF7BFD or not ks or ks[1] != KEY.get('KGATE') \
            or W(pat, ks[0]) & 0xFFC00000 != 0xF9400000 or W(pat, ks[0] + 4) != LDR_X29_SP or W(pat, ks[0] + 8) != 0xD63F03C0 \
            or W(pat, ks[0] + 12) != 0xA8C17BFD or W(pat, ks[0] + 16) != ow \
            or W(pat, ks[0] + 20) != B(ks[0] + 20, off + 4):
        bad(f'observation gate block does not match the key page template {off:#x}'); continue
    if gates.get(ks[0] + 12) != ('obs', oid, off): bad(f'observation gate row mismatch {off:#x}: table {gates.get(ks[0] + 12)}')
    seen.add(ks[0] + 12)
if set(gates) != seen: bad(f'gates.tsv has extra rows: {len(set(gates) - seen)}')

# ⑥ Thread slots
_base = {'K18', 'KTLS', 'KTLR', 'KGATE'}; _extra = set(KEY) - _base   # Extra keys = parameter-gate keys (KRES* / KASPECT* ...; the rewriter's global key table, listed in the headers of all three modules): must be uppercase names starting with K, with key numbers below the four base keys
if set(KEY) != {'K18', 'KTLS', 'KTLR', 'KGATE', 'KPARAM0', 'KPARAM1', 'KPARAM2', 'KPARAM3'} \
        or not {k for *_, k in params} <= set(KEY) or set(KEY.values()) != set(range(8)):
    bad(f'header key page indices are invalid: {KEY} (must contain eight keys with indices exactly 0..7)')
kv = {v: k for k, v in KEY.items()}; nk = {}
for o in range(PO, len(pat) - 4, 4):
    w = W(pat, o)
    if w & 0xFFFFFFE0 != (0xD53BD040 if AND else 0xD53BD060): continue           # mrs xN, tpidrro_el0 (android: mrs xN, tpidr_el0)
    n = w & 31; ks = keyseq(o); k = kv.get(ks[1]) if ks else None
    if not ks or k is None:
        bad(f'patch area {o:#x}: tpidrro read is not followed by the key page indirect slot sequence'); continue
    nk[k] = nk.get(k, 0) + 1
expected = {}
for kind in sites.values():
    key = {'x18': 'K18', 'W': 'K18', 'R': 'K18', 'tpidr_r': 'KTLS', 'tpidr_w': 'KTLS', 'tpidrro_r': 'KTLR'}.get(kind)
    if key: expected[key] = expected.get(key, 0) + 1
expected['KGATE'] = len(gsites) + len(obs)
for _, _, _, key in params: expected[key] = expected.get(key, 0) + 1
if any(nk.get(k, 0) != v for k, v in expected.items()) or any(k not in expected and v for k, v in nk.items()):
    bad(f'key page slot access counts do not match: expected {expected} actual {nk}')

# ⑧ Parameter gates (PARAMS, host only): a 64-bit constant originally built by movz xR,#lo / movk xR,#hi,lsl #32 is replaced at equal length by mrs xR,tpidrro_el0 / ldr xR,[xR,#key*8]
#   Checks: original word A is a 64-bit movz (hw=0) writing xR, original word B is a 64-bit movk (hw=2) writing xR; the new words = the template bit for bit; instructions between A and B do not read or write xR;
#   (A, B] is not the target of any direct branch nor a function entry (otherwise a path entering in the middle would get the slot pointer instead of the constant); the key number is registered in the header; no overlap with rewrite sites / cuts / observation gates.
#   When the host gives the slot the original value (packed width/height = the constants below), the semantics are bit-for-bit equivalent to the original two instructions: xR has the same value after B, and nobody reads xR between A and B.
par_const = {}
if params:
    import capstone
    _cs = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM); _cs.detail = True
    _tg = set()
    for o in range(0, TE & ~3, 4):                                  # Direct branch targets across all of text
        w = W(orig, o)
        if w & 0x7C000000 == 0x14000000: d = ((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000; _tg.add(o + 4 * d)
        elif w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000: d = (((w >> 5) & 0x7FFFF) ^ 0x40000) - 0x40000; _tg.add(o + 4 * d)
        elif w & 0x7E000000 == 0x36000000: d = (((w >> 5) & 0x3FFF) ^ 0x2000) - 0x2000; _tg.add(o + 4 * d)
    _fs = set(nsoimg.func_starts(orig, TE))
    _busy8 = {a for a in sites} | set(inwin) | obs_heads | {o & ~3 for o in cutb}
    for a, b, r, k in params:
        wa, wb = W(orig, a), W(orig, b)
        # Two kinds of original instruction pair: 64-bit movz xR,#lo / movk xR,#hi,lsl #32 (KRES*); 32-bit movz wR,#lo / movk wR,#hi,lsl #16 (KASPECT*, original value = hi<<16|lo,
        # writing a w register zeroes the upper 32 bits ⇒ the host slot must be stored as a u32 bit pattern with the upper 32 bits 0 (a runtime constraint that cannot be checked statically, see platform thread.c)
        w64 = wa & 0xFFE00000 == 0xD2800000 and wb & 0xFFE00000 == 0xF2C00000
        w32 = wa & 0xFFE00000 == 0x52800000 and wb & 0xFFE00000 == 0x72A00000
        if not (w64 or w32) or wa & 31 != r or wb & 31 != r:
            bad(f'parameter gate {a:#x}/{b:#x}: original instructions {wa:#010x}/{wb:#010x} are not a movz/movk constant-building pair (64-bit lsl #32 or 32-bit lsl #16) writing x{r}')
        if not a < b: bad(f'parameter gate {a:#x}/{b:#x}: wrong order')
        if k not in KEY: bad(f'parameter gate {a:#x}: key {k} is not registered in the header'); continue
        s = btarget(a); ks = paramseq(s, r) if s is not None else None
        ok = ks and ks[1] == KEY[k] and W(pat, b) == 0xD503201F
        if not ok: bad(f'parameter gate {a:#x}/{b:#x}: key page indirect template mismatch')
        for o in range(a + 4, b, 4):
            ins = next(_cs.disasm(orig[o:o + 4], o), None)
            if ins is None: bad(f'parameter gate {a:#x}: {o:#x} in between cannot be decoded'); continue
            rr, ww = ins.regs_access()
            names = {ins.reg_name(x) for x in rr + ww}
            if f'x{r}' in names or f'w{r}' in names: bad(f'parameter gate {a:#x}: {o:#x} {ins.mnemonic} {ins.op_str} in between reads/writes x{r}')
        bt = sorted(t for t in _tg if a < t <= b) + sorted(t for t in _fs if a < t <= b)
        if bt: bad(f'parameter gate {a:#x}..{b:#x}: branch target / function entry inside the range {[hex(x) for x in bt[:4]]}')
        if {a, b} & _busy8: bad(f'parameter gate {a:#x}/{b:#x} overlaps rewrite sites / windows / observation gates / cuts')
        par_const[(a, k)] = ((wb >> 5) & 0xFFFF) << (16 if w32 else 32) | ((wa >> 5) & 0xFFFF)
    _kc = {}
    for (a, k), v in par_const.items(): _kc.setdefault(k, set()).add(v)
    for k, vs in _kc.items():
        if len(vs) != 1: bad(f'parameter gate key {k} maps to several original constants {[hex(v) for v in vs]} (one slot cannot give a unique original value)')
    print('parameter gates ' + ', '.join(f'{a:#x}={k}:original {v:#x}' for (a, k), v in par_const.items()))
    # Downstream of a parameter register there must be no further masking by a constant: the compiler folds "the value can only be one of these constants" into later instructions - in the operation mode notification handler at 0xe10e48,
    # mov w9,#0x740; and x9,x25,x9 is exactly a mask derived from 1280|1600, and once replaced by a parameter the width gets masked to 768 / 64. From each parameter site, do taint propagation in linear order within the function
    # (mov / csel kinds propagate, other writes clear; branches are not followed, a conservative approximation); report when a tainted register meets and / ands / bic whose other operand is an immediate or a register built by a mov immediate
    _fsl = sorted(_fs)
    def _rn(ins, x): return re.sub(r'^[wx]', '', ins.reg_name(x)) if re.match(r'^[wx]\d+$', ins.reg_name(x) or '') else None
    _donef = set()
    for a, b, r, k in params:
        import bisect as _bi
        i = _bi.bisect_right(_fsl, a); f0 = _fsl[i - 1]; f1 = _fsl[i] if i < len(_fsl) else TE
        if f0 in _donef: continue
        _donef.add(f0)
        seeds = {(pa, str(pr)) for pa, pb, pr, pk in params if f0 <= pa < f1}
        taint, const = set(), {}
        for o in range(f0, f1, 4):
            for pa, pr in seeds:
                if o == pa: taint.add(pr)
            if any(o == pa for pa, _ in seeds): continue
            ins = next(_cs.disasm(pat[o:o + 4], o), None)
            if ins is None: continue
            rr, ww = ins.regs_access(); rd = {x for x in (_rn(ins, q) for q in rr) if x}; wr = {x for x in (_rn(ins, q) for q in ww) if x}
            m = ins.mnemonic
            if m in ('and', 'ands', 'bic', 'bics') and rd & taint:
                imm = any(op.type == capstone.arm64.ARM64_OP_IMM for op in ins.operands)
                if imm or (rd - taint) & set(const): bad(f'constant mask downstream of a parameter gate {o:#x}: {m} {ins.op_str} (function {f0:#x}, a value tainted by the parameter register is masked by a constant again)')
            if m in ('mov', 'csel', 'csinc', 'csinv', 'csneg') and rd & taint: taint |= wr; [const.pop(x, None) for x in wr]
            else:
                taint -= wr
                isimm = m in ('mov', 'movz') and len(ins.operands) == 2 and ins.operands[1].type == capstone.arm64.ARM64_OP_IMM
                for x in wr:
                    if isimm: const[x] = ins.operands[1].imm
                    else: const.pop(x, None)

# ⑨ Static check of temporary registers: every non-original-instruction destination register in a patch block must have a matching red-zone save/restore pair at the same offset.
import capstone
def _writes(ins):
    _, ww = ins.regs_access()
    return {'x29' if ins.reg_name(r) == 'fp' else 'x30' if ins.reg_name(r) == 'lr' else re.sub(r'^[wx]', 'x', ins.reg_name(r))
            for r in ww if ins.reg_name(r) not in ('sp', 'pc')}
def _mem_sp(ins, load):
    if (load and not ins.mnemonic.startswith('ld')) or (not load and not ins.mnemonic.startswith('st')):
        return None
    for op in ins.operands:
        if op.type == capstone.arm64_const.ARM64_OP_MEM and ins.reg_name(op.mem.base) == 'sp':
            if op.mem.disp < 0: return op.mem.disp
            if load and ins.mnemonic == 'ldp' and op.mem.disp == 0: return -16
    return None
_SPM = re.compile(r'\[sp(?:, #(-?(?:0x)?[0-9a-f]+))?\](!|, #(-?(?:0x)?[0-9a-f]+))?')
def _sp_regs(block):
    """android: (saved, restored) as {register: [(address from the sp the block was entered with, index)]}, sp followed through the block's
    pre- and post-index forms (a pair's second register 8 or 4 bytes on); only addresses below the entry sp count, as the red-zone pairing does"""
    saved, restored, d = {}, {}, 0
    for i, ins in enumerate(block):
        m = _SPM.search(ins.op_str)
        if not m or not (ins.mnemonic.startswith('st') or ins.mnemonic.startswith('ld')): continue
        off = int(m.group(1), 0) if m.group(1) else 0
        if m.group(2) == '!': at = d + off; d = at
        elif m.group(3): at = d; d = d + int(m.group(3), 0)
        else: at = d + off
        regs = ins.op_str.split('[')[0].rstrip(', ').split(', ')
        for k, r in enumerate(regs):
            name = 'x29' if r == 'fp' else 'x30' if r == 'lr' else re.sub(r'^[wx]', 'x', r)
            a = at + k * (8 if r.startswith('x') or r in ('fp', 'lr') else 4)
            if a < 0: (saved if ins.mnemonic.startswith('st') else restored).setdefault(name, []).append((a, i))
    return saved, restored
def _block(start, size=256):
    out = []
    for ins in _md.disasm(pat[start:start + size], start):
        out.append(ins)
        if ins.mnemonic == 'b':
            t = ins.operands[0].imm
            if not (PO <= t < len(pat)): break
    return out
static_entries = [(a, set()) for a in sites]
for a, b, r, _ in params:
    static_entries.append((a, {f'x{r}'}))
static_seen = 0
# mov sp, x18 (stackalloc) blocks reload x16 / x17 through x17 = old sp, which the red-zone pairing below cannot see: check them word for word instead
def _movsp_ok(a, s):
    if AND:
        q = list(_md.disasm(bytes(pat[s:s + 48]), s)); ks = keyseq(s + 4)
        return (len(q) == 12 and f'{q[0].mnemonic} {q[0].op_str}' == 'stp x16, x17, [sp, #-0x10]!' and ks is not None and ks[1] == KEY['K18']
                and f'{q[6].mnemonic} {q[6].op_str}' == 'ldr x16, [x16]'
                and [f'{x.mnemonic} {x.op_str}' for x in q[7:]] == ['mov x17, sp', 'mov sp, x16', 'ldr x16, [x17]', 'ldr x17, [x17, #8]', f'b #{a + 4:#x}'])
    q = list(_md.disasm(bytes(pat[s:s + 48]), s)); ks = keyseq(s + 8)
    return (len(q) == 12 and W(pat, s) == 0xF81F03F0 and W(pat, s + 4) == 0xF81E83F1                # stur x16,[sp,#-16]; stur x17,[sp,#-24]
            and q[2].op_str == 'x16, tpidrro_el0' and ks is not None and ks[1] == KEY['K18']
            and f'{q[6].mnemonic} {q[6].op_str}' == 'ldr x16, [x16]'
            and [f'{x.mnemonic} {x.op_str}' for x in q[7:]] == ['mov x17, sp', 'mov sp, x16', 'ldur x16, [x17, #-0x10]', 'ldur x17, [x17, #-0x18]', f'b #{a + 4:#x}'])
n_movsp = 0
for a, allowed in static_entries:
    s = btarget(a)
    if s is None: continue
    if next(_md.disasm(orig[a:a + 4], a), None) is not None and W(orig, a) == 0x9100025F:          # mov sp, x18
        static_seen += 1; n_movsp += 1
        if not _movsp_ok(a, s): bad(f'mov sp, x18 block {a:#x} is not the template')
        continue
    block = _block(s, 4 * (12 + 4 * len(set(tabs[a])))) if a in tabs else _block(s); static_seen += 1   # a JT block reaches its first landing pad after the compare chain
    if not allowed:
        end = next((hi for lo, hi in wins if lo == a), a + 4)
        for lo, hi in ([(lo, hi) for lo, hi, c in psecs[a]] if a in psecs else [(a, end)]):
            for ins in _md.disasm(orig[lo:hi], lo): allowed |= _writes(ins)
        if next(_md.disasm(orig[a:a + 4], a), None).mnemonic in ('stp', 'ldp'):
            allowed.update(('x0', 'x1'))
    saved = {}
    restored = {}
    if AND and a not in BELOW:
        saved, restored = _sp_regs(block); block_ = []
    else: block_ = block
    for i, ins in enumerate(block_):
        d = _mem_sp(ins, False)
        if d is not None:
            regs = [op.reg for op in ins.operands[:2] if op.type == capstone.arm64_const.ARM64_OP_REG]
            for reg in regs:
                name = ins.reg_name(reg); name = 'x29' if name == 'fp' else 'x30' if name == 'lr' else re.sub(r'^[wx]', 'x', name)
                saved.setdefault(name, []).append((d, i))
        d = _mem_sp(ins, True)
        if d is not None:
            regs = [op.reg for op in ins.operands[:2] if op.type == capstone.arm64_const.ARM64_OP_REG]
            for reg in regs:
                name = ins.reg_name(reg); name = 'x29' if name == 'fp' else 'x30' if name == 'lr' else re.sub(r'^[wx]', 'x', name)
                restored.setdefault(name, []).append((d, i))
    writes = set().union(*(_writes(ins) for ins in block)) if block else set()
    for reg in sorted(writes - allowed):
        wi = [i for i, ins in enumerate(block) if reg in _writes(ins) and not (i > 0 and any(i == ri for ri_ in restored.values() for _, ri in ri_))]
        dead = any(ins.mnemonic in ('br', 'blr') and ins.operands and ins.reg_name(ins.operands[0].reg) in (reg, reg[1:]) for ins in block)
        ok = dead or any(sd == rd and si < min(wi, default=10**9) and ri > max(wi, default=-1)
                 for sd, si in saved.get(reg, []) for rd, ri in restored.get(reg, []))
        if not ok: bad(f'temp register {a:#x}: block writes {reg}, which is not an original destination register and has no save/restore pair at the same offset')
print(f'STATIC {mod} scanned={static_seen} violations={sum(1 for x in fails if x.startswith("temp register"))} mov_sp_template={n_movsp}')

# ⑩ Trampoline exits (the handheld-switch crash: the parameter-gate trampoline returned to B+4, skipping the unmodified original instructions between A and B and another trampoline head):
#   for every b in text that jumps into the patch area (trampoline head a), follow the patch block through all exits back to text (sequential b and conditional branches; bl is treated as returning; ret / br do not count as returning to text).
#   Legal exits: a+4; the address after skipped words that are all words absorbed by the rewrite (≠ the original word and not another trampoline head, e.g. the nop at parameter-gate B);
#   window (W row [lo, hi)) heads: hi and the branch targets of the original instructions inside the window; non-window heads: the branch targets of the original instructions inside the contiguous absorbed span.
_heads = {o for o in range(0, TE & ~3, 4) if W(pat, o) & 0xFC000000 == 0x14000000 and btarget(o) is not None and btarget(o) >= PO}
_wend = dict(wins)
def _bt(w, o):
    if w & 0x7C000000 == 0x14000000: return o + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000)
    if w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000: return o + 4 * ((((w >> 5) & 0x7FFFF) ^ 0x40000) - 0x40000)
    if w & 0x7E000000 == 0x36000000: return o + 4 * ((((w >> 5) & 0x3FFF) ^ 0x2000) - 0x2000)
    return None
def _exits(start):
    ex, seen, st = set(), set(), [start]
    while st:
        o = st.pop()
        while o not in seen and o + 4 <= len(pat):
            seen.add(o); w = W(pat, o)
            if w & 0xFC000000 == 0x14000000:
                t = _bt(w, o)
                if t < PO: ex.add(t); break
                o = t; continue
            if w & 0xFC000000 == 0x94000000: o += 4; continue
            if w == 0xD65F03C0 or w & 0xFFFFFC1F == 0xD61F0000 or w & 0xFFE0001F == 0xD4200000: break   # ret / br / brk
            t = _bt(w, o)
            if t is not None:
                if t < PO: ex.add(t)
                else: st.append(t)
            o += 4
    return ex
nexit = 0; _cutoff = {c[0] for c in cuts}; _cutex = []
for a in sorted(_heads):
    if a in caves:   # a code cave leaves only to the exits it lists (⑬ checks the list)
        for t in _exits(btarget(a)):
            nexit += 1
            if t not in caves[a][3]: bad(f'code cave {a:#x}: exit to {t:#x}, which caves.tsv does not list')
        continue
    if a in psecs:
        inr = {o for lo, hi, c in psecs[a] for o in range(lo, hi, 4)}
        legal = ({hi for lo, hi, c in psecs[a]} | {t for o in inr for t in [_bt(W(orig, o), o)] if t is not None}) - inr
        for t in _exits(btarget(a)):
            nexit += 1
            if t not in legal: bad(f'trampoline exit {a:#x} → {t:#x}: not an address outside the exclusive section that follows one of its instructions')
        continue
    if a in _wend:
        seg = range(a, _wend[a], 4); legal = {_wend[a]}
    else:
        e = a + 4
        while e < TE and W(pat, e) != W(orig, e) and e not in _heads: e += 4
        seg = range(a, e, 4); legal = {a + 4 * k for k in range(1, len(seg) + 1)}
    legal |= {t for o in seg for t in [_bt(W(orig, o), o)] if t is not None} | set(tabs.get(a, ()))
    for t in _exits(btarget(a)):
        nexit += 1
        if t not in legal and a in _cutoff: _cutex.append(f'{a:#x}→{t:#x}'); continue   # Registered cuts (with a reason, deliberately changing control flow): let through and listed
        if t not in legal: bad(f'trampoline exit {a:#x} → {t:#x}: neither {a + 4:#x} / end of the absorbed span / end of the window, nor a branch target of an original instruction in the span (skips unmodified original instructions or another trampoline head)')
for a in sorted(gated):   # a cave behind an option is walked from its routine (the branch to it is written at load)
    for t in _exits(caves[a][1]):
        nexit += 1
        if t not in caves[a][3]: bad(f'code cave {a:#x}: exit to {t:#x}, which caves.tsv does not list')
print(f'EXITS {mod} tramp_heads={len(_heads)} exits={nexit} cut_exits={_cutex}')

# ⑬ Code caves (CAVES, plan item 139): the original word at the offset is the listed one, the image has `b <routine start>` there, the patch area holds
#   the listed words, the routine's last word is an unconditional branch back to text (offset + 4 or a listed exit), every exit is in text and
#   listed, the reason is not empty, and no cave sits on a rewrite site, window, gate or cut. A cave behind an option (plan item 163) keeps the
#   original word in the image and options.tsv has exactly one row there, of that option: the original word to `b <routine start>`. What a
#   routine does is the game's tests' to check
for a, (ow, st, ws, ex, gate, why) in sorted(caves.items()):
    if W(orig, a) != ow: bad(f'code cave {a:#x}: original word {W(orig, a):#010x}, caves.tsv says {ow:#010x}')
    if gate:
        if W(pat, a) != ow: bad(f'code cave {a:#x} (option {gate}): the image does not hold the original word')
        rows = opt_rows.get(a, [])
        if rows != [(gate, struct.pack('<I', ow), struct.pack('<I', 0x14000000 | (((st - a) >> 2) & 0x3FFFFFF)))]:
            bad(f'code cave {a:#x} (option {gate}): options.tsv has not exactly its one row, the original word to b {st:#x} ({rows})')
    elif btarget(a) != st: bad(f'code cave {a:#x}: not a branch to its routine {st:#x}')
    if st < PO or [W(pat, st + 4 * k) for k in range(len(ws))] != ws: bad(f'code cave {a:#x}: the patch area does not hold the listed routine')
    last = ws[-1] if ws else 0
    if not (last & 0xFC000000 == 0x14000000 and _bt(last, st + 4 * (len(ws) - 1)) in ex): bad(f'code cave {a:#x}: the routine does not end in a branch back to a listed exit')
    if any(e >= TE or e % 4 for e in ex) or not why.strip(): bad(f'code cave {a:#x}: an exit outside text, or no reason')
    if a in sites or a in inwin or a in obs_heads or a in par_words or a in cutw: bad(f'code cave {a:#x}: on another change')
print(f'CAVES {mod} caves={len(caves)} behind_options={len(gated)}')

# ⑪ Exclusive sections (W / R rows) and jump tables with explicit targets (JT rows), word by word
def STRX_(t, n): return 0xF9000000 | (n << 5) | t                                  # str xt, [xn]
def LDP_(t, t2): return 0xA97F0000 | (t2 << 10) | (31 << 5) | t                     # ldp xt, xt2, [sp, #-16]
def LDP_POST_(t, t2): return 0xA8C10000 | (t2 << 10) | (31 << 5) | t                # ldp xt, xt2, [sp], #16 (android)
_sec = {lo: [(lo, hi, None)] for lo, hi in wins} | psecs                          # entry → pieces (a window is one piece; its copy follows the prologue)
_into = {}                                                                        # instruction of a section → its entry
for e, ps in _sec.items():
    for lo, hi, c in ps:
        for o in range(lo, hi, 4):
            if o in _into: bad(f'exclusive sections {_into[o]:#x} and {e:#x} share {o:#x}')
            _into[o] = e
for o in range(0, TE & ~3, 4):                                                    # entries from outside: direct branches and fall-through (independent of the rewriter's CFG)
    w = W(orig, o); t = _bt(w, o)
    if t is None and w & 0xFC000000 == 0x94000000: t = o + 4 * (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000)   # bl
    if t in _into and t != _into[t] and _into.get(o) != _into[t]: bad(f'{o:#x} branches into exclusive section {_into[t]:#x} at {t:#x}')
for a, tg in tabs.items():
    for t in tg:
        if t in _into and t != _into[t]: bad(f'jump table {a:#x} target {t:#x} lies inside exclusive section {_into[t]:#x}')
_nofall = lambda w: w & 0xFC000000 == 0x14000000 or w == 0xD65F03C0 or w & 0xFFFFFC1F == 0xD61F0000   # b / ret / br
for o, e in _into.items():
    if o != e and _into.get(o - 4) != e and not _nofall(W(orig, o - 4)): bad(f'{o - 4:#x} falls through into exclusive section {e:#x} at {o:#x}')
_xst = [o for o, t in D.items() if RX18.search(t) and re.match(r'st[l]?x[rp][bh]?\s', t) and o not in _into]
if _xst: bad(f'exclusive stores with x18 outside every exclusive section: {[(hex(o), D[o]) for o in _xst[:5]]}')
def _q(b, o): return next(_md.disasm(b[o:o + 4], o), None)
def _txt(b, o):
    q = _q(b, o)
    return f'{q.mnemonic} {q.op_str}' if q else None
_FIELDS = sum(31 << p for p in (0, 5, 10, 16))
def _subst_ok(ow, pw, S):
    """pw is ow with every register field that holds 18 (and only those) set to S."""
    return not (ow ^ pw) & ~_FIELDS and all(((ow >> p) & 31 == (pw >> p) & 31) or ((ow >> p) & 31 == 18 and (pw >> p) & 31 == S) for p in (0, 5, 10, 16))
_BMASK = lambda w: 0xFC000000 if w & 0x7C000000 == 0x14000000 else 0xFFF80000 if w & 0x7E000000 == 0x36000000 else 0xFF000000   # branch bits outside the target and Rt / cond (b: imm26 only)
nsec = 0
for e, ps in sorted(_sec.items()):
    s = btarget(e)
    if s is None: continue                                                        # ② already reported the head
    q0 = _q(pat, s); ks = keyseq(s + 4)
    m = re.fullmatch(r'x(\d+), x(\d+), \[sp, #-0x10\](!?)', q0.op_str) if q0 and q0.mnemonic == 'stp' else None
    nrz = bool(m and m.group(3)); tpl = 8 if AND else 4   # the slot base: one instruction (apple), two (android)
    if not m or not ks or ks[1] != KEY['K18'] or _txt(pat, s + 4) != f'mrs x{m.group(2)}, {"tpidr_el0" if AND else "tpidrro_el0"}' \
            or _txt(pat, s + 16 + tpl) != f'ldr x{m.group(1)}, [x{m.group(2)}]' or nrz != (AND and e not in BELOW):
        bad(f'exclusive section {e:#x}: prologue is not stp xS, xT, [sp, #-16]{"! (or, listed below sp, without !)" if AND else ""}; slot address of K18 in xT; ldr xS, [xT]'); continue
    S, T = int(m.group(1)), int(m.group(2)); nsec += 1; c0 = s + 20 + tpl
    inr = {o for lo, hi, c in ps for o in range(lo, hi, 4)}
    if ps[0][2] is None: ps = [(ps[0][0], ps[0][1], c0)]
    elif e != ps[0][0]:
        if btarget(c0) != next(c + e - lo for lo, hi, c in ps if lo <= e < hi): bad(f'exclusive section {e:#x}: no b to the entry\'s copy after the prologue')
    elif ps[0][2] != c0: bad(f'exclusive section {e:#x}: the first piece is not copied right after the prologue')
    cpos = {o: c + o - lo for lo, hi, c in ps for o in range(lo, hi, 4)}
    exit_ok = lambda x, t: x is not None and W(pat, x) == STRX_(S, T) and W(pat, x + 4) == (LDP_POST_ if nrz else LDP_)(S, T) and btarget(x + 8) == t
    for lo, hi, c in ps:
        for o in range(lo, hi, 4):
            ow, cp = W(orig, o), cpos[o]; pw = W(pat, cp); t = _bt(ow, o)
            x18 = bool(RX18.search(D.get(o, '')))
            if t is not None:
                pt = _bt(pw, cp)
                if pt is None or (ow ^ pw) & _BMASK(ow) or (ow & 0x7C000000 != 0x14000000 and (ow & 31) != (pw & 31) and not (x18 and ow & 31 == 18 and pw & 31 == S)):
                    bad(f'exclusive section {e:#x}: copy of branch {o:#x} is not the same branch'); continue
                if t in inr:
                    if pt != cpos[t]: bad(f'exclusive section {e:#x}: branch {o:#x} → {t:#x} inside the section does not go to its copy')
                elif not exit_ok(pt, t): bad(f'exclusive section {e:#x}: branch {o:#x} → {t:#x} does not go to an exit to {t:#x}')
            elif x18:
                if _txt(pat, cp) != re.sub(r'\b([xw])18\b', lambda mm: f'{mm.group(1)}{S}', _txt(orig, o)) or not _subst_ok(ow, pw, S):
                    bad(f'exclusive section {e:#x}: copy of {o:#x} is not the original with x18 → x{S}')
            elif pw != ow: bad(f'exclusive section {e:#x}: copy of {o:#x} differs from the original')
        if W(orig, hi - 4) & 0xFC000000 != 0x14000000:                            # the piece falls through: to an exit back to hi (a window's first exit follows the copy)
            x = c + hi - lo
            if not (exit_ok(x, hi) or exit_ok(btarget(x), hi)): bad(f'exclusive section {e:#x}: piece {lo:#x}-{hi:#x} does not fall through to an exit to {hi:#x}')
_fs11 = nsoimg.func_starts(orig, TE)
for a, tg in sorted(tabs.items()):
    if sites.get(a) != 'x18' or _txt(orig, a) != 'br x18': bad(f'jump table {a:#x}: not an x18 site at br x18'); continue
    i = _bs.bisect_right(_fs11, a) - 1; f0, f1 = _fs11[i], (_fs11[i + 1] if i + 1 < len(_fs11) else TE)
    if any(not (f0 <= t < f1) or t % 4 for t in tg): bad(f'jump table {a:#x}: a target outside its function {f0:#x}-{f1:#x}')
    s = btarget(a); u = sorted(set(tg)); ks = (keyseq(s + 4) if AND else keyseq(s + 8)) if s is not None else None
    if AND: ok = bool(ks) and ks[1] == KEY['K18'] and _txt(pat, s) == 'stp x16, x17, [sp, #-0x10]!' and _txt(pat, s + 4) == 'mrs x16, tpidr_el0' and _txt(pat, s + 24) == 'ldr x16, [x16]'
    else: ok = bool(ks) and ks[1] == KEY['K18'] and _txt(pat, s) == 'stur x16, [sp, #-0x10]' and _txt(pat, s + 4) == 'stur x17, [sp, #-0x18]' \
        and _txt(pat, s + 8) == 'mrs x16, tpidrro_el0' and _txt(pat, s + 24) == 'ldr x16, [x16]'
    pads = (s or 0) + 28 + 16 * len(u) + 4
    for k, t in enumerate(u if ok else []):
        o = s + 28 + 16 * k; qa, qb = _q(pat, o), _q(pat, o + 4)
        ok = ok and qa.mnemonic == 'adrp' and qa.op_str.startswith('x17,') and qa.operands[1].imm == t & ~0xFFF \
            and qb.mnemonic == 'add' and qb.op_str.startswith('x17, x17, #') and qb.operands[2].imm == t & 0xFFF and _txt(pat, o + 8) == 'sub x17, x17, x16' \
            and _txt(pat, o + 12) == f'cbz x17, #{pads + 12 * k:#x}' and btarget(pads + 12 * k + 8) == t \
            and ([_txt(pat, pads + 12 * k), _txt(pat, pads + 12 * k + 4)] == (['ldp x16, x17, [sp], #0x10', 'nop '] if AND else ['ldur x16, [sp, #-0x10]', 'ldur x17, [sp, #-0x18]']))
    if not (ok and _txt(pat, pads - 4) == 'brk #0x18'): bad(f'jump table {a:#x}: block is not the compare chain for exactly its {len(u)} targets')
print(f'SECTIONS {mod} windows={len(wins)} pieces_sections={len(psecs)} checked={nsec} explicit_jump_tables={len(tabs)} targets={sum(len(set(t)) for t in tabs.values())}')

cnt = {}
if _hostcall: print(f'cuts introduce {len(_hostcall)} host-call gates: ' + ', '.join(f"{a:#x}=svc#{n:#x}" for a, n in _hostcall.items()))
for k in sites.values(): cnt[k] = cnt.get(k, 0) + 1
if AND: print(f'ANDROID {mod} below_sp={len(BELOW)} {[hex(x) for x in sorted(BELOW)][:12]}')
print(f'STRUCT {mod} {"OK" if not fails else "FAIL"} sites={len(sites)} {cnt} jump_tables={jumps} fnptr_jumps={fnptr} x18_in_windows={nwin_ref} '
      f'acquire={len(acq)} cuts={len(cuts)}(code{ncut_code}/data{ncut_data}) obs_gates={len(obs)} param_gates={len(params)} '
      f'indep_denom={len(ind)} text_words_changed={len(diff)} gates={len(gsites)} slot_accesses={nk} failures={len(fails)}')
sys.exit(1 if fails else 0)
