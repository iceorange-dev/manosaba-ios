// Reflection json → nvn_refl (nvn_refl.m): the binding kinds dmsh_build.py writes. sbuf_ubo = a storage buffer whose address the shader reads from a
// uniform buffer (uniform slot reg, byte off): kind, slot, offset and the no-reuse flag; a missing, odd or out-of-range offset and a missing slot are refused.
// The projected fetch proof's two halves: a fetch call's "proj" and a vertex shader's "screen_locn" (-DDM_TEST_REFL_NO_SCREEN, leaving both unread, fails 2).
// The readable-tile version's outputs: up to eight colours (plan item 326; -DDM_TEST_REFL_FOUR_OUTPUTS, four as before, fails 2).
// A uniform binding's slots read and the bytes they reach (plan item 271; -DDM_TEST_REFL_NO_CRN, the extent left unknown, fails 1).
#import <Foundation/Foundation.h>
#include <stdio.h>
#include "../nvn/nvn_refl.h"
int mtl_c2_tex_base(void) { return 0x20; }
int mtl_c2_samp_base(void) { return 0x568; }
const char *mtl_depth_mode_name(void) { return "near_is_minus_w"; }
static int g_fail, g_n;
#define EXPECT(name, want, got) do { long long w_ = (long long)(want), g_ = (long long)(got); g_n++; if (w_ != g_) g_fail++; \
    printf("%-60s expected %-6lld → got %-6lld %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); } while (0)
static int parse(NSArray *bindings, nvn_refl *o, char *err) {
    NSDictionary *j = @{@"route": @"B", @"stage": @"vs", @"depth_mode": @"near_is_minus_w", @"c2": @{@"tex": @[@0x20, @32], @"samp": @[@0x568, @128]}, @"bindings": bindings};
    return nvn_refl_from_json(j, o, err, 256);
}
int main(void) { @autoreleasepool {
    nvn_refl o; char err[256];
    int r = parse(@[@{@"kind": @"cbuf", @"reg": @2, @"arg_id": @0, @"msl_name": @"vp_c5"},
                    @{@"kind": @"sbuf_ubo", @"reg": @1, @"off": @0x40, @"rw": @NO, @"arg_id": @3, @"msl_name": @"vp_s0"}], &o, err);
    EXPECT("sbuf_ubo parses", 0, r);
    EXPECT("two bindings", 2, o.nb);
    EXPECT("kind SBUFU", NVN_RB_SBUFU, o.b[1].kind);
    EXPECT("uniform slot 1", 1, o.b[1].reg);
    EXPECT("offset 0x40", 0x40, o.b[1].off);
    EXPECT("argument id 3", 3, o.b[1].aid);
    EXPECT("argument blocks not reused (ubo_addr)", 1, o.ubo_addr);
    r = parse(@[@{@"kind": @"sbuf", @"reg": @0, @"rw": @YES, @"arg_id": @0, @"msl_name": @"vp_s0"}], &o, err);
    EXPECT("plain storage buffer: rw kept, argument blocks reusable", 1, !r && o.b[0].kind == NVN_RB_SBUFP && o.b[0].rw && !o.ubo_addr);
    // a uniform binding's slots read (320g) and how far they reach (271: the private copies of updated ranges hold that much)
    r = parse(@[@{@"kind": @"cbuf", @"reg": @2, @"arg_id": @0, @"msl_name": @"vp_c5", @"cb_slots": @[@[@0, @3], @[@30, @31]]},
                @{@"kind": @"cbuf", @"reg": @3, @"arg_id": @1, @"msl_name": @"vp_c6"},
                @{@"kind": @"cbuf", @"reg": @4, @"arg_id": @2, @"msl_name": @"vp_c7", @"cb_slots": [NSNull null]}], &o, err);
    EXPECT("cb_slots: two ranges parse, the others' slots not known", 1, !r && o.b[0].ncr == 2 && o.b[0].cr[1][0] == 30 && o.b[0].cr[1][1] == 31 && !o.b[1].ncr && !o.b[2].ncr);
    EXPECT("... read through slot 31 and the 48 bytes after it: 560 bytes [-DDM_TEST_REFL_NO_CRN]", 560, o.b[0].crn);
    EXPECT("... not known: 0 (all of the block)", 0, o.b[1].crn + o.b[2].crn);
    EXPECT("sbuf_ubo without an offset is refused", 1, parse(@[@{@"kind": @"sbuf_ubo", @"reg": @1, @"arg_id": @0, @"msl_name": @"vp_s0"}], &o, err));
    EXPECT("sbuf_ubo with an unaligned offset is refused", 1, parse(@[@{@"kind": @"sbuf_ubo", @"reg": @1, @"off": @0x42, @"arg_id": @0, @"msl_name": @"vp_s0"}], &o, err));
    EXPECT("sbuf_ubo with an offset past 64 KiB is refused", 1, parse(@[@{@"kind": @"sbuf_ubo", @"reg": @1, @"off": @0x10000, @"arg_id": @0, @"msl_name": @"vp_s0"}], &o, err));
    EXPECT("sbuf_ubo without a slot is refused", 1, parse(@[@{@"kind": @"sbuf_ubo", @"reg": [NSNull null], @"off": @0x40, @"arg_id": @0, @"msl_name": @"vp_s0"}], &o, err));
    {   // projected fetch proof (dmsh_fetch.py): call 1 projected, call 0 not; the vertex shader's screen outputs 1 and 6, an out-of-range one ignored
        NSDictionary *fj = @{@"route": @"B", @"stage": @"ps", @"depth_mode": @"near_is_minus_w", @"c2": @{@"tex": @[@0x20, @32], @"samp": @[@0x568, @128]}, @"bindings": @[],
                             @"fetch": @{@"calls": @[@{@"fc": @0, @"tex_reg": @16, @"varying_locn": @0}, @{@"fc": @1, @"tex_reg": @1, @"varying_locn": @6, @"proj": @1}],
                                         @"mixed_regs": @[], @"outputs": @[@{@"color": @1, @"type": @"float4"}]}};
        r = nvn_refl_from_json(fj, &o, err, 256);
        EXPECT("fetch calls with a projected one parse", 0, r);
        EXPECT("fc_proj: only call 1", 2, o.fc_proj);
        NSDictionary *vj = @{@"route": @"B", @"stage": @"vs", @"depth_mode": @"near_is_minus_w", @"c2": @{@"tex": @[@0x20, @32], @"samp": @[@0x568, @128]}, @"bindings": @[],
                             @"screen_locn": @[@1, @6, @40]};
        r = nvn_refl_from_json(vj, &o, err, 256);
        EXPECT("screen_locn parses", 0, r);
        EXPECT("scr_locn: outputs 1 and 6", (1 << 1) | (1 << 6), o.scr_locn);
        EXPECT("no screen_locn: nothing proven", 0, (parse(@[], &o, err), o.scr_locn));
        // the readable-tile version's outputs (plan item 326): up to eight, in order; nine refused [-DDM_TEST_REFL_FOUR_OUTPUTS: five refused]
        NSMutableArray *outs = [NSMutableArray array];
        for (NSNumber *c in @[@0, @1, @3, @5, @7]) [outs addObject:@{@"color": c, @"type": @"float4"}];
        NSMutableDictionary *oj = [fj mutableCopy]; NSMutableDictionary *fe = [fj[@"fetch"] mutableCopy]; fe[@"outputs"] = outs; oj[@"fetch"] = fe;
        r = nvn_refl_from_json(oj, &o, err, 256);
        EXPECT("five fetch outputs parse: colours 0, 1, 3, 5 and 7", 1, !r && o.nfo == 5 && o.fo_color[0] == 0 && o.fo_color[1] == 1 && o.fo_color[2] == 3 && o.fo_color[3] == 5 && o.fo_color[4] == 7);
        for (NSNumber *c in @[@2, @4, @6]) [outs addObject:@{@"color": c, @"type": @"float4"}];
        r = nvn_refl_from_json(oj, &o, err, 256);
        EXPECT("eight fetch outputs parse", 1, !r && o.nfo == 8 && o.fo_color[7] == 6);
        [outs addObject:@{@"color": @0, @"type": @"float4"}];
        EXPECT("nine fetch outputs are refused", 1, nvn_refl_from_json(oj, &o, err, 256) != 0);
    }
    printf("== %d checks, %d failed ==\n", g_n, g_fail);
    return g_fail != 0;
} }
