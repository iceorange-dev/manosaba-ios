// gfx/nvn/vk (plan item 301e1), the Vulkan backend on the phone without a window: a pool keeps what it held when it becomes device memory at its own
// address, and again when it goes; a GPU buffer copy and an inline uniform update land in the pool's memory once their submission is done; an event
// write and a timestamp land at completion and not before (the counter half at once); a buffer → texture → buffer round trip; a texture's
// block-linear storage uploaded (deswizzled) before the GPU reads it; ClearTexture; ReadTexels of a GPU-written texture; the format table.
// -DDM_TEST_VK_EVENT_EARLY (event writes and reports landed when recorded): 2 fail.
#include "vk.h"
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
ANativeWindow *front_android_window(void) { return NULL; }
void dm_fault_hook_register(int (*fn)(void *addr, int is_write, void *uc)) { (void)fn; }   // (the host's crash.c; no guarded copies here)
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
