// DM_NVN_KEY_WATCH (gfx/nvn/mtl/draw.inc key_watch_check; plan item 322, a diagnostic that finds the frames a shader the translator warned about
// is used in): with DM_NVN_KEY_WATCH=588e242a52f5f7aa,45eb69b3b99c6600_b,short two programs are drawn twice each: A (vertex 588e242a52f5f7aa,
// fragment aaaaaaaaaaaaaaaa) and B (vertex 588e242a00000000, which shares the first half of a watched key, fragment 45eb69b3b99c6600) and a
// compute program (kernel 45eb69b3b99c6600 too, a third shader object) dispatched. Two keys watched ("short" is not a key), and the three
// shaders whose keys are listed found once each: A's vertex, B's fragment (the _b of its library ignored) and the kernel; A's fragment and B's
// vertex looked at and not watched. With the variable unset no shader is looked at. Only the vertex stage looked at
// (-DDM_TEST_KEY_WATCH_VERTEX_ONLY): 3 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-100s expected %-4lld → got %-4lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static uint8_t *g_cm;
static NvShader *shader(id<MTLLibrary> l, NSString *fn, int stage, const char *key) {
    NvShader *s = [NvShader new]; s->stage = stage; s->fn = [l newFunctionWithName:fn]; snprintf(s->key, sizeof s->key, "%s", key); return s;
}
static void run(prog_t *const *pg, int n, prog_t *cp) {   // each program drawn twice into a fresh 64x64 target, then the compute program dispatched
    memset(g_cm, 0, 512 << 10); chain c = {g_cm, 0};
    static uint64_t ser = 9900; tex_t t = {.serial = ser++}; t.d.width = t.d.height = 64; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8;
    p_set_rt rt = {.n = 1}; rt.color[0] = &t; emit(&c, OP_SET_RT, &rt, sizeof rt);
    p_scissor sc = {0, 0, 64, 64}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
    p_viewports v = {.n = 1}; v.v[0][2] = 64; v.v[0][3] = 64; emit(&c, OP_VIEWPORTS, &v, sizeof v);
    for (int i = 0; i < 2 * n; i++) { p_bind_program bp = {pg[i % n], 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp); p_draw dr = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(&c, OP_DRAW, &dr, sizeof dr); }
    p_set_rt none = {.n = 0}; emit(&c, OP_SET_RT, &none, sizeof none);
    if (cp) { p_bind_program bp = {cp, NVN_SHADER_STAGE_COMPUTE_BIT}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp); p_dispatch d = {1, 1, 1, 0}; emit(&c, OP_DISPATCH, &d, sizeof d); }
    emit(&c, OP_END, NULL, 0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    mtl_wait(mtl_sub_commit(), 5000000000ull);
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_TBDR", "0", 1); unsetenv("DM_PSO_RECORD"); unsetenv("DM_PSO_USER"); unsetenv("DM_SAVE"); setenv("DM_NVN_KEY_WATCH", "588e242a52f5f7aa,45eb69b3b99c6600_b,short", 1); mtl_init();
    if (posix_memalign((void **)&g_cm, getpagesize(), 1 << 20)) abort(); mtl_pool_add(g_cm, 1 << 20);
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 vp(uint i [[vertex_id]]) { return float4(i == 1 ? 3.0 : -1.0, i == 2 ? 3.0 : -1.0, 0, 1); }\n"
        "fragment float4 fp() { return float4(1, 0, 0, 1); }\n"
        "kernel void k() {}\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    static prog_t pa = {.serial = 9801}, pb = {.serial = 9802}, pc = {.serial = 9803}, pd = {.serial = 9804};
    NvProg *a = [NvProg new]; a->sh[0] = shader(l, @"vp", 0, "588e242a52f5f7aa"); a->sh[1] = shader(l, @"fp", 1, "aaaaaaaaaaaaaaaa"); serial_put(pa.serial, a);
    NvProg *b = [NvProg new]; b->sh[0] = shader(l, @"vp", 0, "588e242a00000000"); b->sh[1] = shader(l, @"fp", 1, "45eb69b3b99c6600"); serial_put(pb.serial, b);
    NvProg *k = [NvProg new]; k->sh[5] = shader(l, @"k", 5, "45eb69b3b99c6600"); k->sh[5]->rf.tg[0] = k->sh[5]->rf.tg[1] = k->sh[5]->rf.tg[2] = 1; serial_put(pc.serial, k);
    prog_t *two[2] = {&pa, &pb}; run(two, 2, &pc);
    EXPECT("two keys watched (\"short\" is not one)", 2, key_watch_n);
    EXPECT("three shaders found, each once though drawn twice [-DDM_TEST_KEY_WATCH_VERTEX_ONLY: 2]", 3, key_watch_hits);
    EXPECT("A's vertex shader 588e242a52f5f7aa: watched", 1, a->sh[0]->kw);
    EXPECT("A's fragment shader: looked at, not watched [-DDM_TEST_KEY_WATCH_VERTEX_ONLY: not looked at]", 2, a->sh[1]->kw);
    EXPECT("B's vertex shader 588e242a00000000 (the first half of a watched key): not watched", 2, b->sh[0]->kw);
    EXPECT("B's fragment shader 45eb69b3b99c6600 (listed with _b): watched [-DDM_TEST_KEY_WATCH_VERTEX_ONLY: not looked at]", 1, b->sh[1]->kw);
    EXPECT("the kernel 45eb69b3b99c6600: watched", 1, k->sh[5]->kw);
    unsetenv("DM_NVN_KEY_WATCH"); key_watch_n = -1;
    NvProg *d = [NvProg new]; d->sh[0] = shader(l, @"vp", 0, "588e242a52f5f7aa"); d->sh[1] = shader(l, @"fp", 1, "45eb69b3b99c6600"); serial_put(pd.serial, d);
    prog_t *one[1] = {&pd}; run(one, 1, NULL);
    EXPECT("unset: nothing watched", 0, key_watch_n);
    EXPECT("unset: a shader with a key listed before is not looked at", 0, d->sh[0]->kw + d->sh[1]->kw);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
