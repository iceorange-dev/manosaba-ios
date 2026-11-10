// gfx/nvn/mtl/lod_bias.m: sampler LOD bias in the shader (plan item 318), a translation unit of the Metal backend of its own (plan item 264; what
// the others use of it and it of them: mtl/reg.h)
#include "mtl.h"
// ---------- Sampler LOD bias in the shader, on a GPU that ignores the sampler's (plan item 318; S2Arm's finding and rule, Apache-2.0, written anew) ----------
// MTLSamplerDescriptor.lodBias came with macOS / iOS 26 and S2Arm read it ignored on an M3 (samp_probe.h); Tears of the Kingdom biases 861 samplers.
// Where the probe found it ignored (or the OS has none; DM_SAMP_LB_EMU=1 forces it, 0 turns it off: lb_emu in descriptors.inc) such a sampler is
// made with no bias and a shader stage that samples through it gets a variant whose sample calls carry it, as Vulkan's rule has the sampler's bias
// added to every level of detail, an explicit one too (λ' = λbase + sampler.bias + shaderOp.bias, λbase = shaderOp.Lod when given; Metal applies a
// sampler's lodBias to level() samples the same way, samp_probe's level(0) read): level(l) → level((l) + b), bias(x) → bias((x) + b), a gradient's
// two derivatives × 2^b, none → bias(b) in a fragment shader and level(b) elsewhere. .sample and .sample_compare calls are rewritten (the compare
// value comes before the option; the Metal compiler's own headers give sample_compare the bias, level and gradient overloads from language
// version 2.3 on macOS and every version on iOS: metal/__bits/metal_depth2d, metal_config); gather has no level of detail. The variant is built
// from the shader's MSL as border_variant's (the Mac's workspace has every .metal; an iOS app only those it carries, `games/<id>/lodbias_msl.txt`,
// another gets a red line and draws without the bias). Checked by gfx/tests/t_lodbias.m
#include <ctype.h>
typedef struct { char *p; size_t n, cap; } lb_buf;
static void lb_put(lb_buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void lb_puts(lb_buf *b, const char *s) { lb_put(b, s, strlen(s)); }
static int lb_ident(char c) { return isalnum((unsigned char)c) || c == '_'; }
// The top-level arguments of the call whose '(' is just before s[i]: spans [a[k], e[k]) trimmed; *close at its ')'. -1 if unbalanced or more than max
static int lb_args(const char *s, size_t len, size_t i, size_t *a, size_t *e, int max, size_t *close) {
    int depth = 1, n = 0; size_t st = i;
    for (size_t k = i; k < len; k++) {
        char c = s[k];
        if (c == '(' || c == '[' || c == '{') depth++;
        else if ((c == ')' || c == ']' || c == '}') && --depth == 0) { if (n >= max) return -1; a[n] = st; e[n] = k; n++; *close = k; break; }
        else if (c == ',' && depth == 1) { if (n >= max) return -1; a[n] = st; e[n] = k; n++; st = k + 1; }
        if (k + 1 == len) return -1;
    }
    for (int k = 0; k < n; k++) { while (a[k] < e[k] && isspace((unsigned char)s[a[k]])) a[k]++; while (e[k] > a[k] && isspace((unsigned char)s[e[k] - 1])) e[k]--; }
    return n;
}
static int lb_starts(const char *s, size_t a, size_t e, const char *w) { size_t l = strlen(w); return e - a > l && !strncmp(s + a, w, l) && s[a + l] == '('; }
// Whether the texture member nm of the argument block is an array (its declaration in spvDescriptorSetBuffer0); -1 if not found
static int lb_is_array(const char *s, const char *nm) {
    size_t l = strlen(nm);
    for (const char *p = strstr(s, nm); p; p = strstr(p + 1, nm)) {
        if (p == s || lb_ident(p[-1]) || lb_ident(p[l])) continue;
        const char *q = p + l; while (*q == ' ') q++;
        if (strncmp(q, "[[id(", 5)) continue;   // the declaration: <type><T> nm [[id(N)]]
        const char *t = p; while (t > s && t[-1] == ' ') t--;
        if (t == s || t[-1] != '>') continue;
        int d = 0; do { t--; if (*t == '>') d++; else if (*t == '<') d--; } while (t > s && d);
        const char *te = t; while (t > s && lb_ident(t[-1])) t--;
        size_t tl = (size_t)(te - t);
        return tl > 6 && !strncmp(te - 6, "_array", 6);
    }
    return -1;
}
// The MSL with the .sample and .sample_compare calls through each named sampler member carrying its bias b[i] (frag: a fragment shader); *count
// the calls rewritten, *left those through such a sampler left as they were (an unknown shape)
NSString *lb_src(NSString *src, NSArray<NSString *> *names, const float *bias, int frag, int *count, int *left) {
    const char *s = src.UTF8String; size_t len = strlen(s); lb_buf o = {0}; size_t at = 0; *count = *left = 0;
    const char *pre = "spvDescriptorSet0.";
    for (size_t i = 0; i + 8 < len; i++) {
        int cmp = !strncmp(s + i, ".sample_compare(", 16), smp = !strncmp(s + i, ".sample(", 8);
        if (!cmp && !smp) continue;
        size_t ai = i + (cmp ? 16 : 8), a[8], e[8], close; int n = lb_args(s, len, ai, a, e, 8, &close);
        int which = -1;
        if (n >= 2 && e[0] - a[0] > strlen(pre) && !strncmp(s + a[0], pre, strlen(pre)))
            for (NSUInteger k = 0; k < names.count; k++) { const char *nm = names[k].UTF8String; size_t l = strlen(nm);
                if (e[0] - a[0] == strlen(pre) + l && !strncmp(s + a[0] + strlen(pre), nm, l)) { which = (int)k; break; } }
        if (which < 0) continue;
#ifdef DM_TEST_LB_COMPARE_LEFT   // negative control of t_lodbias only: sample_compare left as it was, as before
        if (cmp) { (*left)++; continue; }
#endif
        float b = bias[which]; char B[48], E[48]; snprintf(B, sizeof B, "float(%a)", (double)b); snprintf(E, sizeof E, "float(%a)", exp2((double)b));
        int lod = -1, mlc = -1;
        for (int k = 2; k < n; k++) {
            if (lb_starts(s, a[k], e[k], "level") || lb_starts(s, a[k], e[k], "bias") || lb_starts(s, a[k], e[k], "gradient2d") || lb_starts(s, a[k], e[k], "gradient3d") || lb_starts(s, a[k], e[k], "gradientcube")) lod = k;
            else if (lb_starts(s, a[k], e[k], "min_lod_clamp")) mlc = k;
        }
        int ins = -1;
        if (lod < 0) {   // no level of detail given: where it goes (after the coordinate, an array's layer and a compare value, before min_lod_clamp and the offset)
            if (mlc >= 0) ins = frag ? mlc : -1;
            else {
                size_t t = i; while (t > 0 && lb_ident(s[t - 1])) t--;
                int arr = -1;
                if (t >= strlen(pre) && !strncmp(s + t - strlen(pre), pre, strlen(pre))) { char nm[128]; size_t l = MIN(i - t, sizeof nm - 1); memcpy(nm, s + t, l); nm[l] = 0; arr = lb_is_array(s, nm); }
                ins = arr < 0 ? -1 : 2 + arr + cmp;
                if (ins > n) ins = -1;
            }
            if (ins < 0) { (*left)++; continue; }
        }
        lb_put(&o, s + at, ai - at);   // up to the call's '('
        for (int k = 0; k < n; k++) {
            if (k) lb_puts(&o, ", ");
            if (k == ins) { lb_puts(&o, frag ? "bias(" : "level("); lb_puts(&o, B); lb_puts(&o, "), "); }
            if (k == lod) {
                size_t p = a[k]; while (s[p] != '(') p++;
                char op[16]; size_t ol = MIN(p - a[k], sizeof op - 1); memcpy(op, s + a[k], ol); op[ol] = 0;
                size_t ia = p + 1, ie = e[k] - 1;   // inside the option's parentheses
                lb_puts(&o, op); lb_puts(&o, "(");
                if (!strcmp(op, "level") || !strcmp(op, "bias")) { lb_puts(&o, "("); lb_put(&o, s + ia, ie - ia); lb_puts(&o, ") + "); lb_puts(&o, B); }
                else {   // gradientN(dPdx, dPdy): both × 2^b
                    size_t ga[2], ge[2], gc; int gn = lb_args(s, len, ia, ga, ge, 2, &gc);
                    if (gn != 2) { lb_put(&o, s + ia, ie - ia); }
                    else { lb_puts(&o, "("); lb_put(&o, s + ga[0], ge[0] - ga[0]); lb_puts(&o, ") * "); lb_puts(&o, E); lb_puts(&o, ", ("); lb_put(&o, s + ga[1], ge[1] - ga[1]); lb_puts(&o, ") * "); lb_puts(&o, E); }
                }
                lb_puts(&o, ")");
            } else lb_put(&o, s + a[k], e[k] - a[k]);
        }
        if (ins == n) { lb_puts(&o, ", "); lb_puts(&o, frag ? "bias(" : "level("); lb_puts(&o, B); lb_puts(&o, ")"); }
        at = close; i = close; (*count)++;
    }
    lb_put(&o, s + at, len - at);
    NSString *r = o.p ? [NSString stringWithUTF8String:o.p] : src; free(o.p);
    return r;
}
// Texture / sampler handles h[0..n) were just bound to stage st (0 vertex, 1 fragment, 5 compute; g_lb_any set): has the stage a sampler bound
// whose bias the shader carries (border_rebind's way: the new handles, and the whole stage only while one was bound)
void lb_rebind(int st, const uint64_t *h, int n) {
    int has = 0;
    pthread_mutex_lock(&g_rmu);
    for (int k = 0; k < n && !has; k++) { int sid = h_sid(h[k]); has = sid > 0 && sid < g_nsreg && g_sreg_lb[sid]; }
    for (int r = 0; r < 128 && !has && S.lb_bound[st]; r++) {
        int a = h_sid(S.tex[st][r]), b = h_sid(S.samp[st][r]);
        has = (a > 0 && a < g_nsreg && g_sreg_lb[a]) || (b > 0 && b < g_nsreg && g_sreg_lb[b]);
    }
    pthread_mutex_unlock(&g_rmu);
    if (st < 2 && (has || S.lb_bound[st])) g_pso_dirty = 1;   // the pipeline is chosen again while such a sampler is (or was) bound
    S.lb_bound[st] = (uint8_t)has;
}
static uint64_t g_lb_variants, g_lb_fallback;
// The shader of stage st (0 vertex, 1 fragment, 5 compute) for the samplers bound now: sh itself, or its variant carrying their LOD biases
NvShader *lb_variant(NvShader *sh, int st) {
    float bias[16]; int aid[16], n = 0;
    pthread_mutex_lock(&g_rmu);
    for (int i = 0; i < sh->rf.nb && n < 16; i++) {
        const nvn_rbind *b = &sh->rf.b[i]; if (b->kind != NVN_RB_SAMP && b->kind != NVN_RB_SAMPT) continue;
        int reg = b->reg, sid = reg < 128 ? h_sid(b->kind == NVN_RB_SAMPT ? S.tex[st][reg] : S.samp[st][reg]) : 0;
        if (sid > 0 && sid < g_nsreg && g_sreg_lb[sid]) { float v = g_sreg[sid].lod_bias; bias[n] = v < -16.f ? -16.f : v > 15.999f ? 15.999f : v; aid[n++] = b->aid; }
    }
    pthread_mutex_unlock(&g_rmu);
    if (!n) return sh;
    NSMutableString *key = [NSMutableString string];
    for (int i = 0; i < n; i++) [key appendFormat:@"%d:%a;", aid[i], (double)bias[i]];
    NvShader *v = sh->lbv[key];
    if (v) return v;
    if (!sh->lbv) sh->lbv = [NSMutableDictionary dictionary];
    v = sh;   // a failure is kept as the plain shader: not tried again
    NSString *base = [NSString stringWithFormat:@"%@/%s", g_msl_dir, sh->key];
    NSString *src = sh->msl ?: [NSString stringWithContentsOfFile:[base stringByAppendingString:@".metal"] encoding:NSUTF8StringEncoding error:nil];
    NSData *jd = [NSData dataWithContentsOfFile:[base stringByAppendingString:@".json"]];
    NSDictionary *j = jd ? [NSJSONSerialization JSONObjectWithData:jd options:0 error:nil] : nil;
    NSMutableArray<NSString *> *names = [NSMutableArray array];
    for (int i = 0; i < n; i++) for (NSDictionary *b in j[@"bindings"]) if ([b[@"arg_id"] intValue] == aid[i] && [b[@"msl_name"] isKindOfClass:NSString.class]) { [names addObject:b[@"msl_name"]]; break; }
    int cnt = 0, left = 0; NSString *msl = src && names.count == (NSUInteger)n ? lb_src(src, names, bias, st == 1, &cnt, &left) : nil;
    id<MTLLibrary> lib = nil; NSError *e = nil;
    if (msl && cnt) { MTLCompileOptions *o = [MTLCompileOptions new]; o.mathMode = MTLMathModeSafe; o.preserveInvariance = YES; lib = [g_dev newLibraryWithSource:msl options:o error:&e]; }
    id<MTLFunction> fn = lib ? (st == 1 && sh->alib ? alpha_plain(lib, j[@"entry"]) : [lib newFunctionWithName:j[@"entry"]]) : nil;
#ifdef DM_TEST_LB_VARIANT_OFF   // negative control of t_lodbias only: no variant, the sampler's bias lost as before the emulation
    fn = nil; e = nil;
#endif
    if (fn) {
        v = [NvShader new]; v->fn = fn; v->rf = sh->rf; v->ae = sh->ae; v->arg_len = sh->arg_len; v->stage = sh->stage;
        memcpy(v->key, sh->key, sizeof v->key); v->c1buf = sh->c1buf; v->c1_gpu = sh->c1_gpu; v->c1off = sh->c1off; v->msl = msl;
        if (sh->vi_lib) { v->vi_lib = lib; v->vi_entry = sh->vi_entry; v->vi_mask = sh->vi_mask; }
        if (sh->alib) v->alib = lib;
        g_lb_variants++;
        MLOG("%s shader %s: sampler LOD bias %g%s carried by %d sample call%s%s (%s)", st == 1 ? "fragment" : st == 5 ? "compute" : "vertex", sh->key,
             bias[0], n > 1 ? " and others" : "", cnt, cnt == 1 ? "" : "s", left ? ", some left as they were (an unknown shape)" : "", g_lb_force == 1 ? "DM_SAMP_LB_EMU=1" : "this GPU ignores the sampler's");
    } else {
        g_lb_fallback++;
        RED_ONCE(124, "%s shader %s samples through a sampler with an LOD bias this GPU ignores, and %s: drawn without the bias", st == 1 ? "fragment" : st == 5 ? "compute" : "vertex", sh->key,
                 !src ? "its MSL source is not here (iOS apps carry only the libraries)" : !msl ? "a sampler's name is not in its reflection" : !cnt ? "no sample call through it could carry it" : e ? e.localizedDescription.UTF8String : "it did not compile");
    }
    sh->lbv[key] = v;
    return v;
}
void mtl_lb_stats(uint64_t *variants, uint64_t *fallback) { *variants = g_lb_variants; *fallback = g_lb_fallback; }
#pragma clang attribute pop
