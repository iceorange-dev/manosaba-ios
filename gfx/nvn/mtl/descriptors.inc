// gfx/nvn/mtl/descriptors.inc, part of nvn_mtl.m (included there in order, one translation unit): Texture and sampler descriptor pools, argument arenas
// ---------- Descriptor pool: texture ID / sampler ID → object ----------
// At registration the whole texture object is snapshotted (NVN semantics: the descriptor has been written into the pool); view: the MTLTexture for that ID (a view or the base texture, held via CFBridgingRetain) and the serial-table / guest-write generations at the time it was obtained
// fin: the texture this entry names was released after nvnTextureFinalize (fin_drain) while the ID stayed registered, at submission fin_sub
typedef struct { tex_t t; int ok, fin; view_t v; int has_v; void *view; uint64_t view_id, view_rid, view_ser, view_dirty, fin_sub, live_gen; int view_kind;
                 void *alt, *alt_of; uint64_t alt_rid; uint8_t alt_type; } treg_t;   // alt: a view of another texture type for shaders that declare one (tex_fit), built from the view alt_of
static void treg_alt_drop(treg_t *r) { if (r->alt) { defer_obj_unref((__bridge id)r->alt); CFRelease(r->alt); r->alt = r->alt_of = NULL; r->alt_rid = 0; } }
static treg_t *g_treg; static int g_ntreg; static uint64_t g_treg_same;   // g_treg_same: identical registrations skipped
// Lock-free copy of texture ID → serial (the replay hot path only needs the serial: store lookahead index, binding set of the clear-only hoist): texture IDs are 20 bits, fixed 1M entries (BSS, only touched pages are used),
// release write at registration, relaxed atomic read on the reader side, without entering g_rmu (the former h_ser locked and unlocked on every lookup, thousands of times per frame in the camp)
static _Atomic uint64_t g_tser[1 << 20];
static samp_par *g_sreg; static uint8_t *g_sreg_ok; static void **g_sview; static uint64_t *g_srid; static int g_nsreg;   // g_sview: sampler ID → MTLSamplerState (retained)
void mtl_reg_tex(int tid, const tex_t *t, const view_t *v) { @autoreleasepool {
    int id_ = tid; if (id_ < 0) return;
    pthread_mutex_lock(&g_rmu);
    if (id_ >= g_ntreg) { int n = id_ + 1024; g_treg = realloc(g_treg, n * sizeof *g_treg); memset(g_treg + g_ntreg, 0, (n - g_ntreg) * sizeof *g_treg); g_ntreg = n; }
    // The same descriptor written into the slot again (games do it all the time) changes nothing: the slot keeps its view and no argument block has to
    // be encoded again for it (g_reg_gen; MacNativeGames, MIT). A slot whose texture was released (fin) takes it as before
    { treg_t *r = &g_treg[id_];
      if (t && r->ok && !r->fin && r->has_v == (v != NULL) && !memcmp(&r->t, t, sizeof *t) && (!v || !memcmp(&r->v, v, sizeof *v))) { g_treg_same++; pthread_mutex_unlock(&g_rmu); return; } }
    g_reg_gen++;
    if (g_treg[id_].view) { defer_obj_unref((__bridge id)g_treg[id_].view); CFRelease(g_treg[id_].view); g_treg[id_].view = NULL; g_treg[id_].view_id = g_treg[id_].view_rid = 0; }
    treg_alt_drop(&g_treg[id_]);
    // A registration without a view must not keep the slot's previous view: tex_by_id reads the view's fields whenever the texture has a swizzle
    // (MacNativeGames found a depth texture sampled through a stale BC3 view; ly05010419/MacNativeGames, MIT)
    g_treg[id_].ok = t != NULL; g_treg[id_].fin = 0; if (t) g_treg[id_].t = *t; g_treg[id_].has_v = v != NULL;
    if (v) g_treg[id_].v = *v; else memset(&g_treg[id_].v, 0, sizeof g_treg[id_].v);
    if (id_ < (1 << 20)) atomic_store_explicit(&g_tser[id_], t ? t->serial : 0, memory_order_release);
    pthread_mutex_unlock(&g_rmu);
} }
// The texture of a serial was finalized: drop the views that texture IDs still cache for it. A cached view is a strong reference (a view also keeps its
// base texture), and the game need not register those IDs again or use them before it exits: a finalized render target stayed allocated until then
// (Mac, a trial scene: ~800 MB of retired render targets within three minutes. An iPhone session ended without a crash report near its memory limit)
static void treg_drop_views(const uint64_t *serials, int n) {   // fin_drain, n <= 64: the serials go into a set of 128 (serial 0 marks an empty place)
    uint64_t set[128] = {0}, lo = UINT64_MAX, hi = 0;
    for (int k = 0; k < n && k < 64; k++) {
        uint64_t s = serials[k]; if (!s) continue;
        unsigned h = (unsigned)((s * 0x9E3779B97F4A7C15ull) >> 57); while (set[h] && set[h] != s) h = (h + 1) & 127;
        set[h] = s; lo = MIN(lo, s); hi = MAX(hi, s);
    }
    if (lo > hi) return;
    pthread_mutex_lock(&g_rmu);
    for (int i = 1; i < g_ntreg && i < (1 << 20); i++) {
        uint64_t s = atomic_load_explicit(&g_tser[i], memory_order_relaxed);
        if (s < lo || s > hi) continue;
        unsigned h = (unsigned)((s * 0x9E3779B97F4A7C15ull) >> 57); while (set[h] && set[h] != s) h = (h + 1) & 127;
        if (set[h] != s || !g_treg[i].ok) continue;
        g_treg[i].fin = 1; g_treg[i].fin_sub = g_sub_serial;
        if (g_treg[i].view) { defer_obj_unref((__bridge id)g_treg[i].view); CFRelease(g_treg[i].view); g_treg[i].view = NULL; g_treg[i].view_id = g_treg[i].view_rid = 0; }
        treg_alt_drop(&g_treg[i]);
    }
    pthread_mutex_unlock(&g_rmu);
}
// A sampler that clamps to a border colour Metal does not have (it has transparent black, opaque black and opaque white only; plan item 161)
static int samp_border_custom(const samp_par *p) {
    if (!(p->ws == 4 || p->ws == 5 || p->wt == 4 || p->wt == 5 || p->wr == 4 || p->wr == 5)) return 0;
    const float *c = p->border;
    return !((c[0] == 0 && c[1] == 0 && c[2] == 0 && (c[3] == 0 || c[3] == 1)) || (c[0] == 1 && c[1] == 1 && c[2] == 1 && c[3] == 1));
}
static int g_border_any;              // such a sampler has been registered (until then nothing below is checked)
void mtl_reg_samp(int sid, const samp_par *sp) { @autoreleasepool {
    int id_ = sid; if (id_ < 0) return;
    pthread_mutex_lock(&g_rmu);
    if (id_ >= g_nsreg) { int n = id_ + 256; g_sreg = realloc(g_sreg, n * sizeof *g_sreg); g_sreg_ok = realloc(g_sreg_ok, n); g_sview = realloc(g_sview, n * sizeof *g_sview); g_srid = realloc(g_srid, n * sizeof *g_srid);
        memset(g_sreg_ok + g_nsreg, 0, n - g_nsreg); memset(g_sview + g_nsreg, 0, (n - g_nsreg) * sizeof *g_sview); memset(g_srid + g_nsreg, 0, (n - g_nsreg) * sizeof *g_srid); g_nsreg = n; }
    // A replaced sampler may still be named (its gpuResourceID) by argument blocks of submissions the GPU has not run yet: released after them, as a
    // texture slot's old view (acnh-mobile 029dd30, MIT; plan item 195)
#ifndef DM_TEST_SAMP_RELEASE_AT_ONCE   // negative control of t_samp only: released at once, as before
    if (g_sview[id_]) { defer_obj_unref((__bridge id)g_sview[id_]); CFRelease(g_sview[id_]); g_sview[id_] = NULL; g_srid[id_] = 0; }
#else
    if (g_sview[id_]) { CFRelease(g_sview[id_]); g_sview[id_] = NULL; g_srid[id_] = 0; }
#endif
    g_reg_gen++;
    g_sreg[id_] = *sp; g_sreg_ok[id_] = samp_border_custom(sp) ? 2 : 1;   // 2: a border colour Metal lacks (plan item 161)
    if (g_sreg_ok[id_] == 2) g_border_any = 1;
    pthread_mutex_unlock(&g_rmu);
} }
static MTLTextureType ttype(int target, int fallback) {
    switch (target) { case 0: return MTLTextureType2D; case 1: case 7: return MTLTextureType2D; case 2: return MTLTextureType3D;   // 1D as one-row 2D (see where textures are created)
        case 3: return MTLTextureType2DArray; case 4: return MTLTextureType2DArray; case 5: return MTLTextureType2DMultisample;
        case 6: return MTLTextureType2DMultisampleArray; case 8: return MTLTextureTypeCube; case 9: return MTLTextureTypeCubeArray; }
    return (MTLTextureType)fallback;
}
// Out-of-pool texture ID = empty slot: the game itself uses GetSeparateTextureHandle(0xFFFFF) to get a handle and bind it to unused slots (measured call site main+0x1746820,
// texture pool capacity 15256) and never registers that ID. Read 0 per D3D / empty-slot semantics (the same all-zero dummy textures as unbound), no failure; unregistered IDs inside the pool are still reported a failure
// c[2] handle table layout of the game's shaders (game.json nvn_c2; hle_nvn.c sets it at nvnDeviceInitialize, the graphics unit tests keep manosaba's)
static struct { int tex, ntex, samp, nsamp; } g_c2 = {0x168, 128, 0x568, 128};
void mtl_set_c2_layout(int tex, int ntex, int samp, int nsamp) {
    if (tex < 0 || samp < 0 || tex + 8 * ntex > 0xA00 || samp + 8 * nsamp > 0xA00) MDIE("c[2] layout tex %#x x %d / samp %#x x %d does not fit the 0xA00-byte bank", tex, ntex, samp, nsamp);
    g_c2.tex = tex; g_c2.ntex = ntex; g_c2.samp = samp; g_c2.nsamp = nsamp;
}
int mtl_c2_tex_base(void) { return g_c2.tex; }
// Depth convention the vertex shaders must have been built for (game.json depth_mode; 1 = near_is_zero, manosaba's, until hle_nvn.c sets it)
static int g_depth_mode = 1;
void mtl_set_depth_mode(int m) { g_depth_mode = m; }
const char *mtl_depth_mode_name(void) { return g_depth_mode ? "near_is_zero" : "near_is_minus_w"; }
int mtl_c2_samp_base(void) { return g_c2.samp; }
static int g_tpool_n; static uint64_t g_stat_tex_oob;
void mtl_set_tex_pool_n(int n) { g_tpool_n = n; }
// The texture / sampler pool a command buffer set last (plan item 64e): handle IDs resolve at its base in the tables (hle_nvn.c pool_ns; 0 for the
// first pool, so one pool's IDs are the table's)
static int g_tex_ns, g_samp_ns;
static inline int h_tid(uint64_t h) { int t = (int)(h & 0xfffff); return t ? t + g_tex_ns : 0; }
static inline int h_sid(uint64_t h) { int s = (int)(h >> 20); return s ? s + g_samp_ns : 0; }
// What a texture ID's cached view was checked against: kind 1 = its MTLTexture follows guest memory (packaged, or CPU-filled: tex_mtl checks it once per
// submission), so the view holds within the submission; kind 0 = it does not (GPU-only, buffer and linear textures), so guest writes never touch it.
// The view used to be dropped at every guest write anywhere (MacNativeGames treg_stamp, MIT; written anew here)
static inline int treg_kind(const tex_t *t) { return (t->has_layout && t->mem) || tex_cpu_candidate(t); }
static inline uint64_t treg_stamp(int kind) { return !kind ? 0 : check_once() ? g_sub_serial : nvn_dirty_now(); }
// Views by their base texture and what they make of it (plan item 271): Tears of the Kingdom initializes its passes' texture objects again every frame,
// each a new serial on the same storage, so its IDs' views were made again each time on the same MTLTexture (about 35 a frame standing on Great Sky
// Island, 3 % of the translation thread in newTextureViewWithPixelFormat). A view is taken from here when the same base texture is viewed the same way;
// an entry is let go VIEW_IDLE submissions after its last use (a view keeps its base texture, so a retired one goes soon after; under g_rmu)
typedef struct { uint64_t base; uint32_t pf, tt, l0, nl, s0, ns; uint8_t sw[4], tswz, pad[7]; } view_key;
typedef struct { void *view; uint64_t last; } view_ent;   // view: CFBridgingRetain'd
static nvn_hmap g_views = {.klen = sizeof(view_key)}; static uint64_t g_view_hits, g_view_made;
enum { VIEW_IDLE = 8 };
static void view_sweep(void) {   // fin_drain, every submission
    pthread_mutex_lock(&g_rmu);
    for (int again = 1; again && g_views.n;) { again = 0;   // a deletion shifts entries: scan again
        for (uint32_t i = 0; i < g_views.cap; i++) { view_ent *e = g_views.vals ? g_views.vals[i] : NULL;
            if (!e || e->last + VIEW_IDLE > g_sub_serial) continue;
            view_key k; memcpy(&k, g_views.keys + (size_t)i * g_views.klen, sizeof k); nvn_hm_del(&g_views, &k);
            defer_obj_unref(CFBridgingRelease(e->view)); free(e); again = 1; break; } }
    pthread_mutex_unlock(&g_rmu);
}
static id<MTLTexture> tex_by_id(int tid) {   // Caller holds g_rmu
    if (g_tpool_n && tid - g_tex_ns >= g_tpool_n) { if (!g_stat_tex_oob++) MLOG("texture ID %d outside the pool (pool capacity %d) treated as an empty slot: reads 0", tid - g_tex_ns, g_tpool_n); return nil; }
    if (tid < 0 || tid >= g_ntreg || !g_treg[tid].ok) { RED_ONCE(10, "texture ID %d not registered", tid); return nil; }
    treg_t *rp = &g_treg[tid];
    uint64_t fg = atomic_load_explicit(&g_fin_gen, memory_order_acquire);
    if (rp->live_gen != fg || rp->fin) {   // a texture was finalized since this entry was checked: if it was this entry's, follow its storage to the live one
        tex_t live;
        if (live_follow(&rp->t, rp->fin, &live)) {
            uint64_t n = atomic_fetch_add(&g_live_follows, 1) + 1;
            if (n <= 4 || !(n % 10000)) MLOG("texture ID %d follows its storage from finalized serial %llu to serial %llu (%llu so far)", tid, (unsigned long long)rp->t.serial, (unsigned long long)live.serial, (unsigned long long)n);
            if (rp->view) { defer_obj_unref((__bridge id)rp->view); CFRelease(rp->view); rp->view = NULL; rp->view_id = rp->view_rid = 0; }
            treg_alt_drop(rp);
            rp->t = live; rp->fin = 0; g_reg_gen++;
            if (tid < (1 << 20)) atomic_store_explicit(&g_tser[tid], live.serial, memory_order_release);
        }
        rp->live_gen = fg;
    }
    // Take the generations first and then look up: if a change happens in between, the next time must take the slow path. Generation steps all for other
    // serials leave the view valid; and only a texture whose MTLTexture follows guest memory looks at guest writes, once per submission (treg_stamp)
    // (the kind is that of rp->t, fixed while the view is: a new registration or a storage follow drops the view)
    uint64_t sg = atomic_load_explicit(&g_ser_gen, memory_order_acquire);
    if (rp->view && rp->view_dirty == treg_stamp(rp->view_kind) && (rp->view_ser == sg || ser_unaffected(rp->view_ser, sg, rp->t.serial))) { rp->view_ser = sg; return (__bridge id<MTLTexture>)rp->view; }
    int kind = treg_kind(&rp->t); uint64_t dg = treg_stamp(kind);
    id<MTLTexture> v = (__bridge id<MTLTexture>)rp->view;
    if (v) {   // The serial table or guest memory has changed: check whether the base texture was re-uploaded and replaced; if so the view is invalidated
        id<MTLTexture> cur = tex_mtl(&rp->t);
        if (cur == v || v.parentTexture == cur) { rp->view_ser = sg; rp->view_dirty = dg; rp->view_kind = kind; return v; }
        defer_obj_unref(v); CFRelease(rp->view); rp->view = NULL; rp->view_rid = 0; v = nil; treg_alt_drop(rp);
    }
    // The game uses an ID whose texture was released after its finalize and grace period: its descriptor still names that storage, so the texture is
    // created again from the snapshot (a render target comes back blank) and queued for release again, so it does not stay forever
    if (rp->fin) {
        // Tears of the Kingdom's loading screen samples two BC4 textures through their IDs after finalizing them: on the console the descriptor still
        // reads their storage, and so does the texture made again here when that storage is its content (CPU-filled) or the storage's shared texture
        // still holds it (stor_*); only a rendered texture with nothing kept comes back blank (red). It stays RECREATE_GRACE submissions this time:
        // released after FIN_GRACE it was made again every 16 submissions while the loading screen lasted
        rp->fin = 0; fin_queue_due(rp->t.serial, g_sub_serial + RECREATE_GRACE);
        stor_key sk; int kept = tex_membacked(&rp->t);
        if (!kept && stor_key_of(&rp->t, &sk)) { pthread_mutex_lock(&g_smu); kept = nvn_hm_get(&g_stor, &sk) != NULL; pthread_mutex_unlock(&g_smu); }
        if (kept) { uint64_t n_kept = ++g_late_kept; if (n_kept <= 4 || !(n_kept % 64)) MLOG("texture ID %d used %llu submissions after its texture (serial %llu, %ux%u format %#x) was released after nvnTextureFinalize: made again from its storage (%llu so far)",
            tid, (unsigned long long)(g_sub_serial - rp->fin_sub), (unsigned long long)rp->t.serial, rp->t.d.width, rp->t.d.height, rp->t.d.format, (unsigned long long)n_kept); }
        else { uint64_t n_late = ++g_late_blank; if (n_late <= 8 || !(n_late % 64)) MLOG("[red] texture ID %d used %llu submissions after its texture (serial %llu, %ux%u format %#x) was released after nvnTextureFinalize: created again blank (%llu so far)",
            tid, (unsigned long long)(g_sub_serial - rp->fin_sub), (unsigned long long)rp->t.serial, rp->t.d.width, rp->t.d.height, rp->t.d.format, (unsigned long long)n_late); }
    }
    treg_t r = *rp;
    if (!r.has_v) memset(&r.v, 0, sizeof r.v);   // only a registered view may change format, levels, layers or target (MacNativeGames)
    id<MTLTexture> base = tex_mtl(&rp->t);
    if (!base) return nil;
    v = base;
    // Swizzle: the view's when it set one (it replaces the texture's, even as the identity), else the texture's; built as a view with swizzle (NVN and
    // Metal swizzle enums have the same numeric values)
    uint8_t esw[4] = {r.t.d.swz[0], r.t.d.swz[1], r.t.d.swz[2], r.t.d.swz[3]}; int tswz = r.t.d.swz_set;
    if (r.has_v && r.v.set_swz) { esw[0] = r.v.swz_r; esw[1] = r.v.swz_g; esw[2] = r.v.swz_b; esw[3] = r.v.swz_a; tswz = !(esw[0] == 2 && esw[1] == 3 && esw[2] == 4 && esw[3] == 5); }
    // The format's own components (nvn_fmt_to_mtl): the RGB 16-bit family sits R/B swapped in its Metal format, the X formats and RGB DXT1 read alpha
    // 1. The guest's selectors name NVN's components, so each that names one goes through the format's (totk-mobile d3dd76c, Apache-2.0, reimplemented:
    // Tears of the Kingdom samples RGB5A1 / RGB565 terrain textures; none is a render target in either game, which the swap would read crossed: red
    // in hle_nvn.c SetRenderTargets)
#ifndef DM_TEST_FMT_SWZ_DROP   // negative control of t_fmt_swz only: the format's swizzle dropped, as before
    { uint8_t nat[4]; uint32_t npf = 0; nvn_fmt_to_mtl(r.has_v && r.v.set_format ? r.v.format : r.t.d.format, &npf, nat);
      if (!(nat[0] == 2 && nat[1] == 3 && nat[2] == 4 && nat[3] == 5)) {
          if (!tswz) { esw[0] = 2; esw[1] = 3; esw[2] = 4; esw[3] = 5; }
          for (int i = 0; i < 4; i++) if (esw[i] >= 2 && esw[i] <= 5) esw[i] = nat[esw[i] - 2];
          tswz = !(esw[0] == 2 && esw[1] == 3 && esw[2] == 4 && esw[3] == 5);
      } }
#endif
    if ((r.has_v && (r.v.set_format || r.v.set_levels || r.v.set_layers || r.v.set_target || r.v.set_swz)) || tswz) {
        uint32_t pf = (uint32_t)base.pixelFormat; uint8_t swz[4];
        if (r.v.set_format && !(nvn_fmt_to_mtl(r.v.format, &pf, swz) && pf)) pf = (uint32_t)base.pixelFormat;
        { uint32_t upf; int ubpp; if (r.v.set_format && bc_unpack(r.v.format, &upf, &ubpp)) pf = upf; }   // No BC: a BC view is switched to the decompressed format just like the base texture
        { int vr, vs; if (r.v.set_format && bc1_astc(r.v.format, &vr, &vs)) pf = vs ? MTLPixelFormatASTC_4x4_sRGB : MTLPixelFormatASTC_4x4_LDR; }   // ... and a BC1 view to ASTC (bc1_astc)
        // Metal makes format views only within one size class, never across compressed ↔ uncompressed or depth / stencil ↔ colour except sRGB pairs;
        // anything else aborts the process in texture view validation. Such a pool entry is sampled in the base format, reported once with its formats
        if (pf != (uint32_t)base.pixelFormat && tex_need_pfv(base.pixelFormat, (MTLPixelFormat)pf)) {
            int bw, bh, vbw, vbh, bb = nvn_fmt_bpb(r.t.d.format, &bw, &bh), vb = nvn_fmt_bpb(r.v.format, &vbw, &vbh);
            if (mtl_fmt_block(base.pixelFormat) || mtl_fmt_block((MTLPixelFormat)pf) || pf_is_depth_stencil(base.pixelFormat) || pf_is_depth_stencil((MTLPixelFormat)pf) || !bb || bb != vb || bw != 1 || bh != 1 || vbw != 1 || vbh != 1) {
                RED_ONCE(72, "texture ID %d: view format %#x (Metal %u) is not a valid Metal view of texture format %#x (Metal %lu, %ux%u, serial %llu): sampled in the texture's format (first case)",
                         tid, r.v.format, pf, r.t.d.format, (unsigned long)base.pixelFormat, r.t.d.width, r.t.d.height, (unsigned long long)r.t.serial);
                pf = (uint32_t)base.pixelFormat;
            }
        }
        if (r.v.set_format && tex_need_pfv(base.pixelFormat, (MTLPixelFormat)pf) && !(base.usage & MTLTextureUsagePixelFormatView)) {
            base = tex_upgrade_pfv(&rp->t, base, (MTLPixelFormat)pf);
            sg = atomic_load_explicit(&g_ser_gen, memory_order_acquire);
        }
        NSUInteger l0 = r.v.set_levels ? r.v.base_level : 0, nl = r.v.set_levels && r.v.nlevels ? r.v.nlevels : base.mipmapLevelCount - l0;
        NSUInteger s0 = r.v.set_layers ? r.v.min_layer : 0;
        NSUInteger ns = base.textureType == MTLTextureType3D ? 1 : (r.v.set_layers && r.v.nlayers ? r.v.nlayers : base.arrayLength * (base.textureType == MTLTextureTypeCube || base.textureType == MTLTextureTypeCubeArray ? 6 : 1) - s0);
        MTLTextureType tt = r.v.set_target ? ttype(r.v.target, base.textureType) : base.textureType;
        if (tt == MTLTextureType2D && ns > 1) ns = 1;
        // A view whose level / layer range does not fit the texture (the game registered the ID with a texture and a view that do not belong together;
        // the console samples garbage, Metal's view validation aborts the process): sampled as the whole texture, reported once
        NSUInteger nsl = base.textureType == MTLTextureType3D ? 1 : base.arrayLength * (base.textureType == MTLTextureTypeCube || base.textureType == MTLTextureTypeCubeArray ? 6 : 1);
        if (!nl || l0 + nl > base.mipmapLevelCount || !ns || s0 + ns > nsl) {
            RED_ONCE(73, "texture ID %d: view levels %lu+%lu / layers %lu+%lu do not fit the texture (format %#x, %ux%u, %lu levels, %lu slices, view format %#x): sampled as the whole texture (first case)",
                     tid, (unsigned long)l0, (unsigned long)nl, (unsigned long)s0, (unsigned long)ns, r.t.d.format, r.t.d.width, r.t.d.height, (unsigned long)base.mipmapLevelCount, (unsigned long)nsl, r.v.set_format ? r.v.format : 0);
            l0 = 0; nl = base.mipmapLevelCount; s0 = 0; tt = base.textureType; ns = tt == MTLTextureType2D ? 1 : nsl; pf = (uint32_t)base.pixelFormat;   // a cube's slices stay a multiple of 6
        }
#ifndef DM_TEST_CUBE_VIEW_KEPT   // negative control of t_mtlguard only: the cube type kept (Metal aborts)
        // A cube-typed view whose slices are not a multiple of 6 (one face of a cube viewed with the cube's type: Metal's view validation aborts the
        // process; super-mario-odyssey-ios 356ab26, MIT; plan item 64m): one face as a 2D view, several as a 2D array
        if ((tt == MTLTextureTypeCube || tt == MTLTextureTypeCubeArray) && ns % 6) {
            static int said; if (!said++) MLOG("texture ID %d: a cube view of %lu faces (from face %lu) made as a 2D%s view (first case)", tid, (unsigned long)ns, (unsigned long)s0, ns > 1 ? " array" : "");
            tt = ns == 1 ? MTLTextureType2D : MTLTextureType2DArray;
        }
#endif
        view_key vk; memset(&vk, 0, sizeof vk); vk.base = (uint64_t)(uintptr_t)(__bridge void *)base; vk.pf = pf; vk.tt = (uint32_t)tt;
        vk.l0 = (uint32_t)l0; vk.nl = (uint32_t)nl; vk.s0 = (uint32_t)s0; vk.ns = (uint32_t)ns; vk.tswz = (uint8_t)tswz; if (tswz) memcpy(vk.sw, esw, 4);
        view_ent *ve = NULL;
#ifndef DM_TEST_VIEW_NOCACHE   // negative control of t_stor only: every view made anew, as before
        ve = nvn_hm_get(&g_views, &vk);
#endif
        if (ve) { v = (__bridge id<MTLTexture>)ve->view; ve->last = g_sub_serial; g_view_hits++; }
        else {
            if (tswz) v = [base newTextureViewWithPixelFormat:(MTLPixelFormat)pf textureType:tt levels:NSMakeRange(l0, nl) slices:NSMakeRange(s0, ns)
                                                     swizzle:MTLTextureSwizzleChannelsMake((MTLTextureSwizzle)esw[0], (MTLTextureSwizzle)esw[1], (MTLTextureSwizzle)esw[2], (MTLTextureSwizzle)esw[3])];
            else v = [base newTextureViewWithPixelFormat:(MTLPixelFormat)pf textureType:tt levels:NSMakeRange(l0, nl) slices:NSMakeRange(s0, ns)];
            if (!v) { RED_ONCE(11, "cannot create texture view id %d", tid); v = base; }
            else { g_view_made++;
#ifndef DM_TEST_VIEW_NOCACHE
                view_ent *ne = malloc(sizeof *ne); if (!ne) MDIE("texture view cache"); ne->view = (void *)CFBridgingRetain(v); ne->last = g_sub_serial; nvn_hm_put(&g_views, &vk, ne);
#endif
            } }
    }
    rp->view = (void *)CFBridgingRetain(v); rp->view_rid = v.gpuResourceID._impl; rp->view_ser = sg; rp->view_dirty = dg; rp->view_kind = kind;
    return v;
}
// Hot-path object fetch: if the table entry is valid, return the table-held pointer directly (without ARC retain / autorelease), otherwise take the slow path to build / validate. Caller holds g_rmu.
static inline __attribute__((always_inline)) void *tex_fast(int tid) {
    if (tid > 0 && tid < g_ntreg && g_treg[tid].ok && g_treg[tid].view && g_treg[tid].view_ser == g_ser_gen && g_treg[tid].view_dirty == treg_stamp(g_treg[tid].view_kind)) return g_treg[tid].view;
    return (__bridge void *)tex_by_id(tid);
}
static inline __attribute__((always_inline)) uint64_t tex_fast_rid(int tid, void **out) {
    if (tid > 0 && tid < g_ntreg && g_treg[tid].ok && g_treg[tid].view && g_treg[tid].view_ser == g_ser_gen && g_treg[tid].view_dirty == treg_stamp(g_treg[tid].view_kind)) { if (out) *out = g_treg[tid].view; return g_treg[tid].view_rid; }
    id<MTLTexture> t = tex_by_id(tid); if (out) *out = (__bridge void *)t; return t ? t.gpuResourceID._impl : 0;
}
// The shader declares another texture type than the bound texture has: bind a compatible view (nxnative 42a50ef, seedds, MIT). Maxwell sample
// instructions carry their own dimension (a non-array sample of a 2D array reads layer 0); on Metal a 2D array bound to a texture2d argument is a type
// mismatch with undefined reads (nxnative measured one fixed value: Super Mario 3D World's one-layer 2D array pre-lighting buffer sampled as 2D washed
// the whole picture out). The supported pairs are in nvn_tex_fit.h; any other mismatch is reported once and bound as it is. The view is kept with
// the texture ID until its view changes. Caller holds g_rmu
// Which shaders meet another texture type than they declare (plan item 168: Tears of the Kingdom's block encoders declare texture3d and MacNativeGames'
// shared list has them bound to 2D textures): each (shader, stage, slot, declared, bound) logged once, the first 32 of them
static void ttm_note(const char *key, int stage, int reg, MTLTextureType want, id<MTLTexture> t, int tid) {
    static struct { char key[17]; uint8_t stage, reg, want, have; } seen[32]; static int nseen; static uint64_t over;
    for (int i = 0; i < nseen; i++) if (seen[i].stage == stage && seen[i].reg == reg && seen[i].want == want && seen[i].have == t.textureType && !strncmp(seen[i].key, key, 16)) return;
    if (nseen == 32) { if (!over++) MLOG("texture type mismatches: more than 32 kinds, the rest not listed"); return; }
    snprintf(seen[nseen].key, 17, "%.16s", key); seen[nseen].stage = (uint8_t)stage; seen[nseen].reg = (uint8_t)reg; seen[nseen].want = (uint8_t)want; seen[nseen].have = (uint8_t)t.textureType; nseen++;
    MLOG("texture type mismatch %d: shader %.16s stage %d slot %d declares type %lu, bound type %lu (texture ID %d: %lux%lux%lu, %lu levels, Metal format %lu, NVN format %#x)", nseen, key, stage, reg,
         (unsigned long)want, (unsigned long)t.textureType, tid, (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.depth, (unsigned long)t.mipmapLevelCount, (unsigned long)t.pixelFormat,
         tid > 0 && tid < g_ntreg ? g_treg[tid].t.d.format : 0);
}
static id<MTLTexture> tex_fit(int tid, id<MTLTexture> t, MTLTextureType want, uint64_t *rid) {
    MTLTextureType have = t.textureType; NSUInteger ns = nvn_tex_fit_slices(want, have, t.arrayLength);
    if (!ns || tid <= 0 || tid >= g_ntreg) { RED_ONCE(69, "texture type mismatch: the shader declares %lu, the texture is %lu (texture ID %d: %lux%lux%lu, %lu levels, Metal format %lu, NVN format %#x, serial %llu); bound as it is",
        (unsigned long)want, (unsigned long)have, tid, (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.depth, (unsigned long)t.mipmapLevelCount, (unsigned long)t.pixelFormat,
        tid > 0 && tid < g_ntreg ? g_treg[tid].t.d.format : 0, tid > 0 && tid < g_ntreg ? (unsigned long long)g_treg[tid].t.serial : 0ull); return nil; }
    treg_t *r = &g_treg[tid];
    if (r->alt && r->alt_of == (__bridge void *)t && r->alt_type == (uint8_t)want) { *rid = r->alt_rid; return (__bridge id<MTLTexture>)r->alt; }
    treg_alt_drop(r);
    id<MTLTexture> v = nvn_tex_fit_view(t, want, ns);
    if (!v) { RED_ONCE(70, "cannot create a compatible texture view %lu → %lu (texture ID %d)", (unsigned long)have, (unsigned long)want, tid); return nil; }
    static uint64_t n_fit; if (!n_fit++) MLOG("texture bound as another type through a compatible view: %lu → %lu (texture ID %d; first of its kind, tex_fit)", (unsigned long)have, (unsigned long)want, tid);
    r->alt = (void *)CFBridgingRetain(v); r->alt_of = (__bridge void *)t; r->alt_type = (uint8_t)want; r->alt_rid = v.gpuResourceID._impl;
    *rid = r->alt_rid; return v;
}
static MTLSamplerAddressMode wrapm(int w) {
    switch (w) { case 0: case 7: return MTLSamplerAddressModeClampToEdge; case 1: return MTLSamplerAddressModeRepeat;
        case 3: case 2: return MTLSamplerAddressModeMirrorClampToEdge; case 5: case 4: return MTLSamplerAddressModeClampToBorderColor;
        case 6: return MTLSamplerAddressModeMirrorRepeat; }
    return MTLSamplerAddressModeRepeat;
}
static int g_samp_old_once;
// Border colours Metal does not have (plan item 161, the census before shader variants; MacNativeGames b6231e6 / 874a32d, #49: the grass map's
// border 0.5 read as transparent black): Metal clamps to transparent black, opaque black or opaque white only. Each other colour of a sampler that
// clamps to its border is logged once with what it is drawn as; the samplers with border wrap modes and those of them off the three are counted
// (mtl_border_census, the power line)
static uint64_t g_border_samp, g_border_off;
static void border_census(const samp_par *p, MTLSamplerBorderColor drawn) {
    if (!(p->ws == 4 || p->ws == 5 || p->wt == 4 || p->wt == 5 || p->wr == 4 || p->wr == 5)) return;
    g_border_samp++;
    if (!samp_border_custom(p)) return;
    g_border_off++; const float *c = p->border;
    static float seen[16][4]; static int nseen; int dup = 0;
    for (int i = 0; i < nseen; i++) dup |= !memcmp(seen[i], c, sizeof seen[i]);
    if (dup || nseen == 16) return;
    memcpy(seen[nseen++], c, sizeof seen[0]);
    MLOG("sampler border colour (%g, %g, %g, %g) with wrap modes %d / %d / %d: Metal has no such border; clamped to the edge, the shaders that sample through it return the colour outside the texture (border_variant; the edge texel without one, %s before) (sampler census: %llu with border wrap, %llu off Metal's three)",
         c[0], c[1], c[2], c[3], p->ws, p->wt, p->wr, drawn == MTLSamplerBorderColorOpaqueWhite ? "opaque white" : drawn == MTLSamplerBorderColorOpaqueBlack ? "opaque black" : "transparent black",
         (unsigned long long)g_border_samp, (unsigned long long)g_border_off);
}
void mtl_border_census(uint64_t *with_border, uint64_t *off) { *with_border = g_border_samp; *off = g_border_off; }
static id<MTLSamplerState> samp_by_id(int sid) {   // Caller holds g_rmu
    if (sid < 0 || sid >= g_nsreg || !g_sreg_ok[sid]) { RED_ONCE(12, "sampler ID %d not registered", sid); return nil; }
    if (g_sview[sid]) return (__bridge id<MTLSamplerState>)g_sview[sid];
    samp_par p = g_sreg[sid];
    MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
    d.magFilter = p.magf ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    d.minFilter = (p.minf == 1 || p.minf == 3 || p.minf == 5) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    d.mipFilter = p.minf <= 1 ? MTLSamplerMipFilterNotMipmapped : (p.minf == 2 || p.minf == 3) ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
    d.sAddressMode = wrapm(p.ws); d.tAddressMode = wrapm(p.wt); d.rAddressMode = wrapm(p.wr);
    d.lodMinClamp = p.lod_min < 0 ? 0 : p.lod_min; d.lodMaxClamp = p.lod_max;
    static int force = -1; if (force < 0) { const char *e = getenv("DM_NVN_ANISO"); force = e ? atoi(e) : 0; if (force > 1) MLOG("DM_NVN_ANISO=%d: anisotropic filtering raised on mipmapped linear samplers", force); }
    d.maxAnisotropy = nvn_aniso(p.aniso, p.minf, p.cmp_mode, p.reduction, force);
    if (p.cmp_mode) d.compareFunction = (MTLCompareFunction)(p.cmp_func - 1);
    // LOD bias and min / max reduction (NVN values 0 average, 1 min, 2 max = MTLSamplerReductionMode's) came with macOS / iOS 26: the bias was stored
    // and never applied (Tears of the Kingdom sets it on 861 samplers). Metal ignores the reduction with nearest filtering, as NVN does
    if (@available(macOS 26.0, iOS 26.0, *)) { d.lodBias = p.lod_bias < -16.f ? -16.f : p.lod_bias > 15.999f ? 15.999f : p.lod_bias; d.reductionMode = (MTLSamplerReductionMode)p.reduction; }
    else if ((p.reduction || p.lod_bias != 0.f) && !g_samp_old_once++) MLOG("[red] sampler LOD bias %g / reduction %d need macOS / iOS 26: ignored", p.lod_bias, (int)p.reduction);
    d.borderColor = p.border[3] > 0.5f ? (p.border[0] > 0.5f ? MTLSamplerBorderColorOpaqueWhite : MTLSamplerBorderColorOpaqueBlack) : MTLSamplerBorderColorTransparentBlack;
    border_census(&p, d.borderColor);
#ifndef DM_TEST_BORDER_OFF   // negative control of t_border only: the nearest of Metal's three, as before
    if (g_sreg_ok[sid] == 2) {   // the shader returns the colour outside the texture (border_variant); inside it the edge texels must not blend with a wrong border
        if (d.sAddressMode == MTLSamplerAddressModeClampToBorderColor) d.sAddressMode = MTLSamplerAddressModeClampToEdge;
        if (d.tAddressMode == MTLSamplerAddressModeClampToBorderColor) d.tAddressMode = MTLSamplerAddressModeClampToEdge;
        if (d.rAddressMode == MTLSamplerAddressModeClampToBorderColor) d.rAddressMode = MTLSamplerAddressModeClampToEdge;
    }
#endif
    d.supportArgumentBuffers = YES;
    id<MTLSamplerState> s = [g_dev newSamplerStateWithDescriptor:d];
    if (s) { g_sview[sid] = (void *)CFBridgingRetain(s); g_srid[sid] = s.gpuResourceID._impl; }
    return s;
}
static inline __attribute__((always_inline)) void *samp_fast(int sid) {
    if (sid > 0 && sid < g_nsreg && g_sreg_ok[sid] && g_sview[sid]) return g_sview[sid];
    return (__bridge void *)samp_by_id(sid);
}

// ---------- Argument buffer blocks: one block per submission, reclaimed after the GPU completes ----------
@interface NvArena : NSObject { @public id<MTLBuffer> buf; uint64_t gpu, fence; size_t used, cap; uint8_t *cpu; } @end   // cap, cpu: buf's length and contents
@implementation NvArena @end
static NSMutableArray<NvArena *> *g_arenas; static NvArena *g_cur_arena; static NSMutableArray<NvArena *> *g_sub_arenas;
static NvArena *arena_new(void) {
    // The for-in loop variable does not retain the object: take a strong reference first and then remove it from the array, otherwise what is returned after the array releases it is a dangling pointer
    for (NvArena *it in g_arenas) if (it->fence && it->fence <= g_done) { NvArena *r = it; [g_arenas removeObject:r]; r->fence = 0; r->used = 0; return r; }
    NvArena *a = [NvArena new]; a->buf = [g_dev newBufferWithLength:4 << 20 options:MTLResourceStorageModeShared]; a->gpu = a->buf.gpuAddress; a->cap = a->buf.length; a->cpu = a->buf.contents;
    res_add(a->buf); return a;
}
void arena_begin(void) { if (!g_arenas) { g_arenas = [NSMutableArray array]; g_sub_arenas = [NSMutableArray array]; } g_cur_arena = nil; }
void arena_commit(uint64_t fence) { for (NvArena *a in g_sub_arenas) { a->fence = fence; [g_arenas addObject:a]; } [g_sub_arenas removeAllObjects]; g_cur_arena = nil; }
static size_t arena_alloc(size_t n, ARENA_OUT id<MTLBuffer> *buf) {
    n = (n + 255) & ~(size_t)255;
    if (!g_cur_arena || g_cur_arena->used + n > g_cur_arena->cap) {
        pthread_mutex_lock(&g_mu); g_cur_arena = arena_new(); pthread_mutex_unlock(&g_mu);
        [g_sub_arenas addObject:g_cur_arena];
    }
    size_t off = g_cur_arena->used; g_cur_arena->used += n; *buf = g_cur_arena->buf; return off;
}

