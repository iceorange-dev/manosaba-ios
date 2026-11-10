# "Readable-tile" version of a fragment shader (merge B step 2): within one pass, when a later draw samples, at its own pixel, a color attachment written by an earlier draw,
# it is changed to read the color directly from the tile (framebuffer fetch); the two render passes become one, and the intermediate attachment need not be stored to main memory and loaded back just to be sampled.
# Built offline; at runtime only function-constant specialization is done (no source compilation). Usage: variant(msl, bindings, om_only_ok=False) → (new MSL, metadata) or (None, reason)
# Function constant numbering: FX_i (which color index the i-th rewritable sample reads, -1 = sample as usual) = i; FB_k (enable color input k) = 32 + k;
#               OM_j_k (the j-th output writes to attachment k) = 64 + 8 j + k
# Rewritable samples: .sample(sampler, select(_N, float2(0.0), isnan(_N))) on a 2D float texture, where _N = float2(in.m_X.x, in.m_X.y) (interpolated output unchanged);
# whether the texel is really fetched at this pixel is proven at runtime (correspondence between vertex uv and position, texture size = target size); if it cannot be proven, the original version is used.
# Also _N = float2(in.m_X.x * r, in.m_X.y * r) with r = 1.0 / in.m_X.w of a perspective-interpolated input ("proj": the decals of Tears of the Kingdom's
# G-buffer, which read their own pixel of the pass they draw into): at a pixel centre that is exactly the pixel's texel centre when the paired vertex
# shader writes that output as ((w + x) * 0.5, (w - y) * 0.5, -, w) of its final position (screen_locn below, the vertex side of the proof) and the
# viewport covers the whole target; the host checks both. After OJZen/totk-mobile's framebuffer-fetch proof (host/nvn_framebuffer_fetch.inc,
# Apache-2.0), written anew for this tool.
# The final position's x and y are written last by the translator's own edge push (EmitterContext.EmitWidenPushAxis, plan items 320f / 320g:
# x' = x * (1 - shrink), moved by push * w inside a band, from support_buffer.widen_push / widen_push_y). With those fields all 0, as the host
# leaves them for every draw but a HUD draw it pushes (and it pushes none that reads the tile: gfx/nvn/mtl/draw.inc), x' is x bit for bit:
# x * 1.0, no move (the band's two tests both false), an int reinterpreted back. screen_locn takes the position's x and y from before that
# tail when the tail is exactly the push (_push_tail). Taking the tail's result instead, the proof found no vertex shader at all from chain
# 28614768c859 on: Tears of the Kingdom's decals went back to copies and splits (plan item 325)
import os, re

NFB = 8
_NO_PROJ = bool(os.environ.get('DM_TEST_FETCH_NO_PROJ'))   # negative control of tools/tests/t_dmsh_fetch.py only: projected samples left alone (before 2026-10-22)
_FOUR_OUT = bool(os.environ.get('DM_TEST_FETCH_FOUR_OUTPUTS'))   # negative control of tools/tests/t_dmsh_fetch.py only: at most four colour outputs and no depth output, as before plan item 326
_NO_TAIL = bool(os.environ.get('DM_TEST_FETCH_NO_TAIL'))   # negative control of tools/tests/t_dmsh_fetch.py and gfx/tests/t_fetch_cache.py only: the edge push not seen through (chains 28614768c859 to ba6c7622e044)
WIDEN_PUSH = {'x': 2560, 'y': 2576}   # support_buffer.widen_push / widen_push_y (gfx/nvn/mtl/args.inc SUPPORT_WIDEN_PUSH, SUPPORT_WIDEN_PUSH_Y; t_dmsh_fetch.py compares)
_SIZE = {'uint': (4, 4), 'int': (4, 4), 'float': (4, 4), 'uint4': (16, 16), 'int4': (16, 16), 'float4': (16, 16), 'char': (1, 1)}

def _support_offsets(src):
    """{member: offset} of the support buffer's struct, by Metal's alignment (as gfx/tests/t_scale_msl.py measures it); {} without one"""
    m = re.search(r'constant (\w+)\* support_buffer \[\[id\(\d+\)\]\];', src)
    body = m and re.search(r'struct %s\s*\{(.*?)\};' % m.group(1), src, re.S)
    if not body: return {}
    out, off = {}, 0
    for t, nm, cnt in re.findall(r'^\s*(\w+) (\w+)(?:\[(\d+)\])?;', body.group(1), re.M):
        if t not in _SIZE: return {}
        sz, al = _SIZE[t]; off = (off + al - 1) // al * al; out[nm] = off; off += sz * int(cnt or 1)
    return out

def _push_tail(b, axis, v, offsets):
    """b: a vertex function's body, v: the value last written to gl_Position.<axis>. When v is exactly the translator's edge push of that axis
    (every statement EmitWidenPushAxis emits, whatever the SSA names) reading the support buffer's field at WIDEN_PUSH[axis]: the offset in b of
    the statement that loads gl_Position.<axis> for it (the game's value: what the push returns when the field is 0). Else None"""
    def d(x):   # the only definition of an SSA value of any scalar type, with where it is
        r = list(re.finditer(r'\n +(?:float|bool|int|uint) %s = ([^;\n]+);' % re.escape(x), b)) if x else []
        return (r[0].group(1), r[0].start()) if len(r) == 1 else (None, -1)
    def m(x, pat):   # the groups of x's definition against pat (%s: an SSA name), or None
        e = d(x)[0]
        r = e and re.fullmatch(pat.replace('%s', r'(_\d+)'), e)
        return r.groups() if r else None
    def two(x, op, fn):   # a op b, or spv<fn>(a, b)
        return m(x, r'%s ' + re.escape(op) + r' %s') or m(x, 'spv' + fn + r'\(%s, %s\)')
    def field(x, comp):   # the support buffer member x is loaded from, when its component is comp
        e = d(x)[0]
        r = e and re.fullmatch(r'\(\*spvDescriptorSet0\.support_buffer\)\.(\w+)\.%s' % comp, e)
        return r.group(1) if r else None
    g = m(v, r'as_type<float>\(%s\)')
    g = g and m(g[0], r'%s \? %s : as_type<int>\(%s\)')
    if not g: return None
    move, sel, xs = g
    g = two(xs, '*', 'FMul')
    if not g: return None
    x0, s1 = g
    e0, p0 = d(x0)
    sh = m(s1, r'1\.0 - %s')
    mem = sh and field(sh[0], 'z')
    if e0 != 'out.gl_Position.' + axis or not mem or offsets.get(mem) != WIDEN_PUSH[axis]: return None
    sh = sh[0]
    g = m(move, r'%s && %s'); pi, band = g if g else (None, None)
    g = m(pi, r'%s && %s'); past, inside = g if g else (None, None)
    g = m(past, r'%s < %s'); thr, t2 = g if g else (None, None)
    if field(thr, 'y') != mem or m(inside, r'%s < 0\.9940090179443359375') != (t2,): return None
    g = m(band, r'%s \|\| %s'); shpos, near = g if g else (None, None)
    g = m(near, r'%s < %s'); thrk = g[1] if g and g[0] == t2 else None
    if m(shpos, r'0\.0 < %s') != (sh,) or (m(thrk, r'%s \* 1\.012073040008544921875') or m(thrk, r'spvFMul\(%s, 1\.012073040008544921875\)')) != (thr,): return None
    g = two(t2, '*', 'FMul'); t = g[0] if g and g[0] == g[1] else None
    g = m(t, r'%s / %s'); w0 = g[1] if g and g[0] == xs else None
    ew, pw = d(w0)
    if ew != 'out.gl_Position.w': return None
    g = m(sel, r'%s \? as_type<int>\(%s\) : as_type<int>\(%s\)'); pos, a, c = g if g else (None, None, None)
    ga, gc = two(a, '+', 'FAdd'), two(c, '-', 'FSub')
    if m(pos, r'0\.0 < %s') != (t,) or not ga or not gc or ga[0] != xs or gc[0] != xs or ga[1] != gc[1]: return None
    g = two(ga[1], '*', 'FMul')
    if not g or field(g[0], 'x') != mem or g[1] != w0: return None
    if re.search(r'^ +out\.gl_Position(\.w)? = ', b[pw:], re.M): return None   # the w it read is the final one
    return p0


def _def(src, v):   # the only definition "float v = ...;" of an SSA value (None when there is not exactly one)
    d = re.findall(r'\n +float %s = ([^;\n]+);' % re.escape(v), src)
    return d[0] if len(d) == 1 else None

def _mul(e):   # the two operands of "_a * _b" / "spvFMul(_a, _b)"
    m = e and (re.fullmatch(r'(_\d+) \* (_\d+)', e) or re.fullmatch(r'spvFMul\((_\d+), (_\d+)\)', e))
    return (m.group(1), m.group(2)) if m else None

def _projected(msl, va, vb):
    """The interpolated input X when va = in.X.x * r and vb = in.X.y * r with r = 1.0 / in.X.w (operands in either order), else None"""
    def part(v):
        p = _mul(_def(msl, v))
        for a, r in ((p, p[::-1]) if p else ()):
            mi = re.fullmatch(r'in\.(m_\d+)\.([xy])', _def(msl, a) or ''); mr = re.fullmatch(r'1\.0 / (_\d+)', _def(msl, r) or '')
            if mi and mr: return mi.group(1), mi.group(2), mr.group(1)
        return None
    pa, pb = part(va), part(vb)
    if not pa or not pb or pa[0] != pb[0] or (pa[1], pb[1]) != ('x', 'y') or pa[2] != pb[2]: return None
    mw = re.fullmatch(r'in\.(m_\d+)\.w', _def(msl, pa[2]) or '')
    return pa[0] if mw and mw.group(1) == pa[0] else None

def screen_locn(vs):
    """Vertex shader: the user(locn) outputs written as ((w + x) * 0.5, (w - y) * 0.5, -, w) of the final position, sorted. Each component used is decided
    by its last write, made outside any block after any whole-vector write, and the function has one return, its last statement. Anything else: not proven"""
    body = re.search(r'^vertex \w+ main0\(.*?\)\n\{\n(.*?)\n\}\n', vs, re.S | re.M)
    ost = re.search(r'struct main0_out\n\{\n(.*?)\n\};', vs, re.S)
    if not body or not ost: return []
    b = body.group(1)
    rets = [m.start() for m in re.finditer(r'^ +return\b', b, re.M)]
    if len(rets) != 1 or b[rets[0]:].rstrip() != '    return out;': return []
    def last(vec, comp, end=None):   # (end: only the writes before that offset of the body)
        if re.search(r'^ +out\.%s\.[xyzw]{2,4} = ' % vec, b, re.M): return None   # (a swizzled write)
        every = [(m.start(), m.group(2)) for m in re.finditer(r'^( +)out\.%s\.%s = ([^;\n]+);$' % (vec, comp), b, re.M) if len(m.group(1)) == 4] + \
                [(m.start(), None) for m in re.finditer(r'^( +)out\.%s\.%s = ' % (vec, comp), b, re.M) if len(m.group(1)) != 4]
        if end is not None: every = [e for e in every if e[0] < end]
        if not every: return None
        p, v = max(every)
        if v is None or not re.fullmatch(r'_\d+', v) or any(m.start() > p for m in re.finditer(r'^ +out\.%s = ' % vec, b, re.M)): return None
        return v
    offsets = _support_offsets(vs)
    def position(axis):   # the position's x or y: its last write, or the one before the translator's edge push of that axis
        v = last('gl_Position', axis)
        p0 = None if _NO_TAIL or not v else _push_tail(b, axis, v, offsets)
        return v if p0 is None else last('gl_Position', axis, p0)
    X, Y, W = position('x'), position('y'), last('gl_Position', 'w')
    if not (X and Y and W): return []
    def half(v):   # v = s * 0.5 -> s
        e = _def(b, v) or ''
        m = re.fullmatch(r'(_\d+) \* 0\.5', e) or re.fullmatch(r'spvFMul\((_\d+), 0\.5\)', e)
        return m.group(1) if m else None
    def add(v):   # v = a + b -> {a, b}
        e = _def(b, v or '') or ''
        m = re.fullmatch(r'(_\d+) \+ (_\d+)', e) or re.fullmatch(r'spvFAdd\((_\d+), (_\d+)\)', e)
        return {m.group(1), m.group(2)} if m and m.group(1) != m.group(2) else None
    res = []
    for mem, loc in re.findall(r'^    float4 (m_\d+) \[\[user\(locn(\d+)\)\]\];$', ost.group(1), re.M):
        ox, oy, ow = last(mem, 'x'), last(mem, 'y'), last(mem, 'w')
        if not (ox and oy and ow) or ow != W: continue
        sx, sy = add(half(ox)), add(half(oy))
        if sx != {W, X} or not sy or W not in sy: continue
        if _def(b, (sy - {W}).pop()) == '-' + Y: res.append(int(loc))
    return sorted(res)

def variant(msl, bindings, om_only_ok=False):
    tex = {m.group(1) for m in re.finditer(r'texture2d<float> (\w+) \[\[id\(\d+\)\]\];', msl)}
    locn = {m.group(1): int(m.group(2)) for m in re.finditer(r'float4 (m_\d+) \[\[user\(locn(\d+)\)\]\];', msl.split('struct main0_in', 1)[-1].split('};', 1)[0])}
    reg = {b['msl_name']: b['reg'] for b in bindings if b.get('kind') == 'tex' and b.get('msl_name') and b.get('reg') is not None}
    pat = re.compile(r'spvDescriptorSet0\.(\w+)\.sample\(spvDescriptorSet0\.(\w+)Smplr, select\((_\d+), float2\(0\.0\), isnan\(\3\)\)\)')
    calls, out_parts, pos = [], [], 0
    for m in pat.finditer(msl):
        member, var = m.group(1), m.group(3)
        if member not in tex or member != m.group(2) or member not in reg: continue
        d = re.search(r'float2 %s = float2\((_\d+), (_\d+)\);' % var, msl)
        a = d and re.search(r'float %s = in\.(m_\d+)\.x;' % d.group(1), msl); b = d and re.search(r'float %s = in\.(m_\d+)\.y;' % d.group(2), msl)
        proj = None
        if not (a and b) or a.group(1) != b.group(1) or a.group(1) not in locn:
            proj = d and not _NO_PROJ and _projected(msl, d.group(1), d.group(2))
            if not proj or proj not in locn: continue
        i = len(calls)
        if i >= 32: break
        sel = ''.join(f'FX_{i} == {k} ? fb{k} : ' for k in range(NFB))
        out_parts.append(msl[pos:m.start()]); out_parts.append(f'({sel}{m.group(0)})'); pos = m.end()
        calls.append({'fc': i, 'tex_reg': reg[member], 'varying_locn': locn[proj or a.group(1)]} | ({'proj': 1} if proj else {}))
    if not calls and not om_only_ok:
        return None, 'no sample to rewrite'
    # Mixed-use slots: the same texture also has other (non-rewritable) uses - at runtime this texture must not be switched to tile reads (the rewritten reads and the unrewritten ones would not see the same data)
    conv = {}
    for c in calls: conv[c['tex_reg']] = conv.get(c['tex_reg'], 0) + 1
    mixed = sorted({reg[mb] for mb in reg if mb in tex and msl.count(f'spvDescriptorSet0.{mb}.') > conv.get(reg[mb], 0)})
    body = ''.join(out_parts) + msl[pos:]
    om = re.search(r'struct main0_out\s*\{(.*?)\};', body, re.S)
    if not om: return None, 'no output struct'
    outs = [(t, n, int(c)) for t, n, c in re.findall(r'(\w+) (m_\d+) \[\[color\((\d+)\)\]\];', om.group(1))]
    # Up to NFB colour outputs, and a depth output beside them, which stays as it is (plan item 326: at most four and colours only before; Tears
    # of the Kingdom's G-buffer draws that read their own pixel write five to eight colours, some a depth: 356 of its fragment shaders, each
    # draw copied for and its pass split)
    rest = re.sub(r'\w+ m_\d+ \[\[color\(\d+\)\]\];', '', om.group(1))
    if not _FOUR_OUT: rest = re.sub(r'float gl_FragDepth \[\[depth\(any\)\]\];', '', rest, count=1)
    if not outs or len(outs) > (4 if _FOUR_OUT else NFB) or rest.strip(): return None, 'the output struct is not colour outputs with at most a depth output (or more than %d colours)' % (4 if _FOUR_OUT else NFB)
    members = ''.join(f'    {t} {n}_at{k} [[color({k}), function_constant(OM_{j}_{k})]];\n' for j, (t, n, c) in enumerate(outs) for k in range(NFB))
    keep = ''.join(l + '\n' for l in om.group(1).split('\n') if 'gl_FragDepth' in l)   # (the depth output's member, after the colours)
    body = body[:om.start(1)] + '\n' + members + keep + body[om.end(1):]
    consts = ''.join(f'constant int FX_{c["fc"]} [[function_constant({c["fc"]})]];\n' for c in calls) + \
             ''.join(f'constant bool FB_{k} [[function_constant({32 + k})]];\n' for k in range(NFB)) + \
             ''.join(f'constant bool OM_{j}_{k} [[function_constant({64 + 8 * j + k})]];\n' for j in range(len(outs)) for k in range(NFB))
    body = body.replace('using namespace metal;\n', 'using namespace metal;\n\n' + consts, 1)
    i0 = body.find('fragment main0_out main0(')
    i1 = body.find(')\n{', i0) if i0 >= 0 else -1   # end of signature: closing parenthesis immediately followed by a newline and an opening brace (parameter attributes contain brackets such as [[buffer(0)]])
    if i0 < 0 or i1 < 0: return None, 'main function signature not recognized'
    body = body[:i1] + ''.join(f', float4 fb{k} [[color({k}), function_constant(FB_{k})]]' for k in range(NFB)) + body[i1:]
    decl = ''.join(f'    {t} spv_o_{n} = {t}(0);\n' for t, n, c in outs)
    body = body.replace('    main0_out out = {};\n', '    main0_out out = {};\n' + decl, 1)
    for t, n, c in outs: body = body.replace(f'out.{n}', f'spv_o_{n}')
    put = ''.join(f'    if (OM_{j}_{k}) out.{n}_at{k} = spv_o_{n};\n' for j, (t, n, c) in enumerate(outs) for k in range(NFB))
    if body.count('    return out;\n') < 1: return None, 'no return out'
    body = body.replace('    return out;\n', put + '    return out;\n')
    return body, {'calls': calls, 'mixed_regs': mixed, 'outputs': [{'color': c, 'type': t} for t, n, c in outs]}
