// gfx/nvn/mtl/border_warm.inc, part of nvn_mtl.m (included there in order, one translation unit): Sampler border colours in the shader (plan item 161), the pipeline warm-up, depth-stencil states
// ---------- Sampler border colours Metal lacks, in the shader (plan item 161; MacNativeGames b6231e6 / 874a32d, #49, MIT, written anew) ----------
// Such a sampler clamps to its edge (samp_by_id), and a shader stage that samples through it gets a variant whose samples return the colour when the
// coordinate is outside [0, 1]: its MSL with each `X.sample(spvDescriptorSet0.<sampler>, ` made `dm_border_sample(colour, X, spvDescriptorSet0.<sampler>, `
// (2D, 2D array and 3D textures; cubes have no border; depth gives the colour's red), compiled as dmsh_build.py compiles (safe math, invariance kept).
// The variant is another NvShader with the same reflection, so its pipelines have keys of their own while the warm-up list records the plain ones.
// The source is the shader's .metal beside its library: the Mac's workspace has it; an iOS app carries those of the game's border_msl.txt (plan
// item 214, ios/tools/border_msl.py) and compiles the variant on the device; another one gets a red line once and the edge colour.
// Tears of the Kingdom's grass interaction map samples with border (0.5, 0.5, 0.5, 0.5): drawn as transparent black, the grass lay flat and could not
// be cut. Checked by gfx/tests/t_border.m
// ax: the axes whose wrap mode is a border (bit 0 s, 1 t, 2 r; plan item 204, MacNativeGames PR #148 / be82504, MIT): a coordinate outside [0, 1]
// on another axis repeats, mirrors or clamps as its sampler does (before, the colour was returned when any coordinate was outside)
#ifndef DM_TEST_BORDER_ANY_AXIS   // negative control of t_border only: any axis outside, as before
#define DM_BORDER_OUT2 "((ax & 1u) && (uv.x < 0.0 || uv.x > 1.0)) || ((ax & 2u) && (uv.y < 0.0 || uv.y > 1.0))"
#define DM_BORDER_OUT3 DM_BORDER_OUT2 " || ((ax & 4u) && (uv.z < 0.0 || uv.z > 1.0))"
#else
#define DM_BORDER_OUT2 "any(uv < 0.0) || any(uv > 1.0)"
#define DM_BORDER_OUT3 DM_BORDER_OUT2
#endif
static NSString *const BORDER_HELPER =
    @"template<typename T, typename... A> static inline vec<T, 4> dm_border_sample(float4 b, uint ax, texture2d<T> t, sampler s, float2 uv, A... a)\n"
    @"{ return " DM_BORDER_OUT2 " ? vec<T, 4>(b) : t.sample(s, uv, a...); }\n"
    @"template<typename T, typename... A> static inline vec<T, 4> dm_border_sample(float4 b, uint ax, texture2d_array<T> t, sampler s, float2 uv, A... a)\n"
    @"{ return " DM_BORDER_OUT2 " ? vec<T, 4>(b) : t.sample(s, uv, a...); }\n"
    @"template<typename T, typename... A> static inline vec<T, 4> dm_border_sample(float4 b, uint ax, texture3d<T> t, sampler s, float3 uv, A... a)\n"
    @"{ return " DM_BORDER_OUT3 " ? vec<T, 4>(b) : t.sample(s, uv, a...); }\n"
    @"template<typename T, typename... A> static inline vec<T, 4> dm_border_sample(float4 b, uint ax, texturecube<T> t, sampler s, float3 uv, A... a) { return t.sample(s, uv, a...); }\n"
    @"template<typename T, typename... A> static inline T dm_border_sample(float4 b, uint ax, depth2d<T> t, sampler s, float2 uv, A... a)\n"
    @"{ return " DM_BORDER_OUT2 " ? T(b.x) : t.sample(s, uv, a...); }\n";
// The MSL with the samples through each named sampler member returning its colour outside the texture; *count the calls rewritten
static NSString *border_src(NSString *src, NSArray<NSString *> *names, const float (*col)[4], const unsigned *axes, int *count) {
    NSMutableString *m = [src mutableCopy]; *count = 0;
    for (NSUInteger i = 0; i < names.count; i++) {
        NSString *pat = [NSString stringWithFormat:@"([A-Za-z_][A-Za-z0-9_\\.]*)\\.sample\\(\\s*(spvDescriptorSet0\\.%@)\\s*,", [NSRegularExpression escapedPatternForString:names[i]]];
        NSRegularExpression *re = [NSRegularExpression regularExpressionWithPattern:pat options:0 error:nil];
        NSString *tpl = [NSString stringWithFormat:@"dm_border_sample(float4(%a, %a, %a, %a), %uu, $1, $2,", col[i][0], col[i][1], col[i][2], col[i][3], axes[i]];
        *count += (int)[re replaceMatchesInString:m options:0 range:NSMakeRange(0, m.length) withTemplate:tpl];
    }
    NSRange u = [m rangeOfString:@"using namespace metal;"];
    if (u.location == NSNotFound) { *count = 0; return m; }
    [m insertString:[@"\n" stringByAppendingString:BORDER_HELPER] atIndex:NSMaxRange(u)];
    return m;
}
// Texture / sampler handles h[0..n) were just bound to stage st (0 vertex, 1 fragment; g_border_any set): has the stage such a sampler bound now. Only
// the new handles are looked at, and the whole stage only while one was bound (to see it go): a full scan at every bind cost 7 % of the replay
static void border_rebind(int st, const uint64_t *h, int n) {
    int has = 0;
    pthread_mutex_lock(&g_rmu);
    for (int k = 0; k < n && !has; k++) { int sid = h_sid(h[k]); has = sid > 0 && sid < g_nsreg && g_sreg_ok[sid] == 2; }
    for (int r = 0; r < 128 && !has && S.border_bound[st]; r++) {
        int a = h_sid(S.tex[st][r]), b = h_sid(S.samp[st][r]);
        has = (a > 0 && a < g_nsreg && g_sreg_ok[a] == 2) || (b > 0 && b < g_nsreg && g_sreg_ok[b] == 2);
    }
    pthread_mutex_unlock(&g_rmu);
    if (has || S.border_bound[st]) g_pso_dirty = 1;   // the pipeline is chosen again while such a sampler is (or was) bound
    S.border_bound[st] = (uint8_t)has;
}
static uint64_t g_border_variants, g_border_fallback;
// The shader of stage st (0 vertex, 1 fragment) for the samplers bound now: sh itself, or its variant returning the border colours Metal lacks
static NvShader *border_variant(NvShader *sh, int st) {
    float col[16][4]; int aid[16], n = 0; unsigned axes[16];
    pthread_mutex_lock(&g_rmu);
    for (int i = 0; i < sh->rf.nb && n < 16; i++) {
        const nvn_rbind *b = &sh->rf.b[i]; if (b->kind != NVN_RB_SAMP && b->kind != NVN_RB_SAMPT) continue;
        int reg = b->reg, sid = reg < 128 ? h_sid(b->kind == NVN_RB_SAMPT ? S.tex[st][reg] : S.samp[st][reg]) : 0;
        if (sid > 0 && sid < g_nsreg && g_sreg_ok[sid] == 2) { const samp_par *p = &g_sreg[sid]; memcpy(col[n], p->border, sizeof col[n]);
            axes[n] = (p->ws == NVN_WRAP_MODE_CLAMP_TO_BORDER || p->ws == NVN_WRAP_MODE_MIRROR_CLAMP_TO_BORDER ? 1u : 0u) | (p->wt == NVN_WRAP_MODE_CLAMP_TO_BORDER || p->wt == NVN_WRAP_MODE_MIRROR_CLAMP_TO_BORDER ? 2u : 0u)
                    | (p->wr == NVN_WRAP_MODE_CLAMP_TO_BORDER || p->wr == NVN_WRAP_MODE_MIRROR_CLAMP_TO_BORDER ? 4u : 0u);
            aid[n++] = b->aid; }
    }
    pthread_mutex_unlock(&g_rmu);
    if (!n) return sh;
    NSMutableString *key = [NSMutableString string];
    for (int i = 0; i < n; i++) [key appendFormat:@"%d:%u:%a,%a,%a,%a;", aid[i], axes[i], col[i][0], col[i][1], col[i][2], col[i][3]];
    NvShader *v = sh->bdv[key];
    if (v) return v;
    if (!sh->bdv) sh->bdv = [NSMutableDictionary dictionary];
    v = sh;   // a failure is kept as the plain shader: not tried again
    NSString *base = [NSString stringWithFormat:@"%@/%s", g_msl_dir, sh->key];
    NSString *src = [NSString stringWithContentsOfFile:[base stringByAppendingString:@".metal"] encoding:NSUTF8StringEncoding error:nil];
    NSData *jd = [NSData dataWithContentsOfFile:[base stringByAppendingString:@".json"]];
    NSDictionary *j = jd ? [NSJSONSerialization JSONObjectWithData:jd options:0 error:nil] : nil;
    NSMutableArray<NSString *> *names = [NSMutableArray array];
    for (int i = 0; i < n; i++) for (NSDictionary *b in j[@"bindings"]) if ([b[@"arg_id"] intValue] == aid[i] && [b[@"msl_name"] isKindOfClass:NSString.class]) { [names addObject:b[@"msl_name"]]; break; }
    int cnt = 0; NSString *msl = src && names.count == (NSUInteger)n ? border_src(src, names, (const float (*)[4])col, axes, &cnt) : nil;
    id<MTLLibrary> lib = nil; NSError *e = nil;
    if (msl && cnt) { MTLCompileOptions *o = [MTLCompileOptions new]; o.mathMode = MTLMathModeSafe; o.preserveInvariance = YES; lib = [g_dev newLibraryWithSource:msl options:o error:&e]; }
    id<MTLFunction> fn = lib ? (st == 1 && sh->alib ? alpha_plain(lib, j[@"entry"]) : [lib newFunctionWithName:j[@"entry"]]) : nil;
    if (fn) {
        v = [NvShader new]; v->fn = fn; v->rf = sh->rf; v->ae = sh->ae; v->arg_len = sh->arg_len; v->stage = sh->stage;
        memcpy(v->key, sh->key, sizeof v->key); v->c1buf = sh->c1buf; v->c1_gpu = sh->c1_gpu; v->c1off = sh->c1off;
        if (sh->vi_lib) { v->vi_lib = lib; v->vi_entry = sh->vi_entry; v->vi_mask = sh->vi_mask; }
        if (sh->alib) v->alib = lib;
        g_border_variants++;
        MLOG("%s shader %s: sampler border colour (%g, %g, %g, %g)%s returned outside the texture in %d sample call%s (Metal has no such border)", st ? "fragment" : "vertex", sh->key,
             col[0][0], col[0][1], col[0][2], col[0][3], n > 1 ? " and others" : "", cnt, cnt == 1 ? "" : "s");
    } else {
        g_border_fallback++;
        RED_ONCE(100, "%s shader %s samples through a sampler whose border colour Metal lacks, and %s: drawn with the edge colour instead", st ? "fragment" : "vertex", sh->key,
                 !src ? "its MSL source is not here (iOS apps carry only the libraries)" : !cnt ? "no sample call through it was found" : e ? e.localizedDescription.UTF8String : "it did not compile");
    }
    sh->bdv[key] = v;
    return v;
}
static __unsafe_unretained id<MTLRenderPipelineState> pso_get(NvProg *pr, MTLRenderPassDescriptor *unused) {
    (void)unused;
    __unsafe_unretained NvShader *vs = pr->sh[0], *fs = pr->sh[1];
    if (!vs) { RED_ONCE(13, "program has no vertex shader (or it is not mapped)"); return nil; }
    if (g_border_any) { if (S.border_bound[0]) vs = border_variant(vs, 0); if (S.border_bound[1] && fs) fs = border_variant(fs, 1); }   // plan item 161
    pso_key k; pso_key_fill(&k, vs, fs, g_pass.cfmt, g_pass.dfmt, g_pass.sfmt, g_pass.samples, g_pso_lmap != NULL);
    if (g_pso_lmap) { memcpy(k.lmap, g_pso_lmap, 8); k.fsfn = (__bridge void *)g_pso_fsfn; }
    __unsafe_unretained id<MTLRenderPipelineState> cached = hm_get_id(&g_pso, &k);
    if (cached) return cached;
    if (hm_get_id(&g_pso_fail, &k)) return nil;
    pair_note(vs, fs);
    id<MTLRenderPipelineState> ps;
    NSError *e = nil; uint64_t bt = bp_now();
    ps = g_pso_lmap ? nil : pso_pend_take(&k);
    if (ps) g_pp_taken++;
    else {
        if (pp_scan_due(g_sub_serial)) pso_predict_rest();   // a miss: first kick the later ones out to compile in parallel, then synchronously compile the one at hand
        MTLRenderPipelineDescriptor *d = pso_desc(&k, vs, fs, g_pso_fsfn, g_pso_lmap != NULL);
        int gave_up; ps = mtl_retry(^id(NSError **er) { return [g_dev newRenderPipelineStateWithDescriptor:d error:er]; }, &e, &gave_up);
        if (!ps && gave_up) { g_bp.pso_n++; g_bp.pso_ns += bp_now() - bt; MLOG("[red] render pipeline not made, the compiler service kept failing (vertex %s, fragment %s): tried again at its next use", vs->key, fs ? fs->key : "-"); return nil; }
    }
    g_bp.pso_n++; g_bp.pso_ns += bp_now() - bt;
    if (!ps) { MLOG("[red] failed to create render pipeline (vertex %s, fragment %s): %s", vs->key, fs ? fs->key : "-", e.localizedDescription.UTF8String);
        if (fs) fout_note(fs, g_pass.cfmt);
        hm_put_id(&g_pso_fail, &k, (id)kCFBooleanTrue); return nil; }
    pso_skey sk; char shex[sizeof sk * 2 + 1]; pso_skey_from(&k, vs, fs, g_pso_lmap != NULL, g_pso_lmap ? &g_pso_fz : NULL, &sk); pso_skey_hex(&sk, shex);
    pso_record("R\t%s\n", shex);
    pso_user_record("R\t%s\n", shex);
    hm_put_id(&g_pso, &k, ps);
    return ps;
}
static NSString *warm_key(const char *s, size_t n) {
    size_t z = strnlen(s, n);
    return [[NSString alloc] initWithBytes:s length:z encoding:NSUTF8StringEncoding];
}
static NSMutableSet<NSString *> *g_warm_missing;   // libraries a list names that this translation does not have (warm queue only)
static NvShader *warm_shader(NSMutableDictionary<NSString *, NvShader *> *dict, NSString *key, int stage) {
    NvShader *sh = dict[key];
    if (sh) return sh;
    // The player's list is kept across translation chain versions: a row whose library this one does not have (Tears of the Kingdom's list has 240
    // such shaders from its first runs) is skipped and counted, not reported in red like a shader the game asks for
    if ([g_warm_missing containsObject:key]) return nil;
    if (access(msl_lib_path([key stringByAppendingString:@".metallib"]).fileSystemRepresentation, F_OK)) {
        if (!g_warm_missing) g_warm_missing = [NSMutableSet set];
        [g_warm_missing addObject:key]; return nil;
    }
    sh = shader_load(key, stage); if (sh) dict[key] = sh;
    return sh;
}
static void warm_enqueue(WarmItem *item) {
    os_unfair_lock_lock(&g_warm_lock);
    [g_warm_items addObject:item];
    atomic_store_explicit(&g_warm_ready, 1, memory_order_release);
    os_unfair_lock_unlock(&g_warm_lock);
}
static void warm_worker(NSArray<NSString *> *paths) {
    @autoreleasepool {
        NSMutableDictionary<NSString *, NvShader *> *dict = [NSMutableDictionary dictionary];
        NSMutableSet<NSString *> *seen = [NSMutableSet set], *canon = [NSMutableSet set];
        for (NSString *path in paths) {
            NSString *txt = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
            if (!txt) { MLOG("[red] failed to read warm-up list: %s", path.UTF8String); continue; }
            for (NSString *line in [txt componentsSeparatedByString:@"\n"]) {
                if (!line.length || [line hasPrefix:@"#"] || [seen containsObject:line]) continue;
                [seen addObject:line];
                NSArray<NSString *> *c = [line componentsSeparatedByString:@"\t"];
                if (!c.count) continue;
                if ([c[0] isEqualToString:@"S"] && c.count >= 3) {
                    NSString *key = c[1]; NvShader *sh = warm_shader(dict, key, c[2].intValue);
                    if (!sh) continue;
                    WarmItem *item = [WarmItem new]; item->kind = 0; item->key = key; item->shader = sh;
                    warm_enqueue(item); g_warm_shaders++;
                } else if ([c[0] isEqualToString:@"R"] && c.count >= 2) {
                    pso_skey sk; if (pso_skey_parse(c[1].UTF8String, &sk)) { g_warm_old_rows++; continue; }   // a key of an earlier layout
                    pso_key_blend_canon(&sk.k);   // a row written before item 115 carries the blend state of slots whose blending is off: as the draws' keys are now
                    if (!sk.merged) pso_key_target_canon(&sk.k);   // ... and before item 253 the masks of targets the pass did not have
                    { char ch[sizeof sk * 2 + 1]; pso_skey_hex(&sk, ch); NSString *cs = @(ch); if ([canon containsObject:cs]) { g_warm_folded++; continue; } [canon addObject:cs]; }
                    NSString *vkey = warm_key(sk.vs, sizeof sk.vs), *fkey = warm_key(sk.fs, sizeof sk.fs);
                    NvShader *vs = vkey.length ? warm_shader(dict, vkey, sk.vs_stage) : nil;
                    NvShader *fs = fkey.length ? warm_shader(dict, fkey, sk.fs_stage) : nil;
                    if (!vs || (fkey.length && !fs)) continue;
                    id<MTLFunction> fsfn = nil;
                    if (sk.fz) {
                        if (sk.fz_desc.kind == 1 && fs) { int fx[32]; for (int i = 0; i < 32; i++) fx[i] = sk.fz_desc.fx[i]; fsfn = bf_fn(fs, fx, sk.fz_desc.fbmask, sk.k.lmap); }
                                                if (!fsfn) { g_warm_skip_fz++; continue; }
                    }
                    pso_key k = sk.k; k.vs = (__bridge void *)vs; k.fs = (__bridge void *)fs; k.fsfn = (__bridge void *)fsfn;
                    NSError *e = nil; id ps = [g_dev newRenderPipelineStateWithDescriptor:pso_desc(&k, vs, fs, fsfn, sk.merged) error:&e];
                    if (!ps) { MLOG("[red] background render pipeline creation failed: %s", e.localizedDescription.UTF8String); continue; }
                    WarmItem *item = [WarmItem new]; item->kind = 1; item->vs_key = vkey; item->fs_key = fkey; item->vs = vs; item->fs = fs; item->fsfn = fsfn; item->pso = k; item->object = ps;
                    warm_enqueue(item); g_warm_rps++;
                } else if ([c[0] isEqualToString:@"C"] && c.count >= 2) {
                    NSString *key = c[1]; if (!key.length) continue;
                    NvShader *cs = warm_shader(dict, key, 5);
                    if (!cs) continue;
                    NSError *e = nil; id ps = [g_dev newComputePipelineStateWithFunction:cs->fn error:&e];
                    if (!ps) { MLOG("[red] background compute pipeline creation failed: %s", e.localizedDescription.UTF8String); continue; }
                    WarmItem *item = [WarmItem new]; item->kind = 2; item->key = key; item->shader = cs; item->object = ps;
                    warm_enqueue(item); g_warm_cps++;
                }
            }
        }
        MLOG("pipeline warm-up complete: %llu shaders / %llu render pipelines (%llu specialized fragments skipped) / %llu compute pipelines, background time %.1f ms; skipped: %llu render rows of an earlier key layout, %llu the same pipeline as another (blend state of slots without blending), rows of %lu libraries not in this translation",
             g_warm_shaders, g_warm_rps, g_warm_skip_fz, g_warm_cps, (bp_now() - g_warm_t0) / 1e6, g_warm_old_rows, g_warm_folded, (unsigned long)g_warm_missing.count);
    }
    atomic_store(&g_warm_running, 0);
}
// Wait for the background warm-up to finish (at most timeout_ms): returns how many milliseconds were actually waited, 0 if it is not running. On first install the system shader cache is fully cold; if warm-up is slower than the player entering a run,
// pipelines not yet warmed are compiled on the spot on the submit thread = in-run stutter; the loading screen on entering a run blocks the player anyway, so let it load a little longer (called where hle_nvn.c presents).
int mtl_warm_wait(int timeout_ms) { @autoreleasepool {
    if (!atomic_load(&g_warm_running)) return 0;
    uint64_t t0 = bp_now();
    while (atomic_load(&g_warm_running) && (bp_now() - t0) / 1000000ull < (uint64_t)timeout_ms) usleep(2000);
    return (int)((bp_now() - t0) / 1000000ull) + 1;
} }
static int warm_path_exists(const char *path) { return access(path, F_OK) == 0; }
static void warm_start(void) {
    if (g_warm_started++) return;
    const char *e = getenv("DM_PSO_MANIFEST");
    if (e && !strcmp(e, "off")) return;
    NSArray<NSString *> *paths;
    if (e && *e) {
        paths = @[@(e)];
    } else {   // the seed manifest, then the player's own list; either one alone is warmed (nvn_warm_list.h)
        NSString *seed = [g_shmap_path.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"pso_manifest.tsv"], *user = pso_user_path();
        NSString *cand[2] = {seed, user}; const char *fs[2] = {seed.fileSystemRepresentation, user.fileSystemRepresentation}; int got[2];
        int n = warm_lists(fs, 2, warm_path_exists, got);
        if (!n) return;
        NSMutableArray<NSString *> *all = [NSMutableArray array]; NSMutableString *desc = [NSMutableString string];
        for (int i = 0; i < n; i++) { [all addObject:cand[got[i]]]; [desc appendFormat:@"%@%@", i ? @", " : @"", cand[got[i]]]; }
        MLOG("pipeline warm-up lists: %s", desc.UTF8String);
        paths = all;
    }
    if (!g_warm_items) g_warm_items = [NSMutableArray array];
    g_warm_t0 = bp_now();
    dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0);   // Not optional background work: it decides whether the pipelines are ready before the player enters a run
    dispatch_queue_t q = dispatch_queue_create("dm.nvn.pso-warm", attr);
    atomic_store(&g_warm_running, 1);
    dispatch_async(q, ^{ warm_worker(paths); });
}
static id<MTLDepthStencilState> dss_get(int has_depth, int has_stencil) {
    struct { ds_st ds; uint32_t vm[2], wm[2]; int hd, hs; } k;
    memset(&k, 0, sizeof k); k.ds = S.ds; memcpy(k.vm, S.st_vmask, 8); memcpy(k.wm, S.st_wmask, 8); k.hd = has_depth; k.hs = has_stencil;
    if (!g_dss.klen) g_dss.klen = sizeof k;
    id<MTLDepthStencilState> s = hm_get_id(&g_dss, &k);
    if (s) return s;
    MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
    if (has_depth && S.ds.dt) { d.depthCompareFunction = (MTLCompareFunction)(S.ds.df - 1); d.depthWriteEnabled = S.ds.dw; }
    if (has_stencil && S.ds.st) {
        MTLStencilDescriptor *f = [MTLStencilDescriptor new], *b = [MTLStencilDescriptor new];
        f.stencilCompareFunction = (MTLCompareFunction)(S.ds.sf_f - 1); f.stencilFailureOperation = (MTLStencilOperation)(S.ds.fail_f - 1);
        f.depthFailureOperation = (MTLStencilOperation)(S.ds.zfail_f - 1); f.depthStencilPassOperation = (MTLStencilOperation)(S.ds.zpass_f - 1);
        f.readMask = S.st_vmask[0]; f.writeMask = S.st_wmask[0];
        b.stencilCompareFunction = (MTLCompareFunction)(S.ds.sf_b - 1); b.stencilFailureOperation = (MTLStencilOperation)(S.ds.fail_b - 1);
        b.depthFailureOperation = (MTLStencilOperation)(S.ds.zfail_b - 1); b.depthStencilPassOperation = (MTLStencilOperation)(S.ds.zpass_b - 1);
        b.readMask = S.st_vmask[1]; b.writeMask = S.st_wmask[1];
        d.frontFaceStencil = f; d.backFaceStencil = b;
    }
    s = [g_dev newDepthStencilStateWithDescriptor:d];
    hm_put_id(&g_dss, &k, s);
    return s;
}
