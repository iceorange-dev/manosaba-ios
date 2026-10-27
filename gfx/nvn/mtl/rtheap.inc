// gfx/nvn/mtl/rtheap.inc, part of nvn_mtl.m (included there in order, one translation unit): Render targets of virtual pools in a heap, the memory census, the finalize queue
// ---------- Render targets of virtual pools in a heap (plan item 138; totk-mobile nvn_texture_alias.inc, Apache-2.0, written anew) ----------
#include "../nvn_rtheap.h"
// Tears of the Kingdom places its transient render targets at overlapping offsets of one virtual, CPU-inaccessible pool (111 MiB), and each
// layout got a Metal texture of its own: the cliff save makes 320 of them, 317 MB. With DM_NVN_RT_HEAP=1 such a pool gets a placement heap of
// RT_HEAP_SCALE times its size and a target goes at its guest offset × the scale when it fits there (nvn_rtheap.h): memory is shared where the game
// shares it and the heap bounds what the pool costs (the cliff save: a 139 MiB heap holds 263 of the 320, 57 others keep 6 MB of their own).
// On by default since item 166 where it can be (rt_heap_decide).
// Metal 4 only: every encoder starts behind a queue barrier (enc_r_begin and the others), which orders a target's last use before an overlapping
// one's writes; Apple does not say whether Metal 3's hazard tracking orders two textures of one heap. Off while the TBDR rewrites run: they merge
// passes and keep content facts (zero-source set, attachment ledger) by texture, and two textures in one heap range would break both. The game never
// writes these pools from the CPU, so nothing is uploaded into them
static struct { const pool_t *pool; uint64_t size; __strong id<MTLHeap> heap; } g_rth[8];
static uint64_t g_rth_placed, g_rth_placed_b, g_rth_out, g_rth_out_b, g_rtv_made;   // census: placed / not fitting (bytes), virtual-pool targets made
static NSHashTable *g_rtv_own; static pthread_mutex_t g_rtv_mu = PTHREAD_MUTEX_INITIALIZER;   // virtual-pool targets with a texture of their own (weak)
static double g_rth_scale = RT_HEAP_SCALE;
// On by default where it can be (plan item 166: Metal 4 with the TBDR rewrites off, as Tears of the Kingdom on the Mac): without it the soak of
// the 60 fps product stopped after 47 min at the GPU budget, its virtual-pool targets 3,185 textures of their own; with it the same soak's GPU
// memory stayed flat. DM_NVN_RT_HEAP=0 turns it off; =1 asked where it cannot be is reported
static int rt_heap_decide(const char *e, int m3, int tbdr) {
#ifdef DM_TEST_RTHEAP_DEFAULT_OFF   // negative control of t_rtheap only: off unless asked, as before
    if (!e) return 0;
#endif
    return !(e && strcmp(e, "1")) && !m3 && !tbdr;
}
static int rt_heap_on(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("DM_NVN_RT_HEAP"), *s = getenv("DM_NVN_RT_HEAP_SCALE");   // scale: experiments only
        if (s && atof(s) >= 1) g_rth_scale = atof(s);
        v = rt_heap_decide(e, DM_NVN_M3, DM_NVN_M3 ? 0 : tbdr_part(TBP_RTHEAP));
        if (e && !strcmp(e, "1") && !v) {
            if (DM_NVN_M3) MLOG("DM_NVN_RT_HEAP=1: not on Metal 3 (its hazard tracking is not known to order two textures of one heap): render targets keep textures of their own");
            else MLOG("DM_NVN_RT_HEAP=1: off while the TBDR rewrites run (DM_NVN_TBDR=0 turns them off)");
        }
        if (v) MLOG("render targets of virtual pools in heaps of %.3g times the pool%s", g_rth_scale, e ? "" : " (the default with the TBDR rewrites off; DM_NVN_RT_HEAP=0 turns it off)");
    }
    return v;
}
static int rt_virtual(const tex_t *t, MTLTextureDescriptor *d) {   // a render target of a virtual, CPU-inaccessible pool, within it
    const uint32_t vf = NVN_MEMORY_POOL_FLAGS_VIRTUAL | NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS;
    return t->pool && (t->pool->flags & vf) == vf && !(t->d.flags & NVN_TEXTURE_FLAGS_DISPLAY) && (d.usage & MTLTextureUsageRenderTarget)
        && d.storageMode == MTLStorageModePrivate && t->size && t->off >= 0 && (uint64_t)t->off + t->size <= t->pool->size;
}
static id<MTLTexture> rt_heap_texture(const tex_t *t, MTLTextureDescriptor *d) {   // nil: not placed (no heap, or it does not fit)
    int slot = -1;
    for (int i = 0; i < 8 && slot < 0; i++) if (g_rth[i].pool == t->pool && g_rth[i].size == t->pool->size) slot = i;
    if (slot < 0) {
        for (int i = 0; i < 8 && slot < 0; i++) if (!g_rth[i].heap) slot = i;
        if (slot < 0) { RED_ONCE(98, "render-target heaps: more than 8 virtual pools, the rest keep textures of their own"); return nil; }
        MTLHeapDescriptor *hd = [MTLHeapDescriptor new];
        hd.type = MTLHeapTypePlacement; hd.storageMode = MTLStorageModePrivate; hd.hazardTrackingMode = MTLHazardTrackingModeUntracked;
        hd.size = rt_heap_size(t->pool->size, g_rth_scale);
        id<MTLHeap> h = [g_dev newHeapWithDescriptor:hd];
        if (!h) { RED_ONCE(99, "render-target heap of %.1f MiB not made: textures of their own", hd.size / 1048576.0); return nil; }
        res_add((id<MTLResource>)h);   // resident as a whole: its textures are not added one by one
        g_rth[slot].pool = t->pool; g_rth[slot].size = t->pool->size; g_rth[slot].heap = h;   // a heap replaced (pool finalized, address reused) lives on in its textures
        MLOG("render-target heap: virtual pool %p of %.1f MiB → a heap of %.1f MiB", (const void *)t->pool, t->pool->size / 1048576.0, hd.size / 1048576.0);
    }
    id<MTLHeap> h = g_rth[slot].heap;
    MTLSizeAndAlign need = [g_dev heapTextureSizeAndAlignWithDescriptor:d]; uint64_t at;
    if (!rt_heap_place((uint64_t)t->off, t->size, g_rth_scale, need.size, need.align, h.size, &at)) { g_rth_out++; g_rth_out_b += need.size; return nil; }
    id<MTLTexture> mt = [h newTextureWithDescriptor:d offset:at];
    if (!mt) { g_rth_out++; g_rth_out_b += need.size; return nil; }
    g_rth_placed++; g_rth_placed_b += need.size;
    return mt;
}
static void rt_census_note(id<MTLTexture> mt, int placed) {
    g_rtv_made++;
    if (placed) return;
    pthread_mutex_lock(&g_rtv_mu); if (!g_rtv_own) g_rtv_own = [NSHashTable weakObjectsHashTable]; [g_rtv_own addObject:mt]; pthread_mutex_unlock(&g_rtv_mu);
}
static void rt_census_line(char *out, size_t n) {   // for the memory line: empty until a virtual-pool target was made
    out[0] = 0;
    if (!g_rtv_made) return;
    unsigned long long own = 0, heaps = 0; unsigned long nown = 0; int nh = 0;
    pthread_mutex_lock(&g_rtv_mu); NSArray *all = g_rtv_own.allObjects; pthread_mutex_unlock(&g_rtv_mu);
    for (id<MTLTexture> x in all) { own += x.allocatedSize; nown++; }
    for (int i = 0; i < 8; i++) if (g_rth[i].heap) { heaps += g_rth[i].heap.size; nh++; }
    size_t o = (size_t)snprintf(out, n, "; render targets of virtual pools: %llu made, %lu live with textures of their own (%llu MB)", (unsigned long long)g_rtv_made, nown, own >> 20);
    if (nh && o < n) snprintf(out + o, n - o, ", %d heap%s of %llu MB (placed %llu, %llu MB; not fitting %llu, %llu MB)", nh, nh == 1 ? "" : "s", heaps >> 20,
                              (unsigned long long)g_rth_placed, (unsigned long long)(g_rth_placed_b >> 20), (unsigned long long)g_rth_out, (unsigned long long)(g_rth_out_b >> 20));
}
static id<MTLTexture> tex_mtl(const tex_t *ct) {
    tex_t *t = (tex_t *)ct;
    if (!t || !t->serial) return nil;
    int packaged = t->has_layout && t->mem;
    id have = serial_get(t->serial);
    if (!packaged && tex_cpu_candidate(t) && !gpu_owned(t->serial) && on_sub_thread()) {   // CPU-filled (see above); uploads run on the submit thread only
        if (have) packaged = [have isKindOfClass:[NvPkg class]] || cpuf_written(t);
        else { packaged = storage_filled(t); if (!packaged) cpuf_watch(t); wb_census_first(t); }
    }
    if (packaged) {
        NvPkg *pk = [have isKindOfClass:[NvPkg class]] ? have : nil;   // a watched GPU-only texture the CPU filled later gets its first upload here (serial_put retires the old one)
        if (pk) {   // Already uploaded: checked once per submission (below), the covered pages only if there has been a guest write since the last check
            // Once per submission on the submit thread: its contents are those of the submit call, as for uniform snapshots (MacNativeGames, MIT: checking at every
            // use whenever any game thread had written any page since was 4.5 % of its replay). Elsewhere (ReadTexels, present) every call checks
            int sub = on_sub_thread();
            if (sub && pk->csub == g_sub_serial && pk->tex && check_once()) return pk->tex;
#ifndef DM_TEST_PKG_AGE_FIRST   // negative control of t_uppool only: a later use not noted (the census would count from the upload)
            if (sub) pk->csub = g_sub_serial;
#endif
            uint64_t now = nvn_dirty_now(), h = 0; int clean;
            if (pk->hashed) { h = pkg_hash(t->mem, t->size); clean = h == pk->chash; }
            else {   // no guest write anywhere since the last check of a tracked texture: clean without looking its range up (a walk of all pools)
                int st = now == pk->seen && pk->tracked ? 0 : nvn_dirty_state_hint(t->mem, t->size, pk->stamp, &pk->dspan);
                clean = st == 0; pk->tracked = st != 2; pk->seen = now;
            }
            if (clean && atomic_load_explicit(&g_up_lost_n, memory_order_relaxed) && up_lost_take(pk->tex)) { clean = 0; uint64_t k = atomic_load(&g_up_lost_redo); if (k <= 3 || !atomic_load(&g_up_lost_n)) MLOG("texture from a lost upload ser=%llu re-uploaded (%llu in total, %d still recorded)", (unsigned long long)t->serial, (unsigned long long)k, atomic_load(&g_up_lost_n)); }
            if (clean) {
                if (dirtycheck() && full_hash(t->mem, t->size) != pk->hash && !g_pkg_miss++)
                    MLOG("[red] missed dirty page: texture ser=%llu pool contents changed but were not detected", t->serial);
                if (pk->spare && g_fence > pk->spare_fence + PKG_SPARE_KEEP) { defer_obj_release(pk->spare); pk->spare = nil; g_pkg_spare_drop++; }   // not re-uploaded again for a while: the spare's memory back
                return pk->tex;
            }
            { static int rl = -1; if (rl < 0) rl = getenv("DM_NVN_REUPLOG") ? atoi(getenv("DM_NVN_REUPLOG")) : 0;   // diagnostic: log the first N re-uploads
              if (rl > 0) { static _Atomic int nrl; if (atomic_fetch_add(&nrl, 1) < rl) MLOG("[reupload] ser=%llu %ux%ux%u lv%u fmt=%#x tflags=%#x pflags=%#x layout=%d storage %p size %#x frame %llu",
                  (unsigned long long)t->serial, t->d.width, t->d.height, t->d.depth, t->d.levels, t->d.format, t->d.flags, t->pool ? t->pool->flags : 0u, t->has_layout, (void *)t->mem, t->size, (unsigned long long)atomic_load(&g_cur_frame)); } }
            // Pool content changed (streamed assets written into the same storage): switch textures; in-flight draws still read the old one, which becomes
            // the spare, written by a later re-upload once its last submission has completed instead of making a new texture each time (nxnative's NvPkg
            // spare, MIT: Link's Awakening's title movie re-uploaded an 8 MB texture every frame, 1.64 -> 0.37 ms a submission)
            id<MTLTexture> reuse = pk->spare && mtl_done_fence() >= pk->spare_fence ? pk->spare : nil;
            uint64_t st; id<MTLTexture> nt = pkg_upload(t, &st, reuse);
            if (nt) { id<MTLTexture> old = pk->tex;
                      if (nt == reuse) g_pkg_spare_reuse++; else if (pk->spare) defer_obj_release(pk->spare);   // a spare still in use or of another shape: retired
                      pk->spare = old; pk->spare_fence = g_fence + 1; pk->tex = nt; res_add(nt); ser_bump(t->serial); pk->stamp = st; pk->seen = st; pk->tracked = !pk->hashed && nvn_dirty_tracked(t->mem, t->size); if (pk->hashed) pk->chash = h; g_pkg_reuploads++; if (dirtycheck()) pk->hash = full_hash(t->mem, t->size); }
            return pk->tex;
        }
        pk = [NvPkg new]; pk->dspan = -1;
        if (counts_on()) { if (!g_pkgs) g_pkgs = [NSHashTable weakObjectsHashTable]; [g_pkgs addObject:pk]; }
        pk->tex = pkg_upload(t, &pk->stamp, nil);
        if (!pk->tex) return nil;
        // seen: the upload's stamp, not the clock after it (here and at a re-upload above): a write released during the upload moved the clock, and
        // "the clock has not moved since seen" would have skipped its check (seedds/nxnative 5b0c11a, MIT)
        pk->seen = pk->stamp; g_pkg_uploads++; if (dirtycheck()) pk->hash = full_hash(t->mem, t->size);
        if ((pk->hashed = pkg_hashed(t))) pk->chash = pkg_hash(t->mem, t->size); else pk->tracked = nvn_dirty_tracked(t->mem, t->size);
        if (on_sub_thread()) pk->csub = g_sub_serial;
        serial_put(t->serial, pk);
        return pk->tex;
    }
    if (have) return have;
    if (t->d.target == 10) {   // Buffer texture (TARGET_BUFFER): create the texture-buffer view directly on the memory pool buffer (zero copy; the GPU reads the pool's current content at execution time, same as NVN semantics)
        uint32_t pf = 0; uint8_t swz[4]; int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
        void *mb; uint64_t mo;
        if (!nvn_fmt_to_mtl(t->d.format, &pf, swz) || !pf || !bpb) { RED_ONCE(36, "buffer texture format %#x not mapped", t->d.format); return nil; }
        if (!t->mem || !mtl_addr((uint64_t)t->mem, (uint64_t)t->d.width * bpb, &mb, &mo)) { RED_ONCE(37, "buffer texture storage is not in a memory pool"); return nil; }
        id<MTLBuffer> b = (__bridge id<MTLBuffer>)mb;
        NSUInteger al = [g_dev minimumLinearTextureAlignmentForPixelFormat:(MTLPixelFormat)pf];
        if (mo % al) { RED_ONCE(38, "buffer texture offset %#llx does not meet %lu-byte alignment", (unsigned long long)mo, (unsigned long)al); return nil; }
        MTLTextureDescriptor *d = [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:(MTLPixelFormat)pf width:t->d.width resourceOptions:b.resourceOptions
                                                                                         usage:MTLTextureUsageShaderRead | (t->d.flags & NVN_TEXTURE_FLAGS_IMAGE ? MTLTextureUsageShaderWrite : 0)];
        size_t row = ((size_t)t->d.width * bpb + al - 1) / al * al;
        id<MTLTexture> mt = [b newTextureWithDescriptor:d offset:mo bytesPerRow:row];
        if (!mt) { RED_ONCE(39, "cannot create buffer texture: %u elements format %#x", t->d.width, t->d.format); return nil; }
        res_add(mt);
        serial_put(t->serial, mt);
        return mt;
    }
    { id<MTLTexture> lt = tex_linear(t); if (lt) { res_add(lt); serial_put(t->serial, lt); return lt; } }
    MTLTextureDescriptor *d = tex_desc(t, 0);
    if (!d) return nil;
    { static int cls = -1; if (cls < 0) cls = getenv("DM_NVN_TEXCLASS") != NULL;   // diagnostic: a GPU-only texture's flags, its pool's flags and how much of its guest storage the CPU filled
      if (cls) { size_t nw = t->mem ? MIN((size_t)t->size, (size_t)64 << 20) / 8 : 0, nz = 0; const uint64_t *w = (const uint64_t *)(const void *)t->mem; for (size_t i = 0; i < nw; i++) nz += w[i] != 0;
          MLOG("[texclass] ser=%llu %ux%ux%u lv%u fmt=%#x tgt=%d tflags=%#x pflags=%#x storage %#x non-zero %.4f", (unsigned long long)t->serial, t->d.width, t->d.height, t->d.depth, t->d.levels,
               t->d.format, t->d.target, t->d.flags, t->pool ? t->pool->flags : 0u, t->size, nw ? (double)nz / nw : 0.0); } }
    stor_key sk; int share = stor_key_of(t, &sk);
    if (share) {   // another live (or lately left) object of this storage, layout and format: its texture
        pthread_mutex_lock(&g_smu); stor_ent *e = nvn_hm_get(&g_stor, &sk); id<MTLTexture> st = e ? (__bridge id<MTLTexture>)e->tex : nil; pthread_mutex_unlock(&g_smu);
        if (st && (st.usage & d.usage) == d.usage) {
            uint64_t n = ++g_stor_shared; if (n <= 4 || !(n % 1000)) MLOG("texture %llu (%ux%u format %#x) shares the texture of its storage with another object (%llu so far)", (unsigned long long)t->serial, t->d.width, t->d.height, t->d.format, (unsigned long long)n);
            serial_put(t->serial, st); return st; }
        if (st) g_stor_usage_miss++;   // it lacks a usage this one needs: a texture of its own, as before
    }
    int rtv = rt_virtual(t, d);
    id<MTLTexture> mt = rtv && rt_heap_on() ? rt_heap_texture(t, d) : nil; int placed = mt != nil;
    if (!mt) mt = [g_dev newTextureWithDescriptor:d];
    if (!mt) { RED_ONCE(2, "failed to create MTLTexture %ux%u format %#x target %d", t->d.width, t->d.height, t->d.format, t->d.target); return nil; }
    if (!placed) res_add(mt);   // a placed texture is resident with its heap
    if (rtv) rt_census_note(mt, placed);
    if (share) {
        pthread_mutex_lock(&g_smu);
        if (!nvn_hm_get(&g_stor, &sk)) { stor_ent *e = calloc(1, sizeof *e); if (!e) MDIE("storage entry"); e->k = sk; e->tex = (void *)CFBridgingRetain(mt);
            uint64_t ok = (uint64_t)(uintptr_t)e->tex; nvn_hm_put(&g_stor, &sk, e); nvn_hm_put(&g_stor_obj, &ok, e);
            if (g_nstor == g_stor_cap) { g_stor_cap = g_stor_cap ? g_stor_cap * 2 : 256; g_stor_list = realloc(g_stor_list, (size_t)g_stor_cap * sizeof *g_stor_list); if (!g_stor_list) MDIE("storage list"); }
            g_stor_list[g_nstor++] = e; g_stor_entries++; }
        pthread_mutex_unlock(&g_smu);
    }
    serial_put(t->serial, mt);
    return mt;
}
// Memory readout line (always on, low frequency, capped): resident memory, GPU allocated, deferred release queue, residency set object count, registered memory pool count / peak / tracking table capacity, cumulative texture re-uploads, game heap total / free / largest allocatable block (host dm_guest_heap_stat), and how far from the system memory limit (iOS only).
// Purpose: to pin down "crashes with no log" -- the system gives no notice before killing a process for memory, and only readouts left beforehand can show which number kept climbing.
// Called by the frame hook every 300 frames; printed when resident or GPU allocated changed by >= 64 MB, or game heap free space or largest allocatable block changed by >= 16 MB (the main heap normally has only about 100 MB left), otherwise one line every 3000 frames; capped at 400 lines per run. All integers (the frame hook runs on the small stack of a guest thread, so no floating-point formatting)
static void res_census(unsigned long long frame, unsigned long long rss_mb) {
    @autoreleasepool {
        unsigned long nl[2] = {0, 0}, nr[2] = {0, 0}; unsigned long long bl[2] = {0, 0}, br[2] = {0, 0};   // [0] textures [1] buffers; l = in the residency set, r = retired but not destroyed
        pthread_mutex_lock(&g_res_mu);
        for (id o in g_res_ever.allObjects) { int k = [o conformsToProtocol:@protocol(MTLBuffer)] ? 1 : 0; unsigned long long b = (unsigned long long)[(id<MTLResource>)o allocatedSize];
            if ([g_res_objects containsObject:o]) { nl[k]++; bl[k] += b; } else { nr[k]++; br[k] += b;
                if (!k && nr[0] <= 3) { id<MTLTexture> t = o; MLOG("residency census: retired texture still alive %p %lux%lu format %lu usage %#lx (%llu MB)", (__bridge void *)o,
                    (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.pixelFormat, (unsigned long)t.usage, b >> 20); } } }
        pthread_mutex_unlock(&g_res_mu);
        MLOG("residency census up to frame %llu: RSS %llu MB, GPU allocated %llu MB; in the residency set: %lu textures %llu MB, %lu buffers %llu MB; retired but not destroyed: %lu textures %llu MB, %lu buffers %llu MB", frame, rss_mb, (unsigned long long)(g_dev.currentAllocatedSize >> 20),
             nl[0], bl[0] >> 20, nl[1], bl[1] >> 20, nr[0], br[0] >> 20, nr[1], br[1] >> 20);
    }
}
// DM_NVN_RT_CENSUS=1 (plan item 280): at the memory line, the render targets of recorded commands (the snapshots, core/live.c) by pool: how many,
// their Metal bytes (each MTLTexture once: storage-shared ones are one), the union of their guest ranges against the sum, and how many overlap
// another one; the eight pools with the most Metal bytes
typedef struct { const pool_t *pool; int64_t off; uint32_t size; int depth; __unsafe_unretained id<MTLTexture> m; } rtc_ent;
static int rtc_by_pool(const void *x, const void *y) { const rtc_ent *a = x, *b = y; return a->pool != b->pool ? (a->pool < b->pool ? -1 : 1) : (a->off > b->off) - (a->off < b->off); }
static int rtc_by_obj(const void *x, const void *y) { const rtc_ent *a = x, *b = y; return ((uintptr_t)(__bridge void *)a->m > (uintptr_t)(__bridge void *)b->m) - ((uintptr_t)(__bridge void *)a->m < (uintptr_t)(__bridge void *)b->m); }
static void rt_census(unsigned long long frame) { @autoreleasepool {
    int cap = 65536; tex_t *ts = malloc(sizeof *ts * (size_t)cap); rtc_ent *e = malloc(sizeof *e * (size_t)cap); if (!ts || !e) { free(ts); free(e); return; }
    int nt = tex_snap_copy(ts, cap); if (nt > cap) nt = cap;
    int n = 0;
    for (int i = 0; i < nt; i++) { const tex_t *t = &ts[i]; if (!t->pool || t->off < 0 || !t->size) continue;
        __unsafe_unretained id o = serial_get(t->serial); if (!o || ![o conformsToProtocol:@protocol(MTLTexture)]) continue;
        id<MTLTexture> m = o; if (!(m.usage & MTLTextureUsageRenderTarget)) continue;
        e[n++] = (rtc_ent){t->pool, t->off, t->size, pf_is_depth_stencil(m.pixelFormat), m}; }
    qsort(e, (size_t)n, sizeof *e, rtc_by_pool);
    typedef struct { const pool_t *pool; int n, over, nd; uint64_t mtl, md, guest, uni; } rtc_pool;
    rtc_pool *pp = calloc((size_t)n + 1, sizeof *pp); int np = 0; uint64_t all = 0, alld = 0; int uniq = 0;
    for (int i = 0; i < n;) {
        int j = i; while (j < n && e[j].pool == e[i].pool) j++;
        rtc_pool *q = &pp[np++]; q->pool = e[i].pool;
        int64_t hi = -1, lo = 0;
        for (int k = i; k < j; k++) { q->n++; q->guest += e[k].size;
            int64_t a = e[k].off, b = e[k].off + e[k].size;
            if (a < hi) { q->over++; if (b > hi) { q->uni += (uint64_t)(b - hi); hi = b; } }
            else { q->uni += (uint64_t)(b - a); lo = a; hi = b; } }
        (void)lo;
        qsort(e + i, (size_t)(j - i), sizeof *e, rtc_by_obj);
        for (int k = i; k < j; k++) if (k == i || e[k].m != e[k - 1].m) { uint64_t b = (uint64_t)e[k].m.allocatedSize; q->mtl += b; uniq++; if (e[k].depth) { q->md += b; q->nd++; } }
        all += q->mtl; alld += q->md; i = j;
    }
    MLOG("render-target census at frame %llu: %d targets of recorded commands, %d Metal textures %.0f MB (depth / stencil %.0f MB), in %d pools", frame, n, uniq, all / 1048576.0, alld / 1048576.0, np);
    for (int r = 0; r < 8 && r < np; r++) { int best = r; for (int k = r + 1; k < np; k++) if (pp[k].mtl > pp[best].mtl) best = k; rtc_pool t = pp[r]; pp[r] = pp[best]; pp[best] = t;
        const rtc_pool *q = &pp[r];
        MLOG("  pool %p flags %#x %.1f MB: %d targets (%d depth / stencil), Metal %.1f MB (depth / stencil %.1f), guest %.1f MB, their union %.1f MB, %d overlap one before",
             (const void *)q->pool, q->pool->flags, q->pool->size / 1048576.0, q->n, q->nd, q->mtl / 1048576.0, q->md / 1048576.0, q->guest / 1048576.0, q->uni / 1048576.0, q->over); }
    free(pp); free(e); free(ts);
} }
void dm_alias_stat(size_t *mapped_mb, size_t *host_mb); int dm_mem_low(void);
// Metal objects in the residency set by kind, for the low-memory watch (memstat.m; any thread): private render-target textures, other private textures,
// shared buffers wrapping guest memory pools (no copy: their memory is the guest's), other buffers (upload staging and the like); count and MB each,
// the deferred-release queue, and the five largest textures
static int pf_is_depth_stencil(MTLPixelFormat f) { return f == MTLPixelFormatDepth16Unorm || f == MTLPixelFormatDepth32Float || f == MTLPixelFormatStencil8 || f == MTLPixelFormatDepth32Float_Stencil8 || f == MTLPixelFormatX32_Stencil8; }
void mtl_mem_census(char *out, size_t n) { @autoreleasepool {
    unsigned long cnt[5] = {0}; unsigned long long mb[5] = {0}; struct { unsigned long long b; unsigned w, h, f, rt; } top[5] = {{0}};
    pthread_mutex_lock(&g_mu); NSMutableSet *pools = [NSMutableSet set]; for (int i = 0; i < g_npool; i++) [pools addObject:g_pools[i].buf]; pthread_mutex_unlock(&g_mu);
    pthread_mutex_lock(&g_res_mu); NSArray *all = g_res_objects.allObjects; pthread_mutex_unlock(&g_res_mu);
    for (id o in all) {
        unsigned long long b = (unsigned long long)[(id<MTLResource>)o allocatedSize]; int k;
        if ([o conformsToProtocol:@protocol(MTLTexture)]) { id<MTLTexture> t = o; int rt = (t.usage & MTLTextureUsageRenderTarget) != 0, ds = pf_is_depth_stencil(t.pixelFormat); k = rt ? (ds ? 4 : 0) : 1;
            for (int i = 0; i < 5; i++) if (b > top[i].b) { memmove(&top[i + 1], &top[i], (4 - i) * sizeof top[0]); top[i].b = b; top[i].w = (unsigned)t.width; top[i].h = (unsigned)t.height; top[i].f = (unsigned)t.pixelFormat; top[i].rt = rt; break; } }
        else if ([o conformsToProtocol:@protocol(MTLHeap)]) k = 0;   // a render-target heap (rt_heap_texture): its textures are not in the set
        else k = [pools containsObject:o] ? 2 : 3;
        cnt[k]++; mb[k] += b;
    }
    pthread_mutex_lock(&g_deferred_mu); unsigned long nd = (unsigned long)g_deferred.count; pthread_mutex_unlock(&g_deferred_mu);
    size_t o = (size_t)snprintf(out, n, "color render targets %lu / %llu MB, depth/stencil render targets %lu / %llu MB, other textures %lu / %llu MB, pool buffers %lu / %llu MB, other buffers %lu / %llu MB, deferred-release queue %lu; largest textures:",
        cnt[0], mb[0] >> 20, cnt[4], mb[4] >> 20, cnt[1], mb[1] >> 20, cnt[2], mb[2] >> 20, cnt[3], mb[3] >> 20, nd);
    for (int i = 0; i < 5 && top[i].b && o < n; i++) o += (size_t)snprintf(out + o, n - o, " %ux%u fmt %u%s %llu MB", top[i].w, top[i].h, top[i].f, top[i].rt ? " RT" : "", top[i].b >> 20);
} }
unsigned long long mtl_gpu_allocated_mb(void) { id<MTLDevice> d = g_dev; return d ? (unsigned long long)(d.currentAllocatedSize >> 20) : 0; }   // GPU allocated (0 before the graphics layer starts); callable from any thread
// The command allocators' memory (Metal 4; plan item 178, lordran-native 24740d4, MIT): the main submission's, the uploads', the presents'; zeros on Metal 3
void mtl_cmd_alloc_bytes(uint64_t out[3]) {
    out[0] = out[1] = out[2] = 0;
#if !DM_NVN_M3
    for (int i = 0; i < g_alloc_n; i++) out[0] += g_alloc4[i].allocatedSize;
    for (int i = 0; i < 3; i++) { out[1] += g_up_alloc4[i].allocatedSize; out[2] += g_present_alloc4[i].allocatedSize; }
#endif
}
void mtl_mem_stat(unsigned long long frame, unsigned long long rss_mb, int heap_ok, unsigned long long heap_mb, unsigned long long heap_free_mb, unsigned long long heap_maxblk_kb) {
    if (g_res_census > 0) res_census(frame, rss_mb);
    { static int rc = -1; if (rc < 0) rc = getenv("DM_NVN_RT_CENSUS") && strcmp(getenv("DM_NVN_RT_CENSUS"), "0"); if (rc) rt_census(frame); }
    static unsigned long long last_rss, last_gpu, last_frame, last_hfree, last_hmax; static int lines; unsigned long long gpu = (unsigned long long)(g_dev.currentAllocatedSize >> 20);
    unsigned long long dr = rss_mb > last_rss ? rss_mb - last_rss : last_rss - rss_mb, dg = gpu > last_gpu ? gpu - last_gpu : last_gpu - gpu;
    unsigned long long dh = heap_free_mb > last_hfree ? heap_free_mb - last_hfree : last_hfree - heap_free_mb, hmax_mb = heap_maxblk_kb >> 10, dm = hmax_mb > last_hmax ? hmax_mb - last_hmax : last_hmax - hmax_mb;
    static int all = -1; if (all < 0) all = getenv("DM_MEM_STAT_ALL") != NULL;   // For measurement: printed every time, uncapped
    int every = all || dm_mem_low();   // also within 1 GB of the iOS memory limit (memstat.m low-memory watch): past the line cap, since the last readouts before the system kills the process are the useful ones
    if (!every && (lines >= 400 || (lines && dr < 64 && dg < 64 && dh < 16 && dm < 16 && frame - last_frame < 3000))) return; lines++; last_rss = rss_mb; last_gpu = gpu; last_frame = frame; last_hfree = heap_free_mb; last_hmax = hmax_mb;
    pthread_mutex_lock(&g_deferred_mu); unsigned long nd = (unsigned long)g_deferred.count; pthread_mutex_unlock(&g_deferred_mu);
    pthread_mutex_lock(&g_res_mu); unsigned long nr = (unsigned long)g_res_objects.count; pthread_mutex_unlock(&g_res_mu);
    int live = 0, peak = 0, cap = 0; nvn_dirty_stat(&live, &peak, &cap);
    char avail[32] = "none";   // os_proc_available_memory: how far this process is from being terminated by the system for memory, iOS only; the Mac host writes "none"
#if TARGET_OS_IPHONE
    snprintf(avail, sizeof avail, "%llu MB", (unsigned long long)(os_proc_available_memory() >> 20));
#endif
    size_t am, ah; dm_alias_stat(&am, &ah);
    char rtv[512]; rt_census_line(rtv, sizeof rtv);
#if !DM_NVN_M3 && !defined(DM_TEST_MEMLINE_NO_ALLOC)   // (the macro: negative control of t_cmdalloc only)
    { uint64_t ca[3]; mtl_cmd_alloc_bytes(ca); size_t l = strlen(rtv);
      snprintf(rtv + l, sizeof rtv - l, "; command allocators %.1f MB (%d for the main submission), uploads %.1f MB, presents %.1f MB", ca[0] / 1048576.0, g_alloc_n, ca[1] / 1048576.0, ca[2] / 1048576.0); }
#endif
    MLOG("memory up to frame %llu: RSS %llu MB, GPU allocated %llu MB, deferred-release queue %lu, residency set %lu, memory pools %d (peak %d / tracking table capacity %d), texture re-uploads %llu in total; game heap %llu MB, free %llu MB, largest allocatable block %llu KB%s; alias region mapped %zu MB in %zu MB of host pages; headroom to the system memory limit %s%s", frame, rss_mb, gpu, nd, nr, live, peak, cap, (unsigned long long)g_pkg_reuploads,
        heap_mb, heap_free_mb, heap_maxblk_kb, heap_ok ? "" : " (not read)", am, ah, avail, rtv);
}
void mtl_pkg_stats(uint64_t *uploads, uint64_t *reuploads) { *uploads = g_pkg_uploads; *reuploads = g_pkg_reuploads; }
uint64_t mtl_pkg_miss(void) { return g_pkg_miss; }
// nvnTextureFinalize: the texture stays usable for FIN_GRACE more submissions before it is released. The game may finalize a texture that commands
// it recorded earlier and submits afterwards still sample by ID (seen at a scene change); on the console the descriptor still names the storage, so
// they read its content. Serials are never reused (nvn_new_serial), so a pending release cannot hit a new texture
enum { FIN_GRACE = 16 };
typedef struct { uint64_t serial, due; } fin_t;   // due: submission index at which it is released (0 = not yet stamped by the submit thread)
static fin_t *g_fin; static int g_nfin, g_fincap; static pthread_mutex_t g_fin_mu = PTHREAD_MUTEX_INITIALIZER;
static void fin_queue_due(uint64_t serial, uint64_t due) {   // due 0: FIN_GRACE submissions after the submit thread first sees it
    pthread_mutex_lock(&g_fin_mu);
    if (g_nfin == g_fincap) { g_fincap = g_fincap ? g_fincap * 2 : 256; g_fin = realloc(g_fin, g_fincap * sizeof *g_fin); }
    g_fin[g_nfin++] = (fin_t){serial, due};
    pthread_mutex_unlock(&g_fin_mu);
}
static void fin_queue(uint64_t serial) { fin_queue_due(serial, 0); }
enum { RECREATE_GRACE = 600 };   // tex_by_id: a texture made again for an ID used after its release stays this long
static uint64_t g_late_kept, g_late_blank;
