#!/usr/bin/env python3
"""gfx/tools/dmsh_fetch.py, the static halves of the projected fetch proof on MSL as the translator writes it:
- a fragment shader's sample at float2(in.m.x * r, in.m.y * r), r = 1.0 / in.m.w of a perspective input, becomes a readable-tile call marked "proj"
  (operands in either order, spvFMul too), with the input's location; a flat input, x and y of different inputs, the w of another input, swapped
  components and a value defined twice are left alone; an unprojected sample stays a plain call;
- a vertex shader's screen outputs: ((w + x) * 0.5, (w - y) * 0.5, -, w) of the final position; a later flip of y, a write inside a block, the sign
  of y wrong, another w, a second return and a whole-vector write after the components are not proven;
- the translator's own edge push after the game's code (plan items 320f / 320g: the position's x and y written last from
  support_buffer.widen_push / widen_push_y) is seen through, in x and y, with spvFMul / spvFAdd / spvFSub too: the screen output is still
  proven (plan item 325: it was not from chain 28614768c859 to ba6c7622e044, and every decal of Tears of the Kingdom was copied for). A tail
  that is not exactly the push is not seen through: another bound, the other field, a field at another offset, a position's w written after
  the push read it, a second change of x after the push; the fields' offsets are the Metal host's (gfx/nvn/mtl/args.inc).
- a fragment shader's outputs (plan item 326): up to eight colours, and a depth output beside them, which the readable-tile version keeps
  as it is (every colour gets its eight places, the depth member and its write untouched); nine colours, a sample mask output or a depth
  output declared another way are refused.
DM_TEST_FETCH_NO_PROJ=1 (projected samples left alone, as before 2026-10-22) fails 5 (the three outputs checks use one); DM_TEST_FETCH_NO_TAIL=1 (the push not seen through) 3;
DM_TEST_FETCH_FOUR_OUTPUTS=1 (four colours at most and no depth output, as before) 3."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'gfx', 'tools'))
import dmsh_fetch as f
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<74} {"✓" if ok else "✗"}')

FS = '''#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct spvDescriptorSetBuffer0
{
    texture2d<float> fp_t_tcb_A [[id(0)]];
    sampler fp_t_tcb_ASmplr [[id(1)]];
};

struct main0_out
{
    float4 m_62 [[color(1)]];
};

struct main0_in
{
    float4 m_47 [[user(locn0)]];
    float4 m_57 [[user(locn6)]];
    float4 m_59 [[user(locn7), flat]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])
{
    main0_out out = {};
BODY
    float4 _273 = spvDescriptorSet0.fp_t_tcb_A.sample(spvDescriptorSet0.fp_t_tcb_ASmplr, select(_959, float2(0.0), isnan(_959))).xyzw;
    out.m_62 = _273;
    return out;
}
'''
BIND = [{'kind': 'tex', 'msl_name': 'fp_t_tcb_A', 'reg': 1}]
def fs(body):
    v, m = f.variant(FS.replace('BODY', body), BIND)
    return m['calls'] if v else []
PROJ = '''    float _115 = in.m_57.x;
    float _117 = in.m_57.y;
    float _206 = in.m_57.w;
    float _230 = 1.0 / _206;
    float _252 = _115 * _230;
    float _254 = _230 * _117;
    float2 _959 = float2(_252, _254);'''
c = fs(PROJ)
check('projected sample: one call, "proj", the input\'s location 6, texture slot 1', c == [{'fc': 0, 'tex_reg': 1, 'varying_locn': 6, 'proj': 1}])
c = fs(PROJ.replace('_115 * _230', 'spvFMul(_115, _230)').replace('_230 * _117', 'spvFMul(_230, _117)'))
check('the same through spvFMul', len(c) == 1 and c[0].get('proj') == 1)
check('a flat input is left alone', fs(PROJ.replace('in.m_57', 'in.m_59')) == [])
check('x and y of different inputs are left alone', fs(PROJ.replace('float _117 = in.m_57.y;', 'float _117 = in.m_47.y;')) == [])
check('the w of another input is left alone', fs(PROJ.replace('float _206 = in.m_57.w;', 'float _206 = in.m_47.w;')) == [])
check('swapped components are left alone', fs(PROJ.replace('float2 _959 = float2(_252, _254);', 'float2 _959 = float2(_254, _252);')) == [])
check('a value defined twice is left alone', fs(PROJ + '\n    float _230 = 2.0;') == [])
c = fs('    float _115 = in.m_47.x;\n    float _117 = in.m_47.y;\n    float2 _959 = float2(_115, _117);')
check('an unprojected sample stays a plain call (location 0, no "proj")', c == [{'fc': 0, 'tex_reg': 1, 'varying_locn': 0}])

VS = '''#include <metal_stdlib>
using namespace metal;

struct main0_out
{
    float4 m_44 [[user(locn0)]];
    float4 m_54 [[user(locn6)]];
    float4 gl_Position [[position, invariant]];
};

vertex main0_out main0(main0_in in [[stage_in]])
{
    main0_out out = {};
    out.m_44 = float4(0.0, 0.0, 0.0, 1.0);
    out.m_54 = float4(0.0, 0.0, 0.0, 1.0);
    out.gl_Position.x = 0.0;
    out.gl_Position.y = 0.0;
    out.gl_Position.w = 1.0;
    float _342 = in.m_1.y;
    float _356 = in.m_1.w;
    float _358 = in.m_1.x;
    out.gl_Position.y = _342;
    out.gl_Position.w = _356;
    float _360 = -_342;
    float _362 = _356 + _360;
    out.gl_Position.x = _358;
    float _364 = _356 + _358;
    out.m_54.w = _356;
    float _366 = _362 * 0.5;
    float _368 = _364 * 0.5;
    out.m_54.y = _366;
    out.m_54.x = _368;
    out.m_44.x = _358;
    out.m_44.y = _342;
    out.m_44.w = _356;
TAIL    return out;
}
'''
def vs(tail='', src=None): return f.screen_locn((src or VS).replace('TAIL', tail))
check('vertex: output 6 holds the screen position (output 0 does not)', vs() == [6])
check('a later flip of the position\'s y: not proven', vs('    float _400 = -_342;\n    out.gl_Position.y = _400;\n') == [])
check('the last write inside a block: not proven', vs('    if (_356 > 0.0)\n    {\n        out.m_54.x = _364;\n    }\n') == [])
check('the sign of y wrong ((w + y) / 2): not proven', vs(src=VS.replace('float _362 = _356 + _360;', 'float _362 = _356 + _342;')) == [])
check('another w: not proven', vs(src=VS.replace('    out.m_54.w = _356;', '    out.m_54.w = _364;')) == [])
check('a second return: not proven', vs('    if (_356 > 0.0)\n    {\n        return out;\n    }\n') == [])
check('a whole-vector write after the components: not proven', vs('    out.m_54 = float4(0.0, 0.0, 0.0, 1.0);\n') == [])

# The edge push as EmitterContext.EmitWidenPushAxis emits it (Tears of the Kingdom's 1986375bdaab24ad_b, the SSA names renumbered)
SUPPORT = '''struct _10
{
    uint _m0;
    uint4 _m1[8];
    float4 _m2;
    float4 _m3;
    int _m4;
    float4 _m5[73];
    int4 _m6;
    int _m7;
    float4 _m8[73];
    float4 _m9;
    float4 _m10;
};

struct spvDescriptorSetBuffer0
{
    constant _10* support_buffer [[id(0)]];
};

'''
def push(axis, mem, n):
    return f'''    float _{n} = out.gl_Position.{axis};
    float _{n+2} = out.gl_Position.w;
    float _{n+4} = (*spvDescriptorSet0.support_buffer).{mem}.x;
    float _{n+6} = (*spvDescriptorSet0.support_buffer).{mem}.y;
    float _{n+8} = (*spvDescriptorSet0.support_buffer).{mem}.z;
    float _{n+10} = 1.0 - _{n+8};
    float _{n+12} = _{n} * _{n+10};
    float _{n+14} = _{n+12} / _{n+2};
    float _{n+16} = _{n+14} * _{n+14};
    bool _{n+18} = _{n+6} < _{n+16};
    bool _{n+20} = _{n+16} < 0.9940090179443359375;
    bool _{n+22} = 0.0 < _{n+8};
    float _{n+24} = _{n+6} * 1.012073040008544921875;
    bool _{n+26} = _{n+16} < _{n+24};
    bool _{n+28} = _{n+22} || _{n+26};
    bool _{n+30} = _{n+18} && _{n+20};
    bool _{n+32} = _{n+30} && _{n+28};
    float _{n+34} = _{n+4} * _{n+2};
    bool _{n+36} = 0.0 < _{n+14};
    float _{n+38} = _{n+12} + _{n+34};
    float _{n+40} = _{n+12} - _{n+34};
    int _{n+42} = _{n+36} ? as_type<int>(_{n+38}) : as_type<int>(_{n+40});
    int _{n+44} = _{n+32} ? _{n+42} : as_type<int>(_{n+12});
    float _{n+1001} = as_type<float>(_{n+44});
    out.gl_Position.{axis} = _{n+1001};
'''
VSP = VS.replace('struct main0_out', SUPPORT + 'struct main0_out').replace('main0(main0_in in [[stage_in]])', 'main0(main0_in in [[stage_in]], constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])')
PX, PY = push('x', '_m9', 500), push('y', '_m10', 600)
check('the edge push in x and y after the game\'s code: output 6 still holds the screen position [DM_TEST_FETCH_NO_TAIL=1: not]', vs(PX + PY, VSP) == [6])
spv = (PX + PY).replace('_500 * _510', 'spvFMul(_500, _510)').replace('_512 + _534', 'spvFAdd(_512, _534)').replace('_612 - _634', 'spvFSub(_612, _634)').replace('_604 * _602', 'spvFMul(_604, _602)')
check('... written with spvFMul / spvFAdd / spvFSub', spv != PX + PY and vs(spv, VSP) == [6])
check('the push in x alone (y as the game wrote it)', vs(PX, VSP) == [6])
check('another bound in the tail (0.99 for 0.994009): not the push, not proven', vs(PX.replace('0.9940090179443359375', '0.99') + PY, VSP) == [])
check('the tail of x reading widen_push_y: not proven', vs(push('x', '_m10', 500) + PY, VSP) == [])
check('the field at another offset (a member less before it): not proven', vs(PX + PY, VSP.replace('    float4 _m3;\n', '')) == [])
check('the position\'s w written after the push read it: not proven', vs(PX + PY + '    out.gl_Position.w = _356;\n', VSP) == [])
check('x changed again after the push: not proven', vs(PX + PY + '    float _800 = -_358;\n    out.gl_Position.x = _800;\n', VSP) == [])
args = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'gfx', 'nvn', 'mtl', 'args.inc')).read()
import re
hx, hy = re.search(r'#define SUPPORT_WIDEN_PUSH (\d+)', args), re.search(r'#define SUPPORT_WIDEN_PUSH_Y (\d+)', args)
check('the fields\' offsets are the Metal host\'s (SUPPORT_WIDEN_PUSH, SUPPORT_WIDEN_PUSH_Y)', hx and hy and f.WIDEN_PUSH == {'x': int(hx.group(1)), 'y': int(hy.group(1))} and f._support_offsets(VSP).get('_m9') == f.WIDEN_PUSH['x'] and f._support_offsets(VSP).get('_m10') == f.WIDEN_PUSH['y'])

# The outputs (plan item 326): Tears of the Kingdom's c345c12c5f1a4f06 writes colours 0, 1, 3, 5 and 7, its ecbe4aa4417b3f80 three colours and a depth
def outs_fs(members, writes=''):
    src = FS.replace('    float4 m_62 [[color(1)]];\n', members).replace('BODY', PROJ).replace('    out.m_62 = _273;\n', writes)
    return f.variant(src, BIND)
five = ''.join(f'    float4 m_{60 + 2 * i} [[color({c})]];\n' for i, c in enumerate((0, 1, 3, 5, 7)))
v, m = outs_fs(five, ''.join(f'    out.m_{60 + 2 * i} = _273;\n' for i in range(5)))
check('five colour outputs (0, 1, 3, 5, 7): a readable-tile version, each output with its eight places [DM_TEST_FETCH_FOUR_OUTPUTS=1: none]',
      bool(v) and [o['color'] for o in m['outputs']] == [0, 1, 3, 5, 7] and all(f'm_{60 + 2 * i}_at{k} [[color({k}), function_constant(OM_{i}_{k})]]' in v for i in range(5) for k in range(8))
      and 'constant bool OM_4_7 [[function_constant(103)]]' in v)
eight = ''.join(f'    float4 m_{60 + 2 * i} [[color({i})]];\n' for i in range(8))
v, m = outs_fs(eight)
check('eight colour outputs: made (the last output\'s last place is function constant 127)', bool(v) and len(m['outputs']) == 8 and 'constant bool OM_7_7 [[function_constant(127)]]' in v)
dep = '    float4 m_62 [[color(1)]];\n    float4 m_64 [[color(3)]];\n    float gl_FragDepth [[depth(any)]];\n'
v, m = outs_fs(dep, '    out.m_62 = _273;\n    out.m_64 = _273;\n    out.gl_FragDepth = _115;\n')
check('two colours and a depth output: made, the depth member and its write as they were',
      bool(v) and [o['color'] for o in m['outputs']] == [1, 3] and '    float gl_FragDepth [[depth(any)]];\n' in v and '    out.gl_FragDepth = _115;\n' in v and 'spv_o_m_62 = _273' in v)
check('nine colour outputs: refused', outs_fs(eight + '    float4 m_90 [[color(8)]];\n')[0] is None)
check('a sample mask output: refused', outs_fs('    float4 m_62 [[color(1)]];\n    uint gl_SampleMask [[sample_mask]];\n')[0] is None)
check('a depth output declared another way (depth(less)): refused', outs_fs('    float4 m_62 [[color(1)]];\n    float gl_FragDepth [[depth(less)]];\n')[0] is None)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
