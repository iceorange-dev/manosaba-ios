// gfx/nvn/mtl/pools.inc, part of nvn_mtl.m (included there in order, one translation unit): Memory pools: the pool table, GPU addresses of guest memory and the buffers behind them
// ---------- Memory pools ----------
typedef struct { uintptr_t lo, hi; uintptr_t base; __unsafe_unretained id<MTLBuffer> buf; uint64_t gpu; const void *key; int res; } pool_ent;   // res: already put in the residency set (on demand, see pool_res)
static pool_ent *g_pools; static int g_npool, g_cap;
_Static_assert(offsetof(pool_ent, lo) == 0 && offsetof(pool_ent, hi) == sizeof(uintptr_t), "nvn_pool_find.h reads lo, hi at the start of an entry");
static pool_find_t g_pfind = {.last = -1};   // address → pool (nvn_pool_find.h); reset whenever g_pools changes, under g_mu
static NSMutableArray *g_keep;                  // Holds the buffer references (pool_ent has non-owning pointers)
static uint64_t g_pool_adds;

#define C1_BANK_SIZE (4u << 20)
#define C1_DECL_SIZE (64u << 10)
static NSMutableArray *g_c1_banks;
static size_t g_c1_bank_off, g_c1_alloc_bytes, g_c1_data_bytes;
static uint64_t g_c1_count, g_c1_exclusive;
static NSMutableSet<NSString *> *g_c1_full;
static int g_c1_full_valid;
static os_unfair_lock g_c1_lock = OS_UNFAIR_LOCK_INIT;

// DM_UDC_SELFTEST=1 (diagnostic, checks DontCare behavior on a real A19 device): private-storage Depth32Float_Stencil8 / RGBA8 (1952x900) are first cleared to write known values,
// then Load + DontCare without drawing, read back and compared pixel by pixel, printing one result line
static void udc_selftest(void) {
    if (!getenv("DM_UDC_SELFTEST")) return;
    id<MTLCommandQueue> q = [g_dev newCommandQueue];
    for (int depth = 1; depth >= 0; depth--) {
        NSUInteger w = 1952, h = 900;
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:depth ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> t = [g_dev newTextureWithDescriptor:td];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        for (int pass = 0; pass < 2; pass++) {
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            MTLLoadAction la = pass ? MTLLoadActionLoad : MTLLoadActionClear; MTLStoreAction sa = pass ? MTLStoreActionDontCare : MTLStoreActionStore;
            if (depth) { rp.depthAttachment.texture = t; rp.depthAttachment.loadAction = la; rp.depthAttachment.storeAction = sa; rp.depthAttachment.clearDepth = 0.3125;
                         rp.stencilAttachment.texture = t; rp.stencilAttachment.loadAction = la; rp.stencilAttachment.storeAction = sa; rp.stencilAttachment.clearStencil = 0x5a; }
            else { rp.colorAttachments[0].texture = t; rp.colorAttachments[0].loadAction = la; rp.colorAttachments[0].storeAction = sa; rp.colorAttachments[0].clearColor = MTLClearColorMake(0.2, 0.4, 0.6, 0.8); }
            [[cb renderCommandEncoderWithDescriptor:rp] endEncoding];
        }
        id<MTLBuffer> out = [g_dev newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
        [b copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1) toBuffer:out destinationOffset:0
            destinationBytesPerRow:w * 4 destinationBytesPerImage:w * h * 4 options:depth ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
        [b endEncoding]; [cb commit]; [cb waitUntilCompleted];
        const uint32_t *px = out.contents; NSUInteger bad = 0; for (NSUInteger i = 0; i < w * h; i++) if (px[i] != px[0]) bad++;
        MLOG("UDC self-test %s %s %lux%lu first value %08x, differing from first value %lu / %lu", g_dev.name.UTF8String, depth ? "Depth32F_S8" : "RGBA8", (unsigned long)w, (unsigned long)h, px[0], (unsigned long)bad, (unsigned long)(w * h));
    }
}
// Entry points the game's threads reach outside a submission run inside their own autorelease pool (@autoreleasepool at the top of each): guest
// threads have no pool, and an autorelease with none creates a thread-level pool that is never drained (seen holding retired textures autoreleased
// in mtl_init and WriteTexels; OBJC_DEBUG_MISSING_POOLS=YES lists what is still missed). Submissions push their own pool (mtl_sub_begin)
void mtl_init(void) { @autoreleasepool {
    if (g_dev) return;
    const char *link = getenv("DM_PRESENT_LINK");
    g_dm_present_link = link ? atoi(link) : 1; if (g_dm_present_link < 0 || g_dm_present_link > 2) g_dm_present_link = 1;   // Default 1 (real-device same-process alternation: when load exceeds one display-link period, long frames 15% → 0 and frame intervals even out; no difference from 0 at a steady 60); DM_PRESENT_LINK=0 turns it off   // 1 = present on every tick of a 60 Hz display link; 2 = 120 Hz display link, the game presents a frame every other display-link tick (when behind, it can present on the next 8.33 ms display-link tick)
    g_latency_set = nvn_latency_setting(getenv("DM_PRESENT_LATENCY"));
    if (!g_latency_set) MLOG("display link frame latency: auto (2 while the link ticks at 30 Hz, 1 otherwise; DM_PRESENT_LATENCY)");
    g_res_check_on = getenv("DM_RES_CHECK") != NULL;
    g_dev = MTLCreateSystemDefaultDevice();
    if (!g_dev) MDIE("no Metal device");
#if TARGET_OS_SIMULATOR   // the Simulator's stand-in reports none; its buffers are the Mac's (plan item 114: tried as they are)
    if (!g_dev.hasUnifiedMemory) MLOG("the iOS Simulator's GPU reports no unified memory: zero-copy memory pools tried anyway");
#else
    if (!g_dev.hasUnifiedMemory) MDIE("a unified-memory device is required (zero-copy memory pools)");
#endif
    g_q = [g_dev newCommandQueue];
#if DM_NVN_M3
    stream_queue_once();
#endif
    NSError *re = nil;
    // Residency sets need a GPU of the Apple6 family (Apple's Metal feature set tables; GirlNet's d2r-mobile 3e051f1, MIT): a virtual GPU (GitHub's
    // macOS runners) is of the Mac2 family only, and the call is not made there (plan item 176). The Metal 3 path then makes resources resident by
    // the encoders' useResource declarations alone, as before iOS 18; Metal 4 has no such declarations and cannot run. DM_TEST_NO_RESIDENCY_SET=1
    // takes that path on any GPU (gfx/tests run-m3)
    int fam = [g_dev supportsFamily:MTLGPUFamilyApple6] && !(getenv("DM_TEST_NO_RESIDENCY_SET") && !strcmp(getenv("DM_TEST_NO_RESIDENCY_SET"), "1"));
#ifdef DM_TEST_RES_IGNORE_FAMILY   // negative control of gfx/tests run-m3 only: the set made whatever the GPU, as before
    fam = 1;
#endif
    if (@available(iOS 18.0, macOS 15.0, *)) {   // (4ppp44's iOS 17 build compiles the set out; checked here, one Metal 3 binary keeps it from iOS 18 on)
      if (fam) {
        MTLResidencySetDescriptor *rd = [MTLResidencySetDescriptor new];
        rd.initialCapacity = 4096;
        g_res = [g_dev newResidencySetWithDescriptor:rd error:&re];
        if (!g_res) MDIE("failed to create MTLResidencySet");
        [g_q addResidencySet:g_res];
      } else {
#if DM_NVN_M3
        MLOG("no residency set: %s is not of the Apple6 family (a virtual GPU) or DM_TEST_NO_RESIDENCY_SET=1: resources are made resident by the encoders' useResource declarations only", g_dev.name.UTF8String);
#else
        MDIE("Metal 4 needs residency sets, which %s does not have (a virtual GPU): run the Metal 3 build (dmhost_m3)", g_dev.name.UTF8String);
#endif
      }
    } else MLOG("no residency set before iOS 18: resources are made resident by the encoders' useResource declarations only");
    { void crash_set_wait_dump(void (*fn)(void)); void mtl_wait_state_dump(void); crash_set_wait_dump(mtl_wait_state_dump); }
#if !DM_NVN_M3
    dispatch_queue_attr_t feedback_attr = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
#ifndef DM_TEST_FEEDBACK_NO_POOL   // negative control of t_feedback_pool only
    // The feedback handlers retire resources and may autorelease objects; a long-lived serial queue only drains its pool when the queue idles, so a
    // steady stream of completions kept them alive (plan item 159, lordran-native 6145d1d, MIT): a pool per work item. Checked by t_feedback_pool
    feedback_attr = dispatch_queue_attr_make_with_autorelease_frequency(feedback_attr, DISPATCH_AUTORELEASE_FREQUENCY_WORK_ITEM);
#endif
    g_feedback_q = dispatch_queue_create("dm.nvn.mtl4-feedback", feedback_attr);
    MTL4CommandQueueDescriptor *q4d = [MTL4CommandQueueDescriptor new]; q4d.feedbackQueue = g_feedback_q;
    g_q4 = [g_dev newMTL4CommandQueueWithDescriptor:q4d error:&re];
    if (!g_q4) MDIE("failed to create MTL4CommandQueue");
    stream_queue_once();
    [g_q4 addResidencySet:g_res];
#endif
    g_alloc_n = 3;
#if !DM_NVN_M3
    for (int i = 0; i < 3; i++) {
        MTL4CommandAllocatorDescriptor *ad = [MTL4CommandAllocatorDescriptor new];
        ad.label = [NSString stringWithFormat:@"nvn-mtl allocator %d", i];
        NSError *ae = nil;
        g_alloc4[i] = [g_dev newCommandAllocatorWithDescriptor:ad error:&ae];
        if (!g_alloc4[i]) MDIE("failed to create MTL4CommandAllocator[%d]: %s", i, ae.localizedDescription.UTF8String);
        g_up_alloc4[i] = [g_dev newCommandAllocatorWithDescriptor:ad error:&ae];
        if (!g_up_alloc4[i]) MDIE("failed to create upload MTL4CommandAllocator[%d]: %s", i, ae.localizedDescription.UTF8String);
        MTL4CommandAllocatorDescriptor *pad = [MTL4CommandAllocatorDescriptor new];
        pad.label = [NSString stringWithFormat:@"nvn-mtl present allocator %d", i];
        g_present_alloc4[i] = [g_dev newCommandAllocatorWithDescriptor:pad error:&ae];
        if (!g_present_alloc4[i]) MDIE("failed to create present MTL4CommandAllocator[%d]: %s", i, ae.localizedDescription.UTF8String);
        g_present_cb[i] = [g_dev newCommandBuffer];
        if (!g_present_cb[i]) MDIE("failed to create present MTL4CommandBuffer[%d]", i);
    }
    MTL4ArgumentTableDescriptor *atd = [MTL4ArgumentTableDescriptor new];
    atd.maxBufferBindCount = 31; atd.maxTextureBindCount = 1;
    NSError *ate = nil;
    g_vt = [g_dev newArgumentTableWithDescriptor:atd error:&ate]; if (!g_vt) MDIE("failed to create vertex argument table: %s", ate.localizedDescription.UTF8String);
    g_ft = [g_dev newArgumentTableWithDescriptor:atd error:&ate]; if (!g_ft) MDIE("failed to create fragment argument table: %s", ate.localizedDescription.UTF8String);
    g_ct = [g_dev newArgumentTableWithDescriptor:atd error:&ate]; if (!g_ct) MDIE("failed to create compute argument table: %s", ate.localizedDescription.UTF8String);
    g_up_ct = [g_dev newArgumentTableWithDescriptor:atd error:&ate]; if (!g_up_ct) MDIE("failed to create upload compute argument table: %s", ate.localizedDescription.UTF8String);
    MTL4ArgumentTableDescriptor *pad = [MTL4ArgumentTableDescriptor new];
    pad.maxTextureBindCount = 1; pad.maxSamplerStateBindCount = 1; pad.maxBufferBindCount = 1;
    g_present_args = [g_dev newArgumentTableWithDescriptor:pad error:&ate];
    if (!g_present_args) MDIE("failed to create present argument table: %s", ate.localizedDescription.UTF8String);
#endif
    for (int i = 0; i < 3; i++) {
        g_present_uv[i] = [g_dev newBufferWithLength:sizeof(float) * 8 options:MTLResourceStorageModeShared];
        if (!g_present_uv[i]) MDIE("failed to create present UV buffer[%d]", i);
        res_add(g_present_uv[i]);
    }
    g_ev = [g_dev newSharedEvent];
    g_keep = [NSMutableArray array];
    g_nvn_deswz_gpu = nvn_deswz_gpu_init(g_dev) == 0;
    if (!g_nvn_deswz_gpu) MLOG("[red] GPU deswizzle init failed, swizzled textures fall back to the CPU");
    MLOG("Metal device %s: maxBufferLength %llu MB, recommended working set %llu MB", g_dev.name.UTF8String,   // (plan item 62f, mush8 PR #3: a pool or upload past it fails)
         (unsigned long long)(g_dev.maxBufferLength >> 20), (unsigned long long)(g_dev.recommendedMaxWorkingSetSize >> 20));
    udc_selftest();
} }

static _Atomic uint64_t g_reg_gen = 1;   // Change generation of memory pool / texture / sampler registration: one of the invalidation conditions for argument buffer reuse
// On-demand residency of memory pools: putting a zero-copy buffer into the residency set makes iOS read the whole pool into memory and pin it as wired (not counted in footprint and never paged out), even a pool the game never uses -
// measured on a 17 Pro Max: the 512 MB render-target pool is addressed 0 times in a run, and putting it into the residency set raises device wired memory by about 530 MB; this is why an A14 4 GB device was ended by the system for running out of memory within seconds of launch (public repository #37).
// So a pool is not put into the residency set when created; it is put in, and the residency set committed on the spot, the first time the GPU gets its address through mtl_addr / mtl_gpu_addr
// (these are the only two ways the GPU gets pool addresses; committing on the spot = earlier than any command buffer commit that uses it, without relying on each commit point calling res_commit). Caller holds g_mu
static uint64_t g_cb_commits, g_pool_res_at;   // number of command buffers committed (main + upload); this number at the last on-demand residency (0 = nothing pending to check)
static void pool_res(int i) {
    if (g_pools[i].res) return;
    res_add(g_pools[i].buf); res_commit(); g_pools[i].res = 1; g_pool_res_at = g_cb_commits + 1;
    MLOG("on-demand memory pool residency: a %llu MB pool had its address taken for the first time, added to the residency set and committed (command buffers committed so far: %llu)",
         (unsigned long long)((g_pools[i].hi - g_pools[i].lo) >> 20), (unsigned long long)g_cb_commits);
}
static void cb_committed(void) {   // called after the main submission / upload submission: checks that "the residency set commit is earlier than the first command buffer commit that uses it", one line after each on-demand residency
    pthread_mutex_lock(&g_mu); uint64_t n = ++g_cb_commits, at = g_pool_res_at; if (at) g_pool_res_at = 0; pthread_mutex_unlock(&g_mu);
    if (at) MLOG("on-demand memory pool residency: the first command buffer committed after that is #%llu (residency set committed before #%llu)", (unsigned long long)n, (unsigned long long)at);
}
// A memory pool's zero-copy buffer makes its whole range resident (on iOS wired). Ranges with unmapped holes, pages that are not readable, or read-only
// pages the dirty tracker did not protect (its read-only pages are mapped and really read-write), and ranges above POOL_BUFFER_MAX are refused with a
// red line instead of being handed to the driver: twice MacNativeGames saw macOS kernel panics in IOGPUFamily right after kIOReturnNoMemory
// (pool_range_check, ly05010419/MacNativeGames, MIT; written anew here, refusing the pool instead of stopping). A refused pool has no buffer: its
// textures still work (separate Metal textures), buffer accesses into it report as outside every pool
#define POOL_BUFFER_MAX (1ull << 30)
static uint64_t g_pool_refused, g_pool_max;
static int pool_range_ok(uintptr_t a, uintptr_t b, const void *mem, uint64_t size) {
    if (b - a > g_pool_max) g_pool_max = b - a;
    if (b - a > POOL_BUFFER_MAX) { MLOG("[red] memory pool %p+%#llx: a %llu MiB zero-copy GPU buffer refused (limit %llu MiB)", mem, (unsigned long long)size, (unsigned long long)((b - a) >> 20), POOL_BUFFER_MAX >> 20); return 0; }
    for (vm_address_t at = a; at < b;) {
        vm_address_t ra = at; vm_size_t rs = 0; vm_region_basic_info_data_64_t ri; mach_msg_type_number_t rc = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t ro = MACH_PORT_NULL;
        if (vm_region_64(mach_task_self(), &ra, &rs, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&ri, &rc, &ro) != KERN_SUCCESS || ra > at) {
            MLOG("[red] memory pool %p+%#llx: unmapped pages at %#llx, refused for a zero-copy GPU buffer", mem, (unsigned long long)size, (unsigned long long)at); return 0; }
        uintptr_t lo = ra < a ? a : (uintptr_t)ra, hi = ra + rs > b ? b : (uintptr_t)(ra + rs);
        if (!(ri.protection & VM_PROT_READ) || (!(ri.protection & VM_PROT_WRITE) && !nvn_dirty_tracked((const void *)lo, hi - lo))) {
            MLOG("[red] memory pool %p+%#llx: pages at %#llx+%#llx have protection %d, refused for a zero-copy GPU buffer", mem, (unsigned long long)size, (unsigned long long)lo, (unsigned long long)(hi - lo), ri.protection); return 0; }
        at = ra + rs;
    }
    return 1;
}
// Address → pool cache per thread for mtl_addr / mtl_gpu_addr (nxnative e06a8ec, MIT): 8 entries replaced in turn, valid while g_pool_gen is
// unchanged (every pool add and removal bumps it under g_mu), holding only pools already resident. Every buffer binding of every draw used to take
// g_mu, which game threads creating pools and GPU completion handlers take too, and search the pool index. A removed pool's buffer is released only
// after the submissions using it complete (defer_obj_release), so an entry read just before a removal still names a live buffer
static _Atomic uint64_t g_pool_gen = 1;
typedef struct { uintptr_t lo, hi, base; __unsafe_unretained id<MTLBuffer> buf; uint64_t gpu; } pc_ent;
// hits: this thread's cache hits (diagnostics, t_poolcache); last: the entry of the last hit, tried first. The translation thread looks up ~40,000
// buffer addresses a frame in Tears of the Kingdom, most in one pool: the lookup (a thread-local access, an atomic count of the hits and the slow
// path's register saves on every call) was 9 % of its time on the M5; with the hit counted per thread without an atomic, the slow path in a function
// of its own and the vertex streams bound once per encoder (draw) it is 5 % (plan item 225)
static __thread struct { uint64_t gen, hits; unsigned n, next, last; pc_ent e[8]; } t_pc;
static inline uint64_t mtl_pc_hits(void) { return t_pc.hits; }
static inline const pc_ent *pc_get(uint64_t addr, uint64_t len) {
    __typeof__(t_pc) *t = &t_pc;
    if (t->gen != atomic_load_explicit(&g_pool_gen, memory_order_acquire)) return NULL;
    const pc_ent *c = &t->e[t->last];
    if (t->last < t->n && addr >= c->lo && addr + len <= c->hi) { t->hits++; return c; }
    for (unsigned k = 0; k < t->n; k++) { c = &t->e[k]; if (addr >= c->lo && addr + len <= c->hi) { t->last = k; t->hits++; return c; } }
    return NULL;
}
static void pc_put(int i) {   // caller holds g_mu; pool i is resident
    uint64_t gen = atomic_load_explicit(&g_pool_gen, memory_order_relaxed);
    if (t_pc.gen != gen) { t_pc.gen = gen; t_pc.n = 0; t_pc.next = 0; t_pc.last = 0; }
    pc_ent *c = &t_pc.e[t_pc.n < 8 ? t_pc.n++ : t_pc.next++ % 8];
    *c = (pc_ent){g_pools[i].lo, g_pools[i].hi, g_pools[i].base, g_pools[i].buf, g_pools[i].gpu};
}
void mtl_pool_add(const void *mem, uint64_t size) { @autoreleasepool {
    mtl_init(); g_reg_gen++;
    uintptr_t pg = (uintptr_t)getpagesize(), lo = (uintptr_t)mem, hi = lo + size;
    uintptr_t a = lo & ~(pg - 1), b = (hi + pg - 1) & ~(pg - 1);
    if (!pool_range_ok(a, b, mem, size)) { g_pool_refused++; return; }
    id<MTLBuffer> buf = [g_dev newBufferWithBytesNoCopy:(void *)a length:b - a options:MTLResourceStorageModeShared deallocator:nil];
    if (!buf) MDIE("memory pool %p+%#llx: failed to create zero-copy buffer", mem, (unsigned long long)size);
    pthread_mutex_lock(&g_mu);
    if (g_npool == g_cap) { g_cap = g_cap ? g_cap * 2 : 64; g_pools = realloc(g_pools, g_cap * sizeof *g_pools); }
    g_pools[g_npool++] = (pool_ent){lo, hi, a, buf, buf.gpuAddress, mem, 0}; pool_find_reset(&g_pfind); atomic_fetch_add(&g_pool_gen, 1);   // not in the residency set yet: put in the first time it is addressed (pool_res)
    g_pool_adds++;
    [g_keep addObject:buf];
    pthread_mutex_unlock(&g_mu);
} }

static void wb_drop_range(uintptr_t lo, uintptr_t hi);   // writeback.inc
void mtl_pool_del(const void *mem) { @autoreleasepool {
    g_reg_gen++;
    id<MTLBuffer> gone = nil; uintptr_t lo = 0, hi = 0;
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_npool; i++)
        if (g_pools[i].key == mem) { gone = g_pools[i].buf; lo = g_pools[i].lo; hi = g_pools[i].hi; [g_keep removeObject:gone]; g_pools[i] = g_pools[--g_npool]; pool_find_reset(&g_pfind); atomic_fetch_add(&g_pool_gen, 1); break; }
    pthread_mutex_unlock(&g_mu);
    if (hi) wb_drop_range(lo, hi);   // its GPU write-back records go with it (writeback.inc)
    defer_obj_release(gone);   // retire: release after the submissions that used it complete, and remove from the residency set (it used to be only removed from g_keep - the residency set holds it strongly, so the zero-copy buffer object was never destroyed)
} }

__attribute__((noinline)) static int pool_lookup(uint64_t addr, uint64_t len, void **buf, uint64_t *off, uint64_t *gpu) {   // through the lock, then cached
    int ok = 0;
    pthread_mutex_lock(&g_mu);
    int i = pool_find(&g_pfind, g_pools, sizeof *g_pools, g_npool, addr, len);
    if (i >= 0) { pool_res(i); *buf = (__bridge void *)g_pools[i].buf; *off = addr - g_pools[i].base; if (gpu) *gpu = g_pools[i].gpu + *off; ok = 1; pc_put(i); }
    pthread_mutex_unlock(&g_mu);
    return ok;
}
int mtl_addr(uint64_t addr, uint64_t len, void **buf, uint64_t *off) {
    const pc_ent *c = pc_get(addr, len);
    if (c) { *buf = (__bridge void *)c->buf; *off = addr - c->base; return 1; }
    return pool_lookup(addr, len, buf, off, NULL);
}
int mtl_pool_has(uint64_t addr, uint64_t len) {
    pthread_mutex_lock(&g_mu); int i = pool_find(&g_pfind, g_pools, sizeof *g_pools, g_npool, addr, len); pthread_mutex_unlock(&g_mu);
    return i >= 0;
}
int mtl_gpu_addr(uint64_t addr, uint64_t len, void **buf, uint64_t *off, uint64_t *gpu) {
    const pc_ent *c = pc_get(addr, len);
    if (c) { *buf = (__bridge void *)c->buf; *off = addr - c->base; *gpu = c->gpu + *off; return 1; }
    return pool_lookup(addr, len, buf, off, gpu);
}

// ---------- Submission ----------
static void defer_fallback(void);
typedef struct { int kind; volatile uint32_t *val; uint32_t mode, v; uint64_t addr; uint32_t type; void *vis; uint32_t pos; } post_op;   // vis: the visibility slot table for kind 3; pos: work before a report (nvn_ts.h)
