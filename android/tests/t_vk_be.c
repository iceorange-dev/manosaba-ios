// gfx/nvn/vk (plan item 301e1), the Vulkan backend on the phone without a window: a pool keeps what it held when it becomes device memory at its own
// address, and again when it goes; a GPU buffer copy and an inline uniform update land in the pool's memory once their submission is done; an event
// write and a timestamp land at completion and not before (the counter half at once); a buffer → texture → buffer round trip; a texture's
// block-linear storage uploaded (deswizzled) before the GPU reads it; ClearTexture; ReadTexels of a GPU-written texture; the format table.
// Draws (301e2), with t_vk.dmvk (android/tests/mk_vk_bundle.py) as the shader bundle: a quad of NVN's counter-clockwise winding drawn with back
// faces culled, NDC's upper half in the target's top rows (the Metal backend's orientation), the pixel shader's uniform slot and texture slot
// (pool IDs, its sampler from the same handle) found through its moved bindings, the pass's clear as its load action.
// -DDM_TEST_VK_EVENT_EARLY (event writes and reports landed when recorded): 2 fail; -DDM_TEST_VK_WINDING_SWAPPED (vk_draw.c's winding
// turned around): 3 fail.
#include "vk.h"
#include "../../gfx/nvn/core/state.h"
#include <android/native_window.h>
#include "../../tex/nvn_deswizzle.h"
#include "../nvn_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

uint64_t guest_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
uint64_t now_ns(void) { return guest_ns(); }   // (hle_os.c's: the pipelines' timing lines)
ANativeWindow *front_android_window(void) { return NULL; }
void dm_fault_hook_register(int (*fn)(void *addr, int is_write, void *uc)) { (void)fn; }   // (the host's crash.c; no guarded copies here)
void dm_guest_write_prep_register(void (*fn)(void *addr, size_t len)) { (void)fn; }   // (the same: nvn_dirty.c's, no pool is tracked here)
int exec_draw_dropped(void) { return 0; }   // (core/exec.c's: rendering on)
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
static uint8_t g_ub[256];
static void rec_ub(void *a) { (void)a; nvn_be_update_ub(g_ub, (uint64_t)(uintptr_t)(P + (2 << 20)), sizeof g_ub); }
static tex_t g_t, g_t2; static p_copy_b2t g_c;
static void rec_b2t(void *a) { (void)a; nvn_be_copy_b2t(&g_c); }
static void rec_t2b(void *a) { (void)a; nvn_be_copy_t2b(&g_c); }
static p_clear_tex g_clr;
static void rec_clear(void *a) { (void)a; nvn_be_clear_tex(&g_clr); }
// a draw as the replay makes one: the state set, the draw, the pass ended
static tex_t g_rt, g_tx; static prog_t g_prog; static uint8_t *g_vb, *g_ubo;
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

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
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
    for (int i = 0; i < 256; i++) g_ub[i] = (uint8_t)(255 - i);
    f = run(rec_ub, NULL); memset(g_ub, 0, sizeof g_ub); mtl_wait(f, UINT64_MAX);   // (the game may reuse its bytes once recorded)
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
    uint8_t tl[4 * 4 * 4]; for (int i = 0; i < 16; i++) { tl[4 * i] = 0; tl[4 * i + 1] = 255; tl[4 * i + 2] = 255; tl[4 * i + 3] = 255; }
    nvn_swizzle(&g_tx.d, 0, 0, tl, 16, tx_mem);
    const float quad[16] = {-1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, -1, 1, 0, 1}; memcpy(g_vb, quad, sizeof quad);
    const float col[4] = {1, 1, 0.5f, 1}; memcpy(g_ubo, col, sizeof col);
    mtl_reg_tex(5, &g_tx, NULL); samp_par sp = {NVN_MIN_FILTER_NEAREST, NVN_MAG_FILTER_NEAREST, NVN_WRAP_MODE_CLAMP_TO_EDGE, NVN_WRAP_MODE_CLAMP_TO_EDGE,
                                                NVN_WRAP_MODE_CLAMP_TO_EDGE, 0, 0, 0, 0, 1000, 0, 1, {0, 0, 0, 0}}; mtl_reg_samp(2, &sp);
    memset(&g_prog, 0, sizeof g_prog); g_prog.nstage = 2; g_prog.kstage[0] = 0; g_prog.kstage[1] = 1;
    memcpy(g_prog.key[0], "\x74\x65\x73\x74\x00\x00\x00\x01", 8); memcpy(g_prog.key[1], "\x74\x65\x73\x74\x00\x00\x00\x02", 8);
    f = run(rec_draw, NULL); mtl_wait(f, UINT64_MAX);
    uint8_t px[64 * 64 * 4]; memset(px, 0x55, sizeof px); NVNcopyRegion all = {0, 0, 0, 64, 64, 1}; mtl_read_texels(&g_rt, 0, 0, &all, px, 256, 0);
    const uint8_t *top = px + (16 * 64 + 32) * 4, *bot = px + (48 * 64 + 32) * 4;
    CHECK(top[3] == 255, "a quad of NVN's counter-clockwise winding is drawn with back faces culled (alpha %u)", top[3]);
    CHECK(top[3] == 255 && bot[0] == 0 && bot[1] == 0 && bot[2] == 0 && bot[3] == 0, "NDC's upper half lands in the target's top rows, the rest keeps the pass's clear (bottom %u %u %u %u)",
          bot[0], bot[1], bot[2], bot[3]);
    CHECK(top[0] == 0 && top[1] == 255 && (top[2] == 127 || top[2] == 128) && top[3] == 255,
          "the pixel shader's uniform slot 0 and texture slot 3 (pool ID 5, sampler ID 2) are found through its moved bindings: (%u, %u, %u, %u)", top[0], top[1], top[2], top[3]);
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
