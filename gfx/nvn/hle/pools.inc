// gfx/nvn/hle/pools.inc, part of hle_nvn.c (included there in order, one translation unit): Memory pools, buffers, descriptor pools
// ---------- Memory pools ----------
NVN(nvnMemoryPoolBuilderSetDefaults, void, NVNmemoryPoolBuilder *b) { memset(b, 0, sizeof(pool_builder_t)); }
NVN(nvnMemoryPoolBuilderSetDevice, void, NVNmemoryPoolBuilder *b, NVNdevice *d) { ((pool_builder_t *)b)->dev = d; }
NVN(nvnMemoryPoolBuilderSetStorage, void, NVNmemoryPoolBuilder *b, void *m, size_t n) { ((pool_builder_t *)b)->mem = m; ((pool_builder_t *)b)->size = n; }
NVN(nvnMemoryPoolBuilderSetFlags, void, NVNmemoryPoolBuilder *b, int f) { ((pool_builder_t *)b)->flags = f; }
NVN(nvnMemoryPoolInitialize, NVNboolean, NVNmemoryPool *p, const NVNmemoryPoolBuilder *b) {
    // The backend's setup (a Metal buffer over the memory, or on Vulkan device memory made and filled; the dirty tracking's protection) gives the
    // core up: the game's loader threads held one through it (valentderah/totk-pc-recomp 101f2c8, MIT: up to 20 ms; plan item 297)
#ifndef DM_TEST_POOL_KEEPS_CORE   // negative control of t_pools only: the core kept, as before
    NVN_BLOCKING();
#endif
    const pool_builder_t *s = (const pool_builder_t *)b; pool_t *o = (pool_t *)p;
    o->magic = MAGIC('P', 'O', 'O', 'L'); o->flags = s->flags; o->mem = s->mem; o->size = s->size;
    if (o->mem) { mtl_pool_add(o->mem, o->size); nvn_dirty_add_range(o->mem, o->size); }
    if (!o->mem && !(o->flags & 0x200)) LOG("[red] MemoryPoolInitialize: non-virtual pool has no storage flags=%#x size=%#llx", o->flags, o->size);
    OLOG("MemoryPool %p mem=%p size=%#llx flags=%#x", (void *)p, (void *)o->mem, o->size, o->flags);
    return 1;
}
NVN(nvnMemoryPoolFinalize, void, NVNmemoryPool *p) {
    x_drain();   // a queued replay may still use it (plan item 61)
    pool_t *o = (pool_t *)p;
    if (o->mem && !(o->flags & 0x200)) { nvn_dirty_remove_range(o->mem); mtl_pool_del(o->mem); }   // a virtual pool's storage belongs to a physical pool (MapVirtual)
    o->magic = 0;
}
// Virtual pool (flags 0x200): a GPU address range without storage; MapVirtual maps pages of physical pools into it. A request is 0x28 bytes: {physical pool,
// physical offset, virtual offset, size, storage class}. When every request has the same base (same physical pool, same physical offset - virtual offset), the
// virtual pool's storage points straight at that memory of the physical pool, which is what one set of physical pages is on the console, and buffer / Metal
// lookups land on the block the physical pool registered. Mappings with different bases (truly sparse) are not implemented: reported, fails.
// From nxnative's hle_nvn.c (seedds, MIT); TotK main 0x8d04dc maps one at startup
typedef struct { NVNmemoryPool *phys; int64_t poff, voff; uint64_t size; int32_t storage_class, _pad; } map_req;
_Static_assert(sizeof(map_req) == 0x28, "NVNmappingRequest");
NVN(nvnMemoryPoolMapVirtual, NVNboolean, NVNmemoryPool *p, int n, const NVNmappingRequest *rq) {
    pool_t *o = (pool_t *)p; const map_req *r = (const map_req *)rq;
    for (int i = 0; i < n; i++) {
        const pool_t *ph = (const pool_t *)r[i].phys;
        if (!ph) continue;   // unmapping (no physical pool): the storage is kept
        if (!ph->mem) { LOG("[red] MapVirtual: physical pool %p has no storage", (const void *)ph); return 0; }
        if (r[i].voff < 0 || (uint64_t)r[i].voff + r[i].size > o->size || (uint64_t)r[i].poff + r[i].size > ph->size) {
            LOG("[red] MapVirtual: request out of range (virtual %#llx+%#llx / pool %#llx, physical %#llx / pool %#llx)", (unsigned long long)r[i].voff,
                (unsigned long long)r[i].size, (unsigned long long)o->size, (unsigned long long)r[i].poff, (unsigned long long)ph->size);
            return 0;
        }
        uint8_t *base = ph->mem + r[i].poff - r[i].voff;
        if (o->mem && o->mem != base) { LOG("[red] MapVirtual: sparse mapping not implemented: virtual pool %p is on %p, this request wants %p", (void *)o, (void *)o->mem, (void *)base); return 0; }
        if (!o->mem) LOG("[nvn] MapVirtual: virtual pool %p (%#llx bytes) uses physical pool %p + %#llx", (void *)o, (unsigned long long)o->size, (const void *)ph, (unsigned long long)(r[i].poff - r[i].voff));
        o->mem = base;
    }
    return 1;
}
NVN(nvnMemoryPoolFlushMappedRange, void, const NVNmemoryPool *p, ptrdiff_t off, size_t n) { (void)p; (void)off; (void)n; }
NVN(nvnMemoryPoolGetBufferAddress, NVNbufferAddress, const NVNmemoryPool *p) { return (NVNbufferAddress)((const pool_t *)p)->mem; }
// Pool memory is the real memory given by the game (same address for CPU and GPU), so mapping returns the original pointer; cache maintenance has nothing to do
NVN(nvnMemoryPoolMap, void *, const NVNmemoryPool *p) { return ((const pool_t *)p)->mem; }
void mtl_pool_invalidate(const pool_t *p, uint64_t off, uint64_t n);   // the CPU is about to read what the GPU wrote: GPU results of this range are written back (nvn_mtl.m)
NVN(nvnMemoryPoolInvalidateMappedRange, void, const NVNmemoryPool *p, ptrdiff_t off, size_t n) { x_drain(); if (off >= 0) mtl_pool_invalidate((const pool_t *)p, (uint64_t)off, n); }
NVN(nvnMemoryPoolGetSize, size_t, const NVNmemoryPool *p) { return ((const pool_t *)p)->size; }
NVN(nvnMemoryPoolGetFlags, NVNmemoryPoolFlags, const NVNmemoryPool *p) { return (NVNmemoryPoolFlags)((const pool_t *)p)->flags; }
NVN(nvnMemoryPoolBuilderGetSize, size_t, const NVNmemoryPoolBuilder *b) { return ((const pool_builder_t *)b)->size; }
NVN(nvnMemoryPoolBuilderGetFlags, NVNmemoryPoolFlags, const NVNmemoryPoolBuilder *b) { return (NVNmemoryPoolFlags)((const pool_builder_t *)b)->flags; }

// ---------- Buffers: a range inside a pool, address = pool memory + offset ----------
#define BB(b) ((buf_builder_t *)(b))
#define BF(b) ((const buf_t *)(b))
NVN(nvnBufferBuilderSetDefaults, void, NVNbufferBuilder *b) { NVNdevice *d = BB(b)->dev; memset(b, 0, sizeof(buf_builder_t)); BB(b)->dev = d; }
NVN(nvnBufferBuilderSetDevice, void, NVNbufferBuilder *b, NVNdevice *d) { BB(b)->dev = d; }
NVN(nvnBufferBuilderSetStorage, void, NVNbufferBuilder *b, NVNmemoryPool *p, ptrdiff_t off, size_t n) { BB(b)->pool = (pool_t *)p; BB(b)->off = off; BB(b)->size = n; }
NVN(nvnBufferBuilderGetMemoryOffset, ptrdiff_t, const NVNbufferBuilder *b) { return ((const buf_builder_t *)b)->off; }
NVN(nvnBufferBuilderGetSize, size_t, const NVNbufferBuilder *b) { return ((const buf_builder_t *)b)->size; }
NVN(nvnBufferInitialize, NVNboolean, NVNbuffer *b, const NVNbufferBuilder *bb) {
    const buf_builder_t *s = (const buf_builder_t *)bb; buf_t *o = (buf_t *)b;
    if (!s->pool || !s->pool->mem) { LOG("[red] BufferInitialize: pool %p has no storage (virtual pools not supported) off=%#llx size=%#llx", (void *)s->pool, (long long)s->off, s->size); return 0; }
    if ((uint64_t)s->off + s->size > s->pool->size) { LOG("[red] BufferInitialize out of pool bounds: off=%#llx size=%#llx pool_size=%#llx", (long long)s->off, s->size, s->pool->size); return 0; }
    *o = (buf_t){MAGIC('B', 'U', 'F', 0), s->pool, s->off, s->size, s->pool->mem + s->off, nvn_new_serial()};
    OLOG("Buffer %p pool=%p off=%#llx size=%#llx", (void *)b, (void *)s->pool, (long long)s->off, s->size);
    return 1;
}
NVN(nvnBufferFinalize, void, NVNbuffer *b) { ((buf_t *)b)->magic = 0; }
NVN(nvnBufferMap, void *, const NVNbuffer *b) { return BF(b)->mem; }
NVN(nvnBufferGetAddress, NVNbufferAddress, const NVNbuffer *b) { return (NVNbufferAddress)BF(b)->mem; }
NVN(nvnBufferFlushMappedRange, void, const NVNbuffer *b, ptrdiff_t off, size_t n) { (void)b; (void)off; (void)n; }
NVN(nvnBufferInvalidateMappedRange, void, const NVNbuffer *b, ptrdiff_t off, size_t n) { (void)b; (void)off; (void)n; }
NVN(nvnBufferGetMemoryPool, NVNmemoryPool *, const NVNbuffer *b) { return (NVNmemoryPool *)BF(b)->pool; }
NVN(nvnBufferGetMemoryOffset, ptrdiff_t, const NVNbuffer *b) { return BF(b)->off; }
NVN(nvnBufferGetSize, size_t, const NVNbuffer *b) { return BF(b)->size; }
NVN(nvnBufferGetDebugID, uint64_t, const NVNbuffer *b) { return BF(b)->serial; }

// ---------- Descriptor pools ----------
// Several texture / sampler pools (plan item 64e, after bayonetta3-mobile's descriptor namespaces, MIT): each pool's IDs have a range of their own
// in the replay's tables, from the base its descriptor memory got when first initialized (the first pool 0: a game with one pool registers and
// resolves the IDs it uses), and the replay resolves handles against the pool a command buffer set last (nvn_mtl.m h_tid / h_sid). Before, all
// pools shared one ID space and a second pool's IDs read the first's objects (pool_second's red line). The same memory initialized again keeps its
// base (unless it grew)
static int pool_ns(int kind, uintptr_t at, int n) {
#ifdef DM_TEST_POOLS_ONE_SPACE   // negative control of gfx/tests t_pools only: one ID space, as before
    (void)kind; (void)at; (void)n; return 0;
#endif
    static struct { uintptr_t at; int base, n; } t[2][64]; static int nt[2], next[2]; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    if (n < 0) n = 0;
    pthread_mutex_lock(&mu); int base = -1, i = 0;
    for (; i < nt[kind]; i++) if (t[kind][i].at == at) { if (n <= t[kind][i].n) base = t[kind][i].base; break; }
    if (base < 0) {
        base = next[kind]; next[kind] += n;
        if (i < nt[kind]) t[kind][i] = (typeof(t[0][0])){at, base, n}; else if (nt[kind] < 64) t[kind][nt[kind]++] = (typeof(t[0][0])){at, base, n};
        if (base) LOG("[nvn] %s pool %d (%d entries) at IDs %d.. of the replay's tables", kind ? "sampler" : "texture", nt[kind], n, base);
    }
    pthread_mutex_unlock(&mu); return base;
}
static uintptr_t pool_at(const NVNmemoryPool *p, ptrdiff_t off) { const pool_t *o = (const pool_t *)p; return o && o->mem ? (uintptr_t)o->mem + (uintptr_t)off : (uintptr_t)off; }
NVN(nvnTexturePoolInitialize, NVNboolean, NVNtexturePool *tp, const NVNmemoryPool *p, ptrdiff_t off, int n) {
    int base = pool_ns(0, pool_at(p, off), n);
    *(dpool_t *)tp = (dpool_t){(const pool_t *)p, off, n, base}; OLOG("TexturePool n=%d", n); if (!base) mtl_set_tex_pool_n(n); return 1;
}
static int pool_base(const void *p) { return p ? ((const dpool_t *)p)->base : 0; }
NVN(nvnTexturePoolFinalize, void, NVNtexturePool *tp) { (void)tp; }
// Textures Metal cannot create are skipped at replay (nvn_mtl.m tex_desc), and objects registered without being initialized textures hold no valid description:
// log them with the game's call site (first 32) to find which code builds them.
static void tex_diag(const char *what, int id, const tex_t *o, const char *why, uint64_t lr) {
    static _Atomic int n; if (atomic_fetch_add(&n, 1) >= 32) return;
    module_t *m = module_of((void *)lr);
    LOG("[nvn] [red] %s: %s: id %d ser %llu magic %#x target %d format %#x flags %#x samples %d size %ux%ux%u levels %u, call site %s+%#llx",
        what, why, id, o->serial, o->magic, o->d.target, o->d.format, o->d.flags, o->d.samples, o->d.width, o->d.height, o->d.depth, o->d.levels,
        m ? m->name : "?", m ? lr - (uint64_t)m->base : lr);
}
static void tex_reg_check(const char *what, int id, const NVNtexture *t, uint64_t lr) {
    const tex_t *o = (const tex_t *)t; if (!o) return;
    const char *why = o->magic != MAGIC('T', 'E', 'X', 0) ? "not an initialized texture" : nvn_texdesc_metal_check(&o->d);
    if (why) tex_diag(what, id, o, why, lr);
}
NVN(nvnTexturePoolRegisterTexture, void, const NVNtexturePool *tp, int id, const NVNtexture *t, const NVNtextureView *v) {
    tex_reg_check("RegisterTexture", id, t, DM_GUEST_RA()); mtl_reg_tex(pool_base(tp) + id, (const tex_t *)t, (const view_t *)v);
}
// Image descriptors live in the texture pool alongside texture descriptors, in the same ID space (the replay layer's image slots fetch textures from the same registration table, see nvn_mtl.m NVN_RB_IMG)
NVN(nvnTexturePoolRegisterImage, void, const NVNtexturePool *tp, int id, const NVNtexture *t, const NVNtextureView *v) {
    tex_reg_check("RegisterImage", id, t, DM_GUEST_RA()); mtl_reg_tex(pool_base(tp) + id, (const tex_t *)t, (const view_t *)v);
}
NVN(nvnSamplerPoolInitialize, NVNboolean, NVNsamplerPool *sp, const NVNmemoryPool *p, ptrdiff_t off, int n) {
    *(dpool_t *)sp = (dpool_t){(const pool_t *)p, off, n, pool_ns(1, pool_at(p, off), n)}; OLOG("SamplerPool n=%d", n); return 1;
}
NVN(nvnSamplerPoolFinalize, void, NVNsamplerPool *sp) { (void)sp; }
NVN(nvnSamplerPoolRegisterSampler, void, const NVNsamplerPool *sp, int id, const NVNsampler *s) { mtl_reg_samp(pool_base(sp) + id, &((const samp_t *)s)->p); }

