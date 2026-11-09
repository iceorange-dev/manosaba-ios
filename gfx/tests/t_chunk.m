// Mid-submission commits (nvn_mtl.m chunk_maybe_split / mtl_sub_split; MacNativeGames): with the TBDR rewrites off, a submission of 4 passes × 50
// additive draws is committed in parts once 60 draws went in, and the picture is the same as in one command buffer (each draw adds 1 / 255 to red:
// 200). With DM_NVN_CHUNK_DRAWS=0 (negative control) it is one command buffer. --tbdr-nomerge (plan item 228): the TBDR rewrites on but those that
// merge or defer passes (defer, fuse, hoist), 4 passes alternating between two targets: committed in parts, 100 on each (-DDM_TEST_CHUNK_ANY_TBDR,
// no commits with any rewrite on as before: 1 fails); --tbdr-all: every rewrite on, one command buffer, 100 on each. --busy (plan item 315b): the
// game's work held on the GPU (the in-flight count raised by one, as a part still running would): no mid-submission commit, the due one held at the
// boundaries; then with the GPU idle the same submission is committed in parts (-DDM_TEST_CHUNK_BUSY_IGNORED, committed with the GPU busy as
// before: 2 fail; DM_NVN_CHUNK_BUSY=0 --busy: committed in parts while busy). Every run starts with the GPU idle (mtl_init's work reported back);
// the read-back copies' encoder at the end is a boundary too, where the next due commit waits for the first part.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-70s expected %-6lld → got %-6lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static prog_t g_pg;
static int g_busy;   // the in-flight count the test holds up (--busy)
static int g_alt, g_red2;   // g_alt: the passes alternate between two targets (g_red2: the second one's red)
static int run(void) {   // returns red of pixel (3, 3)
    static uint8_t *cm; if (!cm) { if (posix_memalign((void **)&cm, getpagesize(), 1 << 20)) abort(); mtl_pool_add(cm, 1 << 20); }
    memset(cm, 0, 1 << 20); chain c = {cm, 0};
    static uint64_t ser = 9800; tex_t t = {.serial = ser++}; t.d.width = t.d.height = 8; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    tex_t t2 = t; t2.serial = ser++;
    p_bind_program bp = {&g_pg, 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    color_st col = {1, NVN_LOGIC_OP_COPY, NVN_ALPHA_FUNC_ALWAYS}; p_bind_state x = {.kind = 1}; memcpy(x.bytes, &col, sizeof col); emit(&c, OP_BIND_STATE, &x, sizeof x);
    blend_st b = {0, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ONE, NVN_BLEND_EQUATION_ADD, NVN_BLEND_EQUATION_ADD}; x.kind = 0; memcpy(x.bytes, &b, sizeof b); emit(&c, OP_BIND_STATE, &x, sizeof x);
    ds_st d = {0, 0, NVN_DEPTH_FUNC_ALWAYS, 0, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_FUNC_ALWAYS, 1, 1, 1, 1, 1, 1}; x.kind = 5; memcpy(x.bytes, &d, sizeof d); emit(&c, OP_BIND_STATE, &x, sizeof x);
    for (int pass = 0; pass < 4; pass++) {
        p_set_rt rt = {.n = 1}; rt.color[0] = g_alt && (pass & 1) ? &t2 : &t; emit(&c, OP_SET_RT, &rt, sizeof rt);
        p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = 8; emit(&c, OP_VIEWPORTS, &v, sizeof v); p_scissor sc = {0, 0, 8, 8}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
        if (!pass || (g_alt && pass == 1)) { p_clear_color cc = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(&c, OP_CLEAR_COLOR, &cc, sizeof cc); }
        for (int k = 0; k < 50; k++) { p_draw dr = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(&c, OP_DRAW, &dr, sizeof dr); }
        p_set_rt none = {.n = 0}; emit(&c, OP_SET_RT, &none, sizeof none);
    }
    emit(&c, OP_END, NULL, 0);
    for (int i = 0; i < 500 && atomic_load(&g_inflight) > g_busy; i++) usleep(10000);   // the GPU has none of the game's work but what the test holds
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLBuffer> rb = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(rb);
    id<MTLBuffer> rb2 = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(rb2);
    [blit() copyFromTexture:tex_mtl(&t) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    if (g_alt) [blit() copyFromTexture:tex_mtl(&t2) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1) toBuffer:rb2 destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    mtl_wait(mtl_sub_commit(), 5000000000ull); g_red2 = ((uint8_t *)rb2.contents)[(3 * 8 + 3) * 4]; return ((uint8_t *)rb.contents)[(3 * 8 + 3) * 4];
}
int main(int argc, char **argv) { @autoreleasepool {
    int nomerge = argc > 1 && !strcmp(argv[1], "--tbdr-nomerge"), all = argc > 1 && !strcmp(argv[1], "--tbdr-all"), busy = argc > 1 && !strcmp(argv[1], "--busy"),
        nochunks = argc > 1 && !nomerge && !all && !busy;
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_TBDR", nomerge || all ? "1" : "0", 1); if (nomerge) setenv("DM_NVN_TBDR_OFF", "defer,fuse,hoist", 1);
    if (nochunks) setenv("DM_NVN_CHUNK_DRAWS", "0", 1); g_alt = nomerge || all; mtl_init();
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal; vertex float4 v(uint n[[vertex_id]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[n],0,1);}\n"
        "fragment float4 add() { return float4(1.0 / 255.0, 0, 0, 0); }\n" options:nil error:&e];
    g_pg.serial = 8900; NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"]; p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:@"add"]; serial_put(g_pg.serial, p);
    if (busy) {
        const char *e = getenv("DM_NVN_CHUNK_BUSY"); int always = e && !atoi(e); uint64_t st0[2], st1[2], st2[2];
        mtl_chunk_stats(st0); g_busy = 1; atomic_fetch_add(&g_inflight, 1); int red = run(); atomic_fetch_sub(&g_inflight, 1); g_busy = 0; mtl_chunk_stats(st1);
        EXPECT("GPU busy: 200 additive draws, red 200", 200, red);
        EXPECT(always ? "GPU busy, DM_NVN_CHUNK_BUSY=0: committed in parts all the same" : "GPU busy: no mid-submission commit [-DDM_TEST_CHUNK_BUSY_IGNORED: committed]",
               always ? 1 : 0, (long long)(st1[0] > st0[0]));
        EXPECT(always ? "... no boundary held one" : "... the due commit held at the boundaries after it [-DDM_TEST_CHUNK_BUSY_IGNORED: none]", always ? 0 : 1, (long long)(st1[1] > st0[1]));
        red = run(); mtl_chunk_stats(st2);
        EXPECT("then the GPU idle: red 200", 200, red);
        EXPECT("... committed in parts (the first due commit at once)", 1, (long long)(st2[0] > st1[0]));
        EXPECT("no command buffer error", 0, mtl_last_commit_error());
        printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
    }
    uint64_t c0 = mtl_chunk_commits(); int red = run(); uint64_t parts = mtl_chunk_commits() - c0;
    if (g_alt) { EXPECT("two targets, 100 additive draws each: red 100 on the first", 100, red); EXPECT("... and 100 on the second", 100, g_red2);
                 EXPECT(nomerge ? "TBDR rewrites without the merging ones: committed in parts [-DDM_TEST_CHUNK_ANY_TBDR: none]" : "every TBDR rewrite on: one command buffer", nomerge ? 1 : 0, (long long)(parts > 0)); }
    else { EXPECT("200 additive draws: red 200", 200, red);
           EXPECT(nochunks ? "DM_NVN_CHUNK_DRAWS=0: no mid-submission commit" : "committed in parts (at least one mid-submission commit)", nochunks ? 0 : 1, nochunks ? (long long)parts : parts > 0); }
    EXPECT("no command buffer error", 0, mtl_last_commit_error());
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
