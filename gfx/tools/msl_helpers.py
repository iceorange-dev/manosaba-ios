#!/usr/bin/env python3
# SPIRV-Cross writes the additions the SPIR-V marks NoContraction (a Maxwell FADD that must not fuse with the FMUL before it) through a helper
#     template<typename T>
#     [[clang::optnone]] T spvFAdd(T l, T r)
#     {
#         return fma(T(1), l, r);
#     }
# whose optnone keeps the compiler from fusing it, at the price of a real function call on the GPU: 3,088 of chain bab8d4746968's 31,392 files,
# 8,641 call sites. dmsh_build.py passes every MSL through inline_helpers(): the helpers are inlined instead, with FP contraction off inside their
# own bodies only (totk-mobile's nvn_shader_math.h turns it off for the whole file and the math mode with it; Apache-2.0, the idea only), so the
# rest of the file compiles as before. Measured (gfx/tests/t_fpcontract.m, bit for bit against the optnone helper): under our safe math mode not
# even a plain a * b + c is fused, and the inlined fma(1, l, r) stays unfused under fast math too. In dmsh_chain.py's FILES (a change here is a new
# chain). Usage (the tests' runner): msl_helpers.py <MSL> [--plain | --wide | --wide-across]  → the rewritten MSL on stdout (--plain: the broken
# variant, a plain l + r; --wide: wide_loads below, --wide-across its broken variant)
#
# precise_minmax() (plan item 260, after seedds/nxnative 56704a0, MIT, written anew): SPIRV-Cross writes the translator's FMin / FMax / FClamp as
# fast::min / max / clamp, which compile to air.fast_fmin / fast_fmax / fast_clamp in every Metal math mode, safe included, and leave a NaN
# operand's result to the GPU. Maxwell's FMNMX returns the other operand and its saturation gives 0, as precise:: (air.fmin / fmax / clamp, IEEE
# fmin / fmax) does on every GPU. The M4 Pro's and our M5's fast ones happen to agree (fast::max(x, 0) of NaN is 0 on the M5), an A20 Pro's do
# not: Super Mario Galaxy 2's lighting takes max(0, NaN) from light directions left at zero, and half of Mario came out white on that iPhone.
# Only the fast:: calls are rewritten: in a 300-shader sample of chain f9e7d3ecf49e every plain min / max / clamp left is an integer one, and no
# fast_ intrinsic call is left in the compiled libraries.
#
# wide_loads() (plan item 311b, after seedds/nxnative 1b9fbea, MIT, written anew): Maxwell reads a storage buffer 64 or 128 bits at a time
# (LDG.E.64 / .128); the translator splits such a read into 32-bit words, and SPIRV-Cross gives each word an index of its own,
#     int _b = _a + 4;  uint _i = uint(int(uint(_b) >> uint(2)));  float _v = as_type<float>((*spvDescriptorSet0.fp_s0)._m0[int(_i)]);
# which Metal's compiler does not see as neighbours, so every word stays a load of its own (nxnative: Splatoon Raiders' lit pass 70 -> 16 ms on
# an M1 once its light words were read whole). Here the words of one read-only (const device) buffer at byte offsets k, k + 4, ... from one value,
# inside one stretch of straight-line code, become one packed_uint2 / 3 / 4 read at the first word, each word taking its lane: a packed vector needs
# 4-byte alignment only, and only words the shader reads are in it. A stretch ends at a brace and at every line that may write memory or change a
# value or leave (a store, an atomic, a barrier, an image write, an assignment, return / discard_fragment / break / continue), so no write through
# another binding to the same memory falls between the words, and reading the later words earlier gives the values they had. A value the offsets
# lead back through that is assigned anywhere in the shader (a loop's variable) must have its sums in the stretch too. A run starts on a 16-byte
# step of the common value and holds at most four words, as the original reads did; one whose first word is not read first stays as it was.
import re, sys

HELPER = re.compile(r'\[\[clang::optnone\]\] (T spvF(?:Add|Sub|Mul)\(T l, T r\)\n\{\n)')
FAST_MINMAX = re.compile(r'\bfast::(min|max|clamp)\(')

def inline_helpers(src):
    """The MSL with every optnone arithmetic helper inlined, contraction off in its body; unchanged when it has none"""
    return HELPER.sub(lambda m: 'inline __attribute__((always_inline)) ' + m.group(1) + '#pragma clang fp contract(off)\n', src)

def precise_minmax(src):
    """The MSL with every fast::min / max / clamp call written as precise:: (Maxwell's results for a NaN operand), and how many were rewritten"""
    return FAST_MINMAX.subn(r'precise::\1(', src)

WL_ADD = re.compile(r'^\s*int (_\w+) = (_\w+) \+ \(?(-?\d+)\)?;$')                  # a sum (SPIRV-Cross writes a negative one as + (-k))
WL_IDX = re.compile(r'^\s*uint (_\w+) = uint\(int\(uint\((_\w+)\) >> uint\(2\)\)\);$')   # a byte offset to a word index
WL_LOAD = re.compile(r'\(\*spvDescriptorSet0\.((?:vp|fp|cp)_s\d+)\)\._m0\[int\((_\w+)\)\]')   # a word of a storage buffer (dmsh_build.py DRV_PREFIX)
WL_RO = re.compile(r'^\s*const device (_\w+)\* ((?:vp|fp|cp)_s\d+) \[\[id\(\d+\)\]\];$', re.M)
WL_SET = r'(?:[-+*/%|&^]|<<|>>)?=(?!=)'
WL_ASSIGN = re.compile(r'^\s*(_\w+)(?:\.\w+|\[[^\]]*\])*\s*' + WL_SET)               # an assignment to a variable declared before
WL_STOP = re.compile(r'[{}]|atomic|barrier\(|\.write\(|spvArrayCopy|\]\s*' + WL_SET + r'|^\s*\*|\b(?:return|discard_fragment|break|continue)\b')
WL_VEC = {2: 'packed_uint2', 3: 'packed_uint3', 4: 'packed_uint4'}

def wide_loads(src, across=False):
    """The MSL with the split words of each read-only storage buffer read as one packed vector where that gives the same values (see above), and
    how many reads were made. across=True is the broken variant: stretches reach across writes and assignments (braces still end them)"""
    if '>> uint(2)' not in src: return src, 0
    ro = {b for t, b in WL_RO.findall(src) if re.search(r'^struct ' + t + r'\n\{\n    uint _m0\[', src, re.M)}   # buffers of 32-bit words
    if not ro: return src, 0
    lines = src.split('\n')
    stretch, at, decl, adds, idx, assigned = [], 0, {}, {}, {}, set()
    for i, ln in enumerate(lines):   # each line's stretch; the sums and indices and where they are declared; the variables assigned
        a = WL_ASSIGN.match(ln)
        if a: assigned.add(a.group(1))
        if WL_STOP.search(ln) and not (across and not re.search(r'[{}]', ln)) or (a and not across): at += 1; stretch.append(None)   # a read on such a
        else: stretch.append(at)                                                                   # line comes before its write: in no run
        m = WL_ADD.match(ln) or WL_IDX.match(ln)
        if m:
            name = m.group(1); decl[name] = None if name in decl else i   # declared twice: not used
            (adds if m.re is WL_ADD else idx)[name] = (m.group(2), int(m.group(3)) if m.re is WL_ADD else 0)
    words = {}   # (stretch, buffer, value) -> {byte offset: (line, match)}
    for i, ln in enumerate(lines):
        ms = list(WL_LOAD.finditer(ln))
        if len(ms) != 1 or stretch[i] is None or ms[0].group(1) not in ro or decl.get(ms[0].group(2)) is None: continue
        v, off, chain = idx[ms[0].group(2)][0], 0, [ms[0].group(2)]
        while v in adds and decl.get(v) is not None and len(chain) < 64: chain.append(v); v, k = adds[v]; off += k
        if v in decl and decl[v] is None: continue
        if (assigned & {v, *chain}) and any(stretch[decl[c]] != stretch[i] for c in chain): continue   # a variable's sums: in this stretch only
        if off % 4 == 0: words.setdefault((stretch[i], ms[0].group(1), v), {}).setdefault(off, (i, ms[0]))
    edits, n = {}, 0
    for (_, buf, _), by_off in sorted(words.items(), key=lambda kv: min(i for i, _ in kv[1].values())):
        for base in sorted({off - off % 16 for off in by_off}):
            run = []
            while len(run) < 4 and base + 4 * len(run) in by_off: run.append(by_off[base + 4 * len(run)])
            if len(run) < 2 or any(i < run[0][0] for i, _ in run): continue
            name, vec = f'_dmw{n}', WL_VEC[len(run)]; n += 1
            first = f'{vec} {name} = *(const device {vec}*)&(*spvDescriptorSet0.{buf})._m0[int({run[0][1].group(2)})];'
            for w, (i, m) in enumerate(run): edits.setdefault(i, []).append((m.start(), m.end(), f'{name}.{"xyzw"[w]}', first if w == 0 else None))
    for i, es in edits.items():
        ln, pre = lines[i], []
        for s0, e0, rep, first in sorted(es, reverse=True):
            ln = ln[:s0] + rep + ln[e0:]
            if first: pre.insert(0, first)
        ind = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
        lines[i] = ''.join(ind + d + '\n' for d in pre) + ln
    return '\n'.join(lines), n

if __name__ == '__main__':   # (--wide: wide_loads alone, its count on stderr; --wide-across: its broken variant)
    if '--wide' in sys.argv or '--wide-across' in sys.argv:
        out, n = wide_loads(open(sys.argv[1]).read(), across='--wide-across' in sys.argv); sys.stderr.write(f'{n} wide reads\n'); sys.stdout.write(out); sys.exit(0)
    out = inline_helpers(open(sys.argv[1]).read())
    if '--plain' in sys.argv: out = out.replace('#pragma clang fp contract(off)\n    return fma(T(1), l, r);', '    return l + r;')
    sys.stdout.write(out)
