// Shader reflection json (B: produced by translating the original Switch shaders, output of tools/dmsh_build.py) → fixed-length C arrays (nvn_refl.h)
#import "nvn_refl.h"
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>

// Dummy texture type: same prefix rules as the original dummy_tex (an unbound texture slot reads 0 per D3D semantics)
static void dummy_kind(NSString *t, uint8_t *tt, uint8_t *pf) {
    MTLTextureType y = MTLTextureType2D;
    if ([t hasPrefix:@"texture2d_array"] || [t hasPrefix:@"depth2d_array"]) y = MTLTextureType2DArray;
    else if ([t hasPrefix:@"texturecube_array"] || [t hasPrefix:@"depthcube_array"]) y = MTLTextureTypeCubeArray;
    else if ([t hasPrefix:@"texturecube"] || [t hasPrefix:@"depthcube"]) y = MTLTextureTypeCube;
    else if ([t hasPrefix:@"texture3d"]) y = MTLTextureType3D;
    else if ([t hasPrefix:@"texture1d_array"]) y = MTLTextureType1DArray;
    else if ([t hasPrefix:@"texture1d"]) y = MTLTextureType1D;
    *tt = (uint8_t)y;
    *pf = [t hasPrefix:@"depth"] ? NVN_DPF_DEPTH : [t containsString:@"<uint"] ? NVN_DPF_UINT : [t containsString:@"<int"] ? NVN_DPF_SINT : NVN_DPF_FLOAT;
}
// The declared type exactly, for the type check at binding (dummy_kind folds the multisample types into 2D for the dummy textures)
static uint8_t decl_type(NSString *t) {
    if ([t hasPrefix:@"texture2d_ms_array"] || [t hasPrefix:@"depth2d_ms_array"]) return (uint8_t)MTLTextureType2DMultisampleArray;
    if ([t hasPrefix:@"texture2d_ms"] || [t hasPrefix:@"depth2d_ms"]) return (uint8_t)MTLTextureType2DMultisample;
    uint8_t tt, pf; dummy_kind(t, &tt, &pf); return tt;
}

int nvn_refl_from_json(NSDictionary *j, nvn_refl *o, char *err, int errlen) {
    memset(o, 0, sizeof *o);
    #define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); return 1; } while (0)
    if (![j[@"route"] isEqual:@"B"]) FAIL("not a B reflection (route ≠ B; route A / PC-chain reflections are retired)");
    {   // Built for this game's c[2] layout (dmsh_build.py takes it from game.json nvn_c2): slot numbers decoded with another layout would bind wrong textures
        int mtl_c2_tex_base(void), mtl_c2_samp_base(void);
        NSDictionary *c2 = j[@"c2"]; NSArray *t = [c2 isKindOfClass:[NSDictionary class]] ? c2[@"tex"] : nil, *s = [c2 isKindOfClass:[NSDictionary class]] ? c2[@"samp"] : nil;
        if (![t isKindOfClass:[NSArray class]] || ![s isKindOfClass:[NSArray class]] || t.count != 2 || s.count != 2) FAIL("no c2 layout");
        if ([t[0] intValue] != mtl_c2_tex_base() || [s[0] intValue] != mtl_c2_samp_base())
            FAIL("built for c[2] textures %#x / samplers %#x, this game's are %#x / %#x", [t[0] intValue], [s[0] intValue], mtl_c2_tex_base(), mtl_c2_samp_base());
        const char *mtl_depth_mode_name(void); NSString *dm = j[@"depth_mode"];   // vertex shaders: built for the game's depth convention
        if ([j[@"stage"] isEqual:@"vs"] && ![dm isEqual:@(mtl_depth_mode_name())]) FAIL("vertex shader built for depth mode %s, this game's is %s", dm ? dm.UTF8String : "(none)", mtl_depth_mode_name());
    }
    {   // Translated original Switch shaders: kind and NVN slot number are already parsed from the MSL member names by dmsh_build.py
        NSArray *bs = j[@"bindings"];
        if (![bs isKindOfClass:[NSArray class]]) FAIL("missing bindings");
        if (bs.count > NVN_REFL_MAXB) FAIL("%lu bindings, exceeds the limit %d", (unsigned long)bs.count, NVN_REFL_MAXB);
        o->route_b = 1; o->abi = 0;   // spirv-cross set 0 → [[buffer(0)]]
        o->frag_coord = [j[@"frag_coord"] boolValue];   // plan item 320f (absent in no reflection of this chain)
        for (NSDictionary *b in bs) {
            NSString *k = b[@"kind"]; nvn_rbind *r = &o->b[o->nb];
            id rg = b[@"reg"]; int reg = [rg isKindOfClass:[NSNumber class]] ? [rg intValue] : -1, aid = [b[@"arg_id"] intValue];
            r->aid = (uint16_t)aid; r->reg = reg < 0 ? 0 : (uint16_t)reg;
            { id si = b[@"scale_ix"]; int v = [si isKindOfClass:[NSNumber class]] ? [si intValue] : -1;   // plan item 320f: render_scale[1 + v]
              if (v > 71) FAIL("B binding %s: resolution-scale index %d past render_scale's 73", [b[@"msl_name"] UTF8String], v);
              r->six = v >= 0 ? (uint8_t)(v + 1) : 0; }
            if ([k isEqualToString:@"cbuf"]) { r->kind = NVN_RB_CBUF;
                NSArray *cs = b[@"cb_slots"]; r->ncr = 0; r->crh = 0; r->crn = 0;   // plan item 320g: the slots read (absent or null: not known, the whole block)
                if ([cs isKindOfClass:[NSArray class]] && cs.count > 0 && cs.count <= 8) {
                    uint32_t h = 2166136261u;
                    for (NSArray *p in cs) { if (![p isKindOfClass:[NSArray class]] || p.count != 2) { r->ncr = 0; break; }
                        int lo = [p[0] intValue], hi = [p[1] intValue]; if (lo < 0 || hi < lo || hi > 4095) FAIL("B binding %s: slot range %d-%d", [b[@"msl_name"] UTF8String], lo, hi);
                        r->cr[r->ncr][0] = (uint16_t)lo; r->cr[r->ncr][1] = (uint16_t)hi; r->ncr++; h = (h ^ (uint32_t)lo) * 16777619u; h = (h ^ (uint32_t)hi) * 16777619u; }
                    r->crh = r->ncr ? h : 0;
                    uint32_t top = 0; for (int q = 0; q < r->ncr; q++) if (r->cr[q][1] > top) top = r->cr[q][1];
#ifndef DM_TEST_REFL_NO_CRN   // negative control of t_refl only: the extent read left unknown (every private copy whole, as before)
                    r->crn = r->ncr ? (top + 1) * 16 + 48 : 0;
#endif
                } }
            else if ([k isEqualToString:@"samp"]) r->kind = NVN_RB_SAMP;
            else if ([k isEqualToString:@"samp_tex"]) r->kind = NVN_RB_SAMPT;
            else if ([k isEqualToString:@"tex"]) { r->kind = NVN_RB_TEX; dummy_kind(b[@"msl_type"], &r->dtt, &r->dpf); r->decl_tt = decl_type(b[@"msl_type"]); }
            else if ([k isEqualToString:@"img"]) { r->kind = NVN_RB_IMG; dummy_kind(b[@"msl_type"], &r->dtt, &r->dpf); }
            else if ([k isEqualToString:@"tbuf"]) { r->kind = NVN_RB_TBUF; uint8_t tt; dummy_kind(b[@"msl_type"], &tt, &r->dpf); }
            else if ([k isEqualToString:@"sbuf"]) { r->kind = NVN_RB_SBUFP; r->rw = [b[@"rw"] boolValue]; }
            else if ([k isEqualToString:@"ubo_ptr"]) r->kind = NVN_RB_UBOP;
            else if ([k isEqualToString:@"sbuf_ubo"]) { r->kind = NVN_RB_SBUFU; r->rw = [b[@"rw"] boolValue]; int off = [b[@"off"] intValue];
                if (![b[@"off"] isKindOfClass:[NSNumber class]] || off < 0 || off > 0xfff8 || off % 4) FAIL("B binding %s: uniform offset %d of the storage buffer address", [b[@"msl_name"] UTF8String], off);
                r->off = (uint16_t)off; o->ubo_addr = 1; }
            else if ([k isEqualToString:@"support"]) r->kind = NVN_RB_SUPPORT;
            else if ([k isEqualToString:@"c1"]) r->kind = NVN_RB_C1;
            else if ([k isEqualToString:@"drvcb"]) r->kind = [b[@"bank"] intValue] == 2 ? NVN_RB_DRV2 : NVN_RB_DRV0;
            else FAIL("B binding kind %s not implemented (%s)", k.UTF8String, [b[@"msl_name"] UTF8String]);
            if ((r->kind == NVN_RB_CBUF || r->kind == NVN_RB_SAMP || r->kind == NVN_RB_SAMPT || r->kind == NVN_RB_TEX || r->kind == NVN_RB_IMG || r->kind == NVN_RB_TBUF || r->kind == NVN_RB_SBUFP || r->kind == NVN_RB_UBOP || r->kind == NVN_RB_SBUFU) && reg < 0)
                FAIL("B binding %s has no resolved slot number", [b[@"msl_name"] UTF8String]);
            o->nb++;
        }
        NSDictionary *dr = j[@"drv_reads"];
        if ([dr isKindOfClass:[NSDictionary class]]) { o->drv_read[0] = dr[@"0"] != nil; o->drv_read[2] = dr[@"2"] != nil; }
        // A declared driver-segment binding means the shader reads it (spirv-cross declares only what the shader uses): the build's drv_reads scan
        // matched only fragment-shader names (fp_c0), so the vertex and compute shaders reading c0 were given an all-zero segment (nxnative 9579030,
        // MIT; in Tears of the Kingdom's library 229 vertex and 146 compute shaders, all of which declare it)
        for (int i = 0; i < o->nb; i++) { if (o->b[i].kind == NVN_RB_DRV0) o->drv_read[0] = 1; else if (o->b[i].kind == NVN_RB_DRV2) o->drv_read[2] = 1; }
        NSArray *uf = j[@"drv_unfillable"];
        if ([uf isKindOfClass:[NSArray class]] && uf.count) FAIL("driver-segment locations read at run time that the replay layer cannot fill: %s", [[uf componentsJoinedByString:@" "] UTF8String]);
        NSString *c1 = j[@"c1"];
        if (c1.length % 2 || c1.length / 2 > sizeof o->c1) FAIL("c1 is %lu bytes: over the limit or an odd number of hex digits", (unsigned long)c1.length / 2);
        if (c1.length) { o->has_c1 = 1; o->c1_len = (uint16_t)(c1.length / 2); for (unsigned i = 0; i < o->c1_len; i++) o->c1[i] = (uint8_t)strtoul([[c1 substringWithRange:NSMakeRange(2 * i, 2)] UTF8String], NULL, 16); }
        for (NSDictionary *v in j[@"vertex_inputs"]) {   // [[attribute(n)]]: n is the NVN vertex attribute number
            int loc = [v[@"location"] intValue]; NSString *t = v[@"msl_type"];
            if (loc < 0 || loc >= NVN_REFL_MAXV || o->nv >= NVN_REFL_MAXV) FAIL("B vertex input %d out of range", loc);
            o->v[o->nv].loc = o->v[o->nv].nvn = (uint8_t)loc;
            o->v[o->nv].ct = [t hasPrefix:@"uint"] ? NVN_CT_UINT : [t hasPrefix:@"int"] ? NVN_CT_INT : NVN_CT_FLOAT;
            o->nv++;
        }
        NSDictionary *ra = j[@"raster"];
        o->pos_attr = -1; o->may_discard = 1; o->writes_smask = 1;
        if ([ra isKindOfClass:[NSDictionary class]]) {
            if (ra[@"pos_attr"]) { int pa = [ra[@"pos_attr"] intValue]; o->pos_attr = (int8_t)(pa >= 0 && pa < 16 ? pa : -1); o->pos_w1 = [ra[@"pos_w"] isEqual:@"one"]; }
            if (ra[@"discard"]) o->may_discard = [ra[@"discard"] intValue] != 0;
            if (ra[@"sample_mask"]) o->writes_smask = [ra[@"sample_mask"] intValue] != 0;
            for (NSNumber *c in ra[@"colors"]) if (c.intValue >= 0 && c.intValue < 8) o->color_mask |= (uint8_t)(1u << c.intValue);
            o->color_known = [ra[@"colors"] isKindOfClass:[NSArray class]];
        }
        memset(o->uv_attr, -1, sizeof o->uv_attr); memset(o->uv_aff_attr, -1, sizeof o->uv_aff_attr);
        NSDictionary *ua = [ra isKindOfClass:[NSDictionary class]] ? ra[@"uv_affine"] : nil;
        if ([ua isKindOfClass:[NSDictionary class]]) for (NSString *k in ua) { int l = k.intValue; NSDictionary *e = ua[k]; int a = [e[@"attr"] intValue];
            if (l < 0 || l >= 32 || a < 0 || a >= 16) continue;
            NSArray *refs[4] = {e[@"sx"], e[@"ox"], e[@"sy"], e[@"oy"]}; int ok = 1;
            for (int q = 0; q < 4; q++) { NSArray *r = refs[q]; if (![r isKindOfClass:[NSArray class]] || r.count != 3) { ok = 0; break; }
                int sl = [r[0] intValue], vec = [r[1] intValue], cmp = [r[2] intValue]; if (sl < 0 || sl > 15 || vec < 0 || vec > 63 || cmp < 0 || cmp > 3) { ok = 0; break; }
                o->uv_aff[l][q] = (uint16_t)(sl << 8 | vec << 2 | cmp); }
            if (ok) o->uv_aff_attr[l] = (int8_t)a; }
        NSDictionary *uv = [ra isKindOfClass:[NSDictionary class]] ? ra[@"uv_from_attr"] : nil;
        if ([uv isKindOfClass:[NSDictionary class]]) for (NSString *k in uv) { int l = k.intValue, a = [uv[k] intValue]; if (l >= 0 && l < 32 && a >= 0 && a < 16) o->uv_attr[l] = (int8_t)a; }
        NSArray *sl = j[@"screen_locn"];
#ifndef DM_TEST_REFL_NO_SCREEN
        if ([sl isKindOfClass:[NSArray class]]) for (NSNumber *l in sl) if (l.intValue >= 0 && l.intValue < 32) o->scr_locn |= 1u << l.intValue;
#else
        (void)sl;
#endif
        NSDictionary *fe = j[@"fetch"];
        if ([fe isKindOfClass:[NSDictionary class]]) {
            for (NSDictionary *c in fe[@"calls"]) { int i = [c[@"fc"] intValue], r = [c[@"tex_reg"] intValue], l = [c[@"varying_locn"] intValue];
                if (i != o->nfc || i >= 32 || r < 0 || r >= 128 || l < 0 || l >= 32) FAIL("fetch call table not contiguous or out of range (entry %d)", i);
#ifndef DM_TEST_REFL_NO_SCREEN   // negative control of t_refl only
                if ([c[@"proj"] intValue] == 1) o->fc_proj |= 1u << o->nfc;
#endif
                o->fc_reg[o->nfc] = (uint8_t)r; o->fc_locn[o->nfc] = (uint8_t)l; o->nfc++; }
            for (NSNumber *r in fe[@"mixed_regs"]) if (r.intValue >= 0 && r.intValue < 128) o->fc_mixed[r.intValue >> 6] |= 1ull << (r.intValue & 63);
#ifndef DM_TEST_REFL_FOUR_OUTPUTS   // negative control of t_refl only: four outputs at most, as before plan item 326
            for (NSDictionary *u in fe[@"outputs"]) { if (o->nfo >= 8) FAIL("more than 8 fetch outputs"); o->fo_color[o->nfo++] = (uint8_t)[u[@"color"] intValue]; }
#else
            for (NSDictionary *u in fe[@"outputs"]) { if (o->nfo >= 4) FAIL("more than 4 fetch outputs"); o->fo_color[o->nfo++] = (uint8_t)[u[@"color"] intValue]; }
#endif
        }
        NSArray *tg = j[@"threadgroup_size"];
        if ([tg isKindOfClass:[NSArray class]] && tg.count == 3) for (int i = 0; i < 3; i++) o->tg[i] = (uint16_t)[tg[i] intValue];
        return 0;
    }
    #undef FAIL
}
