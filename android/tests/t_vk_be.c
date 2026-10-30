// gfx/nvn/vk (plan item 301e1), the Vulkan backend on the phone without a window: a pool keeps what it held when it becomes device memory at its own
// address, and again when it goes; a GPU buffer copy and an inline uniform update land in the pool's memory once their submission is done; an event
// write and a timestamp land at completion and not before (the counter half at once); a buffer → texture → buffer round trip; a texture's
// block-linear storage uploaded (deswizzled) before the GPU reads it; ClearTexture; ReadTexels of a GPU-written texture; the format table.
// Draws (301e2), with t_vk.dmvk (android/tests/mk_vk_bundle.py) as the shader bundle: a quad of NVN's counter-clockwise winding drawn with back
// faces culled, NDC's upper half in the target's top rows (the Metal backend's orientation), the pixel shader's uniform slot and texture slot
// (pool IDs, its sampler from the same handle) found through its moved bindings, the pass's clear as its load action. 301e2b: indexed quads
// (both triangles of each); a program's own descriptor set in place of pushed descriptors (DM_VK_PUSH=0), a fresh set for each draw; a dispatch
// (its workgroup counts in c0, a writable storage slot, a texture through an image slot, an image store read back from the texture).
// -DDM_TEST_VK_EVENT_EARLY (event writes and reports landed when recorded): 2 fail; -DDM_TEST_VK_WINDING_SWAPPED (vk_draw.c's winding turned
// around): 4 fail (the screenshot is of that target); -DDM_TEST_VK_GRID_ZERO (no workgroup counts in c0): 1 fails; -DDM_TEST_VK_SET_STALE (the first
// set used again unwritten): 1 fails. 301e3: a pass the core continued (the same targets set again) goes on through a draw sampling another texture
// and is ended before a draw sampling its own attachment; its end clears the core's mark (-DDM_TEST_VK_CONT_NO_SPLIT, kept open: 4 fail, with it the
// barriers' split, the count of why passes ended and the passes timed). An inline uniform update between two draws keeps the pass open, the draw
// before it reading the old bytes and the one after the new (a private copy over the core's snapshot), and reaches memory when the submission ends
// (-DDM_TEST_VK_UB_NO_SHADOW, no private copies: 1 fails). A screenshot of the presented texture (the command channel's shot, no window here) read
// back as a PNG (-DDM_TEST_VK_SHOT_FLIPPED, rows bottom first: 1 fails). The GPU time of the frame a submission was committed in (its timestamps;
// -DDM_TEST_VK_TS_NONE, not handed on: 1 fails). Why passes end: the backend's own reasons over the op the replay is at (-DDM_TEST_VK_PE_OP_ONLY,
// only the op: 2 fail). Barriers in a pass: it goes on, ended before a draw after one sampling the pass's attachment and at one after storage
// written (-DDM_TEST_VK_BAR_NO_SPLIT: 2 fail, the passes timed with it). Each pass's GPU time summed by its targets (DM_VK_PASS_GPU;
// -DDM_TEST_VK_PTIME_FIRST_ONLY, a submission's first pass only: 1 fails).
#include "vk.h"
#include "../../gfx/nvn/core/state.h"
#include "../../gfx/nvn/core/pass.h"
#include "../../gfx/nvn/core/usnap.h"
#include "../../gfx/nvn/core/frame.h"
#include <android/native_window.h>
#include "../../tex/nvn_deswizzle.h"
#include "../nvn_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <zlib.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

uint64_t guest_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
uint64_t now_ns(void) { return guest_ns(); }   // (hle_os.c's: the pipelines' timing lines)
ANativeWindow *front_android_window(void) { return NULL; }
void dm_fault_hook_register(int (*fn)(void *addr, int is_write, void *uc)) { (void)fn; }   // (the host's crash.c; no guarded copies here)
void dm_guest_write_prep_register(void (*fn)(void *addr, size_t len)) { (void)fn; }   // (the same: nvn_dirty.c's, no pool is tracked here)
int exec_draw_dropped(void) { return 0; }   // (core/exec.c's: rendering on)
int g_rt_merged;   // (core/pass.c's: the core's mark of a continued pass)
const uint8_t *g_exec_cur;   // (core/pass.c's: the op the replay is at; none here but the SetRenderTargets below)
static uint64_t g_ser = 100;
static tex_t tex2d(uint8_t *mem, uint32_t w, uint32_t h, int fmt) {
    tex_t t; memset(&t, 0, sizeof t); t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = fmt; t.d.width = w; t.d.height = h; t.d.depth = 1; t.d.levels = 1;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.layer_size = (uint32_t)L.layer_size; t.mem = mem; t.serial = ++g_ser; return t;
}
static uint64_t run(void (*rec)(void *), void *a) { mtl_sub_begin(); rec(a); return mtl_sub_commit(); }
static uint8_t *P;   // the pool: 4 MB
static void rec_copy(void *a) { (void)a; nvn_be_copy_b2b((uint64_t)(uintptr_t)P, (uint64_t)(uintptr_t)(P + (1 << 20)), 64 << 10); }
static volatile uint32_t g_ev = 5; static uint64_t g_ts[2];
static void rec_event(void *a) {
    (void)a; nvn_be_signal_event(&g_ev, 0, 7); uint64_t rep[2] = {NVN_COUNTER_TYPE_TIMESTAMP, (uint64_t)(uintptr_t)g_ts}; nvn_be_report_counter(NULL, rep);
}
static uint8_t g_ubytes[256];
static void rec_ub(void *a) { (void)a; nvn_be_update_ub(g_ubytes, (uint64_t)(uintptr_t)(P + (2 << 20)), sizeof g_ubytes); }
static tex_t g_t, g_t2; static p_copy_b2t g_c;
static void rec_b2t(void *a) { (void)a; nvn_be_copy_b2t(&g_c); }
static void rec_t2b(void *a) { (void)a; nvn_be_copy_t2b(&g_c); }
static p_clear_tex g_clr;
static void rec_clear(void *a) { (void)a; nvn_be_clear_tex(&g_clr); }
// a draw as the replay makes one: the state set, the draw, the pass ended
static tex_t g_rt, g_tx; static prog_t g_prog; static uint8_t *g_vb, *g_ubo, tl_px[4 * 4 * 4];
static void rec_draw(void *a) {
    (void)a; st_reset(); memset(&R, 0, sizeof R);
    R.rt.n = 1; R.rt.color[0] = &g_rt; R.has_rt = 1; R.clear_c[0] = 1; R.cc[0] = nvn_rgba_make(0, 0, 0, 0);
    S.prog = &g_prog; S.has_vp = 1; S.vp[0][2] = 64; S.vp[0][3] = 64; S.dr[0] = 0; S.dr[1] = 1;
    S.poly.cull = NVN_FACE_BACK; S.poly.front = NVN_FRONT_FACE_CCW;
    S.attr[0] = (va_st){NVN_FORMAT_RGBA32F, 0, 0}; S.nattr = 1; S.stream[0] = (vs_st){16, 0}; S.nstream = 1;
    S.vbuf[0].address = (uint64_t)(uintptr_t)g_vb; S.vbuf[0].size = 64;
    S.ubo[1][0] = (p_bind_buf){1, 0, (uint64_t)(uintptr_t)g_ubo, 256};
    S.tex[1][3] = 5 | (2ull << 20);   // texture ID 5, sampler ID 2
    p_draw dw = {NVN_DRAW_PRIMITIVE_QUADS, 0, 4, 1, 0}; nvn_be_draw(&dw, NULL); nvn_be_pass_end();
}
// full-target quads: indexed into g_rt2; then the second program (its own set) into g_rt3 and g_rt4 with two uniforms, in one submission
static tex_t g_rt2, g_rt3, g_rt4; static prog_t g_prog3; static uint8_t *g_vb2, *g_ib, *g_ubo2;
static void target(tex_t *t) { R.rt.n = 1; R.rt.color[0] = t; R.has_rt = 1; R.clear_c[0] = 1; R.cc[0] = nvn_rgba_make(0, 0, 0, 0); }
static void rec_draw2(void *a) {
    (void)a; st_reset(); memset(&R, 0, sizeof R);
    S.has_vp = 1; S.vp[0][2] = 64; S.vp[0][3] = 64; S.dr[0] = 0; S.dr[1] = 1;
    S.attr[0] = (va_st){NVN_FORMAT_RGBA32F, 0, 0}; S.nattr = 1; S.stream[0] = (vs_st){16, 0}; S.nstream = 1;
    S.vbuf[0].address = (uint64_t)(uintptr_t)g_vb2; S.vbuf[0].size = 64;
    S.ubo[1][0] = (p_bind_buf){1, 0, (uint64_t)(uintptr_t)g_ubo, 256}; S.tex[1][3] = 5 | (2ull << 20);
    target(&g_rt2); S.prog = &g_prog;
    p_draw_idx di = {NVN_DRAW_PRIMITIVE_QUADS, NVN_INDEX_TYPE_UNSIGNED_SHORT, 4, 0, 0, 1, (uint64_t)(uintptr_t)g_ib}; nvn_be_draw(NULL, &di); nvn_be_pass_end();
    p_draw dw = {NVN_DRAW_PRIMITIVE_QUADS, 0, 4, 1, 0};
    target(&g_rt3); S.prog = &g_prog3; nvn_be_draw(&dw, NULL); nvn_be_pass_end();
    S.ubo[1][0] = (p_bind_buf){1, 0, (uint64_t)(uintptr_t)g_ubo2, 256};
    target(&g_rt4); nvn_be_draw(&dw, NULL); nvn_be_pass_end();
}
// a pass the core continues: draws 1 and 2 (another texture sampled) in one pass, draw 3 (its own attachment sampled, no channel written) after it
static tex_t g_rtA; static uint64_t g_cont_passes, g_cont_pe[VK_PE_N]; static int g_cont_mark;
static void rec_cont(void *a) {
    (void)a; st_reset(); memset(&R, 0, sizeof R);
    uint64_t e0[VK_PE_N], e1[VK_PE_N]; vk_pass_ends(e0);
    S.has_vp = 1; S.vp[0][2] = 64; S.vp[0][3] = 64; S.dr[0] = 0; S.dr[1] = 1;
    S.attr[0] = (va_st){NVN_FORMAT_RGBA32F, 0, 0}; S.nattr = 1; S.stream[0] = (vs_st){16, 0}; S.nstream = 1;
    S.vbuf[0].address = (uint64_t)(uintptr_t)g_vb2; S.vbuf[0].size = 64;
    S.ubo[1][0] = (p_bind_buf){1, 0, (uint64_t)(uintptr_t)g_ubo, 256}; S.tex[1][3] = 5 | (2ull << 20);
    target(&g_rtA); S.prog = &g_prog;
    p_draw dw = {NVN_DRAW_PRIMITIVE_QUADS, 0, 4, 1, 0}; uint64_t p0 = vk_pass_count();
    nvn_be_draw(&dw, NULL);
    g_rt_merged = 1;   // (as exec_set_rt does when SetRenderTargets names the open pass's own targets)
    nvn_be_draw(&dw, NULL);
    S.tex[1][3] = 9 | (2ull << 20); S.chmask.mask = 0;   // texture ID 9: the pass's colour target
    nvn_be_draw(&dw, NULL);
    g_cont_passes = vk_pass_count() - p0;
    static uint8_t op[8 + sizeof(p_set_rt)]; op_hdr h = {OP_SET_RT, sizeof op}; p_set_rt r; memset(&r, 0, sizeof r); r.n = 1; r.color[0] = &g_rt2;
    memcpy(op, &h, sizeof h); memcpy(op + 8, &r, sizeof r);
    g_exec_cur = op; nvn_be_pass_flush(); g_exec_cur = NULL;   // (the replay at a SetRenderTargets of another texture)
    g_cont_mark = g_rt_merged;
    vk_pass_ends(e1); for (int i = 0; i < VK_PE_N; i++) g_cont_pe[i] = e1[i] - e0[i];
}
// an 8-bit RGB PNG of filter 0 rows (vk_shot.c's) read back
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int png_read(const char *path, uint32_t *w, uint32_t *h, uint8_t **rgb) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc((size_t)n); int ok = fread(b, 1, (size_t)n, f) == (size_t)n; fclose(f);
    if (!ok || n < 33 || memcmp(b, "\x89PNG\r\n\x1a\n", 8)) { free(b); return 0; }
    uint8_t *idat = NULL; size_t ni = 0; *w = *h = 0;
    for (long o = 8; o + 12 <= n; o += 12 + (long)be32(b + o)) {
        uint32_t len = be32(b + o); const uint8_t *t = b + o + 4;
        if (!memcmp(t, "IHDR", 4)) { *w = be32(b + o + 8); *h = be32(b + o + 12); if (b[o + 16] != 8 || b[o + 17] != 2) { free(b); return 0; } }
        else if (!memcmp(t, "IDAT", 4)) { idat = realloc(idat, ni + len); memcpy(idat + ni, b + o + 8, len); ni += len; }
    }
    uLongf rn = (uLongf)(*w * 3 + 1) * *h; uint8_t *raw = malloc(rn ? rn : 1); ok = *w && *h && uncompress(raw, &rn, idat, ni) == Z_OK;
    *rgb = malloc((size_t)*w * 3 * *h);
    for (uint32_t y = 0; ok && y < *h; y++) { ok = raw[(size_t)y * (*w * 3 + 1)] == 0; memcpy(*rgb + (size_t)y * *w * 3, raw + (size_t)y * (*w * 3 + 1) + 1, (size_t)*w * 3); }
    free(raw); free(idat); free(b); return ok;
}
// barriers in a pass: draw 1, a barrier, draw 2 (another texture sampled) in one pass, a barrier, draw 3 (its own attachment sampled, no
// channel written) in a pass after it; draw 4 writing storage, then a barrier: the pass ends at the barrier
static tex_t g_rtB; static prog_t g_prog4; static uint8_t *g_sb4; static uint64_t g_bar_passes, g_bar_pe[VK_PE_N];
static void rec_bar(void *a) {
    (void)a; st_reset(); memset(&R, 0, sizeof R);
    uint64_t e0[VK_PE_N], e1[VK_PE_N]; vk_pass_ends(e0);
    S.has_vp = 1; S.vp[0][2] = 64; S.vp[0][3] = 64; S.dr[0] = 0; S.dr[1] = 1;
    S.attr[0] = (va_st){NVN_FORMAT_RGBA32F, 0, 0}; S.nattr = 1; S.stream[0] = (vs_st){16, 0}; S.nstream = 1;
    S.vbuf[0].address = (uint64_t)(uintptr_t)g_vb2; S.vbuf[0].size = 64;
    S.ubo[1][0] = (p_bind_buf){1, 0, (uint64_t)(uintptr_t)g_ubo, 256}; S.tex[1][3] = 5 | (2ull << 20);
    S.ssbo[1][0] = (p_bind_buf){1, 0, (uint64_t)(uintptr_t)g_sb4, 256};
    target(&g_rtB); S.prog = &g_prog;
    p_draw dw = {NVN_DRAW_PRIMITIVE_QUADS, 0, 4, 1, 0}; uint64_t p0 = vk_pass_count();
    int32_t bits = NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT; uint32_t m = S.chmask.mask;
    nvn_be_draw(&dw, NULL); nvn_be_barrier(NULL, &bits); nvn_be_draw(&dw, NULL); nvn_be_barrier(NULL, &bits);
    S.tex[1][3] = 10 | (2ull << 20); S.chmask.mask = 0; nvn_be_draw(&dw, NULL);   // texture ID 10: the pass's colour target
    S.tex[1][3] = 5 | (2ull << 20); S.chmask.mask = m; S.prog = &g_prog4; nvn_be_draw(&dw, NULL); nvn_be_barrier(NULL, &bits);
    g_bar_passes = vk_pass_count() - p0;
    vk_pass_ends(e1); for (int i = 0; i < VK_PE_N; i++) g_bar_pe[i] = e1[i] - e0[i];
}
// two draws of one pass with an inline uniform update between them; the range has a snapshot, as the core makes at the submission's start
static tex_t g_rtU; static uint8_t *g_ubo3, *g_vb3; static uint64_t g_upd_passes, g_upd_pe[VK_PE_N];
static void rec_upd(void *a) {
    (void)a; st_reset(); memset(&R, 0, sizeof R);
    g_us.n = 0; g_us.last = 0; us_reserve(1); us_add((uint64_t)(uintptr_t)g_ubo3, (uint64_t)(uintptr_t)g_ubo3 + 256); us_copy_all();
    S.has_vp = 1; S.vp[0][2] = 64; S.vp[0][3] = 64; S.dr[0] = 0; S.dr[1] = 1;
    S.attr[0] = (va_st){NVN_FORMAT_RGBA32F, 0, 0}; S.nattr = 1; S.stream[0] = (vs_st){16, 0}; S.nstream = 1;
    S.ubo[1][0] = (p_bind_buf){1, 0, (uint64_t)(uintptr_t)g_ubo3, 256}; S.tex[1][3] = 5 | (2ull << 20);
    target(&g_rtU); S.prog = &g_prog;
    p_draw dw = {NVN_DRAW_PRIMITIVE_QUADS, 0, 4, 1, 0}; uint64_t p0 = vk_pass_count();
    S.vbuf[0].address = (uint64_t)(uintptr_t)g_vb; S.vbuf[0].size = 64; nvn_be_draw(&dw, NULL);   // the upper half
    const float y[4] = {1, 0, 1, 1}; nvn_be_update_ub(y, (uint64_t)(uintptr_t)g_ubo3, sizeof y);
    S.vbuf[0].address = (uint64_t)(uintptr_t)g_vb3; nvn_be_draw(&dw, NULL);   // the lower half
    g_upd_passes = vk_pass_count() - p0;
}
// a dispatch of 2 x 1 x 1 groups of 4
static tex_t g_img, g_tx7; static prog_t g_cprog; static uint8_t *g_sb;
static void rec_dispatch(void *a) {
    (void)a; st_reset(); memset(&R, 0, sizeof R);
    S.prog = &g_cprog; S.ssbo[5][0] = (p_bind_buf){5, 0, (uint64_t)(uintptr_t)g_sb, 1024}; S.img[5][0] = 7; S.img[5][1] = 8;
    p_dispatch d = {2, 1, 1, 0}; nvn_be_dispatch(&d);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_VK_PASS_GPU", "1", 1);
    P = mmap(NULL, 4 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for (int i = 0; i < (4 << 20); i++) P[i] = (uint8_t)(i * 7 + (i >> 12));
    mtl_pool_add(P, 4 << 20);
    int same = 1; for (int i = 0; i < (4 << 20); i += 4093) same &= P[i] == (uint8_t)(i * 7 + (i >> 12));
    VkBuffer b; VkDeviceSize off = 0;
    CHECK(V.placed && same && mtl_pool_has((uint64_t)(uintptr_t)P + 100, 1000) && vk_addr((uint64_t)(uintptr_t)P + 4096, 16, &b, &off) && off == 4096,
          "a pool placed at its own address keeps what it held, and is found (offset %llu)", (unsigned long long)off);
    memset(P + (1 << 20), 0, 64 << 10);
    uint64_t f = run(rec_copy, NULL); mtl_wait(f, UINT64_MAX);
    CHECK(!memcmp(P, P + (1 << 20), 64 << 10), "a GPU copy of 64 KB lands in the pool's memory once its submission is done (fence %llu)", (unsigned long long)f);
    memset(g_ts, 0xff, sizeof g_ts);
    mtl_sub_begin(); rec_event(NULL);
    CHECK(g_ev == 5, "an event write waits for its submission (still %u)", g_ev);
    CHECK(g_ts[0] == 0 && g_ts[1] == UINT64_MAX, "a timestamp report: its counter half 0 at once, its time not yet");
    f = mtl_sub_commit(); mtl_wait(f, UINT64_MAX);
    CHECK(g_ev == 7 && g_ts[1] != UINT64_MAX && mtl_done_fence() >= f, "both land at completion (event %u, done fence %llu)", g_ev, (unsigned long long)mtl_done_fence());
    for (int i = 0; i < 256; i++) g_ubytes[i] = (uint8_t)(255 - i);
    f = run(rec_ub, NULL); memset(g_ubytes, 0, sizeof g_ubytes); mtl_wait(f, UINT64_MAX);   // (the game may reuse its bytes once recorded)
    int ok = 1; for (int i = 0; i < 256; i++) ok &= P[(2 << 20) + i] == (uint8_t)(255 - i);
    CHECK(ok, "an inline uniform update lands its bytes as they were when recorded");
    // a 64x64 RGBA8 texture: buffer → texture → buffer
    uint8_t *src = P + (3 << 20), *dst = P + (3 << 20) + (64 << 10), *store = P + (2 << 20) + 4096;
    for (int i = 0; i < 64 * 64 * 4; i++) src[i] = (uint8_t)(i * 13 + 1);
    memset(dst, 0, 64 * 64 * 4);
    g_t = tex2d(store, 64, 64, 0x25); memset(&g_c, 0, sizeof g_c); g_c.tex = &g_t; g_c.r = (NVNcopyRegion){0, 0, 0, 64, 64, 1}; g_c.src = (uint64_t)(uintptr_t)src;
    run(rec_b2t, NULL); g_c.src = (uint64_t)(uintptr_t)dst; f = run(rec_t2b, NULL); mtl_wait(f, UINT64_MAX);
    CHECK(!memcmp(src, dst, 64 * 64 * 4), "buffer → texture → buffer: the same 16 KB");
    // its storage, block linear, uploaded before the first GPU read
    uint8_t lin[64 * 64 * 4]; for (int i = 0; i < (int)sizeof lin; i++) lin[i] = (uint8_t)(i * 5 + 3);
    uint8_t *store2 = P + (3 << 20) + (256 << 10); g_t2 = tex2d(store2, 64, 64, 0x25); nvn_swizzle(&g_t2.d, 0, 0, lin, 256, store2);
    memset(dst, 0, sizeof lin); g_c.tex = &g_t2; f = run(rec_t2b, NULL); mtl_wait(f, UINT64_MAX);
    CHECK(!memcmp(lin, dst, sizeof lin), "a texture's block-linear storage is deswizzled into it before the GPU reads it");
    // ClearTexture of the whole level, then ReadTexels (the GPU's content read back)
    memset(&g_clr, 0, sizeof g_clr); g_clr.tex = &g_t2; g_clr.r = (NVNcopyRegion){0, 0, 0, 64, 64, 1}; g_clr.mask = 0xf; g_clr.c.f[0] = 1; g_clr.c.f[1] = 0.5f; g_clr.c.f[3] = 1;
    f = run(rec_clear, NULL); mtl_wait(f, UINT64_MAX);
    uint8_t back[16 * 16 * 4]; memset(back, 0, sizeof back); NVNcopyRegion rr = {8, 8, 0, 16, 16, 1};
    mtl_read_texels(&g_t2, 0, 0, &rr, back, 64, 0);
    CHECK(back[0] == 255 && (back[1] == 127 || back[1] == 128) && back[2] == 0 && back[3] == 255 && back[sizeof back - 1] == 255,
          "ClearTexture, then ReadTexels of the GPU-written texture: (%u, %u, %u, %u)", back[0], back[1], back[2], back[3]);
    // a draw: a quad over NDC's upper half (y 0..1), counter-clockwise as NVN sees it, back faces culled; colour = uniform × texture
    setenv("DM_VK_SHADERS", "t_vk.dmvk", 1);
    uint8_t *rt_mem = P + 0x380000, *tx_mem = P + 0x390000; g_vb = P + 0x3A0000; g_ubo = P + 0x3B0000;
    g_rt = tex2d(rt_mem, 64, 64, NVN_FORMAT_RGBA8); g_tx = tex2d(tx_mem, 4, 4, NVN_FORMAT_RGBA8);
    for (int i = 0; i < 16; i++) { tl_px[4 * i] = 0; tl_px[4 * i + 1] = 255; tl_px[4 * i + 2] = 255; tl_px[4 * i + 3] = 255; }
    nvn_swizzle(&g_tx.d, 0, 0, tl_px, 16, tx_mem);
    const float quad[16] = {-1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, -1, 1, 0, 1}; memcpy(g_vb, quad, sizeof quad);
    const float col[4] = {1, 1, 0.5f, 1}; memcpy(g_ubo, col, sizeof col);
    mtl_reg_tex(5, &g_tx, NULL); samp_par sp = {NVN_MIN_FILTER_NEAREST, NVN_MAG_FILTER_NEAREST, NVN_WRAP_MODE_CLAMP_TO_EDGE, NVN_WRAP_MODE_CLAMP_TO_EDGE,
                                                NVN_WRAP_MODE_CLAMP_TO_EDGE, 0, 0, 0, 0, 1000, 0, 1, {0, 0, 0, 0}}; mtl_reg_samp(2, &sp);
    memset(&g_prog, 0, sizeof g_prog); g_prog.nstage = 2; g_prog.kstage[0] = 0; g_prog.kstage[1] = 1;
    memcpy(g_prog.key[0], "\x74\x65\x73\x74\x00\x00\x00\x01", 8); memcpy(g_prog.key[1], "\x74\x65\x73\x74\x00\x00\x00\x02", 8);
    atomic_store(&g_cur_frame, 7); f = run(rec_draw, NULL); mtl_wait(f, UINT64_MAX); double gms = mtl_gpu_ms(7); atomic_store(&g_cur_frame, 0);
    CHECK(gms > 0 && gms < 100, "the GPU time of the frame the draw's submission was committed in, from its timestamps: %.3f ms", gms);
    uint8_t px[64 * 64 * 4]; memset(px, 0x55, sizeof px); NVNcopyRegion all = {0, 0, 0, 64, 64, 1}; mtl_read_texels(&g_rt, 0, 0, &all, px, 256, 0);
    const uint8_t *top = px + (16 * 64 + 32) * 4, *bot = px + (48 * 64 + 32) * 4;
    CHECK(top[3] == 255, "a quad of NVN's counter-clockwise winding is drawn with back faces culled (alpha %u)", top[3]);
    CHECK(top[3] == 255 && bot[0] == 0 && bot[1] == 0 && bot[2] == 0 && bot[3] == 0, "NDC's upper half lands in the target's top rows, the rest keeps the pass's clear (bottom %u %u %u %u)",
          bot[0], bot[1], bot[2], bot[3]);
    CHECK(top[0] == 0 && top[1] == 255 && (top[2] == 127 || top[2] == 128) && top[3] == 255,
          "the pixel shader's uniform slot 0 and texture slot 3 (pool ID 5, sampler ID 2) are found through its moved bindings: (%u, %u, %u, %u)", top[0], top[1], top[2], top[3]);
    // a screenshot of the presented target, as a PNG
    unlink("t_shot.png"); dm_gfx_shot_at(0, "t_shot.png"); const int crop[4] = {0, 0, 64, 64}; mtl_present(&g_rt, 1.0 / 60, crop);
    uint32_t sw = 0, shh = 0; uint8_t *sp_rgb = NULL; int got = png_read("t_shot.png", &sw, &shh, &sp_rgb);
    const uint8_t *st_ = got ? sp_rgb + (16 * 64 + 32) * 3 : NULL, *sb_ = got ? sp_rgb + (48 * 64 + 32) * 3 : NULL;
    CHECK(got && sw == 64 && shh == 64 && st_[0] == 0 && st_[1] == 255 && (st_[2] == 127 || st_[2] == 128) && !sb_[0] && !sb_[1] && !sb_[2],
          "a screenshot of the presented texture (no window): a 64 x 64 PNG, the drawn upper half and the cleared lower half as drawn (%u %u %u / %u %u %u)",
          got ? st_[0] : 0, got ? st_[1] : 0, got ? st_[2] : 0, got ? sb_[0] : 0, got ? sb_[1] : 0, got ? sb_[2] : 0);
    free(sp_rgb); unlink("t_shot.png");
    // indexed quads over the whole target; the second program with a set of its own, a fresh one at each draw
    uint8_t *rt2_mem = P + 0x3C0000, *rt3_mem = P + 0x3D0000, *rt4_mem = P + 0x3E0000; g_vb2 = P + 0x3F0000; g_ib = P + 0x3F0100; g_ubo2 = P + 0x3F0200;
    g_rt2 = tex2d(rt2_mem, 64, 64, NVN_FORMAT_RGBA8); g_rt3 = tex2d(rt3_mem, 64, 64, NVN_FORMAT_RGBA8); g_rt4 = tex2d(rt4_mem, 64, 64, NVN_FORMAT_RGBA8);
    const float full[16] = {-1, -1, 0, 1, 1, -1, 0, 1, 1, 1, 0, 1, -1, 1, 0, 1}; memcpy(g_vb2, full, sizeof full);
    const uint16_t ix[4] = {0, 1, 2, 3}; memcpy(g_ib, ix, sizeof ix);
    const float col2[4] = {1, 0, 1, 1}; memcpy(g_ubo2, col2, sizeof col2);
    g_prog3 = g_prog; memcpy(g_prog3.key[1], "\x74\x65\x73\x74\x00\x00\x00\x03", 8);
    setenv("DM_VK_PUSH", "0", 1); f = run(rec_draw2, NULL); unsetenv("DM_VK_PUSH"); mtl_wait(f, UINT64_MAX);
    uint8_t q[64 * 64 * 4], q3[64 * 64 * 4], q4[64 * 64 * 4]; memset(q, 0x55, sizeof q); memset(q3, 0x55, sizeof q3); memset(q4, 0x55, sizeof q4);
    mtl_read_texels(&g_rt2, 0, 0, &all, q, 256, 0); mtl_read_texels(&g_rt3, 0, 0, &all, q3, 256, 0); mtl_read_texels(&g_rt4, 0, 0, &all, q4, 256, 0);
    const uint8_t *tl = q + (2 * 64 + 2) * 4, *br = q + (61 * 64 + 61) * 4;
    CHECK(tl[1] == 255 && tl[3] == 255 && br[1] == 255 && br[3] == 255, "indexed quads (16-bit): both triangles of the quad drawn, the whole target (corners %u / %u)", tl[1], br[1]);
    const uint8_t *c3 = q3 + (32 * 64 + 32) * 4, *c4 = q4 + (32 * 64 + 32) * 4;
    CHECK(c3[1] == 255 && c3[3] == 255 && c4[1] == 0 && c4[2] == 255 && c4[3] == 255,
          "a program with a set of its own (DM_VK_PUSH=0): each draw its own uniform, (%u, %u, %u) then (%u, %u, %u)", c3[0], c3[1], c3[2], c4[0], c4[1], c4[2]);
    // the same-target continuation
    g_rtA = tex2d(P + 0x370000, 64, 64, NVN_FORMAT_RGBA8); mtl_reg_tex(9, &g_rtA, NULL);
    f = run(rec_cont, NULL); mtl_wait(f, UINT64_MAX);
    CHECK(g_cont_passes == 2, "a continued pass goes on through a draw sampling another texture, and ends before one sampling its own attachment (%llu passes)",
          (unsigned long long)g_cont_passes);
    CHECK(g_cont_mark == 0, "the pass's end clears the core's continuation mark");
    // barriers in a pass
    g_rtB = tex2d(P + 0x320000, 64, 64, NVN_FORMAT_RGBA8); mtl_reg_tex(10, &g_rtB, NULL); g_sb4 = P + 0x330000; memset(g_sb4, 0, 256);
    g_prog4 = g_prog; memcpy(g_prog4.key[1], "\x74\x65\x73\x74\x00\x00\x00\x04", 8);
    vk_ptime_row pr[8]; vk_ptime_take(pr, 8, NULL);   // (from here)
    f = run(rec_bar, NULL); mtl_wait(f, UINT64_MAX);
    uint64_t un = 0; int npr = vk_ptime_take(pr, 8, &un);
    CHECK(g_bar_passes == 2 && g_bar_pe[VK_PE_SAMPLES] == 1 && g_bar_pe[VK_PE_BARRIER] == 1,
          "barriers in a pass: it goes on through one, ends before a draw after it sampling its attachment (%llu) and at one after storage written (%llu; %llu passes)",
          (unsigned long long)g_bar_pe[VK_PE_SAMPLES], (unsigned long long)g_bar_pe[VK_PE_BARRIER], (unsigned long long)g_bar_passes);
    CHECK(*(uint32_t *)g_sb4 == 1, "a pixel shader's storage slot written: %u", *(uint32_t *)g_sb4);
    CHECK(npr == 1 && pr[0].k.w == 64 && pr[0].k.h == 64 && pr[0].k.cfmt == VK_FORMAT_R8G8B8A8_UNORM && !pr[0].k.dfmt && pr[0].passes == 2 && pr[0].draws == 4 &&
          pr[0].ns > 0 && pr[0].ns < 1e9 && !un, "each pass's GPU time (DM_VK_PASS_GPU): %d target(s), 64 x 64: %llu passes, %llu draws, %.3f ms; %llu untimed",
          npr, (unsigned long long)(npr ? pr[0].passes : 0), (unsigned long long)(npr ? pr[0].draws : 0), npr ? pr[0].ns / 1e6 : 0.0, (unsigned long long)un);
    // a dispatch: storage slot 0 written, a texture through image slot 0 fetched, image slot 1 stored to
    uint8_t *img_mem = P + 0x360000, *tx7_mem = P + 0x361000; g_sb = P + 0x362000; memset(g_sb, 0, 1024);
    g_img = tex2d(img_mem, 8, 1, NVN_FORMAT_RGBA8); g_img.d.flags |= NVN_TEXTURE_FLAGS_IMAGE; g_tx7 = tex2d(tx7_mem, 4, 4, NVN_FORMAT_RGBA8);
    nvn_swizzle(&g_tx7.d, 0, 0, tl_px, 16, tx7_mem); mtl_reg_tex(7, &g_tx7, NULL); mtl_reg_tex(8, &g_img, NULL);
    memset(&g_cprog, 0, sizeof g_cprog); g_cprog.nstage = 1; g_cprog.kstage[0] = 5; memcpy(g_cprog.key[0], "\x74\x65\x73\x74\x00\x00\x00\x05", 8);
    f = run(rec_dispatch, NULL); mtl_wait(f, UINT64_MAX);
    const uint32_t *v = (const uint32_t *)g_sb; int grid_ok = 1; for (uint32_t i = 0; i < 8; i++) grid_ok &= v[i] == i + 2000 + 100000;
    CHECK(grid_ok && v[8] == 0, "a dispatch of 2 x 1 x 1 groups of 4 reads its workgroup counts from c0: words 0-7 = i + 1000 x + 100000 y (%u %u .. %u, then %u)", v[0], v[1], v[7], v[8]);
    CHECK(v[64] == 255, "a texture through an image slot (c[2]'s image table) is fetched: green %u", v[64]);
    uint8_t ip[8 * 4]; memset(ip, 0x55, sizeof ip); NVNcopyRegion row = {0, 0, 0, 8, 1, 1}; mtl_read_texels(&g_img, 0, 0, &row, ip, 32, 0);
    int img_ok = 1; for (int i = 0; i < 8; i++) img_ok &= ip[4 * i] == 16 * i && ip[4 * i + 3] == 255;
    CHECK(img_ok, "an image store (image slot 1, an IMAGE texture viewed as storage) reaches the texture: red %u %u .. %u", ip[0], ip[4], ip[28]);
    // an inline uniform update inside a pass
    g_rtU = tex2d(P + 0x350000, 64, 64, NVN_FORMAT_RGBA8); g_ubo3 = P + 0x3F0300; g_vb3 = P + 0x3F0400;
    memcpy(g_ubo3, col, sizeof col); const float lower[16] = {-1, -1, 0, 1, 1, -1, 0, 1, 1, 0, 0, 1, -1, 0, 0, 1}; memcpy(g_vb3, lower, sizeof lower);
    uint64_t e0[VK_PE_N], e1[VK_PE_N]; vk_pass_ends(e0);
    f = run(rec_upd, NULL); mtl_wait(f, UINT64_MAX); g_us.n = 0;
    vk_pass_ends(e1); for (int i = 0; i < VK_PE_N; i++) g_upd_pe[i] = e1[i] - e0[i];
    uint8_t qu[64 * 64 * 4]; memset(qu, 0x55, sizeof qu); mtl_read_texels(&g_rtU, 0, 0, &all, qu, 256, 0);
    const uint8_t *ut = qu + (16 * 64 + 32) * 4, *ub = qu + (48 * 64 + 32) * 4;
    CHECK(g_upd_passes == 1 && ut[1] == 255 && (ut[2] == 127 || ut[2] == 128) && ub[1] == 0 && ub[2] == 255,
          "an inline uniform update between two draws: the pass goes on (%llu), the draw before it reads the old bytes (%u, %u) and the one after the new (%u, %u)",
          (unsigned long long)g_upd_passes, ut[1], ut[2], ub[1], ub[2]);
    float mem[4]; memcpy(mem, g_ubo3, sizeof mem);
    CHECK(mem[0] == 1 && mem[1] == 0 && mem[2] == 1 && mem[3] == 1, "the update reaches memory when the submission ends (%g %g %g %g)", mem[0], mem[1], mem[2], mem[3]);
    uint64_t pe_n = 0; for (int i = 0; i < VK_PE_N; i++) pe_n += g_cont_pe[i] + g_upd_pe[i];
    CHECK(g_cont_pe[VK_PE_SAMPLES] == 1 && g_cont_pe[VK_PE_RT] == 1 && g_upd_pe[VK_PE_SUBMIT] == 1 && pe_n == 3,
          "why passes ended: sampling the attachment %llu and SetRenderTargets %llu (the continued pass), the submission %llu (its renamed updates pending; %llu in all)",
          (unsigned long long)g_cont_pe[VK_PE_SAMPLES], (unsigned long long)g_cont_pe[VK_PE_RT], (unsigned long long)g_upd_pe[VK_PE_SUBMIT], (unsigned long long)pe_n);
    // the pool goes: its range is the CPU's again, with the content
    uint8_t keep = P[12345]; mtl_pool_del(P);
    CHECK(P[12345] == keep && !mtl_pool_has((uint64_t)(uintptr_t)P, 16), "after the pool's removal its range holds what it held (and is in no pool)");
    VkFormat vf; VkComponentMapping cm;
    CHECK(vk_fmt(0x25, &vf, &cm) == 1 && vf == VK_FORMAT_R8G8B8A8_UNORM && vk_fmt(0x41, &vf, &cm) == 1 && vf == VK_FORMAT_BC1_RGB_UNORM_BLOCK &&
          vk_fmt(0x94, &vf, NULL) == 1 && vf == VK_FORMAT_ASTC_12x12_SRGB_BLOCK && vk_fmt(0x7E, &vf, NULL) == 1 && vf == VK_FORMAT_ASTC_8x5_UNORM_BLOCK &&
          vk_fmt(0x6D, &vf, &cm) == 1 && cm.a == VK_COMPONENT_SWIZZLE_ONE && vk_fmt(0x35, &vf, NULL) == 1 && vf == VK_FORMAT_D24_UNORM_S8_UINT && !vk_fmt(0x19, &vf, NULL),
          "formats: RGBA8, BC1 without alpha, ASTC 12x12 sRGB and 8x5, RGBX8 (a one), D24S8; RGB8 none");
    printf("t_vk_be: %d checks, %d failed\n", n, fail); return fail != 0;
}
