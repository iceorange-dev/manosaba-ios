// The translator's split storage words read whole (gfx/tools/msl_helpers.py wide_loads, plan item 311b; seedds/nxnative 1b9fbea, MIT): the
// rewritten kernels of t_wide_loads.metal give the original's results bit for bit, compiled as dmsh_build.py compiles the game's shaders (safe
// math, invariance kept). k_words' words become one 4-, one 3- and one 2-word read; k_alias (a store through another binding of the same memory
// between two words) and k_var (the value the offsets come from assigned between their sums) stay as they were. The originals are checked
// against the words the test put there first, so the kernels read what they are meant to.
// Usage: t_wide_loads <original MSL> <rewritten MSL>. The broken variant (msl_helpers.py --wide-across: runs reach across writes and
// assignments): k_alias and k_var read whole too, and their results change: 3 fail.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int fail, total;
static void check(const char *name, int ok) { total++; fail += !ok; printf("%-100s %s\n", name, ok ? "✓" : "✗"); }
static id<MTLDevice> dev; static id<MTLCommandQueue> q;
enum { N = 4096, IN_WORDS = N * 10 + 16 };
static id<MTLLibrary> lib_of(NSString *src) {
    MTLCompileOptions *opt = [MTLCompileOptions new]; opt.mathMode = MTLMathModeSafe; opt.preserveInvariance = YES;   // dmsh_build.py: -fmetal-math-mode=safe -fpreserve-invariance
    NSError *e = nil; id<MTLLibrary> l = [dev newLibraryWithSource:src options:opt error:&e];
    if (!l) { printf("compile: %s\n", e.localizedDescription.UTF8String); exit(2); }
    return l;
}
// Runs fn over N threads with cp_s0 = a fresh copy of the input (cp_s1 the same buffer when alias, else a scratch one), cp_s2 = the output
static id<MTLBuffer> run(id<MTLLibrary> lib, NSString *fn, const uint32_t *in, int alias, NSUInteger out_words) {
    id<MTLFunction> f = [lib newFunctionWithName:fn]; NSError *e = nil;
    id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:f error:&e];
    if (!ps) { printf("pipeline %s: %s\n", fn.UTF8String, e.localizedDescription.UTF8String); exit(2); }
    id<MTLBuffer> s0 = [dev newBufferWithBytes:in length:IN_WORDS * 4 options:MTLResourceStorageModeShared];
    id<MTLBuffer> s1 = alias ? s0 : [dev newBufferWithLength:IN_WORDS * 4 options:MTLResourceStorageModeShared];
    id<MTLBuffer> s2 = [dev newBufferWithLength:out_words * 4 options:MTLResourceStorageModeShared]; memset(s2.contents, 0, out_words * 4);
    id<MTLArgumentEncoder> ae = [f newArgumentEncoderWithBufferIndex:0];
    id<MTLBuffer> ab = [dev newBufferWithLength:ae.encodedLength options:MTLResourceStorageModeShared];
    [ae setArgumentBuffer:ab offset:0]; [ae setBuffer:s0 offset:0 atIndex:0]; [ae setBuffer:s1 offset:0 atIndex:1]; [ae setBuffer:s2 offset:0 atIndex:2];
    id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> en = [cb computeCommandEncoder];
    [en setComputePipelineState:ps]; [en setBuffer:ab offset:0 atIndex:0];
    [en useResource:s0 usage:MTLResourceUsageRead | MTLResourceUsageWrite]; if (!alias) [en useResource:s1 usage:MTLResourceUsageRead | MTLResourceUsageWrite];
    [en useResource:s2 usage:MTLResourceUsageWrite];
    [en dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
    if (cb.error) { printf("%s: %s\n", fn.UTF8String, cb.error.localizedDescription.UTF8String); exit(2); }
    return s2;
}
static int count(NSString *src, NSString *what) { return (int)[src componentsSeparatedByString:what].count - 1; }
int main(int argc, char **argv) { @autoreleasepool {
    if (argc != 3) { fprintf(stderr, "usage: t_wide_loads <original MSL> <rewritten MSL>\n"); return 2; }
    dev = MTLCreateSystemDefaultDevice(); q = [dev newCommandQueue];
    NSString *os = [NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:nil], *ws = [NSString stringWithContentsOfFile:@(argv[2]) encoding:NSUTF8StringEncoding error:nil];
    if (!os || !ws) { fprintf(stderr, "unreadable source\n"); return 2; }
    int v4 = count(ws, @"packed_uint4 _dmw"), v3 = count(ws, @"packed_uint3 _dmw"), v2 = count(ws, @"packed_uint2 _dmw");
    printf("wide reads in the rewritten source: %d of 4 words, %d of 3, %d of 2\n", v4, v3, v2);
    check("k_words read as one 4-, 3- and 2-word vector; k_alias and k_var left as they were [--wide-across]", v4 == 1 && v3 == 1 && v2 == 1);
    id<MTLLibrary> ol = lib_of(os), wl = lib_of(ws);
    static uint32_t in[IN_WORDS]; uint64_t s = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < IN_WORDS; i++) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; in[i] = (uint32_t)(s >> 32) & 0x3fffffffu; }   // no NaN among the floats
    id<MTLBuffer> ow = run(ol, @"k_words", in, 0, N * 8), ww = run(wl, @"k_words", in, 0, N * 8);
    id<MTLBuffer> oa = run(ol, @"k_alias", in, 1, N * 2), wa = run(wl, @"k_alias", in, 1, N * 2);
    id<MTLBuffer> ov = run(ol, @"k_var", in, 0, N * 2), wv = run(wl, @"k_var", in, 0, N * 2);
    const uint32_t *o = ow.contents, *a = oa.contents, *v = ov.contents; int bad = 0;
    for (int i = 0; i < N; i++) {   // the originals against the words they should read (k_words' first word: a float sum, compared GPU to GPU only)
        const uint32_t *w = in + (i * 40 + 4) / 4;
        bad += o[i * 8 + 1] != (w[2] ^ w[3]) || o[i * 8 + 2] != w[4] || o[i * 8 + 3] != w[5] || o[i * 8 + 4] != w[6] || o[i * 8 + 5] != w[8] || o[i * 8 + 6] != w[9] || o[i * 8 + 7] != w[3];
        bad += a[i * 2] != in[i * 4] || a[i * 2 + 1] != in[i * 4] + 1000u;   // the second word read after the store through cp_s1
        bad += v[i * 2] != in[i * 8] || v[i * 2 + 1] != in[i * 8 + 5];   // bytes 0 and 16 + 4 from the variable
    }
    check("the original kernels read the words the test put there", bad == 0);
    check("k_words: the rewritten kernel's results are the original's, bit for bit", !memcmp(ow.contents, ww.contents, N * 8 * 4));
    check("k_alias: the word after the store through the other binding is the stored one [--wide-across]", !memcmp(oa.contents, wa.contents, N * 2 * 4));
    check("k_var: the word after the variable's assignment is read from its new value [--wide-across]", !memcmp(ov.contents, wv.contents, N * 2 * 4));
    printf("WIDE_LOADS %s %d/%d\n", fail ? "FAIL" : "OK", total - fail, total);
    return fail != 0;
} }
