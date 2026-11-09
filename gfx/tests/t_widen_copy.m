// Copies and clears on widened storage (gfx/nvn/mtl/copies.inc copy_t2t, clear_tex; plan item 320e): with DM_WIDEN_SCREEN=2868x1320 (k 1.221875)
// three 1280x720 targets get 1564-wide storage; A cleared red in a pass; a copy of A's whole 1280x720 into B reaches column 1560 of 1564; a copy
// of A's columns 640-1279 into C at 0 fills C's columns 0-781 (640 x k = 782: column 780 red, 790 not); a texture clear of B's whole 1280x720
// clears all 1564 columns (10 and 1560 green); no red line. The game's region copied as it is (-DDM_TEST_WIDEN_COPY_GAME): 2 fail; the clear's
// region compared with the storage alone, as before (-DDM_TEST_WIDEN_CLEAR_GAME, not cleared: its red line): 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-100s expected %-6lld → got %-6lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static tex_t tex(uint64_t ser) { tex_t t = {.serial = ser}; t.d.width = 1280; t.d.height = 720; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8; return t; }
static void clear_pass(chain *c, const tex_t *t, float r, float g) {   // a pass over the whole target, cleared
    p_set_rt rt = {.n = 1}; rt.color[0] = t; emit(c, OP_SET_RT, &rt, sizeof rt);
    p_scissor sc = {0, 0, 1280, 720}; emit(c, OP_SCISSOR, &sc, sizeof sc);
    p_clear_color k = {.index = 0, .mask = 15, .rgba = {r, g, 0, 1}}; emit(c, OP_CLEAR_COLOR, &k, sizeof k);
    p_set_rt none = {.n = 0}; emit(c, OP_SET_RT, &none, sizeof none);
}
static void copy(chain *c, const tex_t *s, const tex_t *d, int sx, int w, int dx) {
    p_copy_t2t x = {.src = s, .dst = d}; x.sr = (NVNcopyRegion){sx, 0, 0, w, 720, 1}; x.dr = (NVNcopyRegion){dx, 0, 0, w, 720, 1}; emit(c, OP_COPY_T2T, &x, sizeof x);
}
static uint8_t g_px[3][1564 * 4];
static void row(chain *c, const tex_t *t, int i) {   // run what is in c, then read row 360 of t into g_px[i]
    emit(c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c->p; mtl_sub_index(&hh, 1); nvn_exec(c->p);
    id<MTLTexture> mt = tex_mtl(t); NSUInteger w = mt.width;
    id<MTLBuffer> rb = [g_dev newBufferWithLength:w * 4 options:MTLResourceStorageModeShared]; res_add(rb);
    [blit() copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 360, 0) sourceSize:MTLSizeMake(w, 1, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:w * 4 destinationBytesPerImage:w * 4];
    mtl_wait(mtl_sub_commit(), 5000000000ull); memset(g_px[i], 0, sizeof g_px[i]); memcpy(g_px[i], rb.contents, MIN(w, 1564u) * 4); c->n = 0;
}
static int red(int i, int x) { return g_px[i][x * 4]; }
static int green(int i, int x) { return g_px[i][x * 4 + 1]; }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_TBDR", "0", 1); mtl_init();
    static uint8_t *cm; if (posix_memalign((void **)&cm, getpagesize(), 1 << 20)) abort(); mtl_pool_add(cm, 1 << 20);
    chain c = {cm, 0}; tex_t A = tex(9500), B = tex(9501), C = tex(9502);
    clear_pass(&c, &A, 1, 0); clear_pass(&c, &B, 0, 0); clear_pass(&c, &C, 0, 0); row(&c, &A, 0);
    EXPECT("A, 1280x720 in 1564-wide storage, cleared red in a pass: column 1560 red", 255, red(0, 1560));
    copy(&c, &A, &B, 0, 1280, 0); copy(&c, &A, &C, 640, 640, 0); row(&c, &B, 1); row(&c, &C, 2);
    EXPECT("A's whole 1280x720 copied into B: column 1560 of 1564 copied [-DDM_TEST_WIDEN_COPY_GAME: not]", 255, red(1, 1560));
    EXPECT("A's columns 640-1279 copied into C at 0: C's columns 0-781 (780 red, 790 not) [-DDM_TEST_WIDEN_COPY_GAME: 0-639]", 1, red(2, 780) == 255 && red(2, 790) == 0);
    p_clear_tex x = {.tex = &B, .r = {0, 0, 0, 1280, 720, 1}, .mask = 15, .c.f = {0, 1, 0, 1}}; emit(&c, OP_CLEAR_TEX, &x, sizeof x); row(&c, &B, 1);
    EXPECT("a texture clear of B's whole 1280x720: all 1564 columns green (10 and 1560) [-DDM_TEST_WIDEN_CLEAR_GAME: not cleared]", 1, green(1, 10) == 255 && green(1, 1560) == 255 && red(1, 1560) == 0);
    EXPECT("no red line (clear shape 44, copy ratios 123) [-DDM_TEST_WIDEN_CLEAR_GAME: 44]", 0, g_red_once[44] + g_red_once[123]);
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
