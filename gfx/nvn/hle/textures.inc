// gfx/nvn/hle/textures.inc, part of hle_nvn.c (included there in order, one translation unit): Textures
// ---------- Textures ----------
#define TB(b) ((tex_builder_t *)(b))
NVN(nvnTextureBuilderSetDefaults, void, NVNtextureBuilder *b) {
    NVNdevice *dev = TB(b)->dev; memset(b, 0, sizeof(tex_builder_t)); TB(b)->dev = dev;
    TB(b)->d.target = NVN_TEXTURE_TARGET_2D; TB(b)->d.width = TB(b)->d.height = TB(b)->d.depth = TB(b)->d.levels = 1;
}
NVN(nvnTextureBuilderSetDevice, void, NVNtextureBuilder *b, NVNdevice *d) { TB(b)->dev = d; }
NVN(nvnTextureBuilderSetFlags, void, NVNtextureBuilder *b, int f) { TB(b)->d.flags = f; }
NVN(nvnTextureBuilderSetTarget, void, NVNtextureBuilder *b, NVNtextureTarget t) { TB(b)->d.target = t; }
NVN(nvnTextureBuilderSetWidth, void, NVNtextureBuilder *b, int v) { TB(b)->d.width = v; }
NVN(nvnTextureBuilderSetHeight, void, NVNtextureBuilder *b, int v) { TB(b)->d.height = v; }
NVN(nvnTextureBuilderSetDepth, void, NVNtextureBuilder *b, int v) { TB(b)->d.depth = v; }
NVN(nvnTextureBuilderSetSize1D, void, NVNtextureBuilder *b, int w) { TB(b)->d.width = w; TB(b)->d.height = TB(b)->d.depth = 1; }
NVN(nvnTextureBuilderSetSize2D, void, NVNtextureBuilder *b, int w, int h) { TB(b)->d.width = w; TB(b)->d.height = h; TB(b)->d.depth = 1; }
NVN(nvnTextureBuilderSetSize3D, void, NVNtextureBuilder *b, int w, int h, int d) { TB(b)->d.width = w; TB(b)->d.height = h; TB(b)->d.depth = d; }
NVN(nvnTextureBuilderSetSwizzle, void, NVNtextureBuilder *b, NVNtextureSwizzle r, NVNtextureSwizzle g, NVNtextureSwizzle bl, NVNtextureSwizzle a) {
    nvn_texdesc *d = &TB(b)->d; d->swz[0] = (uint8_t)r; d->swz[1] = (uint8_t)g; d->swz[2] = (uint8_t)bl; d->swz[3] = (uint8_t)a;
    d->swz_set = !(r == NVN_TEXTURE_SWIZZLE_R && g == NVN_TEXTURE_SWIZZLE_G && bl == NVN_TEXTURE_SWIZZLE_B && a == NVN_TEXTURE_SWIZZLE_A);
}
NVN(nvnTextureBuilderSetDepthStencilMode, void, NVNtextureBuilder *b, NVNtextureDepthStencilMode m) { TB(b)->d.ds_mode = (uint8_t)m; }
NVN(nvnTextureBuilderGetStorageClass, NVNstorageClass, const NVNtextureBuilder *b) { (void)b; return 0; }   // the host has no storage classes (nxnative)
NVN(nvnTextureBuilderSetLevels, void, NVNtextureBuilder *b, int v) { TB(b)->d.levels = v; }
NVN(nvnTextureBuilderSetFormat, void, NVNtextureBuilder *b, NVNformat f) { TB(b)->d.format = f; }
NVN(nvnTextureBuilderSetSamples, void, NVNtextureBuilder *b, int s) { TB(b)->d.samples = s; }
NVN(nvnTextureBuilderSetStride, void, NVNtextureBuilder *b, ptrdiff_t s) { TB(b)->d.stride = (uint32_t)s; }
NVN(nvnTextureBuilderSetStorage, void, NVNtextureBuilder *b, NVNmemoryPool *p, ptrdiff_t off) { { uint64_t lr = DM_GUEST_RA(); module_t *m = module_of((void *)lr); OLOG("TextureBuilderSetStorage b=%p pool=%p off=%#llx lr=%s+%#llx", (void *)b, (void *)p, (long long)off, m ? m->name : "?", m ? lr - (uint64_t)m->base : lr); } TB(b)->pool = (pool_t *)p; TB(b)->off = off; }
// The SDK reference says the console ignores this pointer: packaged data is expected to be in the texture's pool storage already (only the Windows
// reference implementation converts and copies it). Tears of the Kingdom calls it
NVN(nvnTextureBuilderSetPackagedTextureData, void, NVNtextureBuilder *b, const void *data) { (void)b; (void)data; }
NVN(nvnTextureBuilderSetPackagedTextureLayout, void, NVNtextureBuilder *b, const NVNpackagedTextureLayout *l) {
    TB(b)->has_layout = l != NULL; if (l) memcpy(TB(b)->layout, l->layout, 8);
    // Only byte 0 is understood (block height / depth, nvn_tex.c); byte 1 was 0 in all 2,134 layouts logged here (bayonetta3-mobile reads it). Red once
    if (l && l->layout[1]) { static int red; if (!red++) LOG("[red] SetPackagedTextureLayout: byte 1 is %#x, not modelled (layout %02x %02x %02x %02x ...; first case)", l->layout[1], l->layout[0], l->layout[1], l->layout[2], l->layout[3]); }
    TB(b)->d.pkg_layout = l ? 0x100 | l->layout[0] : 0;
}
NVN(nvnTextureBuilderGetFlags, NVNtextureFlags, const NVNtextureBuilder *b) { return (NVNtextureFlags)((const tex_builder_t *)b)->d.flags; }
#define CTB(b) (&((const tex_builder_t *)(b))->d)
static void get_swz(const nvn_texdesc *d, NVNtextureSwizzle *r, NVNtextureSwizzle *g, NVNtextureSwizzle *b, NVNtextureSwizzle *a) {
    *r = d->swz_set ? (NVNtextureSwizzle)d->swz[0] : NVN_TEXTURE_SWIZZLE_R; *g = d->swz_set ? (NVNtextureSwizzle)d->swz[1] : NVN_TEXTURE_SWIZZLE_G;
    *b = d->swz_set ? (NVNtextureSwizzle)d->swz[2] : NVN_TEXTURE_SWIZZLE_B; *a = d->swz_set ? (NVNtextureSwizzle)d->swz[3] : NVN_TEXTURE_SWIZZLE_A;
}
NVN(nvnTextureBuilderGetTarget, NVNtextureTarget, const NVNtextureBuilder *b) { return (NVNtextureTarget)CTB(b)->target; }
NVN(nvnTextureBuilderGetWidth, int, const NVNtextureBuilder *b) { return (int)CTB(b)->width; }
NVN(nvnTextureBuilderGetHeight, int, const NVNtextureBuilder *b) { return (int)CTB(b)->height; }
NVN(nvnTextureBuilderGetDepth, int, const NVNtextureBuilder *b) { return (int)CTB(b)->depth; }
NVN(nvnTextureBuilderGetLevels, int, const NVNtextureBuilder *b) { return (int)CTB(b)->levels; }
NVN(nvnTextureBuilderGetFormat, NVNformat, const NVNtextureBuilder *b) { return (NVNformat)CTB(b)->format; }
NVN(nvnTextureBuilderGetSamples, int, const NVNtextureBuilder *b) { return CTB(b)->samples; }
NVN(nvnTextureBuilderGetSwizzle, void, const NVNtextureBuilder *b, NVNtextureSwizzle *r, NVNtextureSwizzle *g, NVNtextureSwizzle *bl, NVNtextureSwizzle *a) { get_swz(CTB(b), r, g, bl, a); }
NVN(nvnTextureBuilderGetDepthStencilMode, NVNtextureDepthStencilMode, const NVNtextureBuilder *b) { return (NVNtextureDepthStencilMode)CTB(b)->ds_mode; }
NVN(nvnTextureBuilderGetStride, ptrdiff_t, const NVNtextureBuilder *b) { return (ptrdiff_t)CTB(b)->stride; }
NVN(nvnTextureBuilderGetMemoryOffset, ptrdiff_t, const NVNtextureBuilder *b) { return ((const tex_builder_t *)b)->off; }
// ZCull (compressed storage of the depth-culling hardware): no Metal equivalent; the host never reads or writes this buffer (Save / RestoreZCullData are empty implementations), the size only affects the game's own memory accounting.
// It must not be 0: Unity builds the pool from "sum of texture sizes + ZCull size" and carves it with its own allocator (main+0x99d6c0), which allocates size 0 as 1 byte (csinc),
// so it takes 1 byte after the depth texture, pushes the following swapchain textures out by the 0x200 alignment gap, and the third one gets no space (offset -1). Report 0x1000: positive and a multiple of the ZCull allocation alignment (0x1000), leaving no gap.
// The real device's exact value is unverified (it depends on the texture size); the host does not use this memory, so the smallest granularity that keeps the accounting consistent is taken.
#define ZCULL_SIZE 0x1000
NVN(nvnTextureBuilderGetZCullStorageSize, size_t, const NVNtextureBuilder *b) { (void)b; return ZCULL_SIZE; }
#define CTB_(b) (&((const tex_builder_t *)(b))->d)
static uint64_t g_layout_logged;
static void log_layout(const tex_builder_t *b, const nvn_texlayout *L) {
    if (b->has_layout && g_layout_logged++ < 16)
        LOG("[nvn] packaged texture %ux%ux%u levels %u format %#x target %d layout bytes %02x%02x%02x%02x%02x%02x%02x%02x → block height log2=%u (from layout byte 0) size %#llx",
            b->d.width, b->d.height, b->d.depth, b->d.levels, b->d.format, b->d.target, b->layout[0], b->layout[1], b->layout[2],
            b->layout[3], b->layout[4], b->layout[5], b->layout[6], b->layout[7], L->tileH, L->size);
}
NVN(nvnTextureBuilderGetStorageSize, size_t, const NVNtextureBuilder *b) {
    nvn_texlayout L; nvn_tex_layout(&((const tex_builder_t *)b)->d, &L);
    if (!L.bpb) LOG("[red] TextureBuilderGetStorageSize: unknown format %#x", ((const tex_builder_t *)b)->d.format);
    log_layout((const tex_builder_t *)b, &L);
    OLOG("TextureBuilderGetStorageSize b=%p %ux%u fmt %#x flags %#x → %#llx", (const void *)b, CTB_(b)->width, CTB_(b)->height, CTB_(b)->format, CTB_(b)->flags, L.size);
    return L.size;
}
NVN(nvnTextureBuilderGetStorageAlignment, size_t, const NVNtextureBuilder *b) {
    nvn_texlayout L; nvn_tex_layout(&((const tex_builder_t *)b)->d, &L);
    OLOG("TextureBuilderGetStorageAlignment b=%p %ux%u fmt %#x → %#x", (const void *)b, CTB_(b)->width, CTB_(b)->height, CTB_(b)->format, L.align);
    return L.align;
}
#define TX(t) ((tex_t *)(t))
#define CTX(t) ((const tex_t *)(t))
#define tex_snap(t) mtl_tex_snap(CTX(t))   // recorded commands point at a host copy of the texture (nvn_mtl.m)
// DM_NVN_TEXCENSUS=1 (items 82 / 83): textures of the kinds whose layout or sampling rules are not checked against the driver, one line per
// distinct shape (up to 16 per kind) with the running count of that kind: cube arrays, LINEAR_RENDER_TARGET without LINEAR, MINIMAL_LAYOUT,
// packaged 3D, linear block-compressed, and formats with their own swizzle (nvn_fmt_to_mtl: R/B or alpha 1), sampled or as render targets
static void tex_census(const tex_t *o, int rt) {   // rt: o is a colour target of SetRenderTargets (only the swizzle kind is counted then)
    static int on = -1; if (on < 0) on = getenv("DM_NVN_TEXCENSUS") && strcmp(getenv("DM_NVN_TEXCENSUS"), "0");
    if (!on) return;
    enum { K_CUBEARR, K_LINRT, K_MINIMAL, K_PKG3D, K_LINBC, K_SWZ, K_SWZRT, KN };
    static const char *const kn[KN] = {"cube array", "linear render target without LINEAR", "minimal layout", "packaged 3D", "linear block-compressed",
                                       "format with its own swizzle", "render target in a format with its own swizzle"};
    static uint64_t cnt[KN]; static struct { nvn_texdesc d; uint8_t b0, b1; } seen[KN][16]; static int nseen[KN]; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    const nvn_texdesc *d = &o->d; int bw = 1, bh = 1; nvn_fmt_bpb(d->format, &bw, &bh); uint8_t swz[4]; uint32_t pf = 0; nvn_fmt_to_mtl(d->format, &pf, swz);
    int own = swz[0] != NVN_SWZ_R || swz[2] != NVN_SWZ_B || swz[3] != NVN_SWZ_A;
    int hit[KN] = {
        !rt && d->target == NVN_TEXTURE_TARGET_CUBEMAP_ARRAY,
        !rt && (d->flags & 0x40) && !(d->flags & 0x10),
        !rt && (d->flags & 0x200) != 0,
        !rt && o->has_layout && d->target == NVN_TEXTURE_TARGET_3D,
        !rt && (d->flags & 0x50) && (bw > 1 || bh > 1),
        !rt && own,
        rt && own,
    };
    pthread_mutex_lock(&mu);
    for (int k = 0; k < KN; k++) {
        if (!hit[k]) continue;
        cnt[k]++; int dup = 0;
        for (int i = 0; i < nseen[k] && !dup; i++) dup = !memcmp(&seen[k][i].d, d, sizeof *d) && seen[k][i].b0 == o->layout[0] && seen[k][i].b1 == o->layout[1];
        if (dup || nseen[k] >= 16) continue;
        seen[k][nseen[k]].d = *d; seen[k][nseen[k]].b0 = o->layout[0]; seen[k][nseen[k]].b1 = o->layout[1]; nseen[k]++;
        LOG("[nvn] texture census, %s #%llu: target %d format %#x flags %#x %ux%ux%u levels %u samples %d stride %u packaged %d (%02x %02x), size %#x",
            kn[k], (unsigned long long)cnt[k], d->target, d->format, d->flags, d->width, d->height, d->depth, d->levels, d->samples, d->stride,
            o->has_layout, o->layout[0], o->layout[1], o->size);
    }
    pthread_mutex_unlock(&mu);
}
NVN(nvnTextureInitialize, NVNboolean, NVNtexture *t, const NVNtextureBuilder *b) {
    const tex_builder_t *s = (const tex_builder_t *)b; tex_t *o = TX(t);
    if (o->magic == MAGIC('T', 'E', 'X', 0) && o->serial) x_drain();   // a live object initialized again: a queued replay may still read it (plan item 61)
    OLOG("TextureInitialize t=%p b=%p builder.off=%#llx", (void *)t, (const void *)b, (long long)s->off);
    { static int red; if (s->d.ds_mode && !red++) LOG("[red] TextureInitialize: depth-stencil texture sampled as stencil (ds_mode=%d): Metal stencil view not implemented, still sampling depth", s->d.ds_mode); }
    memset(o, 0, sizeof *o);
    o->magic = MAGIC('T', 'E', 'X', 0); o->d = s->d; o->pool = s->pool; o->off = s->off;
    o->has_layout = s->has_layout; memcpy(o->layout, s->layout, 8);
    { nvn_texlayout L; nvn_tex_layout(&o->d, &L); o->size = (uint32_t)L.size; o->layer_size = (uint32_t)L.layer_size; if (L.size >> 32) LOG("[red] TextureInitialize: storage %#llx exceeds 4 GB", L.size); }
    o->mem = s->pool && s->pool->mem ? s->pool->mem + s->off : NULL;
    o->serial = nvn_new_serial();
    mtl_tex_live(o);   // the texture now at this storage (a registration of a finalized one there follows it: nvn_mtl.m tex_by_id)
    { const char *why = !o->d.format ? "format 0" : nvn_texdesc_metal_check(&o->d); if (why) tex_diag("TextureInitialize", -1, o, why, DM_GUEST_RA()); }
    tex_census(o, 0);
    if (g_log && o->d.width == 1920 && o->d.height == 1080) { const uint8_t *q = (const uint8_t *)t; char hx[0xC0 * 2 + 1]; for (int i = 0; i < 0xC0; i++) snprintf(hx + 2 * i, 3, "%02x", q[i]); LOG("[nvn] Texture %p 0xC0 bytes: %s", (void *)t, hx); }
    OLOG("Texture %p ser %llu %ux%ux%u lv%u fmt=%#x tgt=%d flags=%#x size=%#llx mem=%p", (void *)t, o->serial, o->d.width, o->d.height, o->d.depth,
         o->d.levels, o->d.format, o->d.target, o->d.flags, (unsigned long long)o->size, (void *)o->mem);
    return 1;
}
NVN(nvnTextureFinalize, void, NVNtexture *t) { x_drain(); OLOG("TextureFinalize %p ser %llu", (void *)t, TX(t)->serial); mtl_tex_release(TX(t)); TX(t)->magic = 0; }
NVN(nvnTextureGetFlags, NVNtextureFlags, const NVNtexture *t) { return (NVNtextureFlags)CTX(t)->d.flags; }
NVN(nvnTextureGetWidth, int, const NVNtexture *t) { return (int)CTX(t)->d.width; }
NVN(nvnTextureGetHeight, int, const NVNtexture *t) { return (int)CTX(t)->d.height; }
NVN(nvnTextureGetDepth, int, const NVNtexture *t) { return (int)CTX(t)->d.depth; }
NVN(nvnTextureGetStorageSize, int, const NVNtexture *t) { return (int)CTX(t)->size; }
NVN(nvnTextureGetTarget, NVNtextureTarget, const NVNtexture *t) { return (NVNtextureTarget)CTX(t)->d.target; }
NVN(nvnTextureGetLevels, int, const NVNtexture *t) { return (int)CTX(t)->d.levels; }
NVN(nvnTextureGetFormat, NVNformat, const NVNtexture *t) { return (NVNformat)CTX(t)->d.format; }
NVN(nvnTextureGetSamples, int, const NVNtexture *t) { return CTX(t)->d.samples; }
NVN(nvnTextureGetSwizzle, void, const NVNtexture *t, NVNtextureSwizzle *r, NVNtextureSwizzle *g, NVNtextureSwizzle *b, NVNtextureSwizzle *a) { get_swz(&CTX(t)->d, r, g, b, a); }
NVN(nvnTextureGetDepthStencilMode, NVNtextureDepthStencilMode, const NVNtexture *t) { return (NVNtextureDepthStencilMode)CTX(t)->d.ds_mode; }
NVN(nvnTextureGetStride, ptrdiff_t, const NVNtexture *t) { return (ptrdiff_t)CTX(t)->d.stride; }
NVN(nvnTextureGetMemoryOffset, ptrdiff_t, const NVNtexture *t) { return CTX(t)->off; }
// nvn.h declares a by-value NVNmemoryPool return (a DWARF reconstruction error); it actually returns a pointer: at call site main+0x9c07c4 (2.0.4b) x8 holds the function address before the call,
// not an indirect result pointer, and the returned x0 is passed straight to nvnMemoryPoolMap as the pool pointer
NVN_NOPROTO(nvnTextureGetMemoryPool, const NVNmemoryPool *, const NVNtexture *t) { return (const NVNmemoryPool *)CTX(t)->pool; }
NVN(nvnTextureGetDebugID, uint64_t, const NVNtexture *t) { return CTX(t)->serial; }
NVN(nvnTextureGetZCullStorageSize, size_t, const NVNtexture *t) { (void)t; return ZCULL_SIZE; }
NVN(nvnTextureCompare, NVNboolean, const NVNtexture *a, const NVNtexture *b) {
    const tex_t *x = CTX(a), *y = CTX(b);
    return !memcmp(&x->d, &y->d, sizeof x->d) && x->mem == y->mem;
}
// No prototype in the header; call sites such as main 0x174899c compare the return value with 1 and set some status bit 0x100 if not equal. Meaning unverified; return 1 for now
NVN_NOPROTO(nvnTextureGetRawStorageClass, int, const NVNtexture *t) { (void)t; return 1; }
#define VW(v) ((view_t *)(v))
NVN(nvnTextureViewSetDefaults, void, NVNtextureView *v) { memset(v, 0, sizeof(view_t)); }
NVN(nvnTextureViewSetLevels, void, NVNtextureView *v, int base, int n) { VW(v)->set_levels = 1; VW(v)->base_level = base; VW(v)->nlevels = n; }
NVN(nvnTextureViewSetLayers, void, NVNtextureView *v, int min, int n) { VW(v)->set_layers = 1; VW(v)->min_layer = min; VW(v)->nlayers = n; }
NVN(nvnTextureViewSetFormat, void, NVNtextureView *v, NVNformat f) { VW(v)->set_format = 1; VW(v)->format = f; }
NVN(nvnTextureViewSetTarget, void, NVNtextureView *v, NVNtextureTarget t) { VW(v)->set_target = 1; VW(v)->target = t; }
// View swizzle (TotK main 0x531d20): a view property set explicitly replaces the texture's, and only the ones left unset come from the texture
// (NVN reference, nvn::TextureView), so even an identity swizzle set here overrides a swizzled texture. Applied where the texture pool entry's
// view is built (nvn_mtl.m tex_by_id). Stencil mode is not modelled (TotK sets depth only)
NVN(nvnTextureViewSetSwizzle, void, NVNtextureView *v, NVNtextureSwizzle r, NVNtextureSwizzle g, NVNtextureSwizzle bl, NVNtextureSwizzle a) {
    if ((unsigned)r > 5 || (unsigned)g > 5 || (unsigned)bl > 5 || (unsigned)a > 5) { LOG("[red] TextureViewSetSwizzle %d %d %d %d: not a swizzle value, ignored", (int)r, (int)g, (int)bl, (int)a); return; }
    VW(v)->set_swz = 1; VW(v)->swz_r = r; VW(v)->swz_g = g; VW(v)->swz_b = bl; VW(v)->swz_a = a;
}
NVN(nvnTextureViewSetDepthStencilMode, void, NVNtextureView *v, NVNtextureDepthStencilMode m) {
    (void)v; if (m) { static int once; if (!once++) LOG("[red] TextureViewSetDepthStencilMode %d: views read depth", (int)m); }
}
NVN(nvnTextureViewGetLayers, NVNboolean, const NVNtextureView *v, int *min, int *n) {
    const view_t *o = (const view_t *)v; *min = o->min_layer; *n = o->nlayers; return o->set_layers;
}
NVN(nvnTextureViewGetSwizzle, NVNboolean, const NVNtextureView *v, NVNtextureSwizzle *r, NVNtextureSwizzle *g, NVNtextureSwizzle *b, NVNtextureSwizzle *a) {   // (plan item 64a)
    const view_t *o = (const view_t *)v; *r = (NVNtextureSwizzle)o->swz_r; *g = (NVNtextureSwizzle)o->swz_g; *b = (NVNtextureSwizzle)o->swz_b; *a = (NVNtextureSwizzle)o->swz_a; return o->set_swz;
}
// The view's depth / stencil mode is not stored (views read depth, SetDepthStencilMode above): never set
NVN(nvnTextureViewGetDepthStencilMode, NVNboolean, const NVNtextureView *v, NVNtextureDepthStencilMode *m) { (void)v; *m = (NVNtextureDepthStencilMode)0; return 0; }
NVN(nvnTextureGetTextureAddress, NVNtextureAddress, const NVNtexture *t) { return (NVNtextureAddress)(uintptr_t)CTX(t)->mem; }   // its storage in the pool, as GetViewOffset counts from it
NVN(nvnTextureGetViewOffset, ptrdiff_t, const NVNtexture *t, const NVNtextureView *v) {
    const tex_t *o = CTX(t); const view_t *w = (const view_t *)v;
    if (!w) return 0;
    uint64_t off = w->set_levels ? nvn_tex_level_offset(&o->d, (unsigned)w->base_level) : 0;
    if (w->set_layers) off += (uint64_t)w->min_layer * o->layer_size;
    return (ptrdiff_t)off;
}
NVN_NOPROTO(nvnTextureBuilderGetViewOffset, ptrdiff_t, const NVNtextureBuilder *b, const NVNtextureView *v) {   // as the texture's, from the builder (plan item 64a)
    const tex_builder_t *o = (const tex_builder_t *)b; const view_t *w = (const view_t *)v;
    if (!w) return 0;
    nvn_texlayout L; nvn_tex_layout(&o->d, &L);
    uint64_t off = w->set_levels ? nvn_tex_level_offset(&o->d, (unsigned)w->base_level) : 0;
    if (w->set_layers) off += (uint64_t)w->min_layer * L.layer_size;
    return (ptrdiff_t)off;
}
// CPU texture read (NVN ReadTexels): for packaged textures the pool memory is the ground truth (de-layout and read directly); GPU-written textures are read back from the MTLTexture (nvn_mtl.m)
static void read_texels(const tex_t *o, const view_t *v, const NVNcopyRegion *r, uint8_t *dst, ptrdiff_t row, ptrdiff_t img) {
    int bw, bh, bpb = nvn_fmt_bpb(o->d.format, &bw, &bh);
    uint32_t wb = (uint32_t)((r->width + bw - 1) / bw) * bpb, hb = (uint32_t)(r->height + bh - 1) / bh;
    if (!row) row = wb;
    if (!img) img = row * hb;
    unsigned level = v && v->set_levels ? (unsigned)v->base_level : 0, layer0 = v && v->set_layers ? (unsigned)v->min_layer : 0;
    mtl_read_texels(o, level, layer0, r, dst, (size_t)row, (size_t)img);
}
// One thread at a time in the Metal layer: its submission state (current command buffer, blit encoder, render state) and the texel paths are not
// locked against each other. Manosaba calls SubmitCommands / PresentTexture / Read|WriteTexels from one thread, TotK writes texels from its
// ModuleSystemWorker threads while the Presentation Thread submits (a crash inside a copy encoder at a scene load had been seen on the Mac with the
// earlier check-only version), so these entries take one recursive lock. The first wait is logged once, naming both threads.
__attribute__((weak)) int thr_name_of(pthread_t pt, char *out, size_t n) { (void)pt; (void)out; (void)n; return 0; }   // host hle_thread.c (guest thread names); the graphics unit tests have none
static pthread_mutex_t g_gfx_mu; static pthread_once_t g_gfx_mu_once = PTHREAD_ONCE_INIT;
static uint64_t g_gfx_tid; static const char *g_gfx_what; static char g_gfx_name[96];   // the holder (written under the lock)
static void gfx_mu_init(void) { pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(&g_gfx_mu, &a); pthread_mutexattr_destroy(&a); }
static void gfx_mt_enter(const char *what) {
    pthread_once(&g_gfx_mu_once, gfx_mu_init);
    if (pthread_mutex_trylock(&g_gfx_mu)) {
        static _Atomic int said;
        if (!atomic_exchange(&said, 1)) {   // the holder's fields may be mid-update: diagnostics only
            uint64_t me; pthread_threadid_np(NULL, &me); char nm[96] = "-"; thr_name_of(pthread_self(), nm, sizeof nm);
            LOG("[nvn] %s on thread %llu (%s) waits for %s on thread %llu (%s): Metal layer entries are serialized", what, (unsigned long long)me, nm, g_gfx_what ? g_gfx_what : "?", (unsigned long long)g_gfx_tid, g_gfx_name);
        }
        pthread_mutex_lock(&g_gfx_mu);
    }
    pthread_threadid_np(NULL, &g_gfx_tid); g_gfx_what = what; if (!thr_name_of(pthread_self(), g_gfx_name, sizeof g_gfx_name)) strcpy(g_gfx_name, "-");
}
static void gfx_mt_leave(void) { pthread_mutex_unlock(&g_gfx_mu); }
NVN(nvnTextureReadTexels, void, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r, void *dst) {
    x_drain(); gfx_mt_enter("ReadTexels"); read_texels(CTX(t), (const view_t *)v, r, dst, 0, 0); gfx_mt_leave();
}
NVN(nvnTextureReadTexelsStrided, void, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r, void *dst, ptrdiff_t row, ptrdiff_t img) {
    x_drain(); gfx_mt_enter("ReadTexelsStrided"); read_texels(CTX(t), (const view_t *)v, r, dst, row, img); gfx_mt_leave();
}
NVN(nvnTextureInvalidateTexels, void, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r) { (void)t; (void)v; (void)r; }
// CPU texture write (NVN WriteTexels): stride 0 = tightly packed (same as ReadTexels)
static void write_texels(const tex_t *o, const view_t *v, const NVNcopyRegion *r, const uint8_t *src, ptrdiff_t row, ptrdiff_t img) {
    int bw, bh, bpb = nvn_fmt_bpb(o->d.format, &bw, &bh);
    uint32_t wb = (uint32_t)((r->width + bw - 1) / bw) * bpb, hb = (uint32_t)(r->height + bh - 1) / bh;
    if (!row) row = wb;
    if (!img) img = row * hb;
    unsigned level = v && v->set_levels ? (unsigned)v->base_level : 0, layer0 = v && v->set_layers ? (unsigned)v->min_layer : 0;
    mtl_write_texels(o, level, layer0, r, src, (size_t)row, (size_t)img);
}
NVN(nvnTextureWriteTexels, void, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r, const void *src) {
    gfx_mt_enter("WriteTexels"); write_texels(CTX(t), (const view_t *)v, r, src, 0, 0); gfx_mt_leave();
}
NVN(nvnTextureWriteTexelsStrided, void, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r, const void *src, ptrdiff_t row, ptrdiff_t img) {
    gfx_mt_enter("WriteTexelsStrided"); write_texels(CTX(t), (const view_t *)v, r, src, row, img); gfx_mt_leave();
}
NVN(nvnTextureFlushTexels, void, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r) {   // the CPU wrote the region's storage (mtl_flush_texels)
    const view_t *w = (const view_t *)v;
    unsigned level = w && w->set_levels ? (unsigned)w->base_level : 0, layer0 = w && w->set_layers ? (unsigned)w->min_layer : 0;
    gfx_mt_enter("FlushTexels"); mtl_flush_texels(CTX(t), level, layer0, r); gfx_mt_leave();
}

