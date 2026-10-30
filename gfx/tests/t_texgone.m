// Textures whose storage the game took back after the submission that reads them (plan item 290; core/guard.h): with the host's crash handler
// installed, in a pool the Metal backend was not given (so its storage is read on the CPU):
//   a 64x64 RGBA8 texture with the game's layout uploaded, its storage written and then made inaccessible: the next submission's re-upload faults, is caught, and the texture keeps
//   the one it had, the process going on;
//   a 16x16 one (a small texture, checked by its content hash): the hash's read caught the same way, the texture kept;
//   a texture without one (a CPU-filled candidate) first seen with its storage inaccessible: the check whether the CPU filled it caught, a texture
//   without content given;
//   all three counted as faults caught, and the storage back, the big one uploaded again.
// The upload read as before (-DDM_TEST_UPLOAD_UNGUARDED): the process dies of the first fault (the run's log has the crash report).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
void crash_install(void);
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-104s %s\n", what, ok ? "✓" : "✗"); }
static id<MTLTexture> use(uint32_t tid) {   // the texture a submission binds for an ID
    mtl_sub_begin(); pthread_mutex_lock(&g_rmu); id<MTLTexture> mt = tex_by_id(tid); pthread_mutex_unlock(&g_rmu); mtl_wait(mtl_sub_commit(), 5000000000ull);
    return mt;
}
static pool_t g_pl = {.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED};   // (mtl_pool_add never called: the CPU path)
static uint8_t *storage(size_t n, tex_t *t, uint64_t serial, uint32_t w, uint32_t h, int layout) {   // block-linear RGBA8 storage, filled
    *t = (tex_t){.serial = serial, .pool = &g_pl, .has_layout = layout}; t->d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = w, .height = h, .depth = 1, .levels = 1, .samples = 1};
    nvn_texlayout L; nvn_tex_layout(&t->d, &L); size_t sz = ((size_t)L.size + 16383) & ~(size_t)16383; if (sz < n) sz = n;
    uint8_t *m = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t *lin = malloc((size_t)w * h * 4); for (size_t i = 0; i < (size_t)w * h * 4; i++) lin[i] = (uint8_t)(i * 7 + 1);
    t->size = (uint32_t)L.size; t->mem = m; nvn_swizzle(&t->d, 0, 0, lin, w * 4, m); free(lin);
    return m;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_install(); mtl_init();
    tex_t big, small, late; uint64_t f0 = guard_faults();
    uint8_t *bm = storage(0, &big, 9100, 64, 64, 1); mtl_tex_live(&big); mtl_reg_tex(80, &big, NULL);
    uint8_t *sm = storage(0, &small, 9101, 16, 16, 1); mtl_tex_live(&small); mtl_reg_tex(81, &small, NULL);
    id<MTLTexture> b1 = use(80), s1 = use(81);
    check("a 64x64 texture with the game's layout and a 16x16 one, their storage read on the CPU: uploaded", b1 != nil && s1 != nil && b1.width == 64 && s1.width == 16);
    bm[0] ^= 1;   // a CPU write (dirty: the next use uploads again), then the storage taken back
    mprotect(bm, 16384, PROT_NONE); mprotect(sm, 16384, PROT_NONE);
    id<MTLTexture> b2 = use(80);
    check("the big one's storage taken back: its re-upload's read caught, the texture it had kept", b2 == b1 && guard_faults() - f0 == 1);
    id<MTLTexture> s2 = use(81);
    check("the small one's: its content hash's read caught, the texture kept", s2 == s1 && guard_faults() - f0 == 2);
    uint8_t *lm = storage(0, &late, 9102, 64, 64, 0); mprotect(lm, 16384, PROT_NONE); mtl_tex_live(&late); mtl_reg_tex(82, &late, NULL);
    id<MTLTexture> l1 = use(82);
    check("a texture first seen with its storage taken back: the check whether the CPU filled it caught, a texture given", l1 != nil && guard_faults() - f0 == 3);
    mprotect(bm, 16384, PROT_READ | PROT_WRITE);
    id<MTLTexture> b3 = use(80);
    check("the big one's storage back: uploaded again", b3 != nil && b3 != b1 && guard_faults() - f0 == 3);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
