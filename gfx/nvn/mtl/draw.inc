// gfx/nvn/mtl/draw.inc, part of nvn_mtl.m (included there in order, one translation unit): Draws and dispatches (copies, texels and texture clears: copies.inc)
// The draw, in three stages: what keeps it from drawing or comes before it (draw_admit), the render pass it draws in (draw_pass), its encoding
// (draw_encode). draw() is also how deferred and merged draws are drawn again (defer_replay, defer_fallback, fz_check_run)
static id<MTLFunction> g_draw_fz_fn;   // Specialized fragment function for this draw of the later pass in a merged render pass (nil = use the original version)
static int draw_admit(const p_draw *dw, const p_draw_idx *di, NvProg *__unsafe_unretained *out) {
    if (exec_draw_dropped()) return 0;   // rendering off, no program, depth bounds that admit no depth (gfx/nvn/core/exec.c)
    __unsafe_unretained NvProg *pr = prog_get(S.prog); *out = pr;
#ifndef DM_TEST_DISCARD_DRAWN   // negative control of gfx/tests t_nvn64d only: drawn, as before
    if (S.raster_discard) {   // rasterizer discard (plan item 64d): nothing reaches the targets; a vertex stage that may write memory is reported, its writes lost
        __unsafe_unretained NvShader *vs = pr ? pr->sh[0] : nil;
        for (int i = 0; vs && i < vs->rf.nb; i++) if (vs->rf.b[i].kind == NVN_RB_IMG || vs->rf.b[i].kind == NVN_RB_SBUFP || vs->rf.b[i].kind == NVN_RB_SBUFU) {
            RED_ONCE(108, "rasterizer discard with a vertex shader that may write memory (%.16s): left out, its writes lost (first case)", vs->key); break; }
        return 0;
    }
#endif
    if (g_nupd && !g_defer_replaying) upd_guard_draw(pr, di);   // before its render pass is opened
    if (g_ruler_truth == 0) {   // Ruler negative control (DM_NVN_RULER_NEG=<fragment key>): in the mode under test drop this shader's draws, and the ruler must report non-equivalence
        static const char *neg; static int ng = -1; if (ng < 0) { neg = getenv("DM_NVN_RULER_NEG"); ng = neg && *neg; }
        if (ng && pr) for (int st = 0; st < 5; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (sh && sh->stage == 1 && !strncmp(sh->key, neg, strlen(neg))) { static int once; if (!once++) MLOG("DM_NVN_RULER_NEG: measured mode drops the draws of shader %s", sh->key); return 0; } }
    }
    if (zskip_can_draw(pr)) {   // Zero-source skip: the output is always 0, writing 0 over 0, so this draw changes no result (see zskip_can_draw)
        g_zskip_count++; { static int once; if (!once++) MLOG("zero-source skip: first case pass %llu", g_stat_pass + 1); }
        PD("N %llu zero-source skip", g_stat_pass + 1); return 0; }
    g_wide_gpu = 0;
    if (di && di->itype == 0 && di->count > 0 && !g_defer_replaying && pend_hits(di->ibuf, (uint64_t)di->count))   // see wide_u8_gpu; such a draw is not deferred
        g_wide_gpu = wide_u8_gpu(di, S.restart_set && S.restart_en && S.restart_idx == 0xff);
    if (!g_defer_replaying && !g_wide_gpu) {
        if (g_defer.active && g_defer.in_p && !g_defer.at_q) { if (defer_take(dw, di)) return 0; }
    }
    // Draws that cannot draw anything (both faces culled, empty scissor) return before the render pass is opened: otherwise the render pass is already open and on TBDR the whole attachment is loaded and stored for nothing
    // (Mac character select page: 3 render passes are opened only because of such draws, all loaded, zero draws)
    // Diagnostic switch for measuring an upper bound DM_NVN_SKIP_FS=<fragment key>[,<fragment key>...] (none by default): drops all draws of these fragment shaders (the picture will be wrong; only used in real-device ABA to measure "how much GPU time this class of draws is worth in total")
    { static char sk[16][20]; static int nsk = -1;
      if (nsk < 0) { nsk = 0; for (const char *q = getenv("DM_NVN_SKIP_FS"); q && *q && nsk < 16; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) { size_t n = strcspn(q, ","); if (n > 16) n = 16; memcpy(sk[nsk], q, n); sk[nsk++][n] = 0; }
                     if (nsk) MLOG("DM_NVN_SKIP_FS: dropping the draws of %d fragment shaders (diagnostic, rendering is wrong)", nsk); }
      if (g_ub.n > 0 && pr->sh[1] && !g_defer_replaying && ub_skip(pr->sh[1]->key)) return 0;   // In-process alternating measurement: this phase drops this class
      if (nsk && pr->sh[1] && !g_defer_replaying) for (int i = 0; i < nsk; i++) if (!strncmp(pr->sh[1]->key, sk[i], strlen(sk[i]))) return 0; }
    // Similar diagnostic DM_NVN_SKIP_SMALL=<width>: drop all draws whose target width (by the attached level) is <= this value -- measures how much "dropping tiny render passes in bulk" is worth under the parallel pipeline (fixed per-pass overhead)
    { static int small = -1; if (small < 0) { const char *e = getenv("DM_NVN_SKIP_SMALL"); small = e ? atoi(e) : 0; if (small) MLOG("DM_NVN_SKIP_SMALL: dropping draws whose target width is ≤ %d (diagnostic, rendering is wrong)", small); }
      if (small > 0 && R.has_rt && !g_defer_replaying) { int k = R.rt.n && R.rt.color[0] ? 0 : R.rt.depth ? 8 : -1;
        if (k >= 0) { uint64_t s_; int lv, sl; rt_sub(&R.rt, k, &s_, &lv, &sl); const tex_t *t = k == 8 ? R.rt.depth : R.rt.color[0]; if ((int)MAX(t->d.width >> lv, 1u) <= small) return 0; } } }
    if (S.poly.cull == NVN_FACE_FRONT_AND_BACK) {   // points and lines have no face (nvn_obj.h): drawn
        if (nvn_prim_has_face(dw ? dw->prim : di->prim)) return 0;
        static int once; if (!once++) MLOG("cull FRONT_AND_BACK on primitive %d (no face): drawn (first case, pass %llu)", dw ? dw->prim : di->prim, g_stat_pass); }
    if (g_up_fs[0][0] && !g_defer_replaying && fxp_dock(pr)) return 0;   // MetalFX wrote the canvas in this draw's place (plan item 63)
    if (g_fxp.sub) fxp_sub_end();
    if (g_up_fs[1][0] && !g_defer_replaying) fxp_scene(pr);   // MetalFX upscaled the scene this draw samples (plan item 63)
    return 1;
}
static id<MTLRenderCommandEncoder> draw_pass(__unsafe_unretained NvProg *pr, const p_draw *dw, const p_draw_idx *di) {
    // The continued render pass may have been opened with fewer depth / stencil attachments (open modes 2 / 3 / 4): if the plane this draw needs is not attached, split before it (on reopen, attach per the new lookahead)
    if ((g_rt_merged || (g_enc && g_ren.pass == g_pass.serial)) && g_enc_mode != 0 && (((S.ds.dt || S.ds.dw) && !g_pass.hd) || (S.ds.st && !g_pass.hs))) {   // (a pass that went on past a renamed update: its lookahead did too, this is a safety net)
        static int once2; if (!once2++) MLOG("same-target continuation: a draw in the continued pass needs a depth / stencil plane that is not attached, splitting here (first case pass %llu)", g_stat_pass);
        PD("M %llu a draw in the continued pass needs an unattached depth / stencil plane, splitting here", g_stat_pass); rp_end(); }
    if (g_lay.active && (!lay_prim_ok(dw ? dw->prim : di->prim) || lay_reads_target(pr))) { g_lay.splits++; g_lay.refuse = 1; rp_end(); }   // a draw a layered pass cannot hold: the pass ends, the next opens plain
    if (g_rt_merged && draw_samples_rt(pr)) { static int once; if (!once++) MLOG("same-target continuation: a draw in the continued pass samples an attachment, splitting here (first case pass %llu)", g_stat_pass);
        PD("M %llu a draw in the continued pass samples an attachment, splitting here", g_stat_pass); rp_end(); }
    if (g_fz.active && g_enc && g_fz.phase == 2 && !fz_p2_draw(pr, dw, di, g_fz.lmap2, &g_draw_fz_fn)) {   // Cannot be proven: end the merged render pass (the earlier pass's result is stored as usual) and reopen with the later pass's original targets
        static int once3; if (!once3++) MLOG("readable tile pass merge: a draw in the later pass is not provable, falling back to separate passes (first case pass %llu)", g_stat_pass);
        g_fz_fallback++; rp_end(); }
    if (!g_defer_replaying) fbx_prepare(pr, dw, di); else { g_fbx_self = 0; if (g_fbx_nact) { g_fbx_nact = 0; args_dirty_all(); } }   // a replayed deferred draw: not copied for, and it keeps the draw that replays it's picks
    if (R.has_sc && !g_enc) {
        NSUInteger tw0 = 0, th0 = 0; rt_dims(&tw0, &th0);
        long x0 = R.sc.x < 0 ? 0 : R.sc.x, y0 = R.sc.y < 0 ? 0 : R.sc.y, x1 = (long)R.sc.x + R.sc.w, y1 = (long)R.sc.y + R.sc.h;
        if (x1 > (long)tw0) x1 = tw0; if (y1 > (long)th0) y1 = th0;
        if (x1 <= x0 || y1 <= y0) return nil;
    }
    int defer_q_draw = !g_enc && !g_defer_replaying && g_defer_natq > 0;   // The draw that opens Q's render pass: after the render pass is opened as usual through hoist_try / fuse_try, replay P first and then draw this one
    if (!g_enc && tbdr_part(TBP_HOIST)) hoist_try();   // First hoist and execute the pure-clear render passes sandwiched between this render pass and its continuation (before rp_begin_ex, not occupying this render pass)
    // The deferral decision comes after hoist_try: hoisted pure-clear render passes are skipped in the lookahead, and only then can P's same-target continuation segment be recognized
    if (!g_enc && tbdr_part(TBP_DEFER) && !defer_has_in_p() && !g_defer_replaying && !g_fz.active && !g_fz_checking && g_exec_next) { defer_try(dw, di); if (g_defer_cur->active && g_defer_cur->in_p) return nil; }
    if (!g_enc && tbdr_part(TBP_FUSE) && !g_fz_checking) fuse_try(g_enc_mode == 1);   // Readable tile render pass merge is on by default; when the structure / same-pixel proof does not hold it still falls back to a normal render pass, and it is not re-entered during the CHECK redraw
    int need = g_enc || !tbdr_part(TBP_DS) ? 3 : pass_ds_need(), mode = defer_q_draw ? 0 : need == 0 ? 2 : need == 2 ? 3 : need == 1 ? 4 : 0;   // 2 attach neither, 3 attach stencil only, 4 attach depth only
    int skip_d = mode != 0;
    if (!g_enc) { g_fd_dw = dw; g_fd_di = di; }
    id<MTLRenderCommandEncoder> en = rp_begin_ex(mode);
    g_fd_dw = NULL; g_fd_di = NULL;
    if (!en) return nil;
    if (defer_q_draw) defer_replay();
    fbx_activate();
    if (skip_d) { g_stat_ds_omit++; if (mode == 3) g_stat_s_only++; else if (mode == 4) g_stat_d_only++; }
    return en;
}
static void draw_encode(id<MTLRenderCommandEncoder> en, __unsafe_unretained NvProg *pr, const p_draw *dw, const p_draw_idx *di) {
    // The pipeline depends only on (program, render pass targets, blend / color / write mask / multisample state, vertex layout): if these did not change, reuse the previous one
    static id<MTLRenderPipelineState> cur_ps; static NvProg *cur_pr; static uint64_t cur_ps_pass;
    if (g_fz.active && !g_defer_replaying) { g_pso_lmap = g_fz.phase == 1 ? g_fz.lmap1 : g_fz.lmap2; g_pso_fsfn = g_fz.phase == 1 ? nil : g_draw_fz_fn; g_pso_fz = g_fz.phase == 1 ? (pso_fz_desc){0} : g_fz_desc; g_pso_dirty = 1; }
    else if (g_fbx_fn && !g_defer_replaying) { g_pso_lmap = g_fbx_lmap; g_pso_fsfn = g_fbx_fn; g_pso_fz = g_fbx_desc; g_pso_dirty = 1; }   // the readable-tile version fbx_fetch picked (identity mapping)   // Readable tile merge: fetch the pipeline by the physical -> logical mapping and the specialization
    if (g_pso_dirty || pr != cur_pr || cur_ps_pass != g_pass.serial) { cur_ps = pso_get(pr, nil); cur_pr = pr; cur_ps_pass = g_pass.serial; g_pso_dirty = !cur_ps; }
    if (g_pso_lmap) { g_pso_lmap = NULL; g_pso_fsfn = nil; memset(&g_pso_fz, 0, sizeof g_pso_fz); g_pso_dirty = 1; }   // The next draw fetches again according to its own situation
    __unsafe_unretained id<MTLRenderPipelineState> ps = cur_ps;
    if (!ps) return;
    if (ps != g_enc_pso) { enc_r_pso(en, ps); g_enc_pso = ps; }
    static id<MTLDepthStencilState> cur_ds; static uint64_t cur_ds_pass;
    if (g_dss_dirty || cur_ds_pass != g_pass.serial) { cur_ds = dss_get(g_pass.hd, g_pass.hs); cur_ds_pass = g_pass.serial; g_dss_dirty = 0; }
    if (cur_ds != g_enc_dss) { enc_r_dss(en, cur_ds); g_enc_dss = cur_ds; }
    enc_r_stencil_ref(en, S.st_ref[0], S.st_ref[1]);
    if (g_lay.active && g_lay.amp != g_lay.cur - g_lay.first) { g_lay.amp = g_lay.cur - g_lay.first; enc_r_layer(en, (uint32_t)g_lay.amp); }   // this draw's layer in the layered pass
    // Viewport / scissor (clamped within the target size)
    NSUInteger tw = g_pass.tw, th = g_pass.th;
    if (S.has_vp) enc_r_viewport(en, (MTLViewport){S.vp[0][0], S.vp[0][1], S.vp[0][2], S.vp[0][3], S.dr[0], S.dr[1]});
    else enc_r_viewport(en, (MTLViewport){0, 0, tw, th, 0, 1});
    MTLScissorRect sc = {0, 0, tw, th};
    if (R.has_sc) {
        long x0 = R.sc.x < 0 ? 0 : R.sc.x, y0 = R.sc.y <= 0 ? 0 : R.sc.y, x1 = (long)R.sc.x + R.sc.w, y1 = (long)R.sc.y + R.sc.h;
        if (x1 > (long)tw) x1 = tw; if (y1 > (long)th) y1 = th;
        if (x1 <= x0 || y1 <= y0) return;
        sc = (MTLScissorRect){(NSUInteger)x0, (NSUInteger)y0, (NSUInteger)(x1 - x0), (NSUInteger)(y1 - y0)};
    }
    enc_r_scissor(en, sc);
    if (g_cnt_on > 0) { if (S.has_vp) cnt_extent(sc, S.vp[0][0], S.vp[0][1], S.vp[0][0] + S.vp[0][2], S.vp[0][1] + S.vp[0][3]); else cnt_extent(sc, 0, 0, tw, th); }
    if (S.poly.cull == NVN_FACE_FRONT_AND_BACK && nvn_prim_has_face(dw ? dw->prim : di->prim)) return;
    { static int nocull = -1; if (nocull < 0) { nocull = getenv("DM_NVN_NOCULL") != NULL; if (nocull) MLOG("DM_NVN_NOCULL: face culling off (diagnostic, rendering is wrong)"); }   // diagnostic: does a missing draw come back without culling?
      enc_r_cull(en, nocull ? MTLCullModeNone : S.poly.cull == NVN_FACE_FRONT ? MTLCullModeFront : S.poly.cull == NVN_FACE_BACK ? MTLCullModeBack : MTLCullModeNone); }
    enc_r_winding(en, S.poly.front == NVN_FRONT_FACE_CCW ? MTLWindingCounterClockwise : MTLWindingClockwise);
    enc_r_fill(en, S.poly.mode == NVN_POLYGON_MODE_LINE ? MTLTriangleFillModeLines : MTLTriangleFillModeFill);
    if (S.poly.offset_en & 4) enc_r_depth_bias(en, S.po.units, S.po.factor, S.po.clamp); else enc_r_depth_bias(en, 0, 0, 0);
    if (S.db_en) enc_r_depth_bounds(en, S.db_lo > 0 ? S.db_lo : 0, S.db_hi < 1 ? S.db_hi : 1); else enc_r_depth_bounds(en, 0, 1);   // (bounds that admit nothing: draw() returned)
    enc_r_depth_clip(en, !S.depth_clamp);
    enc_r_blend_color(en, S.blend_color[0], S.blend_color[1], S.blend_color[2], S.blend_color[3]);
    // A stream bound to the same address on this encoder is not looked up and set again (plan item 225): every draw set every stream, ~2 a draw
    // and half the translation thread's address lookups in Tears of the Kingdom. Pool changes move addresses: a new generation binds all again
    { uint64_t pg = atomic_load_explicit(&g_pool_gen, memory_order_acquire); es_mine(en); if (g_es.vb_gen != pg) { memset(g_es.vb, 0, sizeof g_es.vb); g_es.vb_gen = pg; } }
    for (int s_ = 0; s_ < 16; s_++) {
        if (!S.vbuf[s_].address) continue;
        void *mb; uint64_t mo;
        uint64_t gpu;
#ifndef DM_TEST_VB_ELIDE_ANY   // negative control of t_vbelide only: a stream bound on this encoder is not set again whatever its address
        int same = g_es.vb[s_] == S.vbuf[s_].address;
#else
        int same = g_es.vb[s_] != 0;
#endif
        if (same) { g_vb_skips++; use_rec("buf", S.vbuf[s_].address, S.vbuf[s_].size, 0, NULL); gw_note(&(p_bind_buf){-1, s_, S.vbuf[s_].address, S.vbuf[s_].size}, 0); continue; }
        if (mtl_gpu_addr(S.vbuf[s_].address, 1, &mb, &mo, &gpu)) { enc_r_vbuf(en, 16 + s_, gpu); g_vb_sets++; use_rec("buf", S.vbuf[s_].address, S.vbuf[s_].size, 0, NULL);
            g_es.vb[s_] = s_ == 14 ? 0 : S.vbuf[s_].address; if (s_ == 14) g_es.vb_zero = 0;   // (index 30: set again for every draw, then the zero buffer over it)
            gw_note(&(p_bind_buf){-1, s_, S.vbuf[s_].address, S.vbuf[s_].size}, 0); }
    }
    if (di) gw_note(&(p_bind_buf){-2, 0, di->ibuf, (uint64_t)di->count * (di->itype == 0 ? 1 : di->itype == 1 ? 2 : 4)}, 0);
    static id<MTLBuffer> zero; if (!zero) { zero = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]; res_add(zero); }
    static uint64_t zero_gpu; if (!zero_gpu) zero_gpu = zero.gpuAddress;
    if (!g_es.vb_zero) { enc_r_vbuf(en, 30, zero_gpu); g_es.vb_zero = 1; }
    if (pr->sh[0]) encode_args(en, pr->sh[0], 0, 1);
    if (pr->sh[1]) encode_args(en, pr->sh[1], 1, 0);
    // Parameter binding captures the actual resources under the registry lock; if the version changed between the eligibility check and the binding, split and retry before any GPU draw.
    int prim = dw ? dw->prim : di->prim;
    MTLPrimitiveType mp;
    switch (prim) {
        case 0: mp = MTLPrimitiveTypePoint; break; case 1: mp = MTLPrimitiveTypeLine; break; case 3: mp = MTLPrimitiveTypeLineStrip; break;
        case 4: case 7: case 8: mp = MTLPrimitiveTypeTriangle; break; case 5: mp = MTLPrimitiveTypeTriangleStrip; break;   // quads and quad strips as triangles
        default: RED_ONCE(19, "primitive type %d not implemented", prim); return;
    }
    if (g_vis_counting) {   // Occlusion query counting: a new slot for this render pass (also switch to a new slot after a report / reset)
        if (!g_enc_has_vis) RED_ONCE(34, "the pass in which occlusion query counting started has no visibility buffer attached (the first reset happened mid-pass), samples of that pass are not counted");
        else {
            if (g_vis_cur < 0 || g_vis_cur_pass != g_pass.serial) {
                if (g_vis_n >= VIS_SLOTS) RED_ONCE(35, "visibility slots exhausted");
                else { g_vis_cur = (int)g_vis_n++; g_vis_cur_pass = g_pass.serial;
                    if (g_nvs == g_vscap) { g_vscap = g_vscap ? g_vscap * 2 : 64; g_vs = realloc(g_vs, g_vscap * sizeof *g_vs); }
                    g_vs[g_nvs++] = (vis_slot){(void *)CFBridgingRetain(g_vis), (uint32_t)g_vis_cur}; }
            }
            if (g_vis_cur >= 0 && g_enc_vis != g_vis_cur + 1) { enc_r_visibility(en, MTLVisibilityResultModeCounting, (NSUInteger)g_vis_cur * 8); g_enc_vis = g_vis_cur + 1; }
        }
    }
    if (g_cnt_on > 0 || (g_cnt_on < 0 && counts_on())) count_self_sample(pr);   // after the splits above: the pass this draw lands in
    g_stat_draw++;
    if (enc_label_on() && !g_el_draws++ && pr->sh[1]) { memcpy(g_el_key, pr->sh[1]->key, 8); g_el_key[8] = 0; }
    if (passdump()) {   // Characteristics of a render pass's first draw (to judge whether the load can be omitted: full screen, no blending, writes all channels, does not depend on depth); with DM_NVN_PASSDUMP_ALL every draw is recorded
        static uint64_t last_pass = ~0ull; static int all = -1; if (all < 0) all = getenv("DM_NVN_PASSDUMP_ALL") != NULL;
        if (all) PD("d %llu bv=%d bi=%d first=%d vs=%s fs=%s vc=%d inst=%d blend=%#x b0=%#llx dt=%d dw=%d df=%d cull=%d st=%d sf=%d,%d sop=%d,%d,%d/%d,%d,%d smask=%#x,%#x/%#x,%#x sref=%#x,%#x ib=%#llx it=%d cmask=%#x", g_stat_pass, di ? di->base_vertex : 0, di ? di->base_instance : 0, dw ? dw->first : 0, pr->sh[0] ? pr->sh[0]->key : "-", pr->sh[1] ? pr->sh[1]->key : "-",
                    dw ? dw->count : di->count, dw ? (dw->instances ? dw->instances : 1) : di->instances, S.color.blend_en, (unsigned long long)*(uint64_t *)&S.blend[0], (int)S.ds.dt, (int)S.ds.dw, (int)S.ds.df, (int)S.poly.cull,
                    (int)S.ds.st, (int)S.ds.sf_f, (int)S.ds.sf_b, (int)S.ds.fail_f, (int)S.ds.zfail_f, (int)S.ds.zpass_f,
                    (int)S.ds.fail_b, (int)S.ds.zfail_b, (int)S.ds.zpass_b, S.st_vmask[0], S.st_vmask[1], S.st_wmask[0], S.st_wmask[1], S.st_ref[0], S.st_ref[1],
                    di ? (unsigned long long)di->ibuf : 0ull, di ? di->itype : 0, S.chmask.mask);
        if (all && pr->sh[0]) {   // Content fingerprint of each constant buffer of the vertex stage (to judge whether a draw is completely identical across frames: used by the shadow cache etc.); when unreadable (a write pending execution in this submit) record P
            char ub[400]; size_t o = 0; NvShader *v = pr->sh[0];
            for (int i = 0; i < v->rf.nb && o < sizeof ub - 40; i++) if (v->rf.b[i].kind == NVN_RB_CBUF && v->rf.b[i].reg < 16) {
                const p_bind_buf *u = &S.ubo[0][v->rf.b[i].reg]; void *mb; uint64_t mo; const uint8_t *base; uint64_t h = 1469598103934665603ull;
                if (!u->addr || !u->size) { o += snprintf(ub + o, sizeof ub - o, " r%d=-", v->rf.b[i].reg); continue; }
                if (pend_hits(u->addr, u->size) || !mtl_addr(u->addr, u->size, &mb, &mo) || !(base = [(__bridge id<MTLBuffer>)mb contents])) { o += snprintf(ub + o, sizeof ub - o, " r%d=P/%llu", v->rf.b[i].reg, (unsigned long long)u->size); continue; }
                uint64_t h64 = 0; for (uint64_t q = 0; q < u->size; q++) { h = (h ^ base[mo + q]) * 1099511628211ull; if (q == 63) h64 = h; }   // h64: the first 64 bytes (world matrix vec0–3 of the object buffer)
                { static uint64_t hp = ~0ull; static int hn; if (hp != g_stat_pass) { hp = g_stat_pass; hn = 0; }   // For the first 6 draws of each render pass, additionally print buffers of <= 608 bytes in hex (to compare byte by byte which fields changed across frames)
                  if (hn < 12 && u->size <= 608 && v->rf.b[i].reg == 3) { hn++; char hx[1300]; for (uint64_t q = 0; q < u->size; q++) snprintf(hx + 2 * q, 3, "%02x", base[mo + q]); PD("x %llu r%d %s", g_stat_pass, v->rf.b[i].reg, hx); } }
                o += snprintf(ub + o, sizeof ub - o, " r%d=%016llx/%llu/%016llx", v->rf.b[i].reg, (unsigned long long)h, (unsigned long long)u->size, (unsigned long long)h64); }
            PD("u %llu%s", g_stat_pass, ub); }
        if (all) { char vb[512]; size_t o = 0; o += snprintf(vb + o, sizeof vb - o, "nattr=%d", S.nattr);
            for (int i = 0; i < S.nattr && i < 6; i++) { int st = S.attr[i].stream;
                o += snprintf(vb + o, sizeof vb - o, " a%d{fmt=%#x off=%d s=%d stride=%u div=%u vb=%#llx/%llu}", i, S.attr[i].fmt, S.attr[i].offset, st,
                              st < 16 ? S.stream[st].stride : 0, st < 16 ? S.stream[st].divisor : 0, st < 16 ? (unsigned long long)S.vbuf[st].address : 0ull, st < 16 ? (unsigned long long)S.vbuf[st].size : 0ull); }
            PD("v %llu %s", g_stat_pass, vb);
            if (S.nattr > 0 && S.attr[0].stream < 16 && S.vbuf[S.attr[0].stream].address && S.attr[0].fmt == 0x29) {   // a0 as RGBA16F: the first 4 vertices
                const uint16_t *hv = (const uint16_t *)(uintptr_t)(S.vbuf[S.attr[0].stream].address + S.attr[0].offset); uint32_t st = S.stream[S.attr[0].stream].stride; char hx[300]; size_t ho = 0;
                for (int k = 0; k < 4 && (uint64_t)k * st + 8 <= S.vbuf[S.attr[0].stream].size; k++) { const uint16_t *e = (const uint16_t *)((const uint8_t *)hv + (size_t)k * st);
                    ho += snprintf(hx + ho, sizeof hx - ho, " (%g %g %g %g)", (double)*(const __fp16 *)&e[0], (double)*(const __fp16 *)&e[1], (double)*(const __fp16 *)&e[2], (double)*(const __fp16 *)&e[3]); }
                PD("v %llu a0 data%s", g_stat_pass, hx); } }
        if (last_pass != g_stat_pass) {
            last_pass = g_stat_pass;
            PD("D %llu prim=%d vc=%d inst=%d blend=%#x wm=%#x dt=%d dw=%d df=%d st=%d sc=%lu,%lu,%lu,%lu tgt=%lux%lu vp=%.0f,%.0f,%.0f,%.0f cull=%d front=%d dr=%.3g,%.3g it=%d", g_stat_pass, prim,
               dw ? dw->count : di->count, dw ? (dw->instances ? dw->instances : 1) : di->instances, S.color.blend_en, S.chmask.mask, (int)S.ds.dt, (int)S.ds.dw, (int)S.ds.df, (int)S.ds.st,
               (unsigned long)sc.x, (unsigned long)sc.y, (unsigned long)sc.width, (unsigned long)sc.height, (unsigned long)tw, (unsigned long)th,
               S.has_vp ? S.vp[0][0] : 0, S.has_vp ? S.vp[0][1] : 0, S.has_vp ? S.vp[0][2] : (float)tw, S.has_vp ? S.vp[0][3] : (float)th, (int)S.poly.cull, (int)S.poly.front, S.dr[0], S.dr[1], di ? di->itype : -1);
        }
    }
    udc_note_draw(pr);
    // A draw with 0 primitives (the game does send count=0) draws no pixels, so skip encoding; Metal requires the count to be non-zero, otherwise the validation layer aborts on the spot (measured on the camp / wilderness / combat pages)
    if (dw && prim == 7) {   // Quads: shared indices 0,1,2,0,2,3
        if (!g_quad_ib) {
            uint32_t n = 65536; uint32_t *ix = malloc(n * 6 * 4);
            for (uint32_t q = 0; q < n; q++) { uint32_t b = q * 4; uint32_t v[6] = {b, b + 1, b + 2, b, b + 2, b + 3}; memcpy(ix + q * 6, v, 24); }
            g_quad_ib = [g_dev newBufferWithBytes:ix length:n * 24 options:MTLResourceStorageModeShared]; g_quad_gpu = g_quad_ib.gpuAddress; res_add(g_quad_ib); free(ix);
        }
        if (dw->count >= 4) enc_r_draw_indexed(en, mp, (dw->count / 4) * 6, MTLIndexTypeUInt32, g_quad_gpu, g_quad_ib.length, dw->instances ? dw->instances : 1, dw->first, dw->base_instance);
    } else if (dw && prim == NVN_DRAW_PRIMITIVE_QUAD_STRIP) {   // two triangles a pair of vertices (nvn_quad_strip.h), from shared indices in runs of QS_MAX quads
        enum { QS_MAX = 65536 };
        if (!g_qs_ib) {
            uint32_t *ix = malloc((size_t)QS_MAX * 24); qs_indices(ix, QS_MAX);
            g_qs_ib = [g_dev newBufferWithBytes:ix length:(size_t)QS_MAX * 24 options:MTLResourceStorageModeShared]; res_add(g_qs_ib); free(ix);
        }
        uint32_t nq = dw->count > 0 ? qs_quads((uint32_t)dw->count) : 0;
        for (uint32_t at = 0; at < nq;) { uint32_t k = nq - at < QS_MAX ? nq - at : QS_MAX;
            enc_r_draw_indexed(en, mp, (NSUInteger)k * 6, MTLIndexTypeUInt32, g_qs_ib.gpuAddress, g_qs_ib.length, dw->instances ? dw->instances : 1, dw->first + (int)(2 * at), dw->base_instance);
            at += k; }
    } else if (dw) {
        if (dw->count) enc_r_draw_instanced(en, mp, dw->first, dw->count, dw->instances ? dw->instances : 1, dw->base_instance);
    } else {
        if (prim == 7) { RED_ONCE(20, "indexed quads not implemented"); return; }
        if (prim == NVN_DRAW_PRIMITIVE_QUAD_STRIP) {   // its indices made triangles' in this submission's arena, restarts as NVN's (nvn_quad_strip.h)
            unsigned width = di->itype == 2 ? 4 : di->itype == 1 ? 2 : 1; void *mb; uint64_t mo;
            if (di->count < 4) return;
            if (!mtl_addr(di->ibuf, (uint64_t)di->count * width, &mb, &mo)) { RED_ONCE(22, "index buffer is not in a memory pool"); return; }
            if (pend_hits(di->ibuf, (uint64_t)di->count * width)) { RED_ONCE(118, "indexed quad strips whose indices the GPU wrote earlier in this submission: not drawn (first case %#llx)", (unsigned long long)di->ibuf); return; }
            use_rec("buf", di->ibuf, (uint64_t)di->count * width, 0, NULL);
            ARENA_OUT id<MTLBuffer> ab; size_t ao = arena_alloc((size_t)di->count * 12, &ab);
            size_t k = qs_indexed((uint32_t *)((uint8_t *)ab.contents + ao), (const void *)(uintptr_t)di->ibuf, (uint32_t)di->count, width, S.restart_set && S.restart_en, S.restart_idx);
            if (k) enc_r_draw_indexed(en, mp, k, MTLIndexTypeUInt32, g_cur_arena->gpu + ao, (NSUInteger)di->count * 12, di->instances ? di->instances : 1, di->base_vertex, di->base_instance);
            return;
        }
        // Primitive restart: NVN restarts any primitive where an index equals the restart index exactly (SDK: an 8-bit index never matches one outside
        // 0..255); Metal restarts only strips, only at the all-ones index of the index type, always. Until the game sets it nothing changes (its default
        // state is not documented). Strips whose restart index is not all-ones get their indices rewritten in this submission's arena; what cannot be
        // expressed (a list with a restart, a strip with restart off holding all-ones) is reported (t_restart)
        int strip = mp == MTLPrimitiveTypeLineStrip || mp == MTLPrimitiveTypeTriangleStrip, rs = S.restart_set && S.restart_en;
        if (di->itype == 0) {   // 8-bit indices (Tears of the Kingdom draws most quads with them): Metal has only 16 / 32-bit index buffers, so they are widened into this submission's arena
            if (!di->count) return;
            const uint8_t *src8 = (const uint8_t *)(uintptr_t)di->ibuf; void *mb; uint64_t mo;
            if (!mtl_addr(di->ibuf, (uint64_t)di->count, &mb, &mo)) { RED_ONCE(21, "8-bit index buffer is not in a memory pool"); return; }
            if (g_wide_gpu) {   // widened on the GPU before the pass (wide_u8_gpu)
                use_rec("buf", di->ibuf, (uint64_t)di->count, 0, NULL);
                enc_r_draw_indexed(en, mp, di->count, MTLIndexTypeUInt16, g_wide_gpu, (NSUInteger)di->count * 2, di->instances ? di->instances : 1, di->base_vertex, di->base_instance);
                g_wide_gpu = 0;
                return;
            }
            if (pend_hits(di->ibuf, (uint64_t)di->count)) RED_ONCE(83, "8-bit indices written by the GPU earlier in this submission are widened from memory (first case %#llx)", (unsigned long long)di->ibuf);
            use_rec("buf", di->ibuf, (uint64_t)di->count, 0, NULL);
            ARENA_OUT id<MTLBuffer> ab; size_t ao = arena_alloc((size_t)di->count * 2, &ab); uint16_t *d16 = (uint16_t *)((uint8_t *)ab.contents + ao);
            int r8 = rs && S.restart_idx == 0xff, hit = 0;
            for (int k = 0; k < di->count; k++) { int r = r8 && src8[k] == 0xff; hit |= r; d16[k] = r ? 0xffff : src8[k]; }
            if (hit && !strip) RED_ONCE(84, "primitive restart in a list (index %#x): Metal restarts only strips, drawn without the restart", S.restart_idx);
            enc_r_draw_indexed(en, mp, di->count, MTLIndexTypeUInt16, g_cur_arena->gpu + ao, (NSUInteger)di->count * 2, di->instances ? di->instances : 1, di->base_vertex, di->base_instance);
        } else {
            void *mb; uint64_t mo;
            uint64_t gpu;
            if (!mtl_gpu_addr(di->ibuf, 1, &mb, &mo, &gpu)) { RED_ONCE(22, "index buffer is not in a memory pool"); return; }
            int w = di->itype == 1 ? 2 : 4; uint32_t ones = w == 2 ? 0xffffu : 0xffffffffu;
            use_rec("buf", di->ibuf, (uint64_t)di->count * (uint64_t)w, 0, NULL);
            NSUInteger len = ((__bridge id<MTLBuffer>)mb).length - mo;
            if (S.restart_set && di->count && (rs ? S.restart_idx != ones : strip && w == 2)) {   // NVN and Metal would restart at different indices
                const uint8_t *src = (const uint8_t *)(uintptr_t)di->ibuf;
                if (pend_hits(di->ibuf, (uint64_t)di->count * (uint64_t)w)) RED_ONCE(85, "indices written by the GPU earlier in this submission are checked for primitive restart in memory (first case %#llx)", (unsigned long long)di->ibuf);
                if (!rs) {   // restart off: an all-ones 16-bit index is a vertex on NVN, a restart in a Metal strip
                    for (int k = 0; k < di->count; k++) { uint16_t v; memcpy(&v, src + 2 * k, 2); if (v == 0xffff) { RED_ONCE(86, "16-bit index 0xffff in a strip with primitive restart off: Metal restarts there"); break; } }
                } else if (!strip) {
                    for (int k = 0; k < di->count; k++) { uint32_t v = 0; memcpy(&v, src + w * k, w); if (v == S.restart_idx) { RED_ONCE(84, "primitive restart in a list (index %#x): Metal restarts only strips, drawn without the restart", S.restart_idx); break; } }
                } else {
                    ARENA_OUT id<MTLBuffer> ab; size_t ao = arena_alloc((size_t)di->count * w, &ab); uint8_t *dst = (uint8_t *)ab.contents + ao;
                    for (int k = 0; k < di->count; k++) { uint32_t v = 0; memcpy(&v, src + w * k, w); if (v == S.restart_idx) v = ones; else if (v == ones) RED_ONCE(54, "all-ones index in a strip whose restart index is %#x: Metal restarts there", S.restart_idx); memcpy(dst + w * k, &v, w); }
                    gpu = g_cur_arena->gpu + ao; len = (NSUInteger)di->count * w;
                }
            }
            if (di->count) enc_r_draw_indexed(en, mp, di->count, w == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32, gpu, len,
                    di->instances ? di->instances : 1, di->base_vertex, di->base_instance);
        }
    }}
static void draw(const p_draw *dw, const p_draw_idx *di) {
    __unsafe_unretained NvProg *pr = nil; if (!draw_admit(dw, di, &pr)) return;
    id<MTLRenderCommandEncoder> en = draw_pass(pr, dw, di); if (!en) return;
    draw_encode(en, pr, dw, di);
}
static nvn_hmap g_cps = {.klen = sizeof(void *)};
static void warm_claim(void) {
    if (!atomic_exchange_explicit(&g_warm_ready, 0, memory_order_acq_rel)) return;
    os_unfair_lock_lock(&g_warm_lock);
    NSArray<WarmItem *> *items = [g_warm_items copy];
    [g_warm_items removeAllObjects];
    os_unfair_lock_unlock(&g_warm_lock);
    for (WarmItem *item in items) {
        if (item->kind == 0) {
            if (!g_shcache) g_shcache = [NSMutableDictionary dictionary];
            if (!g_shcache[item->key]) { g_shcache[item->key] = item->shader; g_warm_claim_shaders++; }
        } else if (item->kind == 1) {
            if (!g_shcache) g_shcache = [NSMutableDictionary dictionary];
            NvShader *vs = g_shcache[item->vs_key], *fs = item->fs_key.length ? g_shcache[item->fs_key] : nil;
            if (!vs) { vs = item->vs; g_shcache[item->vs_key] = vs; }
            if (item->fs_key.length && !fs) { fs = item->fs; g_shcache[item->fs_key] = fs; }
            item->pso.vs = (__bridge void *)vs; item->pso.fs = (__bridge void *)fs;
            if (!hm_get_id(&g_pso, &item->pso)) { hm_put_id(&g_pso, &item->pso, item->object); g_warm_claim_rps++; }
        } else {
            NvShader *cs = g_shcache[item->key];
            if (!cs) { cs = item->shader; if (!g_shcache) g_shcache = [NSMutableDictionary dictionary]; g_shcache[item->key] = cs; }
            const void *key = (__bridge const void *)cs;
            if (!hm_get_id(&g_cps, &key)) { hm_put_id(&g_cps, &key, item->object); g_warm_claim_cps++; }
        }
    }
}
static void dispatch(const p_dispatch *dp) {
    zskip_clear();
    if (g_nupd) upd_flush();   // compute reads uniforms from memory: renamed updates are written first
    if (!S.prog) { RED_ONCE(27, "no program bound before dispatch"); return; }
    NvShader *cs = prog_get(S.prog)->sh[5];
    if (!cs) { RED_ONCE(28, "dispatched program has no compute shader (or it is not mapped)"); return; }
    const void *k = (__bridge const void *)cs;
    id<MTLComputePipelineState> ps = hm_get_id(&g_cps, &k);
    if (!ps) {
        NSError *e = nil; uint64_t bt = bp_now(); int gave_up; id<MTLFunction> fn = cs->fn;
        ps = mtl_retry(^id(NSError **er) { return [g_dev newComputePipelineStateWithFunction:fn error:er]; }, &e, &gave_up);
        g_bp.pso_n++; g_bp.pso_ns += bp_now() - bt;
        if (!ps) { MLOG("[red] failed to create compute pipeline: %s", e.localizedDescription.UTF8String); return; }
        pso_record("C\t%s\n", cs->key);
        if (cs->key[0]) pso_user_record("C\t%s\n", cs->key);
        hm_put_id(&g_cps, &k, ps);
    }
    const uint16_t *tg = cs->rf.tg;
    if (!tg[0]) { RED_ONCE(29, "compute shader lacks threadgroup size reflection"); return; }
#if DM_NVN_M3
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    chunk_maybe_split();
    args_dirty_all(); CENC ce = enc_c_begin(pg_compute_desc());
#else
    // Back-to-back dispatches share one compute encoder (MacNativeGames, MIT): on Metal 4 the copy encoder g_blit is a compute encoder, kept open across
    // copies and closed wherever another encoder starts or the command buffer is committed; a dispatch uses it too, after a barrier that orders the blit
    // and dispatch stages with device visibility, as the end / start barriers of two encoders did (blit()). The argument table is set for every dispatch
    // (copies use none). Each dispatch opened and ended an encoder of its own (Tears of the Kingdom: about 24 a world frame, 13 right after another)
    rp_end(); int open = g_blit != nil;
    CENC ce = blit(); [ce setArgumentTable:g_ct]; args_dirty_all(); g_disp_n++; g_disp_shared += open;
#endif
    enc_c_pso(ce, ps);
    g_dispatch_grid[0] = (uint32_t)dp->x; g_dispatch_grid[1] = (uint32_t)dp->y; g_dispatch_grid[2] = (uint32_t)dp->z;   // c0 + 0x200 (drv_fill)
    encode_args(ce, cs, 5, 0);
#if !DM_NVN_M3
    int pg_slot = pg_dispatch_begin(ce);
#endif
    enc_c_dispatch(ce, MTLSizeMake(dp->x, dp->y, dp->z), MTLSizeMake(tg[0], tg[1], tg[2]));
#if DM_NVN_M3
    enc_c_end(ce);
#else
    pg_dispatch_end(ce, pg_slot);
#endif
}
