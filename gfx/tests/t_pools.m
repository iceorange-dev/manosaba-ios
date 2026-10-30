// Several texture and sampler pools (plan item 64e, after bayonetta3-mobile's descriptor namespaces, MIT, written anew). Through the public NVN
// calls: two texture pools in different memory, a 2x2 and a 4x4 texture registered at the same ID 5, one in each; two sampler pools, a nearest
// and a linear sampler at ID 3. A command buffer that sets pool A resolves the handle of ID 5 to A's texture, after setting B to B's; the sampler
// handle of ID 3 likewise; the first pool's IDs are the table's own (base 0: a game with one pool is unchanged); the same pool memory
// initialized again keeps its base; a second pool's textures do not overwrite the first's. One ID space, as before (-DDM_TEST_POOLS_ONE_SPACE):
// 4 fail. A memory pool's setup (nvnMemoryPoolInitialize) gives the thread's virtual core up and takes it back (plan item 297; core_away_stub.c
// counts; -DDM_TEST_POOL_KEEPS_CORE, the core kept: 1 fails).
#include "../nvn/nvn_mtl.m"
#include "../nvn/hle_nvn.c"
const dm_game_profile dm_game = { .id = "test", .nvn_api_major = 55, .nvn_api_minor = 15, .nvn_c2 = {0x168, 128, 0x568, 128, 0x120, 8}, .nvn_depth_mode = 1 };
uint64_t g_t0_ns;
void dm_touch_screen(int w, int h) { (void)w; (void)h; }
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static uint8_t *g_p_mem; static pool_t g_p_pool;
static void set_pools(const NVNtexturePool *tp, const NVNsamplerPool *sp) {   // a submission that sets the pools
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&g_p_pool, 0x80000, 4096); h_nvnCommandBufferBeginRecording(&c);
    h_nvnCommandBufferSetTexturePool(&c, tp); h_nvnCommandBufferSetSamplerPool(&c, sp); NVNcommandHandle h = h_nvnCommandBufferEndRecording(&c);
    uint64_t hs[1] = {(uint64_t)h}; mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); nvn_exec_rest(NULL, 0); nvn_exec((const uint8_t *)(uintptr_t)h); mtl_wait(mtl_sub_commit(), 5000000000ull);
}
static NSUInteger width_of(uint64_t handle) { pthread_mutex_lock(&g_rmu); id<MTLTexture> t = (__bridge id<MTLTexture>)tex_fast(h_tid(handle)); pthread_mutex_unlock(&g_rmu); return t ? t.width : 0; }
static MTLSamplerMinMagFilter filter_of(uint64_t handle) { int sid = h_sid(handle); return sid > 0 && sid < g_nsreg && g_sreg_ok[sid] ? (MTLSamplerMinMagFilter)(g_sreg[sid].magf == NVN_MAG_FILTER_LINEAR) : (MTLSamplerMinMagFilter)99; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    if (posix_memalign((void **)&g_p_mem, getpagesize(), 1 << 20)) abort(); memset(g_p_mem, 0, 1 << 20); g_p_pool = (pool_t){.mem = g_p_mem, .size = 1 << 20}; mtl_pool_add(g_p_mem, 1 << 20);
    NVNtexturePool ta, tb, ta2; NVNsamplerPool sa, sb;
    h_nvnTexturePoolInitialize(&ta, (const NVNmemoryPool *)&g_p_pool, 0x10000, 64); h_nvnTexturePoolInitialize(&tb, (const NVNmemoryPool *)&g_p_pool, 0x20000, 64);
    h_nvnSamplerPoolInitialize(&sa, (const NVNmemoryPool *)&g_p_pool, 0x30000, 32); h_nvnSamplerPoolInitialize(&sb, (const NVNmemoryPool *)&g_p_pool, 0x40000, 32);
    check("the first texture and sampler pools at base 0, the second ones past them", ((dpool_t *)&ta)->base == 0 && ((dpool_t *)&tb)->base >= 64 && ((dpool_t *)&sa)->base == 0 && ((dpool_t *)&sb)->base >= 32);
    tex_t a = {.magic = MAGIC('T', 'E', 'X', 0), .serial = 2400001}, b = {.magic = MAGIC('T', 'E', 'X', 0), .serial = 2400002};
    a.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = 2, .height = 2, .depth = 1, .levels = 1, .samples = 1}; b.d = a.d; b.d.width = b.d.height = 4;
    h_nvnTexturePoolRegisterTexture(&ta, 5, (const NVNtexture *)&a, NULL); h_nvnTexturePoolRegisterTexture(&tb, 5, (const NVNtexture *)&b, NULL);
    NVNsamplerBuilder bn, bl; NVNsampler s0, s1; h_nvnSamplerBuilderSetDefaults(&bn); h_nvnSamplerBuilderSetDefaults(&bl);
    h_nvnSamplerBuilderSetMinMagFilter(&bn, NVN_MIN_FILTER_NEAREST, NVN_MAG_FILTER_NEAREST); h_nvnSamplerBuilderSetMinMagFilter(&bl, NVN_MIN_FILTER_LINEAR, NVN_MAG_FILTER_LINEAR);
    h_nvnSamplerInitialize(&s0, &bn); h_nvnSamplerInitialize(&s1, &bl); h_nvnSamplerPoolRegisterSampler(&sa, 3, &s0); h_nvnSamplerPoolRegisterSampler(&sb, 3, &s1);
    uint64_t th = (uint64_t)h_nvnDeviceGetTextureHandle(NULL, 5, 3);
    set_pools(&ta, &sa); NSUInteger wa = width_of(th); int fa = (int)filter_of(th);
    set_pools(&tb, &sb); NSUInteger wb = width_of(th); int fb = (int)filter_of(th);
    check("pool A set: ID 5 is A's 2x2 texture; pool B set: B's 4x4", wa == 2 && wb == 4);
    check("sampler pool A set: ID 3 is A's nearest sampler; B: B's linear", fa == 0 && fb == 1);
    set_pools(&ta, &sa);
    check("back to pool A: A's texture again (the second pool did not overwrite it)", width_of(th) == 2);
    h_nvnTexturePoolInitialize(&ta2, (const NVNmemoryPool *)&g_p_pool, 0x10000, 64);
    check("pool A's memory initialized again: the same base", ((dpool_t *)&ta2)->base == ((dpool_t *)&ta)->base);
    // a memory pool's setup through the public call: the core given up and taken back once
    extern int g_core_aways, g_core_backs; int aw0 = g_core_aways, bk0 = g_core_backs;
    static uint8_t pm[1 << 16] __attribute__((aligned(16384))); NVNmemoryPoolBuilder pb; NVNmemoryPool mp;
    h_nvnMemoryPoolBuilderSetDefaults(&pb); h_nvnMemoryPoolBuilderSetStorage(&pb, pm, sizeof pm); h_nvnMemoryPoolBuilderSetFlags(&pb, NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    int okp = h_nvnMemoryPoolInitialize(&mp, &pb);
    check("nvnMemoryPoolInitialize: the virtual core given up once for the backend's setup, taken back once", okp && g_core_aways - aw0 == 1 && g_core_backs - bk0 == 1);
    h_nvnMemoryPoolFinalize(&mp);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
