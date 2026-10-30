// gfx/nvn/mtl/upload.inc, part of nvn_mtl.m (included there in order, one translation unit): Texture uploads: batches, staging blocks, BC on GPUs without it, texture descriptors
// Batching: uploads within one replay submission share one upload command buffer (compute encoder deswizzle + a unified blit), flushed before the replay command buffer is committed;
// the same queue executes in commit order ⇒ uploads are always finished before the first sample. Uploads outside a submission are flushed on the spot. The staging area is cut from 32 MB private blocks, held by the command buffer until completion.
static _Atomic uint64_t g_upload_ns;   // Cumulative time of deswizzle + upload encoding on the submit thread (timing instrumentation)
static _Atomic uint64_t g_upload_gpu_ns;
static CBUF g_up_cb;
static CENC g_up_ce;
static _Atomic int g_up_alloc_busy[3];
static unsigned g_up_seq;
#define UP_BLOCK_SIZE (32u * 1024u * 1024u)
typedef struct {
    __unsafe_unretained id<MTLBuffer> src;
    size_t off, row, image;
    MTLSize size;
    __unsafe_unretained id<MTLTexture> dst;   // Strongly referenced by g_up_texs until the flush is encoded
    NSUInteger slice, level;
} up_blit;
static up_blit *g_up_blits; static int g_nup_blits, g_up_blitcap;
// Without BC, BC1–5 are decompressed on the GPU (A16 logs: CPU deswizzle + CPU decompression took over 300 ms in a single frame): the GPU deswizzle writes the compressed blocks into the staging area, and at flush time, after the barrier,
// the compute kernel bcd decodes them into the texture one thread per block (the target is a 2D view in the decompressed format; an sRGB base texture is written with raw values through an RGBA8Unorm view). BC6H / BC7 / 3D textures are very few and still go through the CPU (nvn_bcdec.c)
typedef struct { __unsafe_unretained id<MTLBuffer> src, params; size_t off, poff; uint32_t bw, bh; __unsafe_unretained id<MTLTexture> view; } up_dec;   // view: held by g_up_texs within the batch, and afterwards by defer_obj_unref until the GPU finishes
static up_dec *g_up_decs; static int g_nup_decs, g_up_deccap;
static const char k_bcd_msl[] =
"#include <metal_stdlib>\nusing namespace metal;\n"
"struct P { uint off, pitch, w, h, fmt, z; };\n"   // fmt: 1 BC1, 2 BC2, 3 BC3, 4 BC4, 5 BC4 signed, 6 BC5, 7 BC5 signed; z: bcd3's depth plane
"static float3 c565(uint c) { return float3((c >> 11) & 31, (c >> 5) & 63, c & 31) / float3(31, 63, 31); }\n"
"static void col(device const uchar *b, thread float4 *o, bool four) {\n"
// the bytes widened to uint before they are shifted (aerolithink 4e98460: uchar promotes to int, and b[7] << 24 reaches the sign bit; checked by
// gfx/tests/t_bcd.m against the CPU decoder; -DDM_TEST_BCD_HIGH_BYTE_LOST drops the index word's top byte there)
"  uint c0 = uint(b[0]) | uint(b[1]) << 8, c1 = uint(b[2]) | uint(b[3]) << 8;\n"
#ifndef DM_TEST_BCD_HIGH_BYTE_LOST
"  uint ix = uint(b[4]) | uint(b[5]) << 8 | uint(b[6]) << 16 | uint(b[7]) << 24;\n"
#else
"  uint ix = uint(b[4]) | uint(b[5]) << 8 | uint(b[6]) << 16;\n"
#endif
"  float3 e0 = c565(c0), e1 = c565(c1); float4 p[4];\n"
"  p[0] = float4(e0, 1); p[1] = float4(e1, 1);\n"
"  if (four || c0 > c1) { p[2] = float4((2 * e0 + e1) / 3, 1); p[3] = float4((e0 + 2 * e1) / 3, 1); } else { p[2] = float4((e0 + e1) / 2, 1); p[3] = float4(0); }\n"
"  for (int i = 0; i < 16; i++) o[i] = p[(ix >> (2 * i)) & 3]; }\n"
"static void a8(device const uchar *b, thread float *o, bool sn) {\n"   // Single-channel block of BC4 / BC3 alpha / BC5
"  float a0 = sn ? max(float(as_type<char>(b[0])), -127.0) / 127 : b[0] / 255.0, a1 = sn ? max(float(as_type<char>(b[1])), -127.0) / 127 : b[1] / 255.0;\n"
"  bool eight = sn ? as_type<char>(b[0]) > as_type<char>(b[1]) : b[0] > b[1]; float lo = sn ? -1 : 0; float p[8]; p[0] = a0; p[1] = a1;\n"
"  for (int k = 2; k < 8; k++) p[k] = eight ? ((8 - k) * a0 + (k - 1) * a1) / 7 : (k < 6 ? ((6 - k) * a0 + (k - 1) * a1) / 5 : (k == 6 ? lo : 1));\n"
"  uint lo24 = uint(b[2]) | uint(b[3]) << 8 | uint(b[4]) << 16, hi24 = uint(b[5]) | uint(b[6]) << 8 | uint(b[7]) << 16;\n"
"  for (int i = 0; i < 16; i++) o[i] = p[i < 8 ? (lo24 >> (3 * i)) & 7 : (hi24 >> (3 * (i - 8))) & 7]; }\n"
"static void blk(device const uchar *src, device const P &p, uint2 g, thread float4 *o) {\n"
"  uint bb = (p.fmt == 1 || p.fmt == 4 || p.fmt == 5) ? 8 : 16; device const uchar *b = src + p.off + g.y * p.pitch + g.x * bb; float a[16], c[16];\n"
"  if (p.fmt == 1) col(b, o, false);\n"
"  else if (p.fmt == 2) { col(b + 8, o, true); for (int i = 0; i < 16; i++) o[i].a = ((b[i / 2] >> (4 * (i & 1))) & 15) / 15.0; }\n"
"  else if (p.fmt == 3) { col(b + 8, o, true); a8(b, a, false); for (int i = 0; i < 16; i++) o[i].a = a[i]; }\n"
"  else if (p.fmt == 4 || p.fmt == 5) { a8(b, a, p.fmt == 5); for (int i = 0; i < 16; i++) o[i] = float4(a[i], 0, 0, 1); }\n"
"  else { a8(b, a, p.fmt == 7); a8(b + 8, c, p.fmt == 7); for (int i = 0; i < 16; i++) o[i] = float4(a[i], c[i], 0, 1); } }\n"
"kernel void bcd(device const uchar *src [[buffer(0)]], device const P &p [[buffer(1)]], texture2d<float, access::write> dst [[texture(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= (p.w + 3) / 4 || g.y >= (p.h + 3) / 4) return; float4 o[16]; blk(src, p, g, o);\n"
"  for (int i = 0; i < 16; i++) { uint2 q = g * 4 + uint2(i & 3, i >> 2); if (q.x < p.w && q.y < p.h) dst.write(o[i], q); } }\n"
"kernel void bcd3(device const uchar *src [[buffer(0)]], device const P &p [[buffer(1)]], texture3d<float, access::write> dst [[texture(0)]], uint2 g [[thread_position_in_grid]]) {\n"   // one depth plane of a 3D texture (62c3)
"  if (g.x >= (p.w + 3) / 4 || g.y >= (p.h + 3) / 4) return; float4 o[16]; blk(src, p, g, o);\n"
"  for (int i = 0; i < 16; i++) { uint2 q = g * 4 + uint2(i & 3, i >> 2); if (q.x < p.w && q.y < p.h) dst.write(o[i], uint3(q, p.z)); } }\n";
static int bcd_fmt(uint32_t f) {   // NVN format → bcd fmt; 0 = not decoded on the GPU (BC6H / BC7 / non-BC)
    switch (f) { case 0x41: case 0x42: case 0x45: case 0x46: return 1; case 0x43: case 0x47: return 2; case 0x44: case 0x48: return 3;
                 case 0x49: return 4; case 0x4a: return 5; case 0x4b: return 6; case 0x4c: return 7; default: return 0; }
}
static id<MTLComputePipelineState> bcd3_pso(void) {   // bcd for one depth plane of a 3D texture (blk_decode)
    static id<MTLComputePipelineState> ps; static int failed;
    if (!ps && !failed) { NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_bcd_msl) options:nil error:&e]; id<MTLFunction> fn = lib ? [lib newFunctionWithName:@"bcd3"] : nil;
        ps = fn ? [g_dev newComputePipelineStateWithFunction:fn error:&e] : nil;
        if (!ps) { failed = 1; MLOG("[red] no BC: cannot create the 3D decompression pipeline %s", e.localizedDescription.UTF8String); } }
    return ps;
}
static id<MTLComputePipelineState> bcd_pso(void) {
    static id<MTLComputePipelineState> ps; static int failed;
    if (!ps && !failed) { NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_bcd_msl) options:nil error:&e]; id<MTLFunction> fn = lib ? [lib newFunctionWithName:@"bcd"] : nil;
        ps = fn ? [g_dev newComputePipelineStateWithFunction:fn error:&e] : nil;
        if (!ps) { failed = 1; MLOG("[red] no BC: cannot create the GPU decompression pipeline %s (falling back to CPU decompression)", e.localizedDescription.UTF8String); } else MLOG("no BC: BC1–5 now decompressed on the GPU"); }
    return ps;
}
// bc1_astc's kernel: the arithmetic of gfx/tex/nvn_bc1astc.c statement for statement (gfx/tests/t_bc1astc.m holds the two to the same bytes)
static const char k_b1a_msl[] =
"#include <metal_stdlib>\nusing namespace metal;\n"
"struct P { uint src, pitch, bw, bh, dst, rgba; };\n"
"constant uchar k_trit[243] = {0,1,2,4,5,6,8,9,10,16,17,18,20,21,22,24,25,26,3,7,11,19,23,27,12,13,14,32,33,34,36,37,38,40,41,42,48,49,50,52,53,54,56,57,58,35,39,43,51,55,59,44,45,46,64,65,66,68,69,70,72,73,74,80,81,82,84,85,86,88,89,90,67,71,75,83,87,91,76,77,78,128,129,130,132,133,134,136,137,138,144,145,146,148,149,150,152,153,154,131,135,139,147,151,155,140,141,142,160,161,162,164,165,166,168,169,170,176,177,178,180,181,182,184,185,186,163,167,171,179,183,187,172,173,174,192,193,194,196,197,198,200,201,202,208,209,210,212,213,214,216,217,218,195,199,203,211,215,219,204,205,206,96,97,98,100,101,102,104,105,106,112,113,114,116,117,118,120,121,122,99,103,107,115,119,123,108,109,110,224,225,226,228,229,230,232,233,234,240,241,242,244,245,246,248,249,250,227,231,235,243,247,251,236,237,238,28,29,30,60,61,62,92,93,94,156,157,158,188,189,190,220,221,222,31,63,95,159,191,223,124,125,126};\n"
"static void bits(thread uchar *o, uint at, uint n, uint v) { for (uint i = 0; i < n; i++) if (v >> i & 1) o[(at + i) >> 3] |= uchar(1u << ((at + i) & 7)); }\n"
"static void wbits(thread uchar *o, uint at, uint n, uint v) { for (uint i = 0; i < n; i++) if (v >> i & 1) { uint b = 127 - (at + i); o[b >> 3] |= uchar(1u << (b & 7)); } }\n"
"static void block(thread uchar *o, int cem, int dual, int lv, thread int *e0, thread int *e1, thread int *w) {\n"
"  int nw = dual ? 32 : 16, nc = cem == 12 ? 4 : 3;\n"
"  if (e1[0] + e1[1] + e1[2] < e0[0] + e0[1] + e0[2]) {\n"
"    for (int c = 0; c < 4; c++) { int t = e0[c]; e0[c] = e1[c]; e1[c] = t; }\n"
"    for (int i = 0; i < nw; i++) w[i] = lv - 1 - w[i]; }\n"
"  for (int i = 0; i < 16; i++) o[i] = 0;\n"
"  bits(o, 0, 11, dual ? 0x441u : lv == 4 ? 0x042u : 0x051u);\n"
"  bits(o, 13, 4, uint(cem));\n"
"  for (int c = 0; c < nc; c++) { bits(o, 17 + 16 * uint(c), 8, uint(e0[c])); bits(o, 25 + 16 * uint(c), 8, uint(e1[c])); }\n"
"  if (lv == 3) for (int g = 0; g * 5 < nw; g++) {\n"
"    int k = 0, m = 1, left = nw - g * 5;\n"
"    for (int j = 0; j < 5; j++, m *= 3) k += (j < left ? w[g * 5 + j] : 0) * m;\n"
"    wbits(o, 8 * uint(g), left >= 5 ? 8 : uint(8 * left + 4) / 5, k_trit[k]); }\n"
"  else for (int i = 0; i < nw; i++) wbits(o, uint(i * (lv == 4 ? 2 : 1)), lv == 4 ? 2 : 1, uint(w[i]));\n"
"  if (dual) bits(o, 128 - uint(nw) - 2, 2, 3); }\n"
"static int x5(int v) { return v << 3 | v >> 2; }\nstatic int x6(int v) { return v << 2 | v >> 4; }\n"
"static void bc1_astc(device const uchar *b, thread uchar *o, int rgba) {\n"
"  int c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8, idx[16], used = 0, w[32];\n"
"  uint ix = uint(b[4]) | uint(b[5]) << 8 | uint(b[6]) << 16 | uint(b[7]) << 24;\n"
"  int p[4][3] = {{x5(c0 >> 11), x6(c0 >> 5 & 63), x5(c0 & 31)}, {x5(c1 >> 11), x6(c1 >> 5 & 63), x5(c1 & 31)}, {0, 0, 0}, {0, 0, 0}};\n"
"  for (int k = 0; k < 16; k++) { idx[k] = int(ix >> (2 * k) & 3); used |= 1 << idx[k]; }\n"
"  const int four[4] = {0, 3, 1, 2}; const int three[3] = {0, 2, 1};\n"
"  int e0[4] = {p[0][0], p[0][1], p[0][2], 255}, e1[4] = {p[1][0], p[1][1], p[1][2], 255};\n"
"  if (c0 > c1 || !(used & 8)) {\n"
"    for (int k = 0; k < 16; k++) w[k] = c0 > c1 ? four[idx[k]] : three[idx[k]];\n"
"    block(o, 8, 0, c0 > c1 ? 4 : 3, e0, e1, w); return; }\n"
"  for (int c = 0; c < 3; c++) p[2][c] = (p[0][c] + p[1][c] + 1) / 2;\n"
"  int colours = used & (rgba ? 7 : 15), best = -1, ea = 0, eb = 0;\n"
"  for (int a = 0; a < 4; a++) for (int c = a; c < 4; c++) {\n"
"    if (!(colours >> a & 1) || !(colours >> c & 1)) continue;\n"
"    int err = 0;\n"
"    for (int k = 0; k < 16; k++) {\n"
"      int i = idx[k], da = 0, dc = 0; if (!(colours >> i & 1)) continue;\n"
"      for (int ch = 0; ch < 3; ch++) { int x = p[i][ch] - p[a][ch], y = p[i][ch] - p[c][ch]; da += x * x; dc += y * y; }\n"
"      err += da < dc ? da : dc; }\n"
"    if (best < 0 || err < best) { best = err; ea = a; eb = c; } }\n"
"  int f0[4] = {p[ea][0], p[ea][1], p[ea][2], 255}, f1[4] = {p[eb][0], p[eb][1], p[eb][2], rgba ? 0 : 255};\n"
"  for (int k = 0; k < 16; k++) {\n"
"    int i = idx[k], near1 = 0;\n"
"    if (colours >> i & 1) { int da = 0, dc = 0; for (int ch = 0; ch < 3; ch++) { int x = p[i][ch] - f0[ch], y = p[i][ch] - f1[ch]; da += x * x; dc += y * y; } near1 = dc < da; }\n"
"    w[2 * k] = near1; w[2 * k + 1] = rgba && i == 3; }\n"
"  block(o, 12, 1, 2, f0, f1, w); }\n"
"kernel void b1a(device uchar *stg [[buffer(0)]], device const P &p [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= p.bw || g.y >= p.bh) return;\n"
"  uchar o[16]; bc1_astc(stg + p.src + g.y * p.pitch + g.x * 8, o, int(p.rgba));\n"
"  device uchar *d = stg + p.dst + (g.y * p.bw + g.x) * 16; for (int i = 0; i < 16; i++) d[i] = o[i]; }\n";
static id<MTLComputePipelineState> b1a_pso(void) {
    static id<MTLComputePipelineState> ps; static int failed;
    if (!ps && !failed) { NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_b1a_msl) options:nil error:&e]; id<MTLFunction> fn = lib ? [lib newFunctionWithName:@"b1a"] : nil;
        ps = fn ? [g_dev newComputePipelineStateWithFunction:fn error:&e] : nil;
        if (!ps) { failed = 1; MLOG("[red] no BC: cannot create the BC1 → ASTC pipeline %s (BC1 transcoded on the CPU)", e.localizedDescription.UTF8String); } else MLOG("no BC: BC1 kept compressed, transcoded to ASTC 4x4 on the GPU"); }
    return ps;
}
typedef struct { __unsafe_unretained id<MTLBuffer> stg, params; size_t poff; uint32_t bw, bh; } up_a1;   // one level / layer for b1a
static up_a1 *g_up_a1s; static int g_nup_a1s, g_up_a1cap;
static up_blit *g_up_posts; static int g_nup_posts, g_up_postcap;   // copies after the kernels (b1a's ASTC blocks into their textures)
static NSMutableArray *g_up_texs;   // Target textures in the batch: before the flush, textures may be replaced / released within the same submission
static id<MTLBuffer> g_up_block;
static size_t g_up_block_off, g_up_block_size;
static NSMutableArray *g_up_keep;
// Upload batches bounded (seedds/nxnative 7dc250e, MIT, written anew): a batch took staging for every upload of a submission, and its one-off
// blocks and CPU-path buffers lived until the next render fence, so a frame that uploads a lot held all of it at once (nxnative: Tears of the
// Kingdom on the Mac peaked at 10.7 / 12.8 GiB of footprint, 4.24 GiB after; an A20 Pro iPhone was killed at 8.09 GiB with 1.1 GB re-uploaded in
// one frame). Now a batch is committed once its staging would pass DM_NVN_UP_BATCH_MB (128; 0: unbounded, as before) and the next one begins, the
// three batch slots in flight holding it back, and a committed batch's staging is released when that batch has completed on the GPU (t_upbound)
static size_t g_up_batch_bytes; static uint64_t g_up_batch_no, g_up_splits; static _Atomic uint64_t g_up_done_batch;
static NSMutableArray *g_up_retire;   // [batch number, its staging objects], oldest first
static size_t up_batch_limit(void) { static long v = -1; if (v < 0) { const char *e = getenv("DM_NVN_UP_BATCH_MB"); v = e ? atol(e) : 128; if (v < 0) v = 0; } return (size_t)v << 20; }
static void up_retire(void) {   // the owner thread: staging of completed batches released, out of the residency set (committed now: the set holds
    int n = 0;                     // what it lists until then)
    while (g_up_retire.count) {
        NSArray *e = g_up_retire[0]; if ([e[0] unsignedLongLongValue] > atomic_load(&g_up_done_batch)) break;
        for (id o in e[1]) { pthread_mutex_lock(&g_res_mu); int reg = [g_res_objects containsObject:o]; pthread_mutex_unlock(&g_res_mu); if (reg) res_remove((id<MTLResource>)o); }
        [g_up_retire removeObjectAtIndex:0]; n++;
    }
    if (n) res_commit();
}
// Pooled staging blocks (MacNativeGames, MIT): a fresh 32 MiB private block per upload batch, added to and later removed from the residency set, made
// every batch flush commit the residency set (Tears of the Kingdom: 2091 blocks in about 4000 frames). Up to UP_POOL blocks are made once and stay
// resident; a block is free again when the batch that used it has completed on the GPU. Larger requests, or every block in flight, use a one-off
// block retired as before (t_uppool)
#if TARGET_OS_IPHONE
#define UP_POOL 2   // about 4 GB per app in total
#else
#define UP_POOL 6
#endif
static __strong id<MTLBuffer> g_up_pool[UP_POOL]; static _Atomic int g_up_pool_busy[UP_POOL]; static int g_up_pool_n, g_up_block_pooled; static uint32_t g_up_batch_mask; static uint64_t g_up_pool_hits, g_up_pool_misses;
static int up_pool_take(void) {   // a free pooled block (made and made resident once), or -1
    for (int i = 0; i < g_up_pool_n; i++) if (!(g_up_batch_mask >> i & 1) && !atomic_load(&g_up_pool_busy[i])) { atomic_store(&g_up_pool_busy[i], 1); g_up_batch_mask |= 1u << i; g_up_pool_hits++; return i; }
    if (g_up_pool_n < UP_POOL) { id<MTLBuffer> b = [g_dev newBufferWithLength:UP_BLOCK_SIZE options:MTLResourceStorageModePrivate]; if (b) { int i = g_up_pool_n++; g_up_pool[i] = b; res_add(b); atomic_store(&g_up_pool_busy[i], 1); g_up_batch_mask |= 1u << i; return i; } }
    g_up_pool_misses++; return -1;
}


// The upload command buffer was discarded by the system (dropped along during GPU error recovery; the first frame on entering Travincal drops one every run): the objects of this batch of textures are built and the timestamps updated, but the content was never written in,
// and if the pool memory on the game side no longer changes it will never be re-uploaded. Record this batch of target textures, and the next time any of them is fetched treat it as dirty and re-upload once.
// DM_TEST_UP_LOST=<batch index> (for testing): treat that batch of uploads as dropped, to verify the re-upload path.
static NSHashTable *g_up_lost; static os_unfair_lock g_up_lost_lk = OS_UNFAIR_LOCK_INIT; static _Atomic int g_up_lost_n; static _Atomic uint64_t g_up_lost_redo;   // Weak-reference table: if a texture in the record is never fetched again and is released normally, this table does not keep it alive
static int up_lost_take(id tex) {   // This texture's upload was dropped: take it out of the record and return 1
    if (!tex) return 0; os_unfair_lock_lock(&g_up_lost_lk); int hit = [g_up_lost containsObject:tex]; if (hit) { [g_up_lost removeObject:tex]; atomic_store(&g_up_lost_n, (int)g_up_lost.allObjects.count); atomic_fetch_add(&g_up_lost_redo, 1); } os_unfair_lock_unlock(&g_up_lost_lk); return hit;
}
static void up_diag(void);
static void up_flush(void) {
    up_owner_check("up_flush");
    if (!g_up_cb) return;
    if (g_up_ce) {
        m4_end(g_up_ce); g_up_ce = nil;
    }
#if DM_NVN_M3
    // Older API: copy and compute are two kinds of encoder - first a copy encoder does "staging block -> texture", then a compute encoder does the GPU decompression for devices without BC
    BENC b = g_nup_blits ? [g_up_cb blitCommandEncoder] : nil;
    for (int i = 0; b && i < g_nup_blits; i++) {
        up_blit *x = &g_up_blits[i];
        enc_b_copy_buffer_texture(b, x->src, x->off, x->row, x->image, x->size, x->dst, x->slice, x->level, MTLOriginMake(0, 0, 0));
    }
    [b endEncoding];
    CENC dc = g_nup_decs ? [g_up_cb computeCommandEncoder] : nil;
    if (dc) {
        [dc setComputePipelineState:bcd_pso()];
        for (int i = 0; i < g_nup_decs; i++) { up_dec *x = &g_up_decs[i];
            [dc setBuffer:x->src offset:0 atIndex:0]; [dc setBuffer:x->params offset:x->poff atIndex:1]; [dc setTexture:x->view atIndex:0];
            [dc dispatchThreadgroups:MTLSizeMake((x->bw + 7) / 8, (x->bh + 7) / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; }
    }
    [dc endEncoding]; g_up_ce = nil;
    CENC ac = g_nup_a1s ? [g_up_cb computeCommandEncoder] : nil;
    if (ac) {
        [ac setComputePipelineState:b1a_pso()];
        for (int i = 0; i < g_nup_a1s; i++) { up_a1 *x = &g_up_a1s[i];
            [ac setBuffer:x->stg offset:0 atIndex:0]; [ac setBuffer:x->params offset:x->poff atIndex:1];
            [ac dispatchThreadgroups:MTLSizeMake((x->bw + 7) / 8, (x->bh + 7) / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; }
        [ac endEncoding];
    }
    BENC pb = g_nup_posts ? [g_up_cb blitCommandEncoder] : nil;
    for (int i = 0; pb && i < g_nup_posts; i++) { up_blit *x = &g_up_posts[i]; enc_b_copy_buffer_texture(pb, x->src, x->off, x->row, x->image, x->size, x->dst, x->slice, x->level, MTLOriginMake(0, 0, 0)); }
    [pb endEncoding];
#else
    CENC b = g_nup_blits || g_nup_decs || g_nup_a1s ? g_up_ce = [g_up_cb computeCommandEncoder] : nil;
    if (b) [b barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    for (int i = 0; b && i < g_nup_blits; i++) {
        up_blit *x = &g_up_blits[i];
        enc_b_copy_buffer_texture(b, x->src, x->off, x->row, x->image, x->size, x->dst, x->slice, x->level, MTLOriginMake(0, 0, 0));
    }
    if (b && g_nup_decs) {   // GPU decompression without BC (see up_dec): offsets in the parameter block are relative to the start of the staging block
        [b setArgumentTable:g_up_ct]; [b setComputePipelineState:bcd_pso()];
        for (int i = 0; i < g_nup_decs; i++) { up_dec *x = &g_up_decs[i];
            [g_up_ct setAddress:x->src.gpuAddress atIndex:0]; [g_up_ct setAddress:x->params.gpuAddress + x->poff atIndex:1]; [g_up_ct setTexture:x->view.gpuResourceID atIndex:0];
            [b dispatchThreadgroups:MTLSizeMake((x->bw + 7) / 8, (x->bh + 7) / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; }
    }
    if (b && g_nup_a1s) {   // BC1 → ASTC (bc1_astc), then its blocks copied into the textures once the kernels are done
        [b setArgumentTable:g_up_ct]; [b setComputePipelineState:b1a_pso()];
        for (int i = 0; i < g_nup_a1s; i++) { up_a1 *x = &g_up_a1s[i];
            [g_up_ct setAddress:x->stg.gpuAddress atIndex:0]; [g_up_ct setAddress:x->params.gpuAddress + x->poff atIndex:1];
            [b dispatchThreadgroups:MTLSizeMake((x->bw + 7) / 8, (x->bh + 7) / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; }
        [b barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];
        for (int i = 0; i < g_nup_posts; i++) { up_blit *x = &g_up_posts[i]; enc_b_copy_buffer_texture(b, x->src, x->off, x->row, x->image, x->size, x->dst, x->slice, x->level, MTLOriginMake(0, 0, 0)); }
    }
    if (b) { m4_end(b); g_up_ce = nil; }
#endif
    CBUF cb = g_up_cb;
    unsigned slot = (g_up_seq - 1) % 3;
    NSMutableArray *dsts = [NSMutableArray arrayWithCapacity:(NSUInteger)g_nup_blits]; for (int i = 0; i < g_nup_blits; i++) if (g_up_blits[i].dst) [dsts addObject:g_up_blits[i].dst];
    static int lost_at = -2; static uint64_t batch_n; if (lost_at == -2) { const char *e = getenv("DM_TEST_UP_LOST"); lost_at = e ? atoi(e) : -1; } int fake_lost = dsts.count && lost_at > 0 && (int)++batch_n == lost_at;
#if !DM_NVN_M3
    MTL4CommitOptions *o = [MTL4CommitOptions new];
#endif
    atomic_fetch_add(&g_up_pending, 1);
    uint32_t pool_mask = g_up_batch_mask; uint64_t bno = g_up_batch_no;
    [FB_ON(o, cb) FB_SEL:^(FB_ARG f) {
        if (f.error) MLOG("[red] upload command buffer execution error: %s", f.error.description.UTF8String);
        if (f.error || fake_lost) {
            os_unfair_lock_lock(&g_up_lost_lk); if (!g_up_lost) g_up_lost = [NSHashTable weakObjectsHashTable]; for (id d in dsts) [g_up_lost addObject:d]; int n = (int)g_up_lost.allObjects.count; atomic_store(&g_up_lost_n, n); os_unfair_lock_unlock(&g_up_lost_lk);
            ser_bump(0);   // Make the view cache of every texture ID take the slow path once (on a cache hit tex_mtl is not passed through and this record is not seen)
            MLOG("%supload command buffer lost: the %lu textures of this batch are marked as unwritten and re-uploaded on next use (%d pending re-upload in total)", fake_lost ? "DM_TEST_UP_LOST: " : "", (unsigned long)dsts.count, n);
        }
        double d = f.GPUEndTime - f.GPUStartTime;
        if (d > 0) atomic_fetch_add(&g_upload_gpu_ns, (uint64_t)(d * 1e9));
        atomic_store(&g_up_alloc_busy[slot], 0);
        for (int i = 0; i < UP_POOL; i++) if (pool_mask >> i & 1) atomic_store(&g_up_pool_busy[i], 0);   // pooled staging blocks of this batch are free again
        uint64_t dn = atomic_load(&g_up_done_batch); while (dn < bno && !atomic_compare_exchange_weak(&g_up_done_batch, &dn, bno)) {}   // its staging may go (up_retire)
        atomic_fetch_sub(&g_up_pending, 1);
        up_notify();
    }];
    res_commit();
#if DM_NVN_M3
    [cb commit];
#else
    [cb endCommandBuffer];
    CBUF buffers[] = {cb};
    [g_q4 commit:buffers count:1 options:o];
#endif
    cb_committed();
    g_up_cb = nil;
#ifndef DM_TEST_UP_RETIRE_AT_FENCE   // negative control of t_upbound only: the staging released after the next render fence, as before
    { NSMutableArray *st = [NSMutableArray arrayWithArray:g_up_keep]; if (g_up_block && !g_up_block_pooled) [st addObject:g_up_block];
      if (st.count) { if (!g_up_retire) g_up_retire = [NSMutableArray array]; [g_up_retire addObject:@[@(bno), st]]; } }
#else
    if (g_up_block && !g_up_block_pooled) defer_obj_release(g_up_block);
    for (id obj in g_up_keep) defer_obj_release(obj);
#endif
    g_up_block = nil; g_up_block_pooled = 0; g_up_block_off = g_up_block_size = 0; g_up_batch_mask = 0; g_up_batch_bytes = 0;
    [g_up_keep removeAllObjects];
    up_diag();
    g_nup_blits = 0; g_nup_decs = 0; g_nup_a1s = 0; g_nup_posts = 0;
    [g_up_texs removeAllObjects];
}

static void up_begin(void) {
    up_owner_check("up_begin");
    if (g_up_cb) return;
    up_retire(); g_up_batch_no++;
    unsigned slot = g_up_seq++ % 3;
    up_wait_zero(&g_up_alloc_busy[slot]);
    atomic_store(&g_up_alloc_busy[slot], 1);
#if DM_NVN_M3
    g_up_cb = [g_q commandBuffer];
    g_up_ce = [g_up_cb computeCommandEncoder];
#else
    [g_up_alloc4[slot] reset];
    g_up_cb = [g_dev newCommandBuffer];
    [g_up_cb beginCommandBufferWithAllocator:g_up_alloc4[slot]];
    g_up_ce = [g_up_cb computeCommandEncoder];
    [g_up_ce barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [g_up_ce setArgumentTable:g_up_ct];
#endif
    if (!g_up_texs) g_up_texs = [NSMutableArray array];
    if (!g_up_keep) g_up_keep = [NSMutableArray array];
}
static void up_bound(size_t n) {   // before an upload that cuts n bytes of staging: the open batch committed first if it would pass the bound
#ifndef DM_TEST_UP_UNBOUNDED   // negative control of t_upbound only: one batch for the whole submission, as before
    size_t lim = up_batch_limit();
    if (lim && g_up_cb && g_up_batch_bytes && g_up_batch_bytes + n > lim) { g_up_splits++; up_flush(); }
#else
    (void)n;
#endif
}

// Diagnostic readout (low frequency; printed only when the block switch count changed and ≥ 1 second since the last line, at most 200 lines per game): how many times an upload batch ran out of staging block and switched blocks midway; GPU allocated, residency set object count, deferred-release queue length.
// If the old block is not retired on a switch it stays in the residency set and can never be freed, and "GPU allocated" climbs one block at a time with the switch count
static uint64_t g_up_blocks_new, g_up_swaps, g_up_swap_bytes; static NSHashTable *g_up_old;   // g_up_old: old blocks switched out (weak references); the diagnostic line counts how many are still alive
static void up_diag(void) {
    // Print a line when the block switch count changes; then one more line 5 seconds later (that batch's commit is long done and deferred retirement drained, so "GPU allocated" is the settled number)
    static uint64_t said_swaps = ~0ull, last_ns; static int lines, follow; uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    int chg = g_up_swaps != said_swaps, fol = follow && now - last_ns >= 5000000000ull; if ((!chg && !fol) || lines >= 200) return;
    if (chg && last_ns && now - last_ns < 1000000000ull) return; last_ns = now; said_swaps = g_up_swaps; lines++; follow = chg;
    unsigned long alive = 0, inres = 0; uint64_t abytes = 0; NSArray *olds = g_up_old.allObjects;
    pthread_mutex_lock(&g_res_mu); unsigned long nres = (unsigned long)g_res_objects.allObjects.count; for (id<MTLBuffer> o in olds) { alive++; abytes += o.length; inres += [g_res_objects containsObject:o]; } pthread_mutex_unlock(&g_res_mu);
    pthread_mutex_lock(&g_deferred_mu); unsigned long ndef = (unsigned long)g_deferred.count; pthread_mutex_unlock(&g_deferred_mu);
    MLOG("upload staging blocks%s: %llu created in total, %llu block switches within a batch (swapped-out old blocks %llu MB in total); swapped-out old blocks still alive now: %lu blocks %llu MB, still in the residency set: %lu blocks; GPU allocated %llu MB, residency set %lu objects, deferred-release queue %lu; %llu batches committed at the bound (DM_NVN_UP_BATCH_MB)", fol && !chg ? " (after settling)" : "",
         (unsigned long long)g_up_blocks_new, (unsigned long long)g_up_swaps, (unsigned long long)(g_up_swap_bytes >> 20), alive, (unsigned long long)(abytes >> 20), inres, (unsigned long long)(g_dev.currentAllocatedSize >> 20), nres, ndef, (unsigned long long)g_up_splits);
}
static id<MTLBuffer> up_alloc(size_t n, size_t *off) {
    size_t need = (n + 255u) & ~((size_t)255u);
    if (!g_up_block || g_up_block_off + need > g_up_block_size) {
        // Block switch: the old block is still referenced by copy / decompress commands already recorded in this batch and cannot be freed on the spot; hand it to g_up_keep, to be retired later with the other staging after the flush commits (removed from the residency set on retire).
        // Previously g_up_block was simply replaced with the new block here: nobody retired the old block, yet it stayed in the residency set, so each extra block switch in an upload batch leaked one block (usually 32 MB) of GPU memory.
        // The old block and the "current block" retired at flush are two objects, so nothing is retired twice
        if (g_up_block && g_up_block_pooled) { g_up_swaps++; g_up_block = nil; }   // a pooled block is freed with its batch (up_flush), not retired
        g_up_blocks_new++; if (g_up_block) { g_up_swaps++; g_up_swap_bytes += g_up_block_size; if (!g_up_old) g_up_old = [NSHashTable weakObjectsHashTable]; [g_up_old addObject:g_up_block]; static int leak = -1; if (leak < 0) leak = getenv("DM_TEST_UP_LEAK") != NULL;   // test switch DM_TEST_UP_LEAK=1: do not retire the old block, as before the fix (for comparison, to measure the leak)
            if (!leak) { if (!g_up_keep) g_up_keep = [NSMutableArray array]; [g_up_keep addObject:g_up_block]; } }
        int pi = need <= UP_BLOCK_SIZE ? up_pool_take() : -1;
        g_up_block_pooled = pi >= 0;
        if (pi >= 0) { g_up_block = g_up_pool[pi]; g_up_block_size = UP_BLOCK_SIZE; g_up_blocks_new--; }
        else {
            g_up_block_size = n > UP_BLOCK_SIZE ? need : UP_BLOCK_SIZE;
            g_up_block = [g_dev newBufferWithLength:g_up_block_size options:MTLResourceStorageModePrivate];
            if (!g_up_block) MDIE("failed to create the upload staging block for swizzled textures %#zx", g_up_block_size);
            res_add(g_up_block);
        }
        g_up_block_off = 0;
    }
    *off = g_up_block_off;
    g_up_block_off += need; g_up_batch_bytes += need;
    return g_up_block;
}

// Deswizzle parameter blocks: 64 bytes per cell, cut sequentially from a 64 KB shared-storage block; a full block is replaced by a new one, and the old block is released lazily by fence
static id<MTLBuffer> g_up_pblock; static size_t g_up_pblock_off;
static id<MTLBuffer> up_param_alloc(size_t *off) {
    if (!g_up_pblock || g_up_pblock_off + 64 > g_up_pblock.length) {
        if (g_up_pblock) defer_obj_release(g_up_pblock);
        g_up_pblock = [g_dev newBufferWithLength:64 << 10 options:MTLResourceStorageModeShared];
        if (!g_up_pblock) MDIE("failed to create the deswizzle parameter block");
        res_add(g_up_pblock); g_up_pblock_off = 0;
    }
    *off = g_up_pblock_off; g_up_pblock_off += 64;
    return g_up_pblock;
}
static void up_blit_add(id<MTLBuffer> src, size_t off, size_t row, size_t image, MTLSize size,
                        id<MTLTexture> dst, NSUInteger slice, NSUInteger level) {
    if (g_nup_blits == g_up_blitcap) {
        g_up_blitcap = g_up_blitcap ? g_up_blitcap * 2 : 256;
        g_up_blits = realloc(g_up_blits, g_up_blitcap * sizeof *g_up_blits);
    }
    g_up_blits[g_nup_blits++] = (up_blit){src, off, row, image, size, dst, slice, level};
}

// Small CPU-filled textures (up to PKG_HASH_MAX bytes) are checked by a content hash once per submission instead of write-protecting their pages
// (MacNativeGames pkg_hashed, MIT; written anew here). Tears of the Kingdom rewrites a few 4x4 textures on the CPU every frame: each write was a page
// fault on a game thread and a step of the guest-write generation, after which every argument block was encoded again and every CPU-filled texture
// bound later was looked up again; their 16 KB host pages also hold other data, whose writes made them dirty without a change. DM_NVN_PKG_HASH=0: protected as the others
#define PKG_HASH_MAX (4u << 10)
static int check_once(void) {   // DM_NVN_CHECK_ONCE=0: a CPU-filled texture is checked at every use, as before
    static int v = -1; if (v < 0) { const char *e = getenv("DM_NVN_CHECK_ONCE"); v = !(e && !strcmp(e, "0")); if (!v) MLOG("DM_NVN_CHECK_ONCE=0: CPU-filled textures checked at every use"); }
    return v;
}
static int pkg_hashed(const tex_t *t) {
    static int on = -1; if (on < 0) { const char *e = getenv("DM_NVN_PKG_HASH"); on = !e || atoi(e); }
    return on && t->mem && t->size && t->size <= PKG_HASH_MAX;
}
static uint64_t pkg_hash(const void *p, uint64_t n) {   // four independent lanes over 8-byte words, then the tail bytes
    const uint8_t *b = p; uint64_t a[4] = {0x9E3779B97F4A7C15ull ^ n, 0xC2B2AE3D27D4EB4Full, 0x165667B19E3779F9ull, 0x27D4EB2F165667C5ull}, i = 0;
    for (; i + 32 <= n; i += 32) for (int k = 0; k < 4; k++) { uint64_t w; memcpy(&w, b + i + 8 * k, 8); a[k] = (a[k] ^ w) * 0xff51afd7ed558ccdull; a[k] ^= a[k] >> 29; }
    uint64_t h = a[0] ^ (a[1] * 3) ^ (a[2] * 5) ^ (a[3] * 7);
    for (; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; return h ^ (h >> 33);
}
static id<MTLTexture> pkg_upload_(const tex_t *t, uint64_t *stamp, id<MTLTexture> reuse);
// A small texture's content hash under the guard (core/guard.h, plan item 290): 0 when its storage could not be read (the game took it back)
typedef struct { const void *p; uint64_t n, h; } pkg_hash_t;
static void pkg_hash_run(void *c) { pkg_hash_t *x = c; x->h = pkg_hash(x->p, x->n); }
static int pkg_hash_guarded(const tex_t *t, uint64_t *h) {
    pkg_hash_t x = {t->mem, t->size, 0};
    if (!guard_run(t->mem, t->size, pkg_hash_run, &x)) { RED_ONCE(117, "texture storage %p + %#x unreadable after its submission (the game took the memory back): the texture kept as it was", (void *)t->mem, t->size); return 0; }
    *h = x.h; return 1;
}
// The CPU path's reads of the storage, under the guard: the deswizzles (and decompressions / transcodes) into the staging memory, and the copies
// into the texture they call for, made after it if nothing faulted
typedef struct { uint32_t w, h, z, y, l; size_t off, row, img; } cpu_blit;
typedef struct { const tex_t *t; uint8_t *stg; unsigned levels, layers; int bw, bh, bpb, a1, a1rgba, unp, ubpp; uint8_t *cb; cpu_blit *bl; int nbl; } cpu_fill_t;
static void cpu_fill(void *c_) {
    cpu_fill_t *c = c_; const tex_t *t = c->t; size_t off = 0;
    for (unsigned l = 0; l < c->levels; l++) {
        uint32_t w = MAX(t->d.width >> l, 1u), h = MAX(t->d.height >> l, 1u), z = t->d.target == 2 ? MAX(t->d.depth >> l, 1u) : 1;
        size_t pitch = (size_t)((w + c->bw - 1) / c->bw) * c->bpb, rows = (h + c->bh - 1) / c->bh;
        for (unsigned y = 0; y < c->layers; y++) {
            if (c->a1) {   // No BC, BC1: deswizzled into a temporary area, transcoded to ASTC into the staging area (nvn_bc1astc.c)
                uint32_t bw4 = (w + 3) / 4, bh4 = (h + 3) / 4; size_t ap = (size_t)bw4 * 16; c->cb = malloc(pitch * rows * z);
                if (!c->cb || nvn_deswizzle(&t->d, l, y, t->mem, c->cb, pitch)) { free(c->cb); c->cb = NULL; RED_ONCE(9, "deswizzle failed format %#x target %d", t->d.format, t->d.target); break; }
                for (uint32_t zi = 0; zi < z; zi++) nvn_bc1_astc_image(c->cb + zi * pitch * rows, pitch, bw4, bh4, c->stg + off + zi * ap * bh4, c->a1rgba);
                free(c->cb); c->cb = NULL;
                c->bl[c->nbl++] = (cpu_blit){w, h, z, y, l, off, ap, ap * bh4};
                off += ap * bh4 * z;
                continue;
            }
            if (c->unp) {   // No BC: first deswizzle into a temporary area (compressed blocks), then decompress layer by layer into the staging area
                c->cb = malloc(pitch * rows * z); size_t up = (size_t)w * c->ubpp;
                if (!c->cb || nvn_deswizzle(&t->d, l, y, t->mem, c->cb, pitch)) { free(c->cb); c->cb = NULL; RED_ONCE(9, "deswizzle failed format %#x target %d", t->d.format, t->d.target); break; }
                for (uint32_t zi = 0; zi < z; zi++) nvn_bc_decode(t->d.format, c->cb + zi * pitch * rows, pitch, w, h, c->stg + off + zi * up * h, up);
                free(c->cb); c->cb = NULL;
                c->bl[c->nbl++] = (cpu_blit){w, h, z, y, l, off, up, up * h};
                off += up * h * z;
                continue;
            }
            if (nvn_deswizzle(&t->d, l, y, t->mem, c->stg + off, pitch)) { RED_ONCE(9, "deswizzle failed format %#x target %d", t->d.format, t->d.target); break; }
            c->bl[c->nbl++] = (cpu_blit){w, h, z, y, l, off, pitch, pitch * rows};
            off += pitch * rows * z;
        }
    }
}
static id<MTLTexture> pkg_upload(const tex_t *t, uint64_t *stamp, id<MTLTexture> reuse) {   // reuse: a texture of this tex_t no GPU work reads any more, written instead of a new one
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    id<MTLTexture> r = pkg_upload_(t, stamp, reuse);
    clock_gettime(CLOCK_MONOTONIC, &b); uint64_t d_ = (uint64_t)((b.tv_sec - a.tv_sec) * 1000000000ll + b.tv_nsec - a.tv_nsec);
    atomic_fetch_add(&g_upload_ns, d_); g_up_total_ns += d_;
    return r;
}
uint64_t mtl_upload_ns_take(void) { return atomic_exchange(&g_upload_ns, 0); }
uint64_t mtl_upload_gpu_ns_take(void) { return atomic_exchange(&g_upload_gpu_ns, 0); }
void mtl_create_stats(uint64_t *lib, uint64_t *pso) { *lib = g_bp.lib_n; *pso = g_bp.pso_n; }
void mtl_create_ns(uint64_t *lib_ns, uint64_t *pso_ns) { *lib_ns = g_bp.lib_ns; *pso_ns = g_bp.pso_ns; }   // Cumulative time spent synchronously creating shader libraries / pipelines on the spot (the long-frame log takes the per-frame difference)
uint64_t mtl_upload_bytes(void) { return g_bp.up_bytes; }   // Cumulative texture upload bytes
static id<MTLTexture> pkg_upload_(const tex_t *t, uint64_t *stamp, id<MTLTexture> reuse) {
    MTLTextureDescriptor *d = tex_desc(t, t->has_layout);   // a CPU-filled texture without a packaged layout may become a render target later (see tex_gpu_write)
    if (!d) return nil;
    int same = reuse && reuse.width == d.width && reuse.height == d.height && reuse.depth == d.depth && reuse.pixelFormat == d.pixelFormat && reuse.textureType == d.textureType &&
               reuse.mipmapLevelCount == d.mipmapLevelCount && reuse.arrayLength == d.arrayLength && reuse.usage == d.usage;
    id<MTLTexture> mt = same ? reuse : [g_dev newTextureWithDescriptor:d];
    if (!mt) { RED_ONCE(2, "failed to create MTLTexture %ux%u format %#x target %d", t->d.width, t->d.height, t->d.format, t->d.target); return nil; }
    res_add(mt);   // Both first upload and re-upload register residency here (Metal 4 does not make resources resident automatically; a missed registration = GPU address page fault)
    *stamp = pkg_hashed(t) ? nvn_dirty_now() : nvn_dirty_protect_stamp(t->mem, t->size);   // the stamp before the protection (nvn_dirty.c)
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    unsigned layers = (t->d.target == 4 || t->d.target == 9) ? t->d.depth : t->d.target == 8 ? 6 : t->d.target == 3 ? t->d.height : 1;   // a cube array's depth is its faces
    uint32_t upf; int ubpp = 0, unp = bc_unpack(t->d.format, &upf, &ubpp);   // No BC: decompress on the CPU after deswizzling, with the staging area sized for the decompressed data
    int a1rgba = 0, a1srgb, a1 = bc1_astc(t->d.format, &a1rgba, &a1srgb);   // No BC, BC1: as ASTC 4x4 (16 bytes a block where BC1 has 8)
    size_t total = 0;
    for (unsigned l = 0; l < d.mipmapLevelCount; l++) {
        uint32_t w = MAX(t->d.width >> l, 1u), h = MAX(t->d.height >> l, 1u), z = t->d.target == 2 ? MAX(t->d.depth >> l, 1u) : 1;
        total += unp ? (size_t)w * ubpp * h * z * layers : (size_t)((w + bw - 1) / bw) * (a1 ? 16 : bpb) * ((h + bh - 1) / bh) * z * layers;
    }
    g_bp.up_bytes += total;
    void *mb; uint64_t mo;
    int gdec = unp && t->d.target != 2 && bcd_fmt(t->d.format) && bcd_pso();   // BC1–5 without BC: GPU deswizzle + GPU decompression
    int ga1 = a1 && t->d.target != 2 && b1a_pso();   // BC1 without BC: GPU deswizzle, then b1a into ASTC blocks behind the BC1 ones (3D: the CPU)
    size_t ctotal = 0;   // Staging size of the compressed blocks (with GPU decompression the staging area holds compressed blocks)
    if (gdec || ga1) for (unsigned l = 0; l < d.mipmapLevelCount; l++) { uint32_t w = MAX(t->d.width >> l, 1u), h = MAX(t->d.height >> l, 1u); ctotal += (size_t)((w + bw - 1) / bw) * bpb * ((h + bh - 1) / bh) * layers; }
    int use_gpu = (!unp || gdec) && (!a1 || ga1) && g_nvn_deswz_gpu && mtl_addr((uint64_t)t->mem, t->size, &mb, &mo);
    if (g_nvn_deswz_gpu && !use_gpu && !unp) RED_ONCE(40, "swizzled texture storage is not in a memory pool, falling back to the CPU");
    if (use_gpu) {
        // The GPU reads the memory pool at execution time, which is closer to a real Switch than a snapshot at CPU upload time; the dirty-page stamp is still taken before encoding, and later writes still trigger a re-upload.
        size_t stg_off, stg_n = ga1 ? ctotal + total : gdec ? ctotal : total ? total : 16;
        up_bound(stg_n); up_begin();
        id<MTLBuffer> stg = up_alloc(stg_n, &stg_off);
        size_t off = 0, aoff = ctotal; int enc_ok = g_up_ce != nil, nb0 = g_nup_blits, nd0 = g_nup_decs, na0 = g_nup_a1s, np0 = g_nup_posts;
        for (unsigned l = 0; enc_ok && l < d.mipmapLevelCount; l++) {
            uint32_t w = MAX(t->d.width >> l, 1u), h = MAX(t->d.height >> l, 1u), z = t->d.target == 2 ? MAX(t->d.depth >> l, 1u) : 1;
            size_t pitch = (size_t)((w + bw - 1) / bw) * bpb, rows = (h + bh - 1) / bh;
            for (unsigned y = 0; y < layers; y++) {
#if DM_NVN_M3
                if (nvn_deswz_gpu_encode(g_up_ce, (__bridge id<MTLBuffer>)mb, mo, &t->d, l, y, stg, stg_off + off, pitch)) { enc_ok = 0; break; }
#else
                size_t poff; id<MTLBuffer> params = up_param_alloc(&poff);   // Metal 4 has no setBytes: the parameter block of each deswizzle is cut from a shared block (no new GPU buffer each time)
                if (nvn_deswz_gpu_encode_m4(g_up_ce, g_up_ct, params, poff, (__bridge id<MTLBuffer>)mb, mo, &t->d, l, y, stg, stg_off + off, pitch)) { enc_ok = 0; break; }
#endif
                if (gdec) {   // Decompression job: dispatched at flush time after the barrier (see up_flush)
                    MTLPixelFormat vf = mt.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB ? MTLPixelFormatRGBA8Unorm : mt.pixelFormat;
                    id<MTLTexture> v = [mt newTextureViewWithPixelFormat:vf textureType:MTLTextureType2D levels:NSMakeRange(l, 1) slices:NSMakeRange(y, 1)];
                    size_t qoff; id<MTLBuffer> q = up_param_alloc(&qoff);
                    if (!v || !q) { enc_ok = 0; break; }
                    uint32_t *pp = (uint32_t *)((uint8_t *)q.contents + qoff); pp[0] = (uint32_t)(stg_off + off); pp[1] = (uint32_t)pitch; pp[2] = w; pp[3] = h; pp[4] = (uint32_t)bcd_fmt(t->d.format);
                    if (g_nup_decs == g_up_deccap) { g_up_deccap = g_up_deccap ? g_up_deccap * 2 : 256; g_up_decs = realloc(g_up_decs, g_up_deccap * sizeof *g_up_decs); }
                    g_up_decs[g_nup_decs++] = (up_dec){stg, q, 0, qoff, (w + 3) / 4, (h + 3) / 4, v}; [g_up_texs addObject:v]; defer_obj_unref(v);   // View: released after this submission finishes running, residency not revoked (what is resident is the base texture)
                } else if (ga1) {   // b1a at flush time: this level / layer's BC1 blocks at off, its ASTC blocks at aoff, then copied into the texture
                    uint32_t bw4 = (w + 3) / 4, bh4 = (h + 3) / 4; size_t qoff; id<MTLBuffer> q = up_param_alloc(&qoff);
                    if (!q) { enc_ok = 0; break; }
                    uint32_t *pp = (uint32_t *)((uint8_t *)q.contents + qoff); pp[0] = (uint32_t)(stg_off + off); pp[1] = (uint32_t)pitch; pp[2] = bw4; pp[3] = bh4; pp[4] = (uint32_t)(stg_off + aoff); pp[5] = (uint32_t)a1rgba;
                    if (g_nup_a1s == g_up_a1cap) { g_up_a1cap = g_up_a1cap ? g_up_a1cap * 2 : 256; g_up_a1s = realloc(g_up_a1s, g_up_a1cap * sizeof *g_up_a1s); }
                    g_up_a1s[g_nup_a1s++] = (up_a1){stg, q, qoff, bw4, bh4};
                    if (g_nup_posts == g_up_postcap) { g_up_postcap = g_up_postcap ? g_up_postcap * 2 : 256; g_up_posts = realloc(g_up_posts, g_up_postcap * sizeof *g_up_posts); }
                    g_up_posts[g_nup_posts++] = (up_blit){stg, stg_off + aoff, (size_t)bw4 * 16, (size_t)bw4 * 16 * bh4, MTLSizeMake(w, h, 1), mt, y, l};
                    aoff += (size_t)bw4 * 16 * bh4;
                } else
                up_blit_add(stg, stg_off + off, pitch, pitch * rows, MTLSizeMake(w, h, z), mt, y, l);
                off += pitch * rows * z;
            }
        }
        if (enc_ok) {
            [g_up_texs addObject:mt];
            if (!g_cb) up_flush();
            return mt;
        }
        g_nup_blits = nb0; g_nup_decs = nd0; g_nup_a1s = na0; g_nup_posts = np0;   // Roll back the copies / decompressions already recorded for this texture: the already-encoded deswizzle only writes the staging area and is harmless; this texture takes the CPU path below instead
        if (!g_cb) up_flush();
        RED_ONCE(41, "swizzled texture: GPU deswizzle does not support format %#x target %d, falling back to the CPU", t->d.format, t->d.target);
    }

    up_bound(total ? total : 16); up_begin();
    id<MTLBuffer> stg = [g_dev newBufferWithLength:total ? total : 16 options:MTLResourceStorageModeShared];
    res_add(stg); [g_up_keep addObject:stg]; g_up_batch_bytes += total ? total : 16;
    cpu_fill_t c = {t, stg.contents, (unsigned)d.mipmapLevelCount, layers, bw, bh, bpb, a1, a1rgba, unp, ubpp, NULL, calloc((size_t)d.mipmapLevelCount * layers + 1, sizeof(cpu_blit)), 0};
#ifndef DM_TEST_UPLOAD_UNGUARDED   // negative control of t_texgone only: read as before (the test's child dies)
    int ok = guard_run(t->mem, t->size, cpu_fill, &c);
#else
    cpu_fill(&c); int ok = 1;
#endif
    if (!ok) {   // the storage taken back mid-read: nothing made from it, the texture left as it was (a re-upload keeps the old one)
        free(c.cb); free(c.bl); if (mt != reuse) res_remove(mt);
        RED_ONCE(117, "texture storage %p + %#x unreadable after its submission (the game took the memory back): the texture kept as it was", (void *)t->mem, t->size);
        up_flush(); return nil;
    }
    for (int i = 0; i < c.nbl; i++) up_blit_add(stg, c.bl[i].off, c.bl[i].row, c.bl[i].img, MTLSizeMake(c.bl[i].w, c.bl[i].h, c.bl[i].z), mt, c.bl[i].y, c.bl[i].l);
    free(c.bl);
    up_flush();
    return mt;
}
