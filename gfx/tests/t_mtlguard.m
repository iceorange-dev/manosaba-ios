// Guards against Metal aborts (plan item 64m, after super-mario-odyssey-ios 693bc6b / 356ab26, MIT), each case in a process of its own (this
// test run again with --case: an abort ends only that one):
//   a red full-screen draw into an RGBA8 target with an R11G11B10F texture bound as the depth target: the submission completes and the colour is
//   red (the depth attachment left out, reported once)
//   a cube texture registered with a view of one face (layers 2 + 1, the cube's type kept): the view is made, as a 2D view of face 2; a view of
//   three faces as a 2D array of three
//   on a GPU without BC (DM_TEST_NO_BC=1: BC4 kept as R8), an 8x8 BC4 texture as a render target through an RGBA16UI view (Tears of the
//   Kingdom's BC encoders), drawn into, then cleared through the same view: the submission completes (the attachment and the clear left out)
// Any format attached as depth (-DDM_TEST_DEPTH_ANY_FORMAT): 1 fails (Metal's validation aborts the child); the cube type kept
// (-DDM_TEST_CUBE_VIEW_KEPT): 2 fail; the views of the unpacked BC texture made (-DDM_TEST_UNPACKED_VIEW): 2 fail.
//   a Depth32Float_Stencil8 texture bound as the colour target (manosaba at a trial's start), drawn into: completes (left out, reported once); the
//   parallel precompile scanning a later pass whose depth target is RGBA8: completes. A depth / stencil texture attached as colour
//   (-DDM_TEST_COLOR_ANY_FORMAT): 1 fails; any format predicted as depth (-DDM_TEST_PREDICT_ANY_DEPTH): 1 fails
//   the same with a 3D BC4 texture (Tears of the Kingdom's encoders write 3D ones; decoded since 62c3): completes
#include "../nvn/nvn_mtl.m"
#include "../nvn/hle_nvn.c"
#include <spawn.h>
#include <sys/wait.h>
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
static void *g_g_mem; static pool_t g_g_pool;
static void setup(void) {
    mtl_init();
    if (posix_memalign(&g_g_mem, getpagesize(), 1 << 20)) abort(); memset(g_g_mem, 0, 1 << 20); g_g_pool = (pool_t){.mem = g_g_mem, .size = 1 << 20}; mtl_pool_add(g_g_mem, 1 << 20);
}
static void submit(NVNcommandBuffer *c) {
    NVNcommandHandle h = h_nvnCommandBufferEndRecording(c); uint64_t hs[1] = {(uint64_t)h}; state_reset(); memset(&R, 0, sizeof R);
    mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); nvn_exec_rest(NULL, 0); nvn_exec((const uint8_t *)(uintptr_t)h); mtl_wait(mtl_sub_commit(), 5000000000ull);
}
static int depth_case(void) {   // 0: completed and red
    setup();
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float2 P[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
        "vertex float4 v(uint n [[vertex_id]]) { return float4(P[n], 0.5, 1); }\nfragment float4 red() { return float4(1, 0, 0, 1); }\n" options:nil error:&e];
    if (!lib) { printf("library: %s\n", e.localizedDescription.UTF8String); return 2; }
    static prog_t pg = {.serial = 2500001}; NvProg *p = [NvProg new];
    p->sh[0] = [NvShader new]; p->sh[0]->fn = [lib newFunctionWithName:@"v"]; p->sh[1] = [NvShader new]; p->sh[1]->fn = [lib newFunctionWithName:@"red"]; p->sh[1]->stage = 1; p->sh[1]->rf.color_mask = 1;
    serial_put(pg.serial, p);
    tex_t color = {.serial = 2500010}, depth = {.serial = 2500011};
    color.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = 4, .height = 4, .depth = 1, .levels = 1, .samples = 1};
    depth.d = color.d; depth.d.format = NVN_FORMAT_R11G11B10F;
    if (!tex_mtl(&color) || !tex_mtl(&depth)) return 3;
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&g_g_pool, 0, 16384); h_nvnCommandBufferBeginRecording(&c);
    const NVNtexture *tg[] = {(const NVNtexture *)&color}; h_nvnCommandBufferSetRenderTargets(&c, 1, tg, NULL, (const NVNtexture *)&depth, NULL);
    h_nvnCommandBufferSetViewports(&c, 0, 1, (float[]){0, 0, 4, 4}); h_nvnCommandBufferSetScissor(&c, 0, 0, 4, 4);
    h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)&pg, 3); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    submit(&c);
    if (mtl_last_commit_error()) return 4;
    id<MTLBuffer> out = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:tex_mtl(&color) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(4, 4, 1) toBuffer:out destinationOffset:0 destinationBytesPerRow:16 destinationBytesPerImage:64];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    return ((const uint8_t *)out.contents)[0] == 255 ? 0 : 5;
}
// manosaba at a trial's start (2026-10-27): a Depth32Float_Stencil8 texture in the colour slot, an RGBA8 one in the depth slot. which 0: the replay
// (a pass with only that colour target, drawn into); 1: the parallel precompile (a pass drawn first misses its pipeline and scans the rest of the
// submission, whose second pass has an RGBA8 colour target and an RGBA8 depth target, drawn into). 0: completed without a command buffer error
static int swapped_case(int which) {
    setup();
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float2 P[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
        "vertex float4 v(uint n [[vertex_id]]) { return float4(P[n], 0.5, 1); }\nfragment float4 red() { return float4(1, 0, 0, 1); }\n" options:nil error:&e];
    if (!lib) { printf("library: %s\n", e.localizedDescription.UTF8String); return 2; }
    static prog_t pg = {.serial = 2500201}; NvProg *p = [NvProg new];
    p->sh[0] = [NvShader new]; p->sh[0]->fn = [lib newFunctionWithName:@"v"]; p->sh[1] = [NvShader new]; p->sh[1]->fn = [lib newFunctionWithName:@"red"]; p->sh[1]->stage = 1; p->sh[1]->rf.color_mask = 1;
    serial_put(pg.serial, p);
    tex_t color = {.serial = 2500210}, ds = {.serial = 2500211}, other = {.serial = 2500212};
    color.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = 4, .height = 4, .depth = 1, .levels = 1, .samples = 1};
    ds.d = color.d; ds.d.format = NVN_FORMAT_DEPTH32F_STENCIL8; other.d = color.d;
    if (!tex_mtl(&color) || !tex_mtl(&ds) || !tex_mtl(&other)) return 3;
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&g_g_pool, 0, 16384); h_nvnCommandBufferBeginRecording(&c);
    h_nvnCommandBufferSetViewports(&c, 0, 1, (float[]){0, 0, 4, 4}); h_nvnCommandBufferSetScissor(&c, 0, 0, 4, 4);
    h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)&pg, 3);
    if (which == 0) { const NVNtexture *tg[] = {(const NVNtexture *)&ds}; h_nvnCommandBufferSetRenderTargets(&c, 1, tg, NULL, NULL, NULL); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3); }
    else { const NVNtexture *t1[] = {(const NVNtexture *)&color}; h_nvnCommandBufferSetRenderTargets(&c, 1, t1, NULL, NULL, NULL); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
           const NVNtexture *t2[] = {(const NVNtexture *)&other}; h_nvnCommandBufferSetRenderTargets(&c, 1, t2, NULL, (const NVNtexture *)&color, NULL); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3); }
    submit(&c);
    usleep(300000);   // the background compiles the scan kicked
    return mtl_last_commit_error() ? 4 : 0;
}
static int cube_case(int layers) {   // 0: the view made as 2D (one face) or a 2D array (several), starting at face 2
    setup();
    NVNtexturePool tp; h_nvnTexturePoolInitialize(&tp, (const NVNmemoryPool *)&g_g_pool, 0x10000, 64);
    tex_t cube = {.magic = MAGIC('T', 'E', 'X', 0), .serial = 2500020};
    cube.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_CUBEMAP, .format = NVN_FORMAT_RGBA8, .width = 4, .height = 4, .depth = 6, .levels = 1, .samples = 1};
    NVNtextureView v; h_nvnTextureViewSetDefaults(&v); h_nvnTextureViewSetLayers(&v, 2, layers);
    h_nvnTexturePoolRegisterTexture(&tp, 7, (const NVNtexture *)&cube, &v);
    uint64_t th = (uint64_t)h_nvnDeviceGetTextureHandle(NULL, 7, 0);
    pthread_mutex_lock(&g_rmu); id<MTLTexture> t = (__bridge id<MTLTexture>)tex_fast(h_tid(th)); pthread_mutex_unlock(&g_rmu);
    if (!t) return 2;
    MTLTextureType want = layers == 1 ? MTLTextureType2D : MTLTextureType2DArray;
    return t.textureType == want && t.parentRelativeSlice == 2 && (layers == 1 || t.arrayLength == (NSUInteger)layers) ? 0 : 3;
}
static int unpacked_case(int is3d) {   // 0: completed without a command buffer error
    setenv("DM_TEST_NO_BC", "1", 1); setup();
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float2 P[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
        "vertex float4 v(uint n [[vertex_id]]) { return float4(P[n], 0.5, 1); }\nfragment uint4 blocks() { return uint4(1, 2, 3, 4); }\n" options:nil error:&e];
    if (!lib) { printf("library: %s\n", e.localizedDescription.UTF8String); return 2; }
    static prog_t pg = {.serial = 2500101}; NvProg *p = [NvProg new];
    p->sh[0] = [NvShader new]; p->sh[0]->fn = [lib newFunctionWithName:@"v"]; p->sh[1] = [NvShader new]; p->sh[1]->fn = [lib newFunctionWithName:@"blocks"]; p->sh[1]->stage = 1; p->sh[1]->rf.color_mask = 1;
    serial_put(pg.serial, p);
    tex_t bc = {.serial = 2500110}; bc.d = (nvn_texdesc){.target = is3d ? NVN_TEXTURE_TARGET_3D : NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGTC1_UNORM, .width = 8, .height = 8, .depth = is3d ? 4 : 1, .levels = 1, .samples = 1};
    if (!tex_mtl(&bc)) return 3;
    NVNtextureView vw; h_nvnTextureViewSetDefaults(&vw); h_nvnTextureViewSetFormat(&vw, NVN_FORMAT_RGBA16UI);
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&g_g_pool, 0, 16384); h_nvnCommandBufferBeginRecording(&c);
    const NVNtexture *tg[] = {(const NVNtexture *)&bc}; const NVNtextureView *tv[] = {&vw}; h_nvnCommandBufferSetRenderTargets(&c, 1, tg, tv, NULL, NULL);
    h_nvnCommandBufferSetViewports(&c, 0, 1, (float[]){0, 0, 2, 2}); h_nvnCommandBufferSetScissor(&c, 0, 0, 2, 2);
    h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)&pg, 3); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    NVNcopyRegion rg = {.width = 2, .height = 2, .depth = 1}; uint32_t z[4] = {0, 0, 0, 0}; h_nvnCommandBufferClearTextureui(&c, (const NVNtexture *)&bc, &vw, &rg, z, 15);
    submit(&c);
    return mtl_last_commit_error() ? 4 : 0;
}
static char *g_self;
static int in_child(int which) {   // the case run in a fresh process (posix_spawn of this test with --case: Metal's compiler service is not reachable from a forked child); its exit status, or 100 + the signal that ended it
    char k[8]; snprintf(k, sizeof k, "%d", which); char *argv[] = {g_self, "--case", k, NULL}; extern char **environ; pid_t pid;
    fflush(stdout); if (posix_spawn(&pid, g_self, NULL, NULL, argv, environ)) return 99;
    int st = 0; waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 100 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0); g_self = argv[0];
    if (argc == 3 && !strcmp(argv[1], "--case")) { int r; @autoreleasepool { switch (atoi(argv[2])) { case 0: r = depth_case(); break; case 1: r = cube_case(1); break; case 2: r = cube_case(3); break; case 3: r = unpacked_case(0); break; case 5: r = swapped_case(0); break; case 6: r = swapped_case(1); break; default: r = unpacked_case(1); } } return r; }
    int r = in_child(0);
    char line[160]; snprintf(line, sizeof line, "an R11G11B10F depth target: the submission completes, the colour red (child status %d)", r); check(line, r == 0);
    r = in_child(1); snprintf(line, sizeof line, "a cube's one-face view with the cube's type: made, as a 2D view of face 2 (child status %d)", r); check(line, r == 0);
    r = in_child(2); snprintf(line, sizeof line, "... a view of three faces: a 2D array of three (child status %d)", r); check(line, r == 0);
    r = in_child(3); snprintf(line, sizeof line, "no BC: a BC4 render target and clear through an RGBA16UI view: completes (child status %d)", r); check(line, r == 0);
    r = in_child(4); snprintf(line, sizeof line, "... a 3D BC4 texture the same: completes (child status %d)", r); check(line, r == 0);
    r = in_child(5); snprintf(line, sizeof line, "a Depth32Float_Stencil8 texture as the colour target: completes, left out (child status %d) [-DDM_TEST_COLOR_ANY_FORMAT]", r); check(line, r == 0);
    r = in_child(6); snprintf(line, sizeof line, "the parallel precompile over a pass with an RGBA8 depth target: completes (child status %d) [-DDM_TEST_PREDICT_ANY_DEPTH]", r); check(line, r == 0);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
