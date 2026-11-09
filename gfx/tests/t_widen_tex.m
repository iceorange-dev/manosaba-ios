// Widened storage (gfx/nvn/mtl/textures.inc tex_widen, plan item 320b): with DM_WIDEN_SCREEN=2868x1320 a 1280x720 RGBA8 texture the GPU draws into
// gets Metal storage of 1564x720 (the game's size stays 1280x720), one of 4 levels a level 3 of 195 (= 1564 >> 3), a 1024x768 one stays; a
// CPU-filled 1280x720 texture (its storage is an image) stays 1280 wide. --tall with DM_WIDEN_SCREEN=2670x1878 (the Duo's inner screen): 1280x720
// → 1280x900. --off without it: 1280x720. CPU-filled textures widened too (-DDM_TEST_WIDEN_ANY): 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-96s expected %-6lld → got %-6lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
static tex_t gpu(uint64_t ser, unsigned w, unsigned h, unsigned lv) {
    tex_t t = {.serial = ser}; t.d.width = w; t.d.height = h; t.d.depth = t.d.samples = 1; t.d.levels = lv; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8; return t;
}
int main(int argc, char **argv) { @autoreleasepool {
    int tall = argc > 1 && !strcmp(argv[1], "--tall"), off = argc > 1 && !strcmp(argv[1], "--off");
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    tex_t a = gpu(9601, 1280, 720, 1);
    mtl_sub_begin(); id<MTLTexture> ma = tex_mtl(&a); mtl_wait(mtl_sub_commit(), 5000000000ull);
    if (off) { EXPECT("without DM_WIDEN_SCREEN: a 1280x720 target stays 1280 wide", 1280, (long long)ma.width); }
    else if (tall) { EXPECT("the Duo's inner screen: a 1280x720 target 1280 wide", 1280, (long long)ma.width); EXPECT("... and 900 tall", 900, (long long)ma.height); }
    else {
        EXPECT("2868x1320: a 1280x720 target the GPU draws into is 1564 wide", 1564, (long long)ma.width);
        EXPECT("... 720 tall; the game's size stays 1280", 720 * 10000 + 1280, (long long)ma.height * 10000 + a.d.width);
        tex_t b = gpu(9602, 1280, 720, 4), c = gpu(9603, 1024, 768, 1);
        mtl_sub_begin(); id<MTLTexture> mb = tex_mtl(&b), mc = tex_mtl(&c); mtl_wait(mtl_sub_commit(), 5000000000ull);
        EXPECT("... one of 4 levels: level 3 is 195 wide (= 1564 >> 3)", 195, (long long)MAX(mb.width >> 3, 1u));
        EXPECT("... a 1024x768 one stays 1024", 1024, (long long)mc.width);
        size_t big = (size_t)8 << 20; uint8_t *mem; if (posix_memalign((void **)&mem, getpagesize(), big)) abort(); memset(mem, 0, big); mtl_pool_add(mem, big);
        static pool_t pl; pl.mem = mem; pl.size = big; pl.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;
        tex_t p = gpu(9604, 1280, 720, 1); nvn_texlayout L; nvn_tex_layout(&p.d, &L); p.size = (uint32_t)L.size; p.mem = mem; p.pool = &pl; p.has_layout = 1;
        uint32_t *lin = malloc(1280 * 720 * 4); for (size_t i = 0; i < 1280 * 720; i++) lin[i] = 0xff00ff00u; nvn_swizzle(&p.d, 0, 0, lin, 1280 * 4, p.mem); free(lin);
        mtl_sub_begin(); id<MTLTexture> mp = tex_mtl(&p); mtl_wait(mtl_sub_commit(), 5000000000ull);
        EXPECT("... a CPU-filled 1280x720 texture (an image) stays 1280 wide [-DDM_TEST_WIDEN_ANY: 1564]", 1280, (long long)mp.width);
        EXPECT("... two textures widened", 2, (long long)mtl_widen_count());
    }
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
