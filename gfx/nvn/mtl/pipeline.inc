// gfx/nvn/mtl/pipeline.inc, part of nvn_mtl.m (included there in order, one translation unit): Draw state, vertex formats, blending, pipeline states, the pipeline prediction
// ---------- Draw state ----------
static void state_vp_ms(float vp[4], int ms[3]) { for (int k = 0; k < 4; k++) vp[k] = S.has_vp ? S.vp[0][k] : 0; ms[0] = S.ms.enable; ms[1] = S.ms.samples; ms[2] = S.ms.raster_samples; }
// BindProgram per stage: prog_bind (gfx/nvn/core/prog.c)
static uint64_t g_udc_rt_walks;   // attachment walks of udc_note_draw (once per pass and attachment set; t_poolcache counts them)
static void udc_note_draw(NvProg *pr) {
    // Zero-source set: an attachment really drawn is no longer all zeros. Done for the first draw of a pass (and again if its attachments change):
    // later draws of the pass would find nothing left to remove (upstream afad65c keeps one draw's attachment events per pass)
    { static uint64_t pass = UINT64_MAX, sig; uint64_t s = (uint64_t)R.rt.n; for (int i = 0; i < R.rt.n && i < 8; i++) s = s * 1000003u ^ (R.rt.color[i] ? R.rt.color[i]->serial : 0);
      if (R.has_rt && (pass != g_stat_pass || sig != s)) { pass = g_stat_pass; sig = s; g_udc_rt_walks++;
          for (int i = 0; i < R.rt.n && i < 8; i++) if (R.rt.color[i]) { zskip_del(R.rt.color[i]->serial); g_zskip_cand_ok[i] = 0; } } }
    if (pr) prog_summary(pr);
    if (pr && pr->sum_img) zskip_clear();   // Image slots are writable: conservatively clear
    int image_write = pr ? pr->sum_imgw : 0;
    uint32_t shader_color_mask = pr ? pr->sum_cmask : 0;
    if (R.has_rt && g_enc) for (int i = 0; i < R.rt.n && i < 8; i++)   // a slot left out of this pass (see rt_dup_slot) really written: those writes are lost
        if ((shader_color_mask >> i & 1) && ((S.chmask.mask >> (4 * i)) & 0xf) && (g_rt_dup_skip >> i & 1))
            RED_ONCE(80, "colour slot %d attaches the same texture as another slot (ser=%llu) and this draw writes it (mask %#x): those writes are lost", i, (unsigned long long)R.rt.color[i]->serial, (S.chmask.mask >> (4 * i)) & 0xf);
    for (int i = 0; i < g_nsd; i++) {
        sd_att *t = &g_sd[i]; uint8_t w0 = t->wwhy; t->wwhy = 0;
        if (image_write) { t->wrote = 1; t->wwhy = 1; }
        else if (t->depth == 0 && t->idx >= 0 && (((S.chmask.mask >> (4 * t->idx)) & 0xf) || (shader_color_mask >> t->idx & 1))) { t->wrote = 1; t->wwhy = (S.chmask.mask >> (4 * t->idx)) & 0xf ? 2 : 3; }
        else if (t->depth == 1 && S.ds.dw) { t->wrote = 1; t->wwhy = 4; }
        else if (t->depth == 2 && S.ds.st && (S.st_wmask[0] || S.st_wmask[1])
                && (S.ds.fail_f != NVN_STENCIL_OP_KEEP || S.ds.zfail_f != NVN_STENCIL_OP_KEEP || S.ds.zpass_f != NVN_STENCIL_OP_KEEP
                    || S.ds.fail_b != NVN_STENCIL_OP_KEEP || S.ds.zfail_b != NVN_STENCIL_OP_KEEP || S.ds.zpass_b != NVN_STENCIL_OP_KEEP)) { t->wrote = 1; t->wwhy = 5; }
#ifndef DM_TEST_FBX_SPLIT_EACH   // negative control of t_fbx only: a sampling draw's writes count too (a split before every draw of a run)
        if (t->wwhy && !g_fbx_self) t->pw = 1;
#else
        if (t->wwhy) t->pw = 1;
#endif
        if (!t->wwhy) t->wwhy = w0;
    }
}
static void state_reset(void) { st_reset(); g_pso_dirty = g_dss_dirty = 1; }   // (the state: gfx/nvn/core/state.c)

static MTLVertexFormat vfmt(int f) {
    switch (f) {
        case 0x01: return MTLVertexFormatUCharNormalized; case 0x02: return MTLVertexFormatCharNormalized; case 0x03: return MTLVertexFormatUChar; case 0x04: return MTLVertexFormatChar;
        case 0x05: return MTLVertexFormatHalf; case 0x06: return MTLVertexFormatUShortNormalized; case 0x07: return MTLVertexFormatShortNormalized;
        case 0x08: return MTLVertexFormatUShort; case 0x09: return MTLVertexFormatShort; case 0x0A: return MTLVertexFormatFloat; case 0x0B: return MTLVertexFormatUInt; case 0x0C: return MTLVertexFormatInt;
        case 0x0D: return MTLVertexFormatUChar2Normalized; case 0x0E: return MTLVertexFormatChar2Normalized; case 0x0F: return MTLVertexFormatUChar2; case 0x10: return MTLVertexFormatChar2;
        case 0x11: return MTLVertexFormatHalf2; case 0x12: return MTLVertexFormatUShort2Normalized; case 0x13: return MTLVertexFormatShort2Normalized; case 0x14: return MTLVertexFormatUShort2;
        case 0x15: return MTLVertexFormatShort2; case 0x16: return MTLVertexFormatFloat2; case 0x17: return MTLVertexFormatUInt2; case 0x18: return MTLVertexFormatInt2;
        case 0x19: return MTLVertexFormatUChar3Normalized; case 0x1A: return MTLVertexFormatChar3Normalized; case 0x1B: return MTLVertexFormatUChar3; case 0x1C: return MTLVertexFormatChar3;
        case 0x1D: return MTLVertexFormatHalf3; case 0x1E: return MTLVertexFormatUShort3Normalized; case 0x1F: return MTLVertexFormatShort3Normalized; case 0x20: return MTLVertexFormatUShort3;
        case 0x21: return MTLVertexFormatShort3; case 0x22: return MTLVertexFormatFloat3; case 0x23: return MTLVertexFormatUInt3; case 0x24: return MTLVertexFormatInt3;
        case 0x25: return MTLVertexFormatUChar4Normalized; case 0x26: return MTLVertexFormatChar4Normalized; case 0x27: return MTLVertexFormatUChar4; case 0x28: return MTLVertexFormatChar4;
        case 0x29: return MTLVertexFormatHalf4; case 0x2A: return MTLVertexFormatUShort4Normalized; case 0x2B: return MTLVertexFormatShort4Normalized; case 0x2C: return MTLVertexFormatUShort4;
        case 0x2D: return MTLVertexFormatShort4; case 0x2E: return MTLVertexFormatFloat4; case 0x2F: return MTLVertexFormatUInt4; case 0x30: return MTLVertexFormatInt4;
        case 0x3D: return MTLVertexFormatUInt1010102Normalized; case 0x69: return MTLVertexFormatInt1010102Normalized;   // RGB10A2SN (TotK full-screen quads; nxnative)
        case 0x3F: return MTLVertexFormatFloatRG11B10; case 0x9A: return MTLVertexFormatUChar4Normalized_BGRA;
        // *_UI2F / *_I2F: the integer converted to its float value (BotW's uking_grass uses RG8_UI2F; jiangjifeng's nxnative fork ae9082d). Metal does
        // exactly that for an 8 / 16-bit integer format read into a float input (measured on an M5: UChar2 17 / 203 read as 17.0 / 203.0, Short2 -32768 as
        // -32768.0); it refuses 32-bit integers for a float input, so the 32-bit and 10:10:10:2 kinds stay unmapped (red 15)
        case 0x51: return MTLVertexFormatUChar; case 0x52: return MTLVertexFormatChar; case 0x53: return MTLVertexFormatUShort; case 0x54: return MTLVertexFormatShort;
        case 0x57: return MTLVertexFormatUChar2; case 0x58: return MTLVertexFormatChar2; case 0x59: return MTLVertexFormatUShort2; case 0x5A: return MTLVertexFormatShort2;
        case 0x5D: return MTLVertexFormatUChar3; case 0x5E: return MTLVertexFormatChar3; case 0x5F: return MTLVertexFormatUShort3; case 0x60: return MTLVertexFormatShort3;
        case 0x63: return MTLVertexFormatUChar4; case 0x64: return MTLVertexFormatChar4; case 0x65: return MTLVertexFormatUShort4; case 0x66: return MTLVertexFormatShort4;
    }
    return MTLVertexFormatInvalid;
}
static int vfmt_scaled(int f) { return (f >= 0x51 && f <= 0x54) || (f >= 0x57 && f <= 0x5A) || (f >= 0x5D && f <= 0x60) || (f >= 0x63 && f <= 0x66); }   // the *_UI2F / *_I2F vfmt maps
// Integer vertex formats meeting a float shader input. Maxwell hands integer attributes to the shader unconverted (the raw 32-bit integers), and the
// translated shaders keep attributes in float registers and bit-cast where the original used integer instructions. Metal refuses a 32-bit integer format
// for a float input ("cannot be read using MTLAttributeFormatUInt"), so those are fetched as the float format of the same width: the shader sees the same
// bits. 8 / 16-bit integer formats Metal converts to their float value, not the bits the shader expects: they stay as they are (logged once)
static MTLVertexFormat vfmt_for_float_input(MTLVertexFormat f) {
#ifdef DM_TEST_VFETCH_NO_FLOATIN   // negative control of t_vfetch only: the integer format kept (Metal then refuses the pipeline)
    return f;
#endif
    switch (f) {
        case MTLVertexFormatUInt: case MTLVertexFormatInt: return MTLVertexFormatFloat;
        case MTLVertexFormatUInt2: case MTLVertexFormatInt2: return MTLVertexFormatFloat2;
        case MTLVertexFormatUInt3: case MTLVertexFormatInt3: return MTLVertexFormatFloat3;
        case MTLVertexFormatUInt4: case MTLVertexFormatInt4: return MTLVertexFormatFloat4;
        default: return f;
    }
}
static int vfmt_is_int(MTLVertexFormat f) {
    switch (f) {
        case MTLVertexFormatUChar: case MTLVertexFormatChar: case MTLVertexFormatUChar2: case MTLVertexFormatChar2: case MTLVertexFormatUChar3: case MTLVertexFormatChar3:
        case MTLVertexFormatUChar4: case MTLVertexFormatChar4: case MTLVertexFormatUShort: case MTLVertexFormatShort: case MTLVertexFormatUShort2: case MTLVertexFormatShort2:
        case MTLVertexFormatUShort3: case MTLVertexFormatShort3: case MTLVertexFormatUShort4: case MTLVertexFormatShort4: case MTLVertexFormatUInt: case MTLVertexFormatInt:
        case MTLVertexFormatUInt2: case MTLVertexFormatInt2: case MTLVertexFormatUInt3: case MTLVertexFormatInt3: case MTLVertexFormatUInt4: case MTLVertexFormatInt4: return 1;
        default: return 0;
    }
}
static int vfmt_is_signed(MTLVertexFormat f) {
    switch (f) { case MTLVertexFormatChar: case MTLVertexFormatChar2: case MTLVertexFormatChar3: case MTLVertexFormatChar4: case MTLVertexFormatShort: case MTLVertexFormatShort2:
        case MTLVertexFormatShort3: case MTLVertexFormatShort4: case MTLVertexFormatInt: case MTLVertexFormatInt2: case MTLVertexFormatInt3: case MTLVertexFormatInt4: return 1; default: return 0; }
}
static MTLBlendFactor bfac(int f) {
    switch (f) { case 1: return MTLBlendFactorZero; case 2: return MTLBlendFactorOne; case 3: return MTLBlendFactorSourceColor; case 4: return MTLBlendFactorOneMinusSourceColor;
        case 5: return MTLBlendFactorSourceAlpha; case 6: return MTLBlendFactorOneMinusSourceAlpha; case 7: return MTLBlendFactorDestinationAlpha; case 8: return MTLBlendFactorOneMinusDestinationAlpha;
        case 9: return MTLBlendFactorDestinationColor; case 10: return MTLBlendFactorOneMinusDestinationColor; case 11: return MTLBlendFactorSourceAlphaSaturated;
        case 0x10: return MTLBlendFactorSource1Color; case 0x11: return MTLBlendFactorOneMinusSource1Color; case 0x12: return MTLBlendFactorSource1Alpha; case 0x13: return MTLBlendFactorOneMinusSource1Alpha;
        case 0x61: return MTLBlendFactorBlendColor; case 0x62: return MTLBlendFactorOneMinusBlendColor; case 0x63: return MTLBlendFactorBlendAlpha; case 0x64: return MTLBlendFactorOneMinusBlendAlpha; }
    return MTLBlendFactorOne;
}
static MTLBlendOperation bop(int e) { switch (e) { case 2: return MTLBlendOperationSubtract; case 3: return MTLBlendOperationReverseSubtract; case 4: return MTLBlendOperationMin; case 5: return MTLBlendOperationMax; } return MTLBlendOperationAdd; }

// Pipeline: cached by (program, target formats, blend / write mask, vertex layout, sample count)
static const uint8_t *g_pso_lmap; static id<MTLFunction> g_pso_fsfn; static pso_fz_desc g_pso_fz, g_fz_desc;
static os_unfair_lock g_fz_fn_lock = OS_UNFAIR_LOCK_INIT;
static NSMutableDictionary<NSString *, id<MTLFunction>> *g_bf_fn_cache;
static NSMutableDictionary<NSString *, id<MTLLibrary>> *g_bf_lib_cache;
// bf_fn's results by a binary key (plan item 185): the NSString keys it formatted for every draw that reads the tile took ~16 % of the translation
// thread's busy time in the iPhone's shrine save on the Mac (sample(1), 2026-10-22); the strings are made only the first time a key is seen
typedef struct { char base[24]; uint8_t fbmask, nfc, lmap[8], pad[6]; int32_t fx[32]; } bf_key;
static nvn_hmap g_bf_fast = {.klen = sizeof(bf_key)}; static uint64_t g_bf_fast_hits;
static nvn_hmap g_pso = {.klen = sizeof(pso_key)}, g_dss;
static nvn_hmap g_pso_fail = {.klen = sizeof(pso_key)};   // keys whose pipeline failed to build: not rebuilt on every draw (each attempt is a synchronous compile)
// The main0_vi variant specialized for these unsigned / signed locations (function constants 128 + loc / 144 + loc, dmsh_build.py vi_variant); cached per
// shader. pso_desc also runs on the warm-up threads, hence the lock
static id<MTLFunction> vi_function(NvShader *vs, uint32_t u, uint32_t sg) {
    NSNumber *key = @((uint64_t)u | (uint64_t)sg << 32);
    @synchronized (vs) {
        id<MTLFunction> f = vs->vi_fns[key]; if (f) return f;
        MTLFunctionConstantValues *cv = [MTLFunctionConstantValues new]; bool t = true;
        for (int l = 0; l < 16; l++) { if (u >> l & 1) [cv setConstantValue:&t type:MTLDataTypeBool atIndex:128 + l]; if (sg >> l & 1) [cv setConstantValue:&t type:MTLDataTypeBool atIndex:144 + l]; }
        NSError *e = nil; f = [vs->vi_lib newFunctionWithName:vs->vi_entry constantValues:cv error:&e];
        if (!f) { MLOG("[red] vertex shader %s: specializing %s failed: %s", vs->key, vs->vi_entry.UTF8String, e.localizedDescription.UTF8String); return nil; }
        if (!vs->vi_fns) vs->vi_fns = [NSMutableDictionary dictionary];
        vs->vi_fns[key] = f; return f;
    }
}
// The alpha test (NVN ColorStateSetAlphaTest + CommandBufferSetAlphaRef; Metal has none), after lordran-native a63dd76 (MIT): when the game profile
// asks for it (game.json alpha_test), every fragment shader ends with a comparison of colour 0's alpha under function constants 160 (the NVN
// comparison) and 161 (the reference) (dmsh_build.py alpha_test). Metal builds no pipeline from an unspecialized function, so such a shader is
// specialized with ALWAYS at load (the comparison and its discard fold away) and a draw with another comparison uses a specialization cached per
// shader, comparison and reference. A shader built without it draws without the test (red once). Checked by gfx/tests/t_alpha.m
static int alpha_on(void) { return S.color.alpha_func && S.color.alpha_func != NVN_ALPHA_FUNC_ALWAYS; }
static id<MTLFunction> alpha_spec(id<MTLLibrary> lib, NSString *entry, uint32_t func, uint32_t refbits, NSError **e) {
    MTLFunctionConstantValues *cv = [MTLFunctionConstantValues new]; float ref; memcpy(&ref, &refbits, sizeof ref);
    [cv setConstantValue:&func type:MTLDataTypeUInt atIndex:160]; [cv setConstantValue:&ref type:MTLDataTypeFloat atIndex:161];
    return [lib newFunctionWithName:entry constantValues:cv error:e];
}
static id<MTLFunction> alpha_plain(id<MTLLibrary> lib, NSString *entry) {   // shader_load: the function as drawn without an alpha test
    NSError *e = nil; id<MTLFunction> fn = alpha_spec(lib, entry, NVN_ALPHA_FUNC_ALWAYS, 0, &e);
    if (!fn) MLOG("[red] fragment function %s: specialization without an alpha test failed: %s", entry.UTF8String, e.localizedDescription.UTF8String);
    return fn;
}
static id<MTLFunction> alpha_fn(const pso_key *k, NvShader *fs) {
    if (!fs) return nil;
    if (!k->alpha_func) return fs->fn;
    if (!fs->alib) { RED_ONCE(95, "alpha test %u on fragment shader %s, built without it (game.json alpha_test is off): drawn without the test", k->alpha_func, fs->key); return fs->fn; }
    uint64_t bits = (uint64_t)k->alpha_func << 32 | k->alpha_ref;
    @synchronized (fs) {
        if (!fs->afn) fs->afn = [NSMutableDictionary dictionary];
        id<MTLFunction> fn = fs->afn[@(bits)];
        if (fn) return fn;
        uint32_t func = k->alpha_func; float ref; memcpy(&ref, &k->alpha_ref, sizeof ref);
        NSError *e = nil; fn = alpha_spec(fs->alib, fs->fn.name, func, k->alpha_ref, &e);
        if (!fn) { MLOG("[red] alpha test specialization of %s failed: %s", fs->key, e.localizedDescription.UTF8String); return fs->fn; }
        fs->afn[@(bits)] = fn;
        static _Atomic unsigned nspec; unsigned c = atomic_fetch_add(&nspec, 1) + 1; if (c == 1 || !(c & (c - 1))) MLOG("alpha test: %u fragment shader specializations so far (latest %s, comparison %u, reference %g)", c, fs->key, func, ref);
        return fn;
    }
}
// The pixel shader's integer-output variant (plan item 162; gfx/tools/dmsh_build.py builds <key>_i_b beside the float library for every key in
// fout.tsv): for a pipeline with an integer colour format in a slot the shader writes. One pixel shader can be bound to float targets and to integer
// ones (Tears of the Kingdom: 23 in MacNativeGames' list); nil when there is none (the float function then fails to link and fout_note notes it)
static int pf_int_kind(MTLPixelFormat f);
static id<MTLFunction> fout_fn(NvShader *fs) {
    if (!fs->ifn_tried) {
        fs->ifn_tried = 1;
        NSString *base = [NSString stringWithFormat:@"%@/%.16s_i_b", g_msl_dir, fs->key]; NSError *e = nil;
        id<MTLLibrary> lib = access([base stringByAppendingString:@".metallib"].fileSystemRepresentation, F_OK) ? nil : lib_from_file([base stringByAppendingString:@".metallib"], &e);
        NSData *jd = lib ? [NSData dataWithContentsOfFile:[base stringByAppendingString:@".json"]] : nil;
        NSDictionary *j = jd ? [NSJSONSerialization JSONObjectWithData:jd options:0 error:nil] : nil;
        fs->ifn = j ? [lib newFunctionWithName:j[@"entry"]] : nil;
        if (lib && !fs->ifn) MLOG("[red] pixel shader %.16s: its integer-output variant has no function %s", fs->key, [[j[@"entry"] description] UTF8String]);
    }
    return fs->ifn;
}
static int pso_int_target(const pso_key *k, NvShader *fs) {   // an integer colour format where the shader writes
    for (int i = 0; i < 8; i++) if (k->cfmt[i] && pf_int_kind((MTLPixelFormat)k->cfmt[i]) && (!fs->rf.color_known || (fs->rf.color_mask >> i & 1))) return 1;
    return 0;
}
static MTLRenderPipelineDescriptor *pso_desc(const pso_key *k, NvShader *vs, NvShader *fs, id<MTLFunction> fsfn, int merged) {
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = vs->fn; d.fragmentFunction = fsfn ? fsfn : alpha_fn(k, fs);
#ifndef DM_TEST_FOUT_NO_VARIANT   // negative control of t_fout only: the float function whatever the target
    if (fs && !fsfn && pso_int_target(k, fs) && fout_fn(fs)) d.fragmentFunction = fs->ifn;
#endif
    // Diagnostic DM_NVN_HILITE=fragment shader key,...: these draws use a solid-color fragment instead (keys 1–6 in order magenta / green / cyan / yellow / blue / white), to see where the geometry lands
    if (fs) { static char keys[6][24]; static int nk = -1; static id<MTLLibrary> hl;
        if (nk < 0) { nk = 0; const char *e = getenv("DM_NVN_HILITE");
            for (const char *q = e; q && *q && nk < 6; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) { size_t n = strcspn(q, ","); if (n > 23) n = 23; memcpy(keys[nk], q, n); keys[nk++][n] = 0; } }
        for (int i = 0; i < nk; i++) if (!strncmp(fs->key, keys[i], strlen(keys[i]))) {
            if (!hl) { NSError *e = nil; hl = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nstruct O { float4 c0 [[color(0)]]; float4 c1 [[color(1)]]; float4 c2 [[color(2)]]; };\n"
                "fragment O hl0() { return {float4(1,0,1,1),float4(1,0,1,1),float4(1,0,1,1)}; } fragment O hl1() { return {float4(0,1,0,1),float4(0,1,0,1),float4(0,1,0,1)}; }\n"
                "fragment O hl2() { return {float4(0,1,1,1),float4(0,1,1,1),float4(0,1,1,1)}; } fragment O hl3() { return {float4(1,1,0,1),float4(1,1,0,1),float4(1,1,0,1)}; }\n"
                "fragment O hl4() { return {float4(0,0,1,1),float4(0,0,1,1),float4(0,0,1,1)}; } fragment O hl5() { return {float4(1,1,1,1),float4(1,1,1,1),float4(1,1,1,1)}; }\n" options:nil error:&e];
                if (!hl) MLOG("[red] HILITE shader compile failed %s", e.localizedDescription.UTF8String); }
            if (hl) d.fragmentFunction = [hl newFunctionWithName:[NSString stringWithFormat:@"hl%d", i]];
            break; } }
    d.rasterSampleCount = k->samples ? k->samples : 1;
    d.alphaToCoverageEnabled = (k->a2c & 1) != 0;
    if (k->a2c & PSO_LAYERED) d.maxVertexAmplificationCount = 2;   // a layered pass: the view mapping picks the layer (no topology class: one would refuse vertex shaders that write point size, t_layer)
    for (int i = 0; i < 8; i++) {
        if (!k->cfmt[i]) continue;
        MTLRenderPipelineColorAttachmentDescriptor *c = d.colorAttachments[i];
        c.pixelFormat = (MTLPixelFormat)k->cfmt[i];
        int l = merged ? k->lmap[i] : i;
        uint32_t m = l == 0xff ? 0 : (k->chmask >> (4 * l)) & 0xf;
        // An output the fragment shader does not write: zero write mask, no blending (nxnative 42a50ef). Maxwell writes only the targets in the shader's
        // output map and leaves the others untouched, whatever the blend state and channel mask; on Metal an attachment the shader does not write gets
        // undefined contents, blended in when blending is on
        int unwritten = fs && fs->rf.color_known && !fsfn && l != 0xff && !(fs->rf.color_mask >> l & 1);
        if (unwritten) m = 0;
        c.writeMask = (m & 1 ? MTLColorWriteMaskRed : 0) | (m & 2 ? MTLColorWriteMaskGreen : 0) | (m & 4 ? MTLColorWriteMaskBlue : 0) | (m & 8 ? MTLColorWriteMaskAlpha : 0);
        if (l != 0xff && k->blend_en >> l & 1 && !unwritten) {
            c.blendingEnabled = YES;
            c.sourceRGBBlendFactor = bfac(k->blend[l].src_rgb); c.destinationRGBBlendFactor = bfac(k->blend[l].dst_rgb);
            c.sourceAlphaBlendFactor = bfac(k->blend[l].src_a); c.destinationAlphaBlendFactor = bfac(k->blend[l].dst_a);
            c.rgbBlendOperation = bop(k->blend[l].eq_rgb); c.alphaBlendOperation = bop(k->blend[l].eq_a);
        }
    }
    d.depthAttachmentPixelFormat = (MTLPixelFormat)k->dfmt; d.stencilAttachmentPixelFormat = (MTLPixelFormat)k->sfmt;
    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
    const nvn_refl *rf = &vs->rf; uint32_t vi_u = 0, vi_s = 0;
    for (int v = 0; v < rf->nv; v++) {
        int loc = rf->v[v].loc, a = rf->v[v].nvn;   // Metal slot loc ← NVN attribute a
        MTLVertexFormat vf = a < 16 ? vfmt(k->attr[a].fmt) : MTLVertexFormatInvalid;
        // When a >= S.nattr, k.attr[a] keeps the 0 from memset, vfmt(0) is Invalid, the same fallback branch is taken and the RED_ONCE condition of fmt being non-0 does not hold.
        if (a < 16 && k->attr[a].fmt && vf == MTLVertexFormatInvalid) RED_ONCE(15, "vertex attribute format %#x not mapped", k->attr[a].fmt);
        if (vf == MTLVertexFormatInvalid) {
            int ct = rf->v[v].ct;
            vd.attributes[loc].format = ct == NVN_CT_UINT ? MTLVertexFormatUInt4 : ct == NVN_CT_INT ? MTLVertexFormatInt4 : MTLVertexFormatFloat4;
            vd.attributes[loc].offset = 0; vd.attributes[loc].bufferIndex = 30;
            vd.layouts[30].stride = 16; vd.layouts[30].stepFunction = MTLVertexStepFunctionConstant; vd.layouts[30].stepRate = 0;
            continue;
        }
        int st = k->attr[a].stream;
        if (rf->v[v].ct == NVN_CT_FLOAT && vfmt_is_int(vf) && !vfmt_scaled(k->attr[a].fmt)) {   // (the *_UI2F / *_I2F formats want Metal's conversion)
            // Integer format, float input: the shader's main0_vi variant takes it as uint / int (same bits); without a variant, 32-bit formats are fetched as
            // float of the same width (same bits as well); 8 / 16-bit ones get Metal's conversion to the value, not the bits (logged once per shader)
            const char *how;
            if (loc < 16 && (vs->vi_mask >> loc & 1)) { if (vfmt_is_signed(vf)) vi_s |= 1u << loc; else vi_u |= 1u << loc; how = "specialized variant main0_vi"; }
            else { MTLVertexFormat ff = vfmt_for_float_input(vf); how = ff == vf ? NULL : "fetched as float of the same width (same bits)"; vf = ff; }
            static char seen[64][24]; static int nseen; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;   // one line per vertex shader
            pthread_mutex_lock(&mu); int dup = 0; for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], vs->key)) dup = 1;
            if (!dup && nseen < 64) strlcpy(seen[nseen++], vs->key, sizeof seen[0]);
            pthread_mutex_unlock(&mu);
            if (!dup) MLOG("%svertex shader %s: NVN attribute %d (format %#x) is integer, the shader input is float: %s", how ? "" : "[red] ", vs->key, a, k->attr[a].fmt,
                           how ? how : "no variant: an 8 / 16-bit value reaches the shader converted to float, not as its bits");
        }
        vd.attributes[loc].format = vf; vd.attributes[loc].offset = k->attr[a].offset; vd.attributes[loc].bufferIndex = 16 + st;
        vd.layouts[16 + st].stride = k->stream[st].stride ? k->stream[st].stride : 4;
        vd.layouts[16 + st].stepFunction = k->stream[st].stride == 0 ? MTLVertexStepFunctionConstant : k->stream[st].divisor ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        vd.layouts[16 + st].stepRate = k->stream[st].stride == 0 ? 0 : k->stream[st].divisor ? k->stream[st].divisor : 1;
    }
    d.vertexDescriptor = vd;
    if (vi_u | vi_s) { id<MTLFunction> f = vi_function(vs, vi_u, vi_s); if (f) d.vertexFunction = f; }
    return d;
}
// Pipeline key: current state S + pass formats (normal pass; a merged pass additionally carries lmap / specialized function, filled in by pso_get)
static void pso_key_fill(pso_key *k, NvShader *vs, NvShader *fs, const uint32_t cfmt[8], uint32_t dfmt, uint32_t sfmt, uint32_t samples, int lmap) {
    memset(k, 0, sizeof *k);
    k->vs = (__bridge void *)vs; k->fs = (__bridge void *)fs;
    memcpy(k->cfmt, cfmt, sizeof k->cfmt); k->dfmt = dfmt; k->sfmt = sfmt; k->samples = samples;
    k->blend_en = S.color.blend_en; k->chmask = S.chmask.mask; k->a2c = S.ms.a2c | (g_lay.active ? PSO_LAYERED : 0);
    if (alpha_on()) { k->alpha_func = S.color.alpha_func; memcpy(&k->alpha_ref, &S.alpha_ref, sizeof k->alpha_ref);
#ifdef DM_TEST_ALPHA_NAN_ZERO   // negative control of t_alpha only: a NaN reference taken as 0 (the comparisons it gets wrong are what t_alpha checks)
        if ((k->alpha_ref & 0x7f800000) == 0x7f800000 && (k->alpha_ref & 0x7fffff)) k->alpha_ref = 0;
#endif
    }
    for (int i = 0; i < 8; i++) if (k->cfmt[i] || lmap) k->blend[i] = S.blend[i];
    pso_key_blend_canon(k);   // zeros where blending is off (nvn_pso_skey.h)
    if (!lmap) pso_key_target_canon(k);   // and for the colour targets the pass does not have
    const nvn_refl *rf = &vs->rf;
    for (int v = 0; v < rf->nv; v++) { int a = rf->v[v].nvn; if (a < 16 && a < S.nattr) { k->attr[a] = S.attr[a]; int st = S.attr[a].stream; if (st < 16) k->stream[st] = S.stream[st]; } }
}
// ---- Parallel precompile: pipelines met for the first time used to be compiled synchronously one by one on the submit thread; 9 misses in a row in one frame = 284 ms (heavy scene on a real device).
// Now: on the first pipeline miss in a submission, scan ahead once along the remaining commands (replaying state only, no drawing) and kick all the normal-pass pipelines that will also miss later to the background to compile in parallel;
// when the submit thread reaches that draw it only waits for 'the remaining time not yet compiled'. The compiled pipelines are identical to synchronously compiled ones (same descriptor). Pipeline keys of merged passes / specialized fragments depend on the planning result at replay time, are not predicted, and are compiled synchronously as before.
// The table (after MacNativeGames PR #29, MIT): a guess no draw took is freed two submissions after it was kicked, once its compile has finished (a
// compile still running keeps its slot: its worker writes into it). Before, a slot was freed only when its pipeline was taken, so the guesses for
// the attach modes not used filled the 384 slots and kicks stopped for good. Each scan is bounded (PP_SCAN_OPS commands, PP_SCAN_KEYS keys per
// queue, PP_SCAN_NS), its likely keys are kicked as they are found, and it guesses only from programs whose shaders are loaded (prog_peek).
// A scan that stopped at a bound left the rest of the submission unscanned, and its other new pipelines were made on the submit thread one after
// another (MacNativeGames PR #166, MIT: 265 in one frame on entering an area): the next miss scans again (pp_scan_due), up to DM_NVN_PSO_RESCANS
// times a submission (32; 0 = one scan, as before), skipping keys already kicked (pp_pending) so that each scan moves on; 768 slots, 64 keys a scan
// (plan item 205; MacNativeGames M4 Max world entry: pipeline time in long frames 35.8 / 7.1 / 2.3 s → 5.5 / 2.9 / 1.05 s)
#define PSO_PEND_MAX 768
enum { PP_SCAN_OPS = 8192, PP_SCAN_KEYS = 64, PP_STALE_SUBMITS = 2 };
#define PP_SCAN_NS 2000000ull
// The compiles run on at most PP_WORKERS threads, the oldest kick first, and a draw that needs a pipeline still waiting for a thread compiles it on
// the submit thread at once instead of waiting behind the others (plan item 283; 4ppp44 cb21bec, MIT, written anew; each kick was a dispatch_async
// of its own, as many compiles at once as the system gave threads, and a take waited for one not started). A guess not started is dropped as stale
// as a finished one is
enum { PP_WORKERS = 2 };   // DM_NVN_PP_WORKERS=<n> sets it (0: a thread for each kick and a take waits, as before)
static int pp_workers(void) {
    static int v = -1; if (v < 0) { const char *e = getenv("DM_NVN_PP_WORKERS"); v = e ? atoi(e) : PP_WORKERS; if (v < 0) v = 0; if (e) MLOG("DM_NVN_PP_WORKERS=%d: %s", v, v ? "pipeline guesses compile on that many threads" : "a thread for each guess, as before"); }
#ifdef DM_TEST_PP_UNBOUNDED   // negative control of t_ppred only: a thread of its own for each kick, as before
    v = 0;
#endif
    return v;
}
static struct { pso_key k; id<MTLRenderPipelineState> ps; MTLRenderPipelineDescriptor *d; uint64_t submit, seq; int used, done, running; } g_pp[PSO_PEND_MAX];
static int g_pp_workers, g_pp_workers_max; static uint64_t g_pp_seq, g_pp_wait_ns, g_pp_waits, g_pp_self;
static pthread_mutex_t g_pp_mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_pp_cv = PTHREAD_COND_INITIALIZER;
static uint64_t g_pp_submit_mark, g_pp_kicked, g_pp_taken;   // Whether this submission has already been predicted (by submission serial); cumulative kicked / taken counts
static uint64_t g_pp_freed, g_pp_full, g_pp_scans, g_pp_stop[3];   // guesses freed untaken, kicks refused with the table full, scans and the bound that ended them (commands, keys, time)
static void pso_predict_rest(void);
static int pso_pend_find(const pso_key *k) { for (int i = 0; i < PSO_PEND_MAX; i++) if (g_pp[i].used && !memcmp(&g_pp[i].k, k, sizeof *k)) return i; return -1; }
static int pp_pending(const pso_key *k) { pthread_mutex_lock(&g_pp_mu); int r = pso_pend_find(k) >= 0; pthread_mutex_unlock(&g_pp_mu); return r; }
static int g_pp_more, g_pp_rescans;   // the last scan of this submission stopped at a bound; the scans after its first
static int pso_rescans(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_NVN_PSO_RESCANS"); v = e ? atoi(e) : 32; if (v < 0) v = 0; } return v; }
static int pp_scan_due(uint64_t sub) {   // a pipeline miss in submission sub: scan ahead now?
    if (g_pp_submit_mark != sub) { g_pp_submit_mark = sub; g_pp_rescans = 0; g_pp_more = 0; return 1; }   // its first miss
#ifndef DM_TEST_PP_ONE_SCAN   // negative control of t_ppred only: one scan a submission, as before
    if (g_pp_more && g_pp_rescans < pso_rescans()) { g_pp_rescans++; return 1; }
#endif
    return 0;
}
static void pso_pend_reclaim(uint64_t submit) {   // g_pp_mu held
#ifndef DM_TEST_PP_NO_RECLAIM   // negative control of t_ppred only: the old table, a slot freed only by a take
    for (int i = 0; i < PSO_PEND_MAX; i++) if (g_pp[i].used && (g_pp[i].done || !g_pp[i].running) && submit - g_pp[i].submit > PP_STALE_SUBMITS) { g_pp[i].ps = nil; g_pp[i].d = nil; g_pp[i].used = 0; g_pp_freed++; }
#else
    (void)submit;
#endif
}
static int pso_pend_claim(const pso_key *k, uint64_t submit) {   // g_pp_mu held: a slot for k kicked in this submission, or -1 when k is there already or the table is full (counted)
    pso_pend_reclaim(submit);
    if (pso_pend_find(k) >= 0) return -1;
    for (int i = 0; i < PSO_PEND_MAX; i++) if (!g_pp[i].used) { g_pp[i].k = *k; g_pp[i].ps = nil; g_pp[i].d = nil; g_pp[i].done = g_pp[i].running = 0; g_pp[i].used = 1; g_pp[i].submit = submit; g_pp[i].seq = ++g_pp_seq; return i; }
    g_pp_full++; return -1;
}
void mtl_pp_stats(uint64_t out[12]) {   // power line (hle_nvn.c): kicked, taken, freed, refused full, scans, ended by commands / keys / time, in the table now; the submit thread's waits (ns, count) and own compiles
    pthread_mutex_lock(&g_pp_mu); int busy = 0; for (int i = 0; i < PSO_PEND_MAX; i++) busy += g_pp[i].used;
    out[0] = g_pp_kicked; out[1] = g_pp_taken; out[2] = g_pp_freed; out[3] = g_pp_full; out[4] = g_pp_scans; out[5] = g_pp_stop[0]; out[6] = g_pp_stop[1]; out[7] = g_pp_stop[2]; out[8] = (uint64_t)busy;
    out[9] = g_pp_wait_ns; out[10] = g_pp_waits; out[11] = g_pp_self;
    pthread_mutex_unlock(&g_pp_mu);
}
static void pso_pend_worker(void);
static void pso_pend_kick(const pso_key *k, NvShader *vs, NvShader *fs) {   // Called by the submit thread: kick only if it is neither in the cache nor in the table
    if (hm_get_id(&g_pso, k)) return;
    pthread_mutex_lock(&g_pp_mu);
    int slot = pso_pend_claim(k, g_sub_serial);
    pthread_mutex_unlock(&g_pp_mu);
    if (slot < 0) return;
    MTLRenderPipelineDescriptor *d = pso_desc(k, vs, fs, nil, 0); g_pp_kicked++;
    if (!pp_workers()) {
    pthread_mutex_lock(&g_pp_mu); g_pp[slot].running = 1; g_pp_workers++; if (g_pp_workers > g_pp_workers_max) g_pp_workers_max = g_pp_workers; pthread_mutex_unlock(&g_pp_mu);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSError *e = nil; id<MTLRenderPipelineState> ps = [g_dev newRenderPipelineStateWithDescriptor:d error:&e];
        pthread_mutex_lock(&g_pp_mu); g_pp[slot].ps = ps; g_pp[slot].done = 1; g_pp_workers--; pthread_cond_broadcast(&g_pp_cv); pthread_mutex_unlock(&g_pp_mu);
    });
    return; }
    pthread_mutex_lock(&g_pp_mu);
    g_pp[slot].d = d;
    int start = g_pp_workers < pp_workers(); if (start) { g_pp_workers++; if (g_pp_workers > g_pp_workers_max) g_pp_workers_max = g_pp_workers; }
    pthread_mutex_unlock(&g_pp_mu);
    if (start) dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ pso_pend_worker(); });
}
static void pso_pend_worker(void) {   // a compile thread: the oldest guess not started, until none is left
    for (;;) { @autoreleasepool {
        pthread_mutex_lock(&g_pp_mu);
        int at = -1; for (int i = 0; i < PSO_PEND_MAX; i++) if (g_pp[i].used && !g_pp[i].running && !g_pp[i].done && g_pp[i].d && (at < 0 || g_pp[i].seq < g_pp[at].seq)) at = i;
        if (at < 0) { g_pp_workers--; pthread_mutex_unlock(&g_pp_mu); return; }
        MTLRenderPipelineDescriptor *d = g_pp[at].d; g_pp[at].d = nil; g_pp[at].running = 1;
        pthread_mutex_unlock(&g_pp_mu);
        NSError *e = nil; id<MTLRenderPipelineState> ps = [g_dev newRenderPipelineStateWithDescriptor:d error:&e];
        pthread_mutex_lock(&g_pp_mu); g_pp[at].ps = ps; g_pp[at].done = 1; pthread_cond_broadcast(&g_pp_cv); pthread_mutex_unlock(&g_pp_mu);
    } }
}
static id<MTLRenderPipelineState> pso_pend_take(const pso_key *k) {   // Called by the submit thread: if it is in the table, take it (compiled here when no thread has started it, else waited for); otherwise return nil
    pthread_mutex_lock(&g_pp_mu);
    int at = pso_pend_find(k); id<MTLRenderPipelineState> ps = nil;
    if (pp_workers() && at >= 0 && !g_pp[at].running && !g_pp[at].done && g_pp[at].d) {   // not started: compiled here, the slot freed
        MTLRenderPipelineDescriptor *d = g_pp[at].d; g_pp[at].d = nil; g_pp[at].used = 0; g_pp_self++;
        pthread_mutex_unlock(&g_pp_mu);
        NSError *e = nil; return [g_dev newRenderPipelineStateWithDescriptor:d error:&e];
    }
    if (at >= 0) { uint64_t t0 = g_pp[at].done ? 0 : bp_now();
        while (!g_pp[at].done) pthread_cond_wait(&g_pp_cv, &g_pp_mu);
        if (t0) { g_pp_wait_ns += bp_now() - t0; g_pp_waits++; }
        ps = g_pp[at].ps; g_pp[at].ps = nil; g_pp[at].used = 0; }
    pthread_mutex_unlock(&g_pp_mu);
    return ps;
}
// 1 / 2: the colour format is unsigned / signed integer (a fragment shader built with float outputs cannot write it)
static int pf_int_kind(MTLPixelFormat f) {
    switch (f) {
        case MTLPixelFormatR8Uint: case MTLPixelFormatR16Uint: case MTLPixelFormatRG8Uint: case MTLPixelFormatR32Uint: case MTLPixelFormatRG16Uint: case MTLPixelFormatRGBA8Uint:
        case MTLPixelFormatRGB10A2Uint: case MTLPixelFormatRG32Uint: case MTLPixelFormatRGBA16Uint: case MTLPixelFormatRGBA32Uint: return 1;
        case MTLPixelFormatR8Sint: case MTLPixelFormatR16Sint: case MTLPixelFormatRG8Sint: case MTLPixelFormatR32Sint: case MTLPixelFormatRG16Sint: case MTLPixelFormatRGBA8Sint:
        case MTLPixelFormatRG32Sint: case MTLPixelFormatRGBA16Sint: case MTLPixelFormatRGBA32Sint: return 2;
        default: return 0;
    }
}
// A pipeline failed: the integer colour attachments its pixel shader writes go to <DM_SHDUMP>/fout.tsv ("<key> <target> uint|sint"), from which
// dmsh_build.py builds that shader with integer outputs (the translator's fragment output types; float by default). Each target once per process
static void fout_note(NvShader *fs, const uint32_t cfmt[8]) {
    static char seen[64][20]; static int nseen;
    for (int i = 0; i < 8; i++) {
        int k = pf_int_kind((MTLPixelFormat)cfmt[i]);
        if (!k || (fs->rf.color_known && !(fs->rf.color_mask >> i & 1))) continue;
        char key[20]; snprintf(key, sizeof key, "%.16s%d", fs->key, i); int dup = 0;
        for (int j = 0; j < nseen; j++) dup |= !strcmp(seen[j], key);
        if (dup) continue;
        if (nseen < 64) strcpy(seen[nseen++], key);
        const char *dir = getenv("DM_SHDUMP"); char path[1024]; FILE *f = NULL;
        if (dir) { snprintf(path, sizeof path, "%s/fout.tsv", dir); f = fopen(path, "a"); }
        if (f) { fprintf(f, "%.16s\t%d\t%s\n", fs->key, i, k == 1 ? "uint" : "sint"); fclose(f); }
        MLOG("[red] pixel shader %.16s writes float to integer colour attachment %d (Metal format %u): %s", fs->key, i, cfmt[i],
             f ? "noted in DM_SHDUMP's fout.tsv for the next shader build" : "add it to the capture directory's fout.tsv and rebuild the shaders (or run with DM_SHDUMP)");
    }
}
// A pipeline about to be built: its vertex / pixel pair goes to <DM_SHDUMP>/pairs.tsv ("vertex key<TAB>pixel key", the format ProgramSetShaders
// writes), so the next shader build pads the vertex shader for it (dmsh_build.py vs_pad). ProgramSetShaders alone sees only the pairs of one
// program; a pair made by BindProgram's stage bits from two programs (Unity binds vertex and pixel in two calls: manosaba's capture has no pair) was
// never written, and a pair whose pixel shader reads an output the vertex shader does not write fails to link. Each pair once per process (plan
// item 137)
static uint64_t g_pair_bind_new;   // pairs written here (t_pairs reads it)
static void pair_note(NvShader *vs, NvShader *fs) {
#ifdef DM_TEST_PAIRS_NO_BIND   // negative control of t_pairs only: pairs are not noted at bind time
    return;
#endif
    static const char *dir; static int init; static nvn_hmap seen; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    if (!init) { dir = getenv("DM_SHDUMP"); init = 1; }
    if (!dir || !fs || strlen(vs->key) < 16 || strlen(fs->key) < 16) return;
    char k[32]; memcpy(k, vs->key, 16); memcpy(k + 16, fs->key, 16);
    pthread_mutex_lock(&mu);
    if (!seen.klen) nvn_hm_init(&seen, sizeof k);
    if (!nvn_hm_get(&seen, k)) {
        nvn_hm_put(&seen, k, (void *)1);
        char path[1200]; snprintf(path, sizeof path, "%s/pairs.tsv", dir); FILE *f = fopen(path, "a");
        if (f) { fprintf(f, "%.16s\t%.16s\n", vs->key, fs->key); fclose(f); g_pair_bind_new++; }
    }
    pthread_mutex_unlock(&mu);
}
