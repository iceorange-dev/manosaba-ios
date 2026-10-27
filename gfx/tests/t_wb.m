// GPU results written back on nvnMemoryPoolInvalidateMappedRange (nvn_mtl.m wb_note / mtl_pool_invalidate; MacNativeGames): a CPU-filled texture in a
// CPU-visible pool that the GPU then renders into keeps the result only in its MTLTexture; an invalidate over its storage writes it back in the NVN
// layout, unless the CPU wrote the storage after the rendering (then the CPU's bytes stay); the CPU writing only other data on the texture's page
// does not count (plan item 203: the texture's own bytes are compared).
// --idle (with DM_NVN_WB_IDLE=1, the experiment of item 94): a texture the GPU rendered and then left alone for 30 submissions is written back to its
// storage without an invalidate; when the CPU then writes the storage, the next submission hands the texture back (not GPU-owned, watched from
// before the write), and its next fetch is an upload of the CPU's bytes.
// Plan item 278: a target up to 64 KiB is written back after the game's fence waits once its rendering completed, with no invalidate. 286: from the copy
// its own submission made at its end, with no command buffer of the write-back's (-DDM_TEST_WB_STAGE_OFF: 2 fail).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-74s expected %-10llx → got %-10llx %s\n", name, (unsigned long long)w, (unsigned long long)g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static void render_clear(const tex_t *t, float r, float g, float b) {   // one submission: a render pass on t cleared to (r, g, b, 1)
    static uint8_t *cm; if (!cm) { if (posix_memalign((void **)&cm, getpagesize(), 65536)) abort(); mtl_pool_add(cm, 65536); }
    chain c = {cm, 0}; memset(cm, 0, 65536);
    p_set_rt rt = {.n = 1}; rt.color[0] = t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_clear_color x = {.index = 0, .mask = 15, .rgba = {r, g, b, 1}}; emit(&c, OP_CLEAR_COLOR, &x, sizeof x);
    emit(&c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p); mtl_wait(mtl_sub_commit(), 5000000000ull);
}
static uint64_t render_clear_commit(const tex_t *t, float r, float g, float b) {   // the same, not waited for: its fence
    static uint8_t *cm; if (!cm) { if (posix_memalign((void **)&cm, getpagesize(), 65536)) abort(); mtl_pool_add(cm, 65536); }
    chain c = {cm, 0}; memset(cm, 0, 65536);
    p_set_rt rt = {.n = 1}; rt.color[0] = t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_clear_color x = {.index = 0, .mask = 15, .rgba = {r, g, b, 1}}; emit(&c, OP_CLEAR_COLOR, &x, sizeof x);
    emit(&c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p); return mtl_sub_commit();
}
static uint32_t texel(const tex_t *t, unsigned x, unsigned y) { uint32_t lin[64]; nvn_deswizzle(&t->d, 0, 0, t->mem, lin, 32); return lin[y * 8 + x]; }
static void empty_sub(void) { state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); mtl_wait(mtl_sub_commit(), 5000000000ull); }
int main(int argc, char **argv) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    int idle = argc > 1 && !strcmp(argv[1], "--idle");
    uint8_t *mem; if (posix_memalign((void **)&mem, getpagesize(), 1 << 20)) abort(); memset(mem, 0, 1 << 20); mtl_pool_add(mem, 1 << 20);
    nvn_dirty_add_range(mem, 1 << 20);   // as nvnMemoryPoolInitialize registers a CPU-visible pool (an untracked range counts as written)
    static pool_t pl; pl.mem = mem; pl.size = 1 << 20; pl.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;
    tex_t t = {.serial = 9990}; t.d.width = t.d.height = 8; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.mem = mem; t.pool = &pl;
    uint32_t lin[64]; for (int i = 0; i < 64; i++) lin[i] = 0x11223344; nvn_swizzle(&t.d, 0, 0, lin, 32, t.mem);   // the CPU fills it
    render_clear(&t, 0, 1, 0);
    if (idle) {
        EXPECT("idle: the GPU rendered green, memory still the CPU's", 0x11223344, texel(&t, 3, 5));
        for (int i = 0; i < 12; i++) empty_sub();
        EXPECT("idle: 12 submissions later, not written back yet", 0x11223344, texel(&t, 3, 5));
        for (int i = 0; i < 28; i++) empty_sub();
        EXPECT("idle: 40 submissions without a GPU write: green written back", 0xff00ff00, texel(&t, 3, 5));
        EXPECT("idle: ... still the GPU's texture", 1, gpu_owned(t.serial));
        for (int i = 0; i < 64; i++) lin[i] = 0x55667788; dm_guest_write_prepare(t.mem, t.size); nvn_swizzle(&t.d, 0, 0, lin, 32, t.mem);   // a tile copied in
        empty_sub();
        EXPECT("idle: the CPU wrote it: handed back (not GPU-owned)", 0, gpu_owned(t.serial));
        EXPECT("idle: ... and its next fetch takes the CPU's bytes (watched, written)", 1, cpuf_written(&t));
        printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
    }
    EXPECT("after the GPU rendered green: memory still holds what the CPU wrote", 0x11223344, texel(&t, 3, 5));
    mtl_pool_invalidate(&pl, 0, t.size);
    EXPECT("after an invalidate over it: green written back (texel 3,5)", 0xff00ff00, texel(&t, 3, 5));
    EXPECT("... and texel 7,7", 0xff00ff00, texel(&t, 7, 7));
    render_clear(&t, 0, 0, 1);
    mtl_pool_invalidate(&pl, 0x40000, 0x1000);   // a range elsewhere in the pool
    EXPECT("an invalidate of another range writes nothing back", 0xff00ff00, texel(&t, 3, 5));
    for (int i = 0; i < 64; i++) lin[i] = 0x55667788; dm_guest_write_prepare(t.mem, t.size); nvn_swizzle(&t.d, 0, 0, lin, 32, t.mem);   // the CPU writes after the rendering
    mtl_pool_invalidate(&pl, 0, t.size);
    EXPECT("the CPU wrote after the GPU rendered blue: its bytes stay", 0x55667788, texel(&t, 3, 5));
    // Plan item 203: the dirty tracking is per 16 KiB page; the CPU writing other data on the texture's page (here the bytes right after it) is not a
    // write of the texture: the rendering is written back. The CPU writing one byte of the texture itself keeps the CPU's bytes.
    // -DDM_TEST_WB_PAGE_ONLY (the pages decide, as before) fails both (nothing red is written back)
    render_clear(&t, 1, 0, 0);
    dm_guest_write_prepare(t.mem + t.size, 16); memset(t.mem + t.size, 0x5a, 16);
    mtl_pool_invalidate(&pl, 0, t.size);
    EXPECT("203: the CPU wrote next to the texture on its page: red written back", 0xff0000ff, texel(&t, 3, 5));
    render_clear(&t, 0, 1, 0);
    dm_guest_write_prepare(t.mem, 1); t.mem[0] ^= 1;
    mtl_pool_invalidate(&pl, 0, t.size);
    EXPECT("203: the CPU wrote one byte of the texture: its bytes stay (texel 7,7 still red)", 0xff0000ff, texel(&t, 7, 7));
    // Plan item 191: every level and layer of an invalidate's textures in one command buffer and one wait (DavidZande/MacNativeGames 8c46687). A
    // 4-level texture and a second texture in one invalidated range: one command buffer; the first's level 0 green, its levels 1-3 the CPU's bytes as
    // uploaded, the second blue. DM_NVN_WB_BATCH=0 (a command buffer per level, as before): 5, and the count fails alone
    tex_t m = {.serial = 9991}; m.d = t.d; m.d.width = m.d.height = 16; m.d.levels = 4; nvn_tex_layout(&m.d, &L); m.size = (uint32_t)L.size; m.mem = mem + 0x10000; m.pool = &pl;
    tex_t s2 = {.serial = 9992}; s2.d = t.d; s2.size = t.size; s2.mem = mem + 0x20000; s2.pool = &pl;
    dm_guest_write_prepare(mem + 0x10000, 0x20000);
    for (unsigned l = 0; l < 4; l++) { uint32_t ml[256]; for (int i = 0; i < 256; i++) ml[i] = 0xa0a0a000u | l; nvn_swizzle(&m.d, l, 0, ml, (16 >> l) * 4, m.mem); }
    for (int i = 0; i < 64; i++) lin[i] = 0x11223344; nvn_swizzle(&s2.d, 0, 0, lin, 32, s2.mem);
    render_clear(&m, 0, 1, 0); render_clear(&s2, 0, 0, 1);
    uint64_t cbs = g_wb_cbs; mtl_pool_invalidate(&pl, 0x10000, 0x20000);
    EXPECT("191: an invalidate over a 4-level texture and another: command buffers", 1, g_wb_cbs - cbs);
    uint32_t ml[256]; nvn_deswizzle(&m.d, 0, 0, m.mem, ml, 64);
    EXPECT("191: ... the 4-level texture's level 0 green (texel 9,13)", 0xff00ff00, ml[13 * 16 + 9]);
    int lv_ok = 1; for (unsigned l = 1; l < 4; l++) { nvn_deswizzle(&m.d, l, 0, m.mem, ml, (16 >> l) * 4); for (unsigned i = 0; i < (16u >> l) * (16u >> l); i++) lv_ok &= ml[i] == (0xa0a0a000u | l); }
    EXPECT("191: ... its levels 1-3 the CPU's bytes as uploaded (every texel)", 1, lv_ok);
    EXPECT("191: ... the other texture blue (texel 3,5)", 0xffff0000, texel(&s2, 3, 5));
    // Plan item 278 (MacNativeGames 1bffa28): after the game's fence waits (SyncWait, QueueFinish) a target up to 64 KiB whose rendering completed is
    // written back with no invalidate, as Tears of the Kingdom's grass cut map is read; one of 128 KiB is not (an invalidate still is), nor one whose
    // rendering has not completed (until it has). Written back on an invalidate only (-DDM_TEST_WB_INVALIDATE_ONLY): 5 fail
    tex_t f = {.serial = 9993}; f.d = t.d; f.size = t.size; f.mem = mem + 0x28000; f.pool = &pl;
    dm_guest_write_prepare(f.mem, f.size); memset(f.mem, 0, f.size);
    render_clear(&f, 0, 1, 0);
    EXPECT("278: a small target rendered green: memory still zero before a fence wait", 0, texel(&f, 3, 5));
    uint64_t cbs0 = g_wb_cbs, used0 = g_wb_stage_used; mtl_wb_fence_done();
    EXPECT("278: ... after a fence wait: green, with no invalidate [-DDM_TEST_WB_INVALIDATE_ONLY]", 0xff00ff00, texel(&f, 3, 5));
    EXPECT("286: ... from the copy its submission made: no command buffer of the write-back's own [-DDM_TEST_WB_STAGE_OFF]", 1, g_wb_stage_used - used0 == 1 && g_wb_cbs == cbs0);
    tex_t bg = {.serial = 9994}; bg.d = t.d; bg.d.width = 256; bg.d.height = 128; nvn_tex_layout(&bg.d, &L); bg.size = (uint32_t)L.size; bg.mem = mem + 0x40000; bg.pool = &pl;
    dm_guest_write_prepare(bg.mem, bg.size); memset(bg.mem, 0, bg.size);
    render_clear(&bg, 1, 0, 0); mtl_wb_fence_done();
    uint32_t *bl = malloc(256 * 128 * 4); nvn_deswizzle(&bg.d, 0, 0, bg.mem, bl, 256 * 4);
    EXPECT("278: a 128 KiB target: not written back after a fence wait", 0, bl[77 * 256 + 200]);
    mtl_pool_invalidate(&pl, 0x40000, bg.size); nvn_deswizzle(&bg.d, 0, 0, bg.mem, bl, 256 * 4);
    EXPECT("278: ... an invalidate over it writes it back (red)", 0xff0000ff, bl[77 * 256 + 200]); free(bl);
    mtl_test_done_hold(1);
    uint64_t fz = render_clear_commit(&f, 0, 0, 1);
    for (int i = 0; i < 5000 && mtl_test_done_to(0) < fz; i++) usleep(1000);   // completed on the GPU; the done fence held back
    mtl_wb_fence_done();
    EXPECT("278: rendered blue, its fence not reached: not written back (still green)", 0xff00ff00, texel(&f, 3, 5));
    used0 = g_wb_stage_used; mtl_test_done_hold(0); mtl_wb_fence_done();
    EXPECT("278: ... once it is: blue [-DDM_TEST_WB_INVALIDATE_ONLY]", 0xffff0000, texel(&f, 3, 5));
    EXPECT("286: ... from its copy too", 1, g_wb_stage_used - used0);
    // A pool finalized while a GPU write-back record of a texture in it is pending, a new pool made on the same memory and the game's own data
    // put there: an invalidate of the new pool writes no old rendering back (MacNativeGames 0930478 / 873edff: heap corruption after a warp in
    // Tears of the Kingdom). The record goes with the pool [-DDM_TEST_WB_KEEP_DEAD: 1 fails]; with it kept and a new range's generations from 0
    // [-DDM_TEST_WB_KEEP_DEAD -DDM_TEST_DIRTY_NO_BORN] the rendering is written over the data: 2 fail
    render_clear(&t, 1, 0, 1);
    uint64_t key = t.serial;
    nvn_dirty_remove_range(mem); mtl_pool_del(mem);   // nvnMemoryPoolFinalize
    EXPECT("a pool finalized: its textures' write-back records go with it [-DDM_TEST_WB_KEEP_DEAD]", 0, nvn_hm_get(&g_wb, &key) != NULL);
    mtl_pool_add(mem, 1 << 20); nvn_dirty_add_range(mem, 1 << 20);   // nvnMemoryPoolInitialize on the same memory
    memset(mem, 0xcc, 0x30000);
    mtl_pool_invalidate(&pl, 0, 0x30000);
    EXPECT("... an invalidate of the pool made on its memory leaves the game's data (texel 3,5)", 0xcccccccc, texel(&t, 3, 5));
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
