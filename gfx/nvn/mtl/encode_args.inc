// gfx/nvn/mtl/encode_args.inc, part of nvn_mtl.m (included there in order, one translation unit): Draws that sample what their own pass draws (plan item 42), argument encoding
#include "../core/widen_rw.h"
// What widening would change in the uniform blocks (plan item 320d, a measurement before the rewrite): DM_WIDEN_SCAN=1 scans each uniform block a
// draw into a 16:9-family target binds (core/widen_rw.h, into a scratch copy: nothing changes) with DM_WIDEN_SCREEN's widening, or 2868x1320's
// without it, and counts per draw whether it reads a projection, view-projection or inverse to change, only the HUD's orthographic projection, or
// neither, and the kinds found; the timing line every 300 frames (hle/timing.inc) gives them
static int widen_scan_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_WIDEN_SCAN"); v = e && atoi(e) > 0; if (v) MLOG("DM_WIDEN_SCAN=1: the uniform blocks of draws into 16:9 targets scanned for what widening would change (nothing is changed)"); } return v; }
static int g_wd_fl;   // this draw's widening flags (320d): the stages' uniform blocks OR-ed; draw.inc clears it before encode_args
static uint64_t g_ws_draw = ~0ull, g_ws_n[3], g_ws_kind[WRW_N], g_ws_bytes; static int g_ws_flags;
static void widen_scan_close(void) { if (g_ws_draw != ~0ull) g_ws_n[g_ws_flags & 1 ? 0 : g_ws_flags & 2 ? 1 : 2]++; }
static void widen_scan_cb(const p_bind_buf *u) {
    if (!u || !u->addr || !u->size) return;
    NSUInteger tw = (NSUInteger)(g_pass.tw / (g_pass.kx > 0 ? g_pass.kx : 1) + 0.5), th = (NSUInteger)(g_pass.th / (g_pass.ky > 0 ? g_pass.ky : 1) + 0.5);
    if (!widen_family((int)tw, (int)th)) return;
    if (g_stat_draw != g_ws_draw) { widen_scan_close(); g_ws_draw = g_stat_draw; g_ws_flags = 0; }
    static widen_t w; static int wi; if (!wi) { wi = 1; w = *widen_get(); if (!w.extra_w && !w.extra_h) w = widen_for_screen(2868, 1320); }
    static uint8_t scratch[65536]; size_t n = u->size < sizeof scratch ? (size_t)u->size : sizeof scratch;
    wrw_hits h = {{0}}; int c = wrw_block(&w, (const uint8_t *)(uintptr_t)u->addr, n, u->addr, scratch, &h);
    for (int k = 0; k < WRW_N; k++) g_ws_kind[k] += h.n[k];
    g_ws_bytes += n;
    if (h.n[WRW_PROJ] || h.n[WRW_VIEWPROJ] || h.n[WRW_INVPROJ]) g_ws_flags |= 1;
    if (h.n[WRW_UI_ORTHO]) g_ws_flags |= 2;
    (void)c;
}
// The rewrite (plan item 320d): a draw into a widened target binds, for each uniform block, a copy with what core/widen_rw.h changes (arena),
// or the block itself when nothing changes; each block (its source's GPU address and size: the pool, a snapshot or an update's private copy)
// scanned once a submission. Its flags: 1 a projection, view-projection or inverse was changed (a 3D draw), 2 the HUD's orthographic projection
// read, 4 a widened texture sampled (a pass over the 3D picture: Tears of the Kingdom's post-processing quads read the HUD's orthographic projection
// too); a draw with 2 and neither 1 nor 4 is centred instead of widened (draw.inc). DM_WIDEN_RW=0: nothing rewritten (the projections stay 16:9)
typedef struct { uint64_t gpu, ngpu; uint32_t size, crh; int fl, keep; __unsafe_unretained id<MTLBuffer> nb; } wrw_ent;
static wrw_ent g_wrw[4096]; static uint64_t g_wrw_sub = ~0ull, g_wrw_scans, g_wrw_copies, g_wrw_bytes, g_wrw_hits;
// Across submissions: a block at the guest address and of the size of one scanned before, with the same content (pkg_hash), has that scan's result
// without a scan: its flags and the 4-byte words the rewrite changed (up to 32; more: not kept). One entry an address and size, 8,192 of them
// (a scan is 0.4-1 ms a MB on the M5, a hash about a tenth of that)
typedef struct { uint64_t addr, hash; uint32_t size, crh; int fl, np, keep; uint32_t po[32], pv[32]; } wrc_ent;
static wrc_ent *g_wrc;
static wrc_ent *wrc_slot(uint64_t addr, uint32_t n, int keep, uint32_t crh) {
    if (!g_wrc && !(g_wrc = calloc(8192, sizeof *g_wrc))) return NULL;
    return &g_wrc[(size_t)(((addr ^ ((uint64_t)n << 40) ^ ((uint64_t)keep << 63) ^ ((uint64_t)crh << 7)) * 0x9E3779B97F4A7C15ull) >> 51) & 8191];
}
// The content a scan with the slots cr depends on (plan item 320g): each range and the 48 bytes after it (a matrix window starting in it), hashed
static uint64_t wrw_hash_slots(const uint8_t *src, uint32_t n, const nvn_rbind *rb) {
    if (!rb || !rb->ncr) return pkg_hash(src, n);
    uint64_t h = 0;
    for (int r = 0; r < rb->ncr; r++) { size_t lo = (size_t)rb->cr[r][0] * 16, hi = ((size_t)rb->cr[r][1] + 1) * 16 + 48; if (lo >= n) continue; if (hi > n) hi = n;
        h = (h ^ pkg_hash(src + lo, hi - lo)) * 0x100000001b3ull + lo; }
    return h;
}
static int widen_rw_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_WIDEN_RW"); v = !(e && !strcmp(e, "0")); if (!v) MLOG("DM_WIDEN_RW=0: uniform blocks not rewritten for the widening"); } return v; }
// keep: screen sizes left as the game's (a fragment shader reading the fragment coordinate, plan item 320f)
// rb: the binding's reflection, its slots read (320g): only those scanned and hashed, both caches keyed on them
static int widen_cb_rewrite(const p_bind_buf *u, uint64_t gpu, void *mb, uint64_t *ngpu, id<MTLBuffer> __unsafe_unretained *nb, int keep, const nvn_rbind *rb) {
    *ngpu = 0; *nb = nil;
    if (!widen_rw_on() || !u || !u->size) return 0;
    if (g_wrw_sub != g_sub_serial) { memset(g_wrw, 0, sizeof g_wrw); g_wrw_sub = g_sub_serial; }
    uint32_t n = u->size < 65536 ? (uint32_t)u->size : 65536;
    size_t hix = (size_t)((gpu * 0x9E3779B97F4A7C15ull) >> 52) & 4095;
    uint32_t crh = rb ? rb->crh : 0;
    for (size_t j = 0; j < 8; j++) { wrw_ent *e = &g_wrw[(hix + j) & 4095]; if (e->gpu == gpu && e->size == n && e->keep == keep && e->crh == crh) { *ngpu = e->ngpu; *nb = e->nb; return e->fl; } if (!e->gpu) break; }
    id<MTLBuffer> b = (__bridge id<MTLBuffer>)mb; const uint8_t *src = (const uint8_t *)b.contents + (gpu - b.gpuAddress);
    uint64_t hs = wrw_hash_slots(src, n, rb); wrc_ent *ce = u->addr ? wrc_slot(u->addr, n, keep, crh) : NULL; int fl;
#ifndef DM_TEST_WIDEN_RW_REUSE_STALE   // negative control of t_widen_draw only: the content not compared (a changed block given its old result)
    int seen = ce && ce->addr == u->addr && ce->size == n && ce->keep == keep && ce->crh == crh && ce->hash == hs;   // scanned before with this content
#else
    int seen = ce && ce->addr == u->addr && ce->size == n && ce->keep == keep && ce->crh == crh;
#endif
#ifndef DM_TEST_WIDEN_RW_NO_REUSE   // negative control of t_widen_draw only: every block scanned
    if (seen) {
        fl = ce->fl; g_wrw_hits++;
        if (ce->np) { ARENA_OUT id<MTLBuffer> ab; size_t off = arena_alloc(n, &ab); uint8_t *o = g_cur_arena->cpu + off; memcpy(o, src, n);
                      for (int i = 0; i < ce->np; i++) memcpy(o + ce->po[i], &ce->pv[i], 4);
                      *ngpu = g_cur_arena->gpu + off; *nb = ab; g_wrw_copies++; }
    } else
#endif
    {
        static uint8_t scratch[65536]; wrw_hits h = {{0}};
        size_t looked = n; int c = wrw_block_r(widen_get(), src, n, scratch, &h, keep, rb ? (const uint16_t (*)[2])rb->cr : NULL, rb ? rb->ncr : 0, &looked);
        g_wrw_scans++; g_wrw_bytes += looked;
        fl = (h.n[WRW_PROJ] || h.n[WRW_VIEWPROJ] || h.n[WRW_INVPROJ] ? 1 : 0) | (h.n[WRW_UI_ORTHO] ? 2 : 0);
        int nr = rb ? rb->ncr : 0;   // (320g: with the slots read, scratch holds only their ranges and the 48 bytes after each)
        if (c) { ARENA_OUT id<MTLBuffer> ab; size_t off = arena_alloc(n, &ab); uint8_t *o = g_cur_arena->cpu + off;
                 if (!nr) memcpy(o, scratch, n);
                 else { memcpy(o, src, n); for (int r = 0; r < nr; r++) { size_t lo = (size_t)rb->cr[r][0] * 16, e = ((size_t)rb->cr[r][1] + 1) * 16 + 48; if (lo >= n) continue; if (e > n) e = n; memcpy(o + lo, scratch + lo, e - lo); } }
                 *ngpu = g_cur_arena->gpu + off; *nb = ab; g_wrw_copies++; }
        if (ce) {   // kept for the next submissions: the words that differ
            int np = 0;
            for (int r = 0; c && r < (nr ? nr : 1) && np <= 32; r++) {
                uint32_t lo = nr ? (uint32_t)rb->cr[r][0] * 16 : 0, e = nr ? ((uint32_t)rb->cr[r][1] + 1) * 16 + 48 : n; if (lo >= n) continue; if (e > n) e = n;
                for (uint32_t i = lo; i + 4 <= e && np <= 32; i += 4) { uint32_t a, b2; memcpy(&a, src + i, 4); memcpy(&b2, scratch + i, 4); if (a != b2) { if (np < 32) { ce->po[np] = i; ce->pv[np] = b2; } np++; } } }
            if (np <= 32) { ce->addr = u->addr; ce->size = n; ce->hash = hs; ce->fl = fl; ce->np = np; ce->keep = keep; ce->crh = crh; } else ce->addr = 0;
        }
    }
    for (size_t j = 0; j < 8; j++) { wrw_ent *e = &g_wrw[(hix + j) & 4095]; if (!e->gpu) { *e = (wrw_ent){gpu, *ngpu, n, crh, fl, keep, *nb}; break; } }
    return fl;
}
void mtl_widen_rw_stats(uint64_t out[4]) { out[0] = g_wrw_scans; out[1] = g_wrw_copies; out[2] = g_wrw_bytes; out[3] = g_wrw_hits; }
void mtl_widen_scan_stats(uint64_t out[10]) {   // draws reading a projection to change / only a HUD orthographic / neither; kinds found; bytes scanned
    out[0] = g_ws_n[0]; out[1] = g_ws_n[1]; out[2] = g_ws_n[2]; for (int k = 0; k < WRW_N; k++) out[3 + k] = g_ws_kind[k]; out[9] = g_ws_bytes;
}
// ---------- Draws that sample what their own pass draws (plan item 42; after tomodachi-native's attachment copies 4dbd30f / da08d11, MIT) ----------
// Tears of the Kingdom samples the level and layer a pass draws into in about 59 draws a frame in the prologue (full-screen passes at 1600x896,
// 800x448 and 400x224; DM_NVN_COUNTS). In a Metal render pass that is undefined; on the M5 such a draw reads the attachment as it was in memory
// before the pass: without the earlier draws of the pass (still in tile memory) and without a clear still pending when the pass opened. NVN defines
// only the case with a texture-invalidating barrier between the earlier draws and the read, which makes their results visible; without one, what
// the console shows is what the M5 reads: copying for every such draw (results of earlier draws and clears visible, tomodachi-native's choice for
// its game) put the prologue's shots outside the console's brightness range in two shots out of the video of the opening (s1300 / s1800: left
// third 39.0 against 44.2-54.5), the M5's own reads and the copies after a barrier inside it. So by default only a draw after such a barrier samples
// a copy: the pass ends there (the earlier draws stored), the whole texture is copied, the pass opens again loading it. DM_NVN_FBX=all copies for
// every such draw (a copy serves until a draw writes the attachment, a pending clear is applied first), DM_NVN_FBX=0 never. Every encoder starts
// with a full barrier, so one copy texture per shape is reused within a submission. Levels and layers the pass does not draw are not copied for
// (a downsample chain reading level k-1)
typedef struct { __strong id<MTLTexture> copy, src; uint64_t draw, sub; __strong id<MTLTexture> vsrc[4], view[4]; int nv; } fbx_ent;
static fbx_ent g_fbx[16]; static int g_nfbx;
static int g_fbx_act[9];   // this draw's: indices into g_fbx (g_fbx_nact of them, declared at rp_end), set when its pass is open (fbx_activate)
static int g_fbx_pend[9], g_fbx_npend;   // fbx_prepare's picks for the draw being prepared
static int g_fbx_on = -1;   // 0 off, 1 after a texture barrier (default), 2 all, 3 split (DM_NVN_FBX=split, plan item 165)
static uint64_t g_fbx_bar_at;   // g_stat_draw at the last barrier that invalidates textures (NVN_BARRIER_INVALIDATE_TEXTURE_BIT)
static uint64_t g_fbx_copies, g_fbx_draws, g_fbx_refused, g_fbx_splits, g_fbx_split_clears;
void mtl_fbx_stats(uint64_t out[3]) { out[0] = g_fbx_draws; out[1] = g_fbx_copies; out[2] = g_fbx_refused; }
void mtl_fbx_split_stats(uint64_t out[2]) { out[0] = g_fbx_splits; out[1] = g_fbx_split_clears; }
// The bound texture t, or the same view of it, on the copy of the attachment it is (nil: not this draw's attachment)
static id<MTLTexture> fbx_sub(id<MTLTexture> t, uint64_t *rid) {
    for (int a = 0; a < g_fbx_nact; a++) { fbx_ent *e = &g_fbx[g_fbx_act[a]];
        if (t == e->src) { *rid = e->copy.gpuResourceID._impl; return e->copy; }
        if (t.parentTexture != e->src) continue;
        for (int i = 0; i < e->nv; i++) if (e->vsrc[i] == t) { *rid = e->view[i].gpuResourceID._impl; return e->view[i]; }
        NSUInteger sl = t.textureType == MTLTextureTypeCube || t.textureType == MTLTextureTypeCubeArray ? t.arrayLength * 6 : t.textureType == MTLTextureType3D ? 1 : t.arrayLength;
        id<MTLTexture> v = [e->copy newTextureViewWithPixelFormat:t.pixelFormat textureType:t.textureType levels:NSMakeRange(t.parentRelativeLevel, t.mipmapLevelCount)
                                                           slices:NSMakeRange(t.textureType == MTLTextureType3D ? 0 : t.parentRelativeSlice, sl) swizzle:t.swizzle];
        if (!v) { RED_ONCE(96, "attachment copy: no view of the copy like the bound one (format %lu type %lu); the draw samples the attachment", (unsigned long)t.pixelFormat, (unsigned long)t.textureType); return nil; }
        int k = e->nv < 4 ? e->nv++ : 3; if (e->view[k]) defer_obj_unref(e->view[k]);
        e->vsrc[k] = t; e->view[k] = v; *rid = v.gpuResourceID._impl; return v;
    }
    return nil;
}
// 0 when a texture's lookup ended the render pass en belongs to (a texture upgraded for a format view, tex_upgrade_pfv: blit() ends it): nothing
// more is encoded on it, the draw begins again in a new pass (draw.inc, plan item 296; on Metal 3 the next useResource on it crashed in the driver)
#ifndef DM_TEST_DRAW_ON_ENDED   // negative control of t_argbind only: encoded on as before
#define ARGS_PASS_GONE() do { if (nvn_stage != 5 && (__bridge void *)g_enc != (__bridge void *)en) { pthread_mutex_unlock(&g_rmu); return 0; } } while (0)   // (USE runs under g_rmu)
#else
#define ARGS_PASS_GONE() do { } while (0)
#endif
static int encode_args(id<MTLCommandEncoder> en, NvShader *sh, int nvn_stage, int metal_vertex) {
    if (!sh->arg_len && !sh->ae) return 1;   // no argument buffer (ae: tests that build a shader by hand, its length from Metal below)
    uint64_t slots[NVN_REFL_MAXB] = {0};
    args_sync_dirty();
    if (!passdump() && !g_args_dirty[nvn_stage] && sh->ab_buf && sh->ab_pass == g_pass.serial && nvn_stage != 5 && !sh->rf.ubo_addr) {   // ubo_addr: an address read from uniform data can change without a binding change
        g_wd_fl |= sh->ab_wfl;
        bind_args(en, nvn_stage, metal_vertex, sh->ab_buf, sh->ab_gpu, sh->ab_off, sh->rf.abi);
        g_stat_args_reuse++;
        return 1;
    }
#if DM_NVN_M3
    // Older API: every resource a shader accesses indirectly through an argument block must have its usage declared on this encoder, so that the system knows its ordering dependency against other encoders (upload copies, other passes);
    // without the declaration a read-only texture gets out of order with the copy that writes it (measured before the move to Metal 4). Repeated declarations within one encoder are idempotent
    #define USE(r, u) do { ARGS_PASS_GONE(); id<MTLResource> r_ = (id<MTLResource>)(r); if (r_) { if (nvn_stage == 5) [(id<MTLComputeCommandEncoder>)en useResource:r_ usage:(u)]; else if (!m3_use_seen((__bridge void *)r_, (uint32_t)(u))) [(id<MTLRenderCommandEncoder>)en useResource:r_ usage:(u) stages:MTLRenderStageVertex | MTLRenderStageFragment]; } \
        if (__builtin_expect(g_res_check_on, 0)) res_check((r), sh->key, reg); } while (0)
    #define USE_SAMP(r, u) do { (void)(u); if (__builtin_expect(g_res_check_on, 0)) res_check((r), sh->key, reg); } while (0)   // samplers are not resources and need no declaration
#else
    #define USE(r, u) do { ARGS_PASS_GONE(); (void)(u); if (__builtin_expect(g_res_check_on, 0)) res_check((r), sh->key, reg); } while (0)
    #define USE_SAMP(r, u) USE(r, u)
#endif
    // DM_NVN_PASSDUMP: on the first new encode per pass per stage, records the shader key (K) and the actual source of each binding (B: UBO address / length / first 4 floats, texture ID and serial, or "dummy")
    static uint64_t bl_pass[6]; static int bl_all = -1; if (bl_all < 0) bl_all = getenv("DM_NVN_PASSDUMP_ALL") != NULL;   // Record on every new encode (otherwise only the first time per pass per stage)
    int bl = passdump() && (bl_all || bl_pass[nvn_stage] != g_stat_pass);
    if (bl) { bl_pass[nvn_stage] = g_stat_pass; PD("K %llu st%d key=%s nb=%d", g_stat_pass, nvn_stage, sh->key, sh->rf.nb); }
    int wfl = 0;   // what this stage's uniform blocks held for the widening (320d)
    int sup_aid = -1, nsc = 0; uint8_t sc_ix[NVN_REFL_MAXB]; float sc_x[NVN_REFL_MAXB], sc_y[NVN_REFL_MAXB];   // the support buffer's slot, the sampled textures' storage ratios (320f)
    pthread_mutex_lock(&g_rmu);   // The texture / sampler registration tables may be being grown or rewritten by the game thread
    for (int i = 0; i < sh->rf.nb; i++) {
        if (__builtin_expect(widen_scan_on(), 0) && sh->rf.b[i].kind == NVN_RB_CBUF && sh->rf.b[i].reg < 16) widen_scan_cb(&S.ubo[nvn_stage][sh->rf.b[i].reg]);   // plan item 320d
        const nvn_rbind *b = &sh->rf.b[i];
        int reg = b->reg, aid = b->aid;
        if (b->kind == NVN_RB_CBUF) {
            const p_bind_buf *u = reg < 16 ? &S.ubo[nvn_stage][reg] : NULL;
            void *mb; uint64_t mo, gpu;
            if (!u || !u->addr || !mtl_gpu_addr(u->addr, 1, &mb, &mo, &gpu)) { RED_ONCE(16, "stage %d cb%d has no uniform buffer bound", nvn_stage, reg); continue; }
            use_rec("buf", u->addr, u->size, 0, NULL);
            if (bl) {
                PD("B %llu st%d cb reg%d addr=%#llx size=%llu pool=%#llx+%#llx f=%g,%g,%g,%g", g_stat_pass, nvn_stage, reg, (unsigned long long)u->addr, (unsigned long long)u->size,
                   (unsigned long long)(u->addr - mo), (unsigned long long)mo, ((float *)u->addr)[0], ((float *)u->addr)[1], ((float *)u->addr)[2], ((float *)u->addr)[3]);
                // DM_NVN_CBDUMP=directory: also saves the whole uniform block to disk as-is (F<frame>_p<pass>_st<stage>_cb<slot>_<address>.bin), for the ground-truth side to compare byte by byte by guest address
                static const char *cbd; static int cbd_init; if (!cbd_init) { cbd = getenv("DM_NVN_CBDUMP"); cbd_init = 1; }
                if (cbd) {
                    char p[1024]; snprintf(p, sizeof p, "%s/F%llu_p%llu_st%d_cb%d_%llx.bin", cbd, (unsigned long long)atomic_load(&g_cur_frame), g_stat_pass, nvn_stage, reg, (unsigned long long)u->addr);
                    FILE *f = fopen(p, "wb"); if (f) { fwrite((const void *)u->addr, 1, u->size, f); fclose(f); } else RED_ONCE(30, "CBDUMP cannot write %s", p);
                }
            }
            void *rb_ = NULL; uint64_t rg_ = upd_any() ? upd_shadow(u->addr, u->size, &rb_) : 0;   // logged updates over it: its private copy
            const us_ent *sc = rg_ ? NULL : us_find(u->addr, u->size);   // contents as of the submit call
            if (rg_) { gpu = rg_; mb = rb_; } else if (sc) { gpu = sc->gpu + (u->addr - sc->lo); mb = sc->buf; }
            if (__builtin_expect(g_pass.kx != 1 || g_pass.ky != 1, 0) && nvn_stage != 5) {   // a widened target (320d): a rewritten copy when the block holds what changes
                uint64_t ng; id<MTLBuffer> __unsafe_unretained nb; wfl |= widen_cb_rewrite(u, gpu, mb, &ng, &nb, nvn_stage == 1 && sh->rf.frag_coord, b);
                if (ng) { gpu = ng; mb = (__bridge void *)nb; }
            }
            slots[aid] = gpu;
            USE((__bridge id<MTLBuffer>)mb, MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_SAMP || b->kind == NVN_RB_SAMPT) {   // SAMPT: the sampler part of a combined handle (texture ID | sampler ID << 20)
            int sid = reg < 128 ? h_sid(b->kind == NVN_RB_SAMPT ? S.tex[nvn_stage][reg] : S.samp[nvn_stage][reg]) : 0;
            __unsafe_unretained id<MTLSamplerState> ss = sid ? (__bridge id<MTLSamplerState>)samp_fast(sid) : nil;
            slots[aid] = sid && sid < g_nsreg && g_srid[sid] ? g_srid[sid] : dummy_samp_rid();
            USE_SAMP(ss ? ss : dummy_samp(), MTLResourceUsageRead);
            if (bl && sid > 0) { int ok = sid < g_nsreg && g_sreg_ok[sid]; samp_par sp = ok ? g_sreg[sid] : (samp_par){0};   // g_rmu is already held in this section
                if (ok) PD("B %llu st%d samp reg%d sid=%d mag=%d min=%d (0 nearest / 1 bilinear; 2–5 with mip) lod %g..%g bias %g wrap %d,%d", g_stat_pass, nvn_stage, reg, sid, (int)sp.magf, (int)sp.minf, sp.lod_min, sp.lod_max, sp.lod_bias, (int)sp.ws, (int)sp.wt); }
        } else if (b->kind == NVN_RB_TEX) {
            int tid = reg < 128 ? h_tid(S.tex[nvn_stage][reg]) : 0;
            void *tv = NULL; uint64_t trid = tid ? tex_fast_rid(tid, &tv) : 0; __unsafe_unretained id<MTLTexture> t = (__bridge id<MTLTexture>)tv;
            if (__builtin_expect(widen_get()->k != 1 || widen_get()->ky != 1, 0) && t && tid > 0 && tid < g_ntreg) {   // the widening (320d-320g)
                // This texture's storage ratio at its view's level, kept with the registration while this texture is bound and no storage changed
                // (320g: no Metal call a binding); widened when over 1; its flags and its scale come from it
                treg_t *tr = &g_treg[tid];
                if (tr->wr_rid != trid || tr->wr_gen != g_ser_gen) {
                    unsigned lv = tr->has_v && tr->v.set_levels ? tr->v.base_level : 0;
                    tr->wr_x = (float)((double)t.width / mip_extent(tr->t.d.width ? tr->t.d.width : 1, lv)); tr->wr_y = (float)((double)t.height / mip_extent(tr->t.d.height ? tr->t.d.height : 1, lv));
                    tr->wr_rid = trid; tr->wr_gen = g_ser_gen; }
                int wide = tr->wr_x > 1 || tr->wr_y > 1, pass_w = g_pass.kx != 1 || g_pass.ky != 1;
#ifndef DM_TEST_WIDEN_SAMPLED_HUD   // negative control of t_widen_draw only: a draw sampling a widened texture taken for the HUD when it reads its projection
                if (wide && pass_w) wfl |= 4;   // samples a widened texture (320d)
#endif
                if (wide && !pass_w && nvn_stage != 5) wfl |= 8;   // the census (320e)
                if (pass_w && nvn_stage == 1 && (tr->t.d.width > 8 || tr->t.d.height > 8)) wfl |= 16;   // larger than 8x8 (320f: its HUD draw not pushed)
                if (b->six && (tr->wr_x != 1 || tr->wr_y != 1) && nsc < NVN_REFL_MAXB) { sc_ix[nsc] = b->six; sc_x[nsc] = tr->wr_x; sc_y[nsc] = tr->wr_y; nsc++; }   // its scale (320f)
            }
            if (t && tid < g_ntreg) use_rec("tex", (uint64_t)g_treg[tid].t.mem, g_treg[tid].t.size, tid, &g_treg[tid].t);
            if (t && passdump() && tid < g_ntreg) PD("R %llu st%d ser=%llu", g_stat_pass, nvn_stage, g_treg[tid].t.serial);
            if (bl) PD("B %llu st%d tex reg%d tid=%d %s ser=%llu fmt=%lu %lux%lux%lu view_lv=%lu+%lu view_sl=%lu array=%lu target=%lu", g_stat_pass, nvn_stage, reg, tid, t ? "real" : "dummy", t && tid < g_ntreg ? g_treg[tid].t.serial : 0ull,
                       (unsigned long)t.pixelFormat, (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.depth,
                       (unsigned long)(t.parentTexture ? t.parentRelativeLevel : 0), (unsigned long)t.mipmapLevelCount,
                       (unsigned long)(t.parentTexture ? t.parentRelativeSlice : 0), (unsigned long)t.arrayLength, (unsigned long)t.textureType);
            if (bl && t && tid < g_ntreg && g_treg[tid].t.mem && getenv("DM_NVN_TEXNZ")) {   // diagnostic: share of non-zero 8-byte words in the texture's guest storage
                const uint64_t *w = (const uint64_t *)(const void *)g_treg[tid].t.mem; size_t nw = MIN((size_t)g_treg[tid].t.size, (size_t)64 << 20) / 8, nz = 0;
                for (size_t i = 0; i < nw; i++) nz += w[i] != 0;
                PD("B %llu st%d tex reg%d storage %p size %#llx non-zero words %.4f", g_stat_pass, nvn_stage, reg, (const void *)g_treg[tid].t.mem, (unsigned long long)g_treg[tid].t.size, nw ? (double)nz / nw : 0.0);
                if (t.pixelFormat >= MTLPixelFormatBC1_RGBA && t.pixelFormat <= MTLPixelFormatBC7_RGBAUnorm_sRGB && t.textureType == MTLTextureType2D) {   // and of the Metal texture's level 0 (read back now, from a command buffer of its own)
                    size_t blk = t.pixelFormat <= MTLPixelFormatBC1_RGBA_sRGB || t.pixelFormat == MTLPixelFormatBC4_RUnorm || t.pixelFormat == MTLPixelFormatBC4_RSnorm ? 8 : 16;
                    NSUInteger tw = t.width, th = t.height; size_t row = (tw + 3) / 4 * blk, img = row * ((th + 3) / 4);
                    id<MTLBuffer> rb = [g_dev newBufferWithLength:img options:MTLResourceStorageModeShared];
                    id<MTLCommandBuffer> rc = q_cb(); id<MTLBlitCommandEncoder> be = [rc blitCommandEncoder];
                    [be copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(tw, th, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:img];
                    [be endEncoding]; [rc commit]; [rc waitUntilCompleted];
                    const uint64_t *mw = (const uint64_t *)rb.contents; size_t mn = img / 8, mz = 0; for (size_t i = 0; i < mn; i++) mz += mw[i] != 0;
                    PD("B %llu st%d tex reg%d Metal level 0 %lux%lu non-zero words %.4f", g_stat_pass, nvn_stage, reg, (unsigned long)tw, (unsigned long)th, mn ? (double)mz / mn : 0.0);
                }
            }
            if (t && t.textureType != (MTLTextureType)b->decl_tt) { ttm_note(sh->key, nvn_stage, reg, (MTLTextureType)b->decl_tt, t, tid);
                // A 3D slot holding another type (Batman: Arkham Knight samples a volume where 2D render targets were left bound): Metal makes no 3D view
                // of it, so the zero 3D texture (seedds/nxnative 2bccdcc, MIT; plan item 296); what the Switch reads there is not known
#ifndef DM_TEST_3D_SLOT_AS_IS   // negative control of t_argbind only: bound as it is, as before
                if (b->decl_tt == MTLTextureType3D) { RED_ONCE(101, "texture ID %d (type %lu) in a 3D slot: the zero 3D texture bound (first case)", tid, (unsigned long)t.textureType); t = nil; }
                else
#endif
                { uint64_t r2 = 0; id<MTLTexture> fv = tex_fit(tid, t, (MTLTextureType)b->decl_tt, &r2); if (fv) { t = fv; trid = r2; } } }
            if (t) t = fxp_sub(nvn_stage, tid, t, &trid);   // the compose pass: the scene MetalFX upscaled (plan item 63)
            if (t && g_fbx_nact) { uint64_t r2 = 0; id<MTLTexture> c = fbx_sub(t, &r2); if (c) { t = c; trid = r2; } }   // the draw samples its own attachment: the copy
            if (!t) { t = dummy_tex(b->dtt, b->dpf); trid = g_dummy_tex_rid[b->dtt & 7][b->dpf & 3]; }
            slots[aid] = trid;
            USE(t, MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_IMG) {   // Writable texture: NVN image slot r (BindImages) ↔ D3D u_r
            int tid = reg < 32 ? h_tid(S.img[nvn_stage][reg]) : 0;
            if (b->rw && tid && tid < g_ntreg) tex_gpu_write(&g_treg[tid].t);   // an image store writes the texture
            void *tv = NULL; uint64_t trid = tid ? tex_fast_rid(tid, &tv) : 0; __unsafe_unretained id<MTLTexture> t = (__bridge id<MTLTexture>)tv;
            if (passdump()) PD("W %llu st%d reg%d tid=%d %s ser=%llu", g_stat_pass, nvn_stage, reg, tid, t ? "real" : "dummy", t && tid < g_ntreg ? g_treg[tid].t.serial : 0ull);
            if (t && !(t.usage & MTLTextureUsageShaderWrite)) { RED_ONCE(32, "texture bound to an image slot lacks the shader-write usage (texture ID %d)", tid); t = nil; }
            if (!t) { t = dummy_img(b->dtt, b->dpf); trid = g_dummy_img_rid[b->dtt & 7][b->dpf & 3]; }   // Unbound: D3D semantics discard the write - write into a dedicated dummy texture so the set that reads 0 is not polluted
            slots[aid] = trid;
            USE(t, MTLResourceUsageRead | MTLResourceUsageWrite);
        } else if (b->kind == NVN_RB_TBUF) {   // Typed buffer: the buffer texture (target BUFFER) in texture slot r
            int tid = reg < 128 ? h_tid(S.tex[nvn_stage][reg]) : 0;
            void *tv = NULL; uint64_t trid = tid ? tex_fast_rid(tid, &tv) : 0; __unsafe_unretained id<MTLTexture> t = (__bridge id<MTLTexture>)tv;
            if (t && t.textureType != MTLTextureTypeTextureBuffer) { RED_ONCE(51, "texture bound to a typed buffer slot is not a buffer texture (texture ID %d type %lu)", tid, (unsigned long)t.textureType); t = nil; }
            if (bl) PD("B %llu st%d tbuf reg%d tid=%d %s ser=%llu", g_stat_pass, nvn_stage, reg, tid, t ? "real" : "dummy", t && tid < g_ntreg ? g_treg[tid].t.serial : 0ull);
            if (!t) { t = dummy_tbuf(b->dpf); trid = g_dummy_tbuf_rid[b->dpf & 3]; }
            slots[aid] = trid;
            USE(t, MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_SBUFP || b->kind == NVN_RB_UBOP) {   // B: storage buffers / compute-stage uniforms are device pointers; the address is taken by NVN slot
            const p_bind_buf *u = reg < 16 ? (b->kind == NVN_RB_UBOP ? &S.ubo[nvn_stage][reg] : &S.ssbo[nvn_stage][reg]) : NULL;
            void *mb; uint64_t mo, gpu;
            if (b->kind == NVN_RB_SBUFP && b->rw && u && u->addr) us_drop(u->addr, sbuf_extent(u->addr, u->size));   // the shader may write it
            void *rb_ = NULL; uint64_t rg_ = b->kind == NVN_RB_UBOP && u && u->addr && upd_any() ? upd_shadow(u->addr, u->size, &rb_) : 0;   // with the submission's updates (see upd_any)
            const us_ent *sc = !rg_ && b->kind == NVN_RB_UBOP && u ? us_find(u->addr, u->size) : NULL;   // compute uniforms as of the submit call
            if (rg_) { slots[aid] = rg_; USE((__bridge id<MTLBuffer>)rb_, MTLResourceUsageRead); }
            else if (sc) { slots[aid] = sc->gpu + (u->addr - sc->lo); USE((__bridge id<MTLBuffer>)sc->buf, MTLResourceUsageRead); }
            else if (u && u->addr && (b->kind == NVN_RB_SBUFP ? sbuf_gpu_addr(u->addr, u->size, &mb, &mo, &gpu) : mtl_gpu_addr(u->addr, u->size ? u->size : 1, &mb, &mo, &gpu))) { use_rec("buf", u->addr, u->size, 0, NULL); slots[aid] = gpu; USE((__bridge id<MTLBuffer>)mb, b->rw ? MTLResourceUsageRead | MTLResourceUsageWrite : MTLResourceUsageRead); }
            else { slots[aid] = dummy_buf_gpu(); USE(dummy_buf(), MTLResourceUsageRead); }
            if (bl) PD("B %llu st%d sbufp reg%d %s addr=%#llx size=%llu %s", g_stat_pass, nvn_stage, reg, u && u->addr ? "real" : "dummy", u ? (unsigned long long)u->addr : 0ull, u ? (unsigned long long)u->size : 0ull, b->rw ? "rw" : "ro");
        } else if (b->kind == NVN_RB_SBUFU) {   // B: a storage buffer the shader addresses from uniform slot r: {address u64, size u32} at byte off (the translator's cb slot r + 3)
            const p_bind_buf *u = reg < 16 ? &S.ubo[nvn_stage][reg] : NULL; uint8_t rec[12] = {0}; uint64_t sa = 0; uint32_t sn = 0;
            int ok = u && u->addr && b->off + 12 <= u->size && pend_read(u->addr + b->off, rec, 12);
            if (u && u->addr && !ok) RED_ONCE(77, "storage buffer address in uniform slot %d +%#x: a GPU copy earlier in the submission writes there, or it lies outside the binding (first case): dummy buffer", reg, b->off);
            if (ok) { memcpy(&sa, rec, 8); memcpy(&sn, rec + 8, 4); }
            if (sa && b->rw) us_drop(sa, sn ? sn : 65536);   // the shader may write it (no size given: 64 KiB)
            void *mb; uint64_t mo, gpu;
            if (sa && sbuf_gpu_addr(sa, sn, &mb, &mo, &gpu)) { use_rec("buf", sa, sn, 0, NULL); slots[aid] = gpu; USE((__bridge id<MTLBuffer>)mb, b->rw ? MTLResourceUsageRead | MTLResourceUsageWrite : MTLResourceUsageRead); }
            else { if (sa) RED_ONCE(78, "storage buffer address %#llx read from uniform slot %d +%#x is not in any memory pool (first case): dummy buffer", (unsigned long long)sa, reg, b->off);
                   slots[aid] = dummy_buf_gpu(); USE(dummy_buf(), MTLResourceUsageRead); }
            if (bl) PD("B %llu st%d sbufu ubo%d+%#x addr=%#llx size=%u %s", g_stat_pass, nvn_stage, reg, b->off, (unsigned long long)sa, sn, b->rw ? "rw" : "ro");
        } else if (b->kind == NVN_RB_C1) {      // B: the shader's own constant segment, built when the shader is fetched
            id<MTLBuffer> cb = sh->c1buf ? sh->c1buf : zero_bank();   // c1 has length 0 (600 of them in the whole library) yet the shader still declares it: reads 0
            slots[aid] = (sh->c1buf ? sh->c1_gpu : zero_bank_gpu()) + (sh->c1buf ? sh->c1off : 0);
            USE(cb, MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_SUPPORT) {   // support_buffer: scale 1.0, BGRA swap 0, viewport transform not disabled (viewport fields are not read)
            slots[aid] = support_gpu(); sup_aid = aid;   // (a copy with this draw's scales after the loop, 320f)
            USE(support_buf(), MTLResourceUsageRead);
        } else if (b->kind == NVN_RB_DRV0 || b->kind == NVN_RB_DRV2) {
            // NVN driver constant segments: those proven unread by static analysis at packaging time are supplied as 0; those read are filled on the spot from the current bindings - c0 = storage buffer table (base + 0x10 r: address 64 bits, size 32 bits),
            // c2 = texture handle table and sampler handle table (bases from the game's nvn_c2, 8 bytes per slot), the values being the NVN handles the game gave at bind time; positions read outside the tables are registered at packaging time and reported a failure at load time
            int bank = b->kind == NVN_RB_DRV2 ? 2 : 0;
            if (!sh->rf.drv_read[bank]) { slots[aid] = zero_bank_gpu(); USE(zero_bank(), MTLResourceUsageRead); }
            else {
                ARENA_OUT id<MTLBuffer> db; size_t doff = arena_alloc(0xA00, &db); uint8_t *d = g_cur_arena->cpu + doff;
                drv_fill(d, bank, nvn_stage);
                if (bank == 0) for (int r = 0; r < 16 && nvn_stage < 6; r++) { uint64_t a = S.ssbo[nvn_stage][r].addr; uint32_t n = (uint32_t)S.ssbo[nvn_stage][r].size;
                    if (a && n) us_drop(a, n); }   // reached through the table: whether the shader writes it is not known
                slots[aid] = g_cur_arena->gpu + doff;
                USE(db, MTLResourceUsageRead);
            }
        } else RED_ONCE(46, "binding kind %d not implemented", b->kind);
    }
    // The resolution scale per draw (plan item 320f; Ryujinx's render_scale, as S2Arm's direct path fills it): a draw widened into a widened target
    // (not the HUD's, centred) divides the fragment coordinate by the target's k (x) and ky (y), so a shader reading it sees the game's pixels;
    // each sampled texture with widened storage has its ratio for its scale index (integer reads times it, size queries divided by it), negative
    // in the fragment stage when it is the target's (Ryujinx: the read at the pixel's own texel). A copy of the static buffer for the draw, else
    // the static one (every scale 1). DM_TEST_WIDEN_SCALE_ONE (t_widen_scale only): the static one always
#ifndef DM_TEST_WIDEN_SCALE_ONE
    if (sup_aid >= 0 && __builtin_expect(widen_get()->k != 1 || widen_get()->ky != 1, 0)) {
        int frag = nvn_stage == 1 && (g_pass.kx != 1 || g_pass.ky != 1) && ((g_wd_fl | wfl) & 7) != 2;
        int push = nvn_stage == 0 && g_wd_push_on && (g_pass.kx != 1 || g_pass.ky != 1);   // the vertex edge push (draw.inc): (1 − 1/k, ((1 − 3‰)/k)², 1 − 1/k) an axis
        if (frag || nsc || push) {
            ARENA_OUT id<MTLBuffer> sb; size_t so = arena_alloc(SUPPORT_LEN, &sb); uint8_t *d = g_cur_arena->cpu + so; memcpy(d, support_buf().contents, SUPPORT_LEN);
            float kx = frag ? (float)g_pass.kx : 1.0f, ky = frag ? (float)g_pass.ky : 1.0f;
            memcpy(d + 192, &kx, 4); memcpy(d + SUPPORT_RENDER_SCALE_Y, &ky, 4);
            if (push && g_pass.kx != 1) { float k = (float)g_pass.kx, pp[3] = {1 - 1 / k, (0.997f / k) * (0.997f / k), 1 - 1 / k}; memcpy(d + SUPPORT_WIDEN_PUSH, pp, sizeof pp); }
            if (push && g_pass.ky != 1) { float k = (float)g_pass.ky, pp[3] = {1 - 1 / k, (0.997f / k) * (0.997f / k), 1 - 1 / k}; memcpy(d + SUPPORT_WIDEN_PUSH_Y, pp, sizeof pp); }   // (320g: a taller screen)
            for (int i = 0; i < nsc; i++) {
                float rx = frag && sc_x[i] != 1.0f && fabsf(sc_x[i] - kx) < 1e-6f ? -sc_x[i] : sc_x[i], ry = frag && sc_y[i] != 1.0f && fabsf(sc_y[i] - ky) < 1e-6f ? -sc_y[i] : sc_y[i];
                memcpy(d + 192 + 16 * sc_ix[i], &rx, 4); memcpy(d + SUPPORT_RENDER_SCALE_Y + 16 * sc_ix[i], &ry, 4);
            }
            slots[sup_aid] = g_cur_arena->gpu + so; { int reg = -1; (void)reg; USE(sb, MTLResourceUsageRead); } g_wce[WCE_SCALED] += nvn_stage == 1;
        }
    }
#endif
    #undef USE
    #undef USE_SAMP
    pthread_mutex_unlock(&g_rmu);
    NSUInteger arg_len = sh->arg_len ? sh->arg_len : sh->ae.encodedLength;
    ARENA_OUT id<MTLBuffer> ab; size_t off = arena_alloc(arg_len, &ab);
    memcpy(g_cur_arena->cpu + off, slots, arg_len);
    bind_args(en, nvn_stage, metal_vertex, ab, g_cur_arena->gpu + off, off, sh->rf.abi);
    sh->ab_buf = ab; sh->ab_gpu = g_cur_arena->gpu + off; sh->ab_off = off; sh->ab_pass = g_pass.serial; g_args_dirty[nvn_stage] = 0; sh->ab_wfl = wfl; g_wd_fl |= wfl;
    g_stat_args_enc++;
    return 1;
}
static id<MTLBuffer> g_quad_ib; static uint64_t g_quad_gpu;
static id<MTLBuffer> g_qs_ib;   // quad strips' shared indices (draw.inc)
static RENC rp_begin(void);
