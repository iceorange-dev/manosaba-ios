// gfx/nvn/mtl/lookahead.inc, part of nvn_mtl.m (included there in order, one translation unit): Pass lookahead: the hoisted clears run, store actions, first-draw coverage, feedback copies
// Where the replay is, the ops run early and the clear-only hoist's plan, depth / stencil use and the store decisions: gfx/nvn/core/pass.c (plan item 264)
// The readable-tile pass merge's plan (fuse_t, g_fz, fuse_try): gfx/nvn/core/fuse.c (plan item 264)
// The deferred merged pass's decisions (defer_t, the groups, defer_try): gfx/nvn/core/defer.c (plan item 264)
static void draw(const p_draw *dw, const p_draw_idx *di);
static void sd_finish(void) {   // the store lookahead's decision (gfx/nvn/core/pass.c sd_decide) as each attachment's store action
    if (!g_nsd) return;
    uint8_t st[9]; sd_decide(st);
    for (int i = 0; i < g_nsd; i++) enc_r_store(g_enc, g_sd[i].depth, st[i] ? MTLStoreActionStore : MTLStoreActionDontCare, (NSUInteger)g_sd[i].idx);
    g_nsd = 0;
}
// Coverage proof (holds only if all are satisfied; anything unrecognized is treated as not holding):
//   Shader: the vertex position xyz is taken as-is from one vertex attribute, w is 1 or the same attribute's .w (dmsh_build raster_facts); the fragment does not discard, does not write sample_mask, and writes this color index
//   State: this attachment has no blending, a full write mask, no alpha-to-coverage, fill mode, no depth test (or ALWAYS), no stencil test, single sample, not 3D
//   Vertices: non-indexed or 16 / 32-bit indexed draws, with indices and positions both read from CPU-visible pool memory; the scissor / viewport / geometry / winding / depth clipping decisions are in nvn_cover.h (pure functions, with unit tests)
// Read out attribute a of all vertices of this draw (non-indexed or 16 / 32-bit indexed; vertex number = first + k or index value + base_vertex), with missing components filled with 0 / 1 per the fetch rules.
// Returns 0 on success, a reason code on failure (6 vertex count / index type, 7 attribute index, 8 format / stream, 9 out of bounds, 10 address)
static int attr_read(int a, const p_draw *dw, const p_draw_idx *di, float P[64][4]) {
    int n = dw ? dw->count : di->count;
    if (n < 1 || n > 64 || (dw && dw->first < 0) || (di && (di->itype < 1 || di->itype > 2))) return 6;
    if (a < 0 || a >= S.nattr || S.attr[a].stream >= 16) return 7;
    va_st at = S.attr[a]; vs_st sm = S.stream[at.stream]; NVNbufferRange vb = S.vbuf[at.stream];
    MTLVertexFormat vf = vfmt(at.fmt); int nc = vf == MTLVertexFormatFloat2 ? 2 : vf == MTLVertexFormatFloat3 ? 3 : vf == MTLVertexFormatFloat4 ? 4 : 0;
    if (!nc || !sm.stride || sm.divisor || !vb.address) return 8;
    const uint8_t *ib = NULL; int isz = di ? (di->itype == 1 ? 2 : 4) : 0;
    if (di) { void *m; uint64_t o; if (!mtl_addr(di->ibuf, (uint64_t)n * isz, &m, &o) || !(ib = [(__bridge id<MTLBuffer>)m contents])) return 10; ib += o; }
    for (int k = 0; k < n; k++) {
        int64_t vid = dw ? (int64_t)dw->first + k : (isz == 2 ? (int64_t)((const uint16_t *)ib)[k] : (int64_t)((const uint32_t *)ib)[k]) + di->base_vertex;
        uint64_t off = (uint64_t)vid * sm.stride + at.offset;
        if (vid < 0 || off + 4 * nc > vb.size) return 9;
        void *mb; uint64_t mo; const uint8_t *base;
        if (!mtl_addr(vb.address + off, 4 * nc, &mb, &mo) || !(base = [(__bridge id<MTLBuffer>)mb contents])) return 10;
        float v[4] = {0, 0, 0, 1}; memcpy(v, base + mo, 4 * nc); memcpy(P[k], v, sizeof v);
    }
    return 0;
}
static int first_draw_covers(int i, id<MTLTexture> t, NSUInteger level) {
    const p_draw *dw = g_fd_dw; const p_draw_idx *di = g_fd_di;
    int n = dw ? dw->count : di->count, prim = dw ? dw->prim : di->prim;
    __unsafe_unretained NvProg *pr = S.prog ? prog_get(S.prog) : nil; if (!pr) return g_fd_why = 1, 0;
    NvShader *vs = pr->sh[0], *fs = pr->sh[1];
    if (!vs || !fs || vs->rf.pos_attr < 0 || fs->rf.may_discard || alpha_on() || fs->rf.writes_smask || !(fs->rf.color_mask >> i & 1)) return g_fd_why = 2, 0;
    if (!t || t.sampleCount != 1 || t.textureType == MTLTextureType3D) return g_fd_why = 3, 0;
    if ((S.color.blend_en >> i & 1) || ((S.chmask.mask >> (4 * i)) & 0xf) != 0xf || S.ms.a2c || S.poly.mode != NVN_POLYGON_MODE_FILL) return g_fd_why = 4, 0;
    if (R.rt.depth && ((S.ds.dt && S.ds.df != NVN_DEPTH_FUNC_ALWAYS) || S.ds.st)) return g_fd_why = 5, 0;
    if (n < 3 || n > 64) return g_fd_why = 6, 0;
    float P[64][4]; int why = attr_read(vs->rf.pos_attr, dw, di, P);
    if (why) return g_fd_why = why, 0;
    if (vs->rf.pos_w1) for (int k = 0; k < n; k++) P[k][3] = 1.0f;   // Position w is the constant 1 (the attribute's .w is not taken)
    int sc[4] = {R.sc.x, R.sc.y, R.sc.w, R.sc.h}; float vp[4] = {S.vp[0][0], S.vp[0][1], S.vp[0][2], S.vp[0][3]};
    g_fd_why = 11;
    return nvn_cover((const float (*)[4])P, n, prim, (int)S.poly.cull, S.poly.front == NVN_FRONT_FACE_CCW,
                     S.has_vp, vp, R.has_sc, sc, (long)MAX(t.width >> level, 1), (long)MAX(t.height >> level, 1));
}

void nvn_be_state(uint32_t op, const void *a, int c) {   // what the backend marks after a command that only sets state (gfx/nvn/nvn_backend.h)
    if (op == OP_BIND_UBO || op == OP_BIND_SSBO) gw_note(a, op == OP_BIND_SSBO);
    if (c & ST_PSO) g_pso_dirty = 1;
    if (c & ST_DSS) g_dss_dirty = 1;
    if (c & ST_ARGS_ALL) args_dirty_all();
    for (int st = 0; st < 6; st++) if (c & ST_ARGS(st)) g_args_dirty[st] = 1;
    if ((c & ST_HANDLES) && g_border_any) { const p_bind_handles *b = a; if (b->stage < 2) border_rebind(b->stage, b->h, b->n); }
}
static int state_op(uint32_t op, const void *a) {   // a command that only sets state (gfx/nvn/core/state.c st_op), and what the backend marks after it
    int c = st_op(op, a);
    if (!c) return 0;
    nvn_be_state(op, a, c); return 1;
}


// Prediction for parallel precompilation (see the note before pso_get): scan from after the current operation to the end of this submit, replaying only state; at a draw, compute the pipeline key from "the state at that time + that render pass's attachment formats" and dispatch those not yet built to the background.
// Whether a render pass actually attaches depth / stencil is only decided at replay (depth-only / stencil-only / neither attached are omitted), so one key is computed for each possible attach mode -- guessing too many only means wasted background compiles and does not affect the result.
static void pso_predict_rest(void) {
    if (!g_exec_next) { g_pp_more = 0; return; }
    static typeof(S) Ssave; memcpy(&Ssave, &S, sizeof S);
    p_scissor sc0 = R.sc; int hsc0 = R.has_sc;   // state_op sets the scissor too: the draw that asked for the prediction used the last one walked over
    int pso_dirty = g_pso_dirty, dss_dirty = g_dss_dirty;
    uint32_t cf[8]; memcpy(cf, g_pass.cfmt, sizeof cf); uint32_t df = g_pass.dfmt, sf = g_pass.sfmt, samples = g_pass.samples, dfull = df, sfull = sf; int kicked0 = (int)g_pp_kicked, draws = 0;
    op_cursor cur = {.p = g_exec_next};
    // Keys for "attachments attached as-is" are kicked as they are found (most likely to be used), the other attach modes after the scan -- background concurrency is limited, and what is dispatched first is compiled first
    enum { QN = PP_SCAN_KEYS }; static struct { pso_key k; void *vs, *fs; } qa[QN], qb[QN]; int na = 0, nb = 0, stop = 0;
    uint64_t t0 = bp_now();
    for (int guard = 0; ; guard++) {
        if (guard >= PP_SCAN_OPS) { stop = 1; break; }
        if (na >= QN) { stop = 2; break; }
        if (bp_now() - t0 > PP_SCAN_NS) { stop = 3; break; }
        const uint8_t *q; op_hdr hq; const void *aq;
        if (!op_next(&cur, &q, &hq, &aq)) break;
        if (hq.op == OP_SET_RT) {
            const p_set_rt *r = aq; memset(cf, 0, sizeof cf); dfull = sfull = 0; samples = 1;
            for (int i = 0; i < r->n && i < 8; i++) { id<MTLTexture> t = r->color[i] && !rt_dup_slot(r, i) ? tex_mtl(r->color[i]) : nil; if (t) { cf[i] = (uint32_t)rt_color_fmt(r->color[i], t, &r->cview[i], r->has_cview[i], 0); samples = (uint32_t)t.sampleCount; } }
            id<MTLTexture> dt = r->depth ? tex_mtl(r->depth) : nil;
#ifndef DM_TEST_PREDICT_ANY_DEPTH   // negative control of t_mtlguard only: any format predicted as depth, as before (Metal aborts in the background compile)
            if (dt && !pf_is_depth_stencil(dt.pixelFormat)) dt = nil;   // not attached at the replay either (rp_begin_ex, 64m)
#endif
            if (dt) { MTLPixelFormat f = dt.pixelFormat; samples = (uint32_t)dt.sampleCount;
                dfull = f != MTLPixelFormatStencil8 ? (uint32_t)f : 0; sfull = (f == MTLPixelFormatStencil8 || f == MTLPixelFormatDepth32Float_Stencil8) ? (uint32_t)f : 0; }
            df = dfull; sf = sfull; continue;
        }
        if (hq.op == OP_DRAW || hq.op == OP_DRAW_IDX) {
            draws++;
            __unsafe_unretained NvProg *pr = prog_peek(S.prog); if (!pr || !pr->sh[0]) continue;
            __unsafe_unretained NvShader *vs = pr->sh[0], *fs = pr->sh[1];
            const uint32_t dv[4] = {dfull, 0, 0, dfull}, sv[4] = {sfull, 0, sfull, 0};
            for (int v = 0; v < 4; v++) {
                if (v && dv[v] == dv[0] && sv[v] == sv[0]) continue;
                if (v == 2 && !sfull) continue; if (v == 3 && (!dfull || !sfull)) continue;
                pso_key k; pso_key_fill(&k, vs, fs, cf, dv[v], sv[v], samples, 0);
                if (hm_get_id(&g_pso, &k) || pp_pending(&k)) continue;   // made, or kicked by an earlier scan: not one of this scan's keys
                int dup = 0; for (int i = 0; i < (v ? nb : na) && !dup; i++) dup = !memcmp(&(v ? qb : qa)[i].k, &k, sizeof k);
                if (dup) continue;
                if (!v && na < QN) { qa[na].k = k; qa[na].vs = (__bridge void *)vs; qa[na].fs = (__bridge void *)fs; na++; pso_pend_kick(&k, vs, fs); }
                else if (v && nb < QN) { qb[nb].k = k; qb[nb].vs = (__bridge void *)vs; qb[nb].fs = (__bridge void *)fs; nb++; }
            }
            continue;
        }
        if (hq.op == OP_CLEAR_COLOR || hq.op == OP_CLEAR_DS) continue;
        state_op(hq.op, aq);
    }
    (void)df; (void)sf;
    for (int i = 0; i < nb; i++) pso_pend_kick(&qb[i].k, (__bridge NvShader *)qb[i].vs, (__bridge NvShader *)qb[i].fs);
    pthread_mutex_lock(&g_pp_mu); g_pp_scans++; if (stop) g_pp_stop[stop - 1]++; pthread_mutex_unlock(&g_pp_mu);
    g_pp_more = stop != 0;
    memcpy(&S, &Ssave, sizeof S); args_dirty_all(); g_pso_dirty = pso_dirty; g_dss_dirty = dss_dirty; R.sc = sc0; R.has_sc = hsc0;
    static int logged; if (g_pp_kicked > (uint64_t)kicked0 && logged++ < 20) MLOG("parallel precompile: %d draws left in this submit, %d pipelines handed to the background", draws, (int)g_pp_kicked - kicked0);
}

void nvn_be_fuse_cover(const p_set_rt *r2, const uint8_t *first, const int8_t *p2map, const uint8_t *clr, int n1, int n) {   // (gfx/nvn/core/fuse.h)
    static typeof(S) Ssave; memcpy(&Ssave, &S, sizeof S); typeof(R) Rsave = R;
    const p_draw *fdw = g_fd_dw; const p_draw_idx *fdi = g_fd_di; int fdwhy = g_fd_why;
    int pso_dirty = g_pso_dirty, dss_dirty = g_dss_dirty;
    op_hdr fh; memcpy(&fh, first, sizeof fh);
    g_fd_dw = fh.op == OP_DRAW ? (const p_draw *)(first + 8) : NULL;
    g_fd_di = fh.op == OP_DRAW_IDX ? (const p_draw_idx *)(first + 8) : NULL;
    R.rt = *r2; R.has_rt = 1;
    op_cursor cur = {.p = g_exec_next};
    for (;;) {
        const uint8_t *q; op_hdr hq; const void *aq;
        if (!op_next(&cur, &q, &hq, &aq)) break;
        if (q == first) break;
        if (hq.op == OP_DRAW || hq.op == OP_DRAW_IDX || hq.op == OP_SET_RT || hq.op == OP_CLEAR_COLOR || hq.op == OP_CLEAR_DS) continue;
        state_op(hq.op, aq);
    }
    for (int j = n1; j < n && j < 8; j++) {
        int k = -1;
        for (int q = 0; q < 8; q++) if (p2map[q] == j) { k = q; break; }
        if (k < 0 || clr[k] || !r2->color[k]) continue;
        uint64_t ser; int level, slice;
        rt_sub(r2, k, &ser, &level, &slice);
        (void)ser; (void)slice;
        g_fz.cov2[j] = (uint8_t)first_draw_covers(k, tex_mtl(r2->color[k]), (NSUInteger)level);
    }
    memcpy(&S, &Ssave, sizeof S); args_dirty_all(); R = Rsave;
    g_fd_dw = fdw; g_fd_di = fdi; g_fd_why = fdwhy;
    g_pso_dirty = pso_dirty; g_dss_dirty = dss_dirty;
}

static int fz_load_covers(int i, id<MTLTexture> t, NSUInteger level) {
    if (g_fz.active && g_fz.phase == 1 && i >= g_fz.n1) return g_fz.cov2[i];
    return first_draw_covers(i, t, level);
}

// Probe wrap-up: for every attachment changed to clear to the sentinel color, read back that level / layer, compare pixel by pixel with a same-format 1x1 sentinel pixel, and count what remains (> 0 means the coverage proof is wrong, report a failure)
static void lp_finish(void) {
    id<MTLCommandBuffer> dcb = q_cb();
    NSArray *ps = g_lp; g_lp = nil;
    for (NSArray *e in ps) {
        id<MTLTexture> t = e[0]; NSUInteger lv = [e[1] unsignedIntegerValue], sl = [e[2] unsignedIntegerValue];
        NSUInteger W = MAX(t.width >> lv, 1), H = MAX(t.height >> lv, 1), bpp = (NSUInteger)(att_bytes(t, lv) / (W * H));
        MTLTextureDescriptor *rd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.pixelFormat width:1 height:1 mipmapped:NO];
        rd.usage = MTLTextureUsageRenderTarget; rd.storageMode = MTLStorageModePrivate;
        id<MTLTexture> ref = [g_dev newTextureWithDescriptor:rd];
        MTLRenderPassDescriptor *pd = [MTLRenderPassDescriptor renderPassDescriptor];
        pd.colorAttachments[0].texture = ref; pd.colorAttachments[0].loadAction = MTLLoadActionClear; pd.colorAttachments[0].storeAction = MTLStoreActionStore;
        pd.colorAttachments[0].clearColor = [e[6] intValue] == 3 ? MTLClearColorMake(NAN, NAN, NAN, NAN) : [e[6] intValue] == 2 ? MTLClearColorMake(0.777, 0.222, 0.555, 0.888) : MTLClearColorMake(0.123, 0.456, 0.789, 0.321);
        [[dcb renderCommandEncoderWithDescriptor:pd] endEncoding];
        id<MTLBuffer> bt = [g_dev newBufferWithLength:W * H * bpp options:MTLResourceStorageModeShared], br = [g_dev newBufferWithLength:bpp options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> b = [dcb blitCommandEncoder];
        [b copyFromTexture:t sourceSlice:sl sourceLevel:lv sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(W, H, 1) toBuffer:bt destinationOffset:0 destinationBytesPerRow:W * bpp destinationBytesPerImage:W * H * bpp];
        [b copyFromTexture:ref sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(1, 1, 1) toBuffer:br destinationOffset:0 destinationBytesPerRow:bpp destinationBytesPerImage:bpp];
    enc_b_end(b);
        unsigned long long pass = [e[3] unsignedLongLongValue], ser = [e[5] unsignedLongLongValue]; int idx = [e[4] intValue];
        [dcb addCompletedHandler:^(id<MTLCommandBuffer> c) { (void)c;
            const uint8_t *p = bt.contents, *r = br.contents; uint64_t left = 0; char at[96] = ""; size_t ao = 0;
            for (NSUInteger k = 0; k < W * H; k++) if (!memcmp(p + k * bpp, r, bpp)) { if (left++ < 4) ao += snprintf(at + ao, sizeof at - ao, " (%lu,%lu)", (unsigned long)(k % W), (unsigned long)(k / W)); }
            if (left) MLOG("[red] load-elision probe: pass %llu c%d ser=%llu lv%lu sl%lu %lux%lu sentinel pixels left %llu, first positions%s", pass, idx, ser, (unsigned long)lv, (unsigned long)sl, (unsigned long)W, (unsigned long)H, (unsigned long long)left, at);
            else MLOG("load-elision probe: pass %llu c%d ser=%llu lv%lu sl%lu %lux%lu fully covered (0 sentinel left)", pass, idx, ser, (unsigned long)lv, (unsigned long)sl, (unsigned long)W, (unsigned long)H);
        }];
    }
    [dcb commit];
}
static int draw_samples_rt(NvProg *pr) {   // Sampled attachments keep the original conservative ser-level guard, not split by level/layer
    uint64_t rs[9]; int nr = 0;
    for (int k = 0; k < R.rt.n && k < 8; k++) if (R.rt.color[k]) rs[nr++] = R.rt.color[k]->serial;
    if (R.rt.depth) rs[nr++] = R.rt.depth->serial;
    if (!pr) return 1;
    for (int st = 0; st < 5; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (!sh) continue;
        for (int i = 0; i < sh->rf.nb; i++) { const nvn_rbind *b = &sh->rf.b[i]; uint64_t h;
            if (b->kind == NVN_RB_IMG) h = b->reg < 32 && sh->stage < 6 ? S.img[sh->stage][b->reg] : 0; else if (b->kind == NVN_RB_TEX || b->kind == NVN_RB_TBUF) h = b->reg < 128 && sh->stage < 6 ? S.tex[sh->stage][b->reg] : 0; else continue;
            int tid = h_tid(h); if (!tid) continue;
            pthread_mutex_lock(&g_rmu); uint64_t ser = tid < g_ntreg && g_treg[tid].ok ? g_treg[tid].t.serial : 0; pthread_mutex_unlock(&g_rmu);
            for (int j = 0; j < nr; j++) if (ser && ser == rs[j]) return 1; } }
    return 0;
}
// DM_NVN_COUNTS=1 (diagnostics, measured for plan items 42-44 before any change): per frame, draws that sample the levels and layers their own pass
// draws into, those of them after other draws of the same Metal pass (they read what the pass started with: the earlier draws' results are still in
// tile memory), and those with an NVN barrier after such a draw (on the console the barrier makes those results visible; the replay keeps the pass);
// timestamp reports and the submissions with two or more of them, ClearTexture calls, and render passes that end without a draw. Printed with the
// power summary (hle_nvn.c); cntpct reads are hle.c's
// [lo, hi) of levels and layers a view covers: a sampled view, or the one level an attachment renders (all layers when the view sets none)
static void view_span(const view_t *v, int has_v, const tex_t *t, int attach, int *l0, int *l1, int *y0, int *y1) {
    int levels = t->d.levels ? t->d.levels : 1;
    *l0 = has_v && v->set_levels ? v->base_level : 0;
    *l1 = attach ? *l0 + 1 : has_v && v->set_levels && v->nlevels ? *l0 + v->nlevels : levels;
    if (has_v && v->set_layers) { *y0 = v->min_layer; *y1 = v->min_layer + (v->nlayers ? v->nlayers : 1); } else { *y0 = 0; *y1 = 1 << 16; }
}
static void count_self_sample(NvProg *pr) {
    if (!pr || (!R.rt.n && !R.rt.depth)) return;
    int hit = 0, ov = 0, dep = 0; g_cnt_why[0] = 0;
    for (int st = 0; st < 5; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (!sh) continue;
        for (int i = 0; i < sh->rf.nb; i++) { const nvn_rbind *b = &sh->rf.b[i]; uint64_t h;
            if (b->kind == NVN_RB_IMG) h = b->reg < 32 && sh->stage < 6 ? S.img[sh->stage][b->reg] : 0; else if (b->kind == NVN_RB_TEX || b->kind == NVN_RB_TBUF) h = b->reg < 128 && sh->stage < 6 ? S.tex[sh->stage][b->reg] : 0; else continue;
            int tid = h_tid(h); if (!tid) continue;
            treg_t r; int ok = 0; pthread_mutex_lock(&g_rmu); if (tid < g_ntreg && g_treg[tid].ok) { r = g_treg[tid]; ok = 1; } pthread_mutex_unlock(&g_rmu);
            if (!ok) continue;
            int l0, l1, y0, y1; view_span(&r.v, r.has_v, &r.t, 0, &l0, &l1, &y0, &y1);
            for (int k = 0; k <= 8; k++) {
                const tex_t *a = k < 8 ? (k < R.rt.n ? R.rt.color[k] : NULL) : R.rt.depth; if (!a) continue;
                if (a->serial != r.t.serial && tex_same_storage(a, &r.t)) {   // measured for plan item 119: another serial on the attachment's storage
                    static uint64_t np; np++; if (np <= 8 || !(np & (np - 1)))
                        MLOG("counts: a draw samples texture %llu (%ux%u format %#x), which shares storage with attachment %d (texture %llu, %ux%u format %#x) of its pass (%llu so far)",
                             (unsigned long long)r.t.serial, r.t.d.width, r.t.d.height, r.t.d.format, k, (unsigned long long)a->serial, a->d.width, a->d.height, a->d.format, (unsigned long long)np); }
                if (a->serial != r.t.serial) continue;
                hit = 1; if (k == 8) dep = 1;
                int m0, m1, z0, z1; view_span(k < 8 ? &R.rt.cview[k] : &R.rt.dview, k < 8 ? R.rt.has_cview[k] : R.rt.has_dview, a, 1, &m0, &m1, &z0, &z1);
                if (l0 < m1 && m0 < l1 && y0 < z1 && z0 < y1) { ov = 1;
                    if (!g_cnt_why[0]) snprintf(g_cnt_why, sizeof g_cnt_why, "texture %ux%u, %d levels, %d layers; samples levels %d-%d layers %d-%d, draws level %d layers %d-%d%s",
                        r.t.d.width, r.t.d.height, r.t.d.levels, r.t.d.depth, l0, l1 - 1, y0, y1 > 65535 ? -1 : y1 - 1, m0, z0, z1 > 65535 ? -1 : z1 - 1, k == 8 ? " (depth)" : ""); }
            } } }
    (void)hit; (void)dep;
    if (ov) { g_cnt_self++; if (g_stat_draw > g_cnt_pass_d0) { g_cnt_self_ov++; if (g_cnt_bar_at > g_cnt_pass_d0) g_cnt_self_dep++; }
        static char seen[16][9]; static int nseen;   // the first 16 fragment shaders seen doing it, with the pass
        if (nseen < 16 && pr->sh[1]) { int k = 0; while (k < nseen && memcmp(seen[k], pr->sh[1]->key, 8)) k++;
            if (k == nseen) { memcpy(seen[nseen++], pr->sh[1]->key, 8);
                MLOG("counts: fragment shader %.8s samples what its pass draws (pass %llu, %u x %u, %d colour targets, draw %llu of the pass, barrier since %d): %s", pr->sh[1]->key,
                     (unsigned long long)g_stat_pass, (unsigned)g_pass.tw, (unsigned)g_pass.th, R.rt.n, (unsigned long long)(g_stat_draw - g_cnt_pass_d0), g_cnt_bar_at > g_cnt_pass_d0, g_cnt_why); } } }
}
// Before a draw (see fbx_sub): the attachments it samples where it draws, copied unless a copy is still current; the copies stand in for them.
// DM_NVN_FBX=split (plan item 165, MacNativeGames PR #67, open, MIT: ground decals sample the G-buffer pass they draw into; an Apple GPU keeps the
// pass's pixels in tile memory, so they read the last frame's): no copies; the pass is stored and reopened before such a draw when an attachment it
// samples holds pixels newer than memory, cleared at load or written by a draw that samples none of the pass's attachments (sd_att.pw), or written
// by any draw before a texture-invalidating barrier (as the default copies, plan item 42). The draws of a run that sample (the decals) do not count
// otherwise, so a run shares one split; a clear still pending before the pass opens is flushed the same way
static __strong id<MTLFunction> g_fbx_fn; static uint8_t g_fbx_lmap[8]; static pso_fz_desc g_fbx_desc; static uint64_t g_fbx_fetches;   // fbx_fetch's pick for this draw
void mtl_fbx_fetch_stats(uint64_t out[1]) { out[0] = g_fbx_fetches; }
static int fbx_fetch(NvProg *pr, unsigned need, const p_draw *dw, const p_draw_idx *di);
static void fbx_prepare(NvProg *pr, const p_draw *dw, const p_draw_idx *di) {
    g_fbx_npend = 0; g_fbx_self = 0; g_fbx_fn = nil;
    if (g_fbx_nact) { g_fbx_nact = 0; args_dirty_all(); }
    if (g_fbx_on < 0) { const char *e = getenv("DM_NVN_FBX"); g_fbx_on = !e ? 1 : !strcmp(e, "0") ? 0 : !strcmp(e, "all") ? 2 : !strcmp(e, "split") ? 3 : 1; }
    if (!g_fbx_on || !pr || (!R.rt.n && !R.rt.depth)) return;
    if (g_fbx_on == 1 && !(g_enc && g_stat_draw > g_cnt_pass_d0 && g_fbx_bar_at > g_cnt_pass_d0)) return;   // no barrier after draws of this pass: read as the console does
    uint64_t rs[9]; int nr = 0;
    for (int k = 0; k < 9; k++) { const tex_t *a = k < 8 ? (k < R.rt.n ? R.rt.color[k] : NULL) : R.rt.depth; rs[k] = a ? a->serial : 0; nr += a != NULL; }
    if (!nr) return;
    unsigned need = 0;
    for (int st = 0; st < 5; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (!sh) continue;
        for (int i = 0; i < sh->rf.nb; i++) { const nvn_rbind *b = &sh->rf.b[i];
            if (b->kind != NVN_RB_TEX || b->reg >= 128 || sh->stage >= 6) continue;
            int tid = h_tid(S.tex[sh->stage][b->reg]); if (!tid) continue;
            uint64_t ser = atomic_load_explicit(&g_tser[tid], memory_order_relaxed); if (!ser) continue;   // lock-free first look
            int any = 0; for (int k = 0; k < 9; k++) any |= rs[k] == ser; if (!any) continue;
            treg_t r; int ok = 0; pthread_mutex_lock(&g_rmu); if (tid < g_ntreg && g_treg[tid].ok) { r = g_treg[tid]; ok = 1; } pthread_mutex_unlock(&g_rmu);
            if (!ok) continue;
            int l0, l1, y0, y1; view_span(&r.v, r.has_v, &r.t, 0, &l0, &l1, &y0, &y1);
            for (int k = 0; k < 9; k++) {
                const tex_t *a = k < 8 ? (k < R.rt.n ? R.rt.color[k] : NULL) : R.rt.depth; if (!a || a->serial != r.t.serial) continue;
                int m0, m1, z0, z1; view_span(k < 8 ? &R.rt.cview[k] : &R.rt.dview, k < 8 ? R.rt.has_cview[k] : R.rt.has_dview, a, 1, &m0, &m1, &z0, &z1);
                if (l0 < m1 && m0 < l1 && y0 < z1 && z0 < y1) need |= 1u << k;
            } } }
    if (!need) return;
    g_fbx_self = 1;
    if (g_fz.active || g_lay.active) { g_fbx_refused++; RED_ONCE(97, "a draw samples what its pass draws inside a merged, layered or range pass: not copied (first case pass %llu)", g_stat_pass); return; }
    g_fbx_draws++;
    { static uint8_t seen[16][8]; static int nseen; __unsafe_unretained NvShader *f = pr->sh[1];   // which shaders (plan item 113): each once
      if (f && nseen < 16) { int k = 0; while (k < nseen && memcmp(seen[k], f->key, 8)) k++;
        if (k == nseen) { memcpy(seen[nseen++], f->key, 8);
            MLOG("attachment copies: fragment shader %.8s samples what its pass drew%s (pass %llu, %u x %u, draw %llu of the pass, attachments %#x)", f->key,
                 g_fbx_bar_at > g_cnt_pass_d0 ? " before a texture barrier" : "", (unsigned long long)g_stat_pass, (unsigned)g_pass.tw, (unsigned)g_pass.th,
                 (unsigned long long)(g_stat_draw - g_cnt_pass_d0), need); } } }
    if (fbx_fetch(pr, need, dw, di)) { g_fbx_fetches++; return; }   // read from the tile: no copy and no split
    if (g_fbx_on == 3) {
        if (g_enc) {
            for (int i = 0; i < g_nsd; i++) { const sd_att *t = &g_sd[i];
                int newer = t->pw || (t->wrote && g_fbx_bar_at > g_cnt_pass_d0);   // a texture barrier after any write of the pass makes it visible too (NVN)
                for (int k = 0; k < 9; k++) if ((need >> k & 1) && newer && t->ser == rs[k] && (k < 8 ? t->depth == 0 && t->idx == k : t->depth != 0)) {
                    PD("M %llu a draw samples an attachment its pass wrote, splitting here (plan item 165)", g_stat_pass); g_fbx_splits++; rp_end(); return; } }
        } else {
            int pend = 0; for (int k = 0; k < 9; k++) if (need >> k & 1) pend |= k < 8 ? R.clear_c[k] != 0 : R.clear_d || R.clear_s;
            if (pend) { g_fbx_split_clears++; rp_begin(); rp_end(); }   // the pending clears into memory first
        }
        return;
    }
    id<MTLTexture> src[9]; int ns = 0, stale = 0;
    for (int k = 0; k < 9; k++) if (need >> k & 1) { id<MTLTexture> t = tex_mtl(k < 8 ? R.rt.color[k] : R.rt.depth); if (!t) continue;
        int dup = 0; for (int j = 0; j < ns; j++) dup |= src[j] == t; if (dup) continue;
        src[ns++] = t; }
    int pick[9];
    for (int j = 0; j < ns; j++) {   // a current copy of it, else a copy texture of its shape this draw does not use yet
        pick[j] = -1;
        for (int i = 0; i < g_nfbx; i++) if (g_fbx[i].src == src[j] && g_fbx[i].draw == g_stat_draw && g_fbx[i].sub == g_fence + 1) { pick[j] = i; break; }
        if (pick[j] >= 0) continue;
        stale = 1; id<MTLTexture> t = src[j];
        for (int i = 0; i < g_nfbx && pick[j] < 0; i++) { id<MTLTexture> c = g_fbx[i].copy; int used = 0; for (int q = 0; q < j; q++) used |= pick[q] == i;
            if (!used && c.textureType == t.textureType && c.pixelFormat == t.pixelFormat && c.width == t.width && c.height == t.height && c.depth == t.depth &&
                c.mipmapLevelCount == t.mipmapLevelCount && c.arrayLength == t.arrayLength && c.sampleCount == t.sampleCount) pick[j] = i; }
        if (pick[j] < 0) {
            int i = g_nfbx < 16 ? g_nfbx++ : 15;
            if (g_fbx[i].copy) { defer_obj_release(g_fbx[i].copy); for (int q = 0; q < g_fbx[i].nv; q++) defer_obj_unref(g_fbx[i].view[q]); }
            MTLTextureDescriptor *d = [MTLTextureDescriptor new];
            d.textureType = t.textureType; d.pixelFormat = t.pixelFormat; d.width = t.width; d.height = t.height; d.depth = t.depth;
            d.mipmapLevelCount = t.mipmapLevelCount; d.arrayLength = t.arrayLength; d.sampleCount = t.sampleCount;
            d.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView; d.storageMode = MTLStorageModePrivate;
            id<MTLTexture> c = [g_dev newTextureWithDescriptor:d];
            if (!c) { RED_ONCE(52, "attachment copy: cannot make a %lux%lu copy texture; the draw samples the attachment", (unsigned long)t.width, (unsigned long)t.height); g_fbx[i] = (fbx_ent){0}; if (i == g_nfbx - 1) g_nfbx--; return; }
            res_add(c); g_fbx[i] = (fbx_ent){.copy = c}; pick[j] = i;
        }
    }
    if (stale) {
        if (!g_enc && (R.clear_d || R.clear_s || memchr(R.clear_c, 1, sizeof R.clear_c))) rp_begin();   // clears still pending: the copy must hold them
        rp_end();
        for (int j = 0; j < ns; j++) { fbx_ent *e = &g_fbx[pick[j]];
            if (e->src == src[j] && e->draw == g_stat_draw && e->sub == g_fence + 1) continue;
            [(id)blit() copyFromTexture:src[j] toTexture:e->copy];
            if (e->src != src[j]) { for (int q = 0; q < e->nv; q++) defer_obj_unref(e->view[q]); memset(e->vsrc, 0, sizeof e->vsrc); memset(e->view, 0, sizeof e->view); e->nv = 0; }
            e->src = src[j]; e->draw = g_stat_draw; e->sub = g_fence + 1; g_fbx_copies++; }
    }
    for (int j = 0; j < ns; j++) g_fbx_pend[g_fbx_npend++] = pick[j];
}
// The draw's pass is open: its copies stand in from here (passes run in between, as the TBDR path's hoisted clears, ended without them)
static void fbx_activate(void) {
    if (!g_fbx_npend) return;
    memcpy(g_fbx_act, g_fbx_pend, sizeof g_fbx_act); g_fbx_nact = g_fbx_npend; g_fbx_npend = 0; args_dirty_all();
}
static uint64_t h_ser(uint64_t h) { int tid = h_tid(h); return tid ? atomic_load_explicit(&g_tser[tid], memory_order_relaxed) : 0; }
uint64_t nvn_be_handle_ser(uint64_t h) { return h_ser(h); }   // (gfx/nvn/core/pass.h)
static void hoist_try(void) {   // the clear-only hoist planned (gfx/nvn/core/pass.c hoist_plan), run here: each group a clear-only render pass before this one
    hoist_grp G[4]; int ng = hoist_plan(G); if (!ng) return;
    p_set_rt rt0 = R.rt; p_scissor sc0 = R.sc; int hs0 = R.has_sc;   // This render pass's targets, scissor and pending clears: restored as they were after execution
    uint8_t cc0f[8]; nvn_rgba cc0[8]; uint8_t cd0f = R.clear_d, cs0f = R.clear_s; double cd0 = R.cd; uint32_t cs0 = R.cs;
    memcpy(cc0f, R.clear_c, 8); memcpy(cc0, R.cc, sizeof cc0);
    for (int gi = 0; gi < ng; gi++) {   // Execute: temporarily swap in that group's targets and clears and open a clear-only render pass
        hoist_grp *g = &G[gi];
        R.rt = *g->rt; memcpy(R.clear_c, g->clear_c, 8); memcpy(R.cc, g->cc, sizeof R.cc); R.clear_d = g->clear_d; R.clear_s = g->clear_s; R.cd = g->cd; R.cs = g->cs;
        R.sc = g->sc; R.has_sc = g->has_sc;
        PD("H %llu hoisted pure-clear pass (original position after this pass)", g_stat_pass + 1);
        rp_begin_ex(1); rp_end();
    }
    R.rt = rt0; R.sc = sc0; R.has_sc = hs0; memcpy(R.clear_c, cc0f, 8); memcpy(R.cc, cc0, sizeof cc0); R.clear_d = cd0f; R.clear_s = cs0f; R.cd = cd0; R.cs = cs0;
    if (!g_hoist_groups) MLOG("pure-clear pass hoist: first case %d groups (before pass %llu)", ng, g_stat_pass + 1);
    g_hoist_groups += (uint64_t)ng;
}
