// gfx/nvn/null/null.c: the null backend (plan item 301c), for the Android host until the Vulkan one (301e): every function the interface layer
// (hle_nvn.c) and the replay core ask of a backend, the Metal one's set (nvn_mtl.h, nvn_obj.h, nvn_backend.h and the core headers' "The backend
// provides"), with a GPU that does nothing and finishes at once. What the CPU sees of the GPU's work is kept: a submission's fence completes when it
// is committed, and its event writes and counter reports land then, in command order (as the Metal backend's completion handler does them), with
// the guest clock's ticks for timestamps and 0 samples passed. Nothing is drawn or copied: what a game reads back of the GPU's results (texels,
// buffers written by copies) stays as it was. No window and no display link: frames are paced by the interface layer's own 60 Hz.
#include "../nvn_backend.h"
#include "../nvn_mtl.h"
#include "../nvn_obj.h"
#include "../core/defer.h"
#include "../core/fuse.h"
#include "../core/pass.h"
#include "../core/store_index.h"
#include "../core/usnap.h"
#include "../../../host/dm.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

int g_dm_present_link;   // the present pacing switch (nvn_mtl.m's): 0, no display link here

// ---------- Submissions: the fence and what lands at completion ----------
static _Atomic uint64_t g_fence;   // the newest committed submission = the newest completed
typedef struct { int kind; volatile uint32_t *val; uint32_t mode, v; uint64_t addr; uint32_t type; } post_op;   // kind 1 event, 2 timestamp, 3 samples passed
static post_op *g_post; static int g_npost, g_postcap;   // this submission's (replay and commit run on one thread at a time, as on Metal)
static void land(const post_op *o);
// DM_TEST_NULL_EARLY (negative control of gfx/tests/t_null_be.c only): each lands when the replay reaches it, before its submission is committed
static void post(post_op o) {
#ifdef DM_TEST_NULL_EARLY
    land(&o); return;
#endif
    if (g_npost == g_postcap) { g_postcap = g_postcap ? 2 * g_postcap : 64; g_post = realloc(g_post, (size_t)g_postcap * sizeof *g_post); if (!g_post) die("null backend: posts"); }
    g_post[g_npost++] = o;
}
static uint64_t gpu_ticks(void) { return guest_ns() * 12 / 625; }   // the guest's 19.2 MHz counter
void mtl_sub_begin(void) {}
static void land(const post_op *o) {
    if (o->kind == 1) { if (o->mode == 1) *o->val += o->v; else *o->val = o->v; }
    else if (o->kind == 3) { uint64_t rec[2] = {0, gpu_ticks()}; memcpy((void *)(uintptr_t)o->addr, rec, sizeof rec); }
    else { uint64_t t = gpu_ticks(); memcpy((uint8_t *)(uintptr_t)o->addr + 8, &t, 8); }
}
uint64_t mtl_sub_commit(void) {
    for (int i = 0; i < g_npost; i++) land(&g_post[i]);
    g_npost = 0;
    return atomic_fetch_add(&g_fence, 1) + 1;
}
uint64_t nvn_be_sub_fence(void) { return atomic_load(&g_fence) + 1; }   // this submission commits with the next fence
uint64_t mtl_done_fence(void) { return atomic_load(&g_fence); }
uint64_t mtl_last_fence(void) { return atomic_load(&g_fence); }
uint64_t nvn_be_done_fence(void) { return atomic_load(&g_fence); }
int mtl_wait(uint64_t fence, uint64_t timeout_ns) { (void)timeout_ns; return atomic_load(&g_fence) >= fence; }   // a committed one is done; a later one is not yet
void mtl_finish(void) {}
void mtl_drain(void) {}
void mtl_wb_fence_done(void) {}
void nvn_be_signal_event(volatile uint32_t *val, uint32_t mode, uint32_t v) { post((post_op){1, val, mode, v, 0, 0}); }
// A report: samples passed, or the counter half (0) now and the timestamp half at completion, as the Metal backend writes them
void nvn_be_report_counter(const uint8_t *p, const void *a) {
    (void)p; uint64_t t, ad; memcpy(&t, a, 8); memcpy(&ad, (const uint8_t *)a + 8, 8);
    if (!ad) return;
    if (t == NVN_COUNTER_TYPE_SAMPLES_PASSED) { post((post_op){3, NULL, 0, 0, ad, (uint32_t)t}); return; }
    uint64_t z = 0; memcpy((void *)(uintptr_t)ad, &z, sizeof z); post((post_op){2, NULL, 0, 0, ad, (uint32_t)t});
}
void nvn_be_reset_counter(const uint8_t *p, const void *a) { (void)p; (void)a; }

// ---------- Memory pools and the arena ----------
// The registered pools, for nvn_be_pool_span / mtl_pool_has: whether a range lies in one (the core keeps snapshots of what does)
typedef struct { uint64_t lo, hi; } span_t;
static span_t *g_pools; static int g_npools, g_poolcap; static pthread_mutex_t g_pool_mu = PTHREAD_MUTEX_INITIALIZER;
void mtl_pool_add(const void *mem, uint64_t size) {
    pthread_mutex_lock(&g_pool_mu);
    if (g_npools == g_poolcap) { g_poolcap = g_poolcap ? 2 * g_poolcap : 64; g_pools = realloc(g_pools, (size_t)g_poolcap * sizeof *g_pools); if (!g_pools) die("null backend: pools"); }
    g_pools[g_npools++] = (span_t){(uint64_t)(uintptr_t)mem, (uint64_t)(uintptr_t)mem + size};
    pthread_mutex_unlock(&g_pool_mu);
}
void mtl_pool_del(const void *mem) {
    pthread_mutex_lock(&g_pool_mu);
    for (int i = 0; i < g_npools; i++) if (g_pools[i].lo == (uint64_t)(uintptr_t)mem) { g_pools[i] = g_pools[--g_npools]; break; }
    pthread_mutex_unlock(&g_pool_mu);
}
int mtl_pool_has(uint64_t addr, uint64_t len) {
    int r = 0; pthread_mutex_lock(&g_pool_mu);
    for (int i = 0; i < g_npools && !r; i++) r = addr >= g_pools[i].lo && addr + len <= g_pools[i].hi;
    pthread_mutex_unlock(&g_pool_mu); return r;
}
int nvn_be_pool_span(uint64_t lo, uint64_t n) { return mtl_pool_has(lo, n); }
void mtl_pool_invalidate(const pool_t *p, uint64_t off, uint64_t n) { (void)p; (void)off; (void)n; }
// The arena the core copies uniform snapshots into: a ring of 64 MB the GPU never reads, so wrapping over old snapshots is harmless (the CPU only reads
// back the current submission's)
enum { ARENA = 64u << 20 };
void nvn_be_arena(size_t n, uint8_t **cpu, uint64_t *gpu, void **buf) {
    static uint8_t *ring; static size_t at;
    if (!ring && !(ring = malloc(ARENA))) die("null backend: arena");
    n = (n + 255) & ~(size_t)255;
    if (n > ARENA) die("null backend: an arena allocation of %zu bytes", n);
    if (at + n > ARENA) at = 0;
    *cpu = ring + at; *gpu = (uint64_t)(uintptr_t)(ring + at); *buf = NULL; at += n;
}

// ---------- What the core asks besides (store_index.h, pass.h, defer.h, fuse.h, usnap.h) ----------
void nvn_be_parallel(int n, void (*fn)(void *ctx, size_t i), void *ctx) { for (int i = 0; i < n; i++) fn(ctx, (size_t)i); }
int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]) { (void)p; (void)rf; (void)stage; return 0; }   // no program is known
void nvn_be_bind_sers(const p_bind_handles *b, int n, uint64_t *ser) { for (int k = 0; k < n; k++) ser[k] = b->h[k]; }
uint64_t nvn_be_handle_ser(uint64_t h) { return h; }
int nvn_be_handle_view(uint64_t h, uint64_t *ser, view_t *v, int *has_v) { (void)h; (void)ser; (void)v; (void)has_v; return 0; }
void nvn_be_fuse_cover(const p_set_rt *r2, const uint8_t *first, const int8_t *p2map, const uint8_t *clr, int n1, int n) { (void)r2; (void)first; (void)p2map; (void)clr; (void)n1; (void)n; }

// ---------- The execution interface (nvn_backend.h): nothing runs ----------
void nvn_be_reset(void) { g_npost = 0; }
void nvn_be_state(uint32_t op, const void *a, int c) { (void)op; (void)a; (void)c; }
void nvn_be_draw(const p_draw *dw, const p_draw_idx *di) { (void)dw; (void)di; }
void nvn_be_dispatch(const p_dispatch *d) { (void)d; }
void nvn_be_copy_b2b(uint64_t src, uint64_t dst, uint64_t n) { (void)src; (void)dst; (void)n; }
void nvn_be_update_ub(const void *data, uint64_t dst, uint64_t n) { (void)data; (void)dst; (void)n; }
void nvn_be_copy_b2t(const p_copy_b2t *c) { (void)c; }
void nvn_be_copy_t2b(const p_copy_b2t *c) { (void)c; }
void nvn_be_copy_t2t(const p_copy_t2t *c) { (void)c; }
void nvn_be_clear_tex(const p_clear_tex *c) { (void)c; }
int nvn_be_pass_mode(void) { return -1; }
void nvn_be_pass_flush(void) {}
void nvn_be_pass_end(void) {}
int nvn_be_layered(void) { return 0; }
int nvn_be_layer_continue(const p_set_rt *r) { (void)r; return 0; }
void nvn_be_clear_quad(const p_clear_color *c) { (void)c; }
void nvn_be_clear_ds_quad(const p_clear_ds *c) { (void)c; }
void nvn_be_barrier(const uint8_t *p, const void *a) { (void)p; (void)a; }
void nvn_be_set_pool(const uint8_t *p, const void *a) { (void)p; (void)a; }

// ---------- Objects, textures and presenting ----------
void mtl_reg_tex(int id, const tex_t *t, const view_t *v) { (void)id; (void)t; (void)v; }
void mtl_reg_samp(int id, const samp_par *p) { (void)id; (void)p; }
void mtl_tex_release(tex_t *t) { (void)t; }
void mtl_prog_release(prog_t *p) { (void)p; }
void mtl_prog_fkeys(uint64_t serial, const uint8_t fk[6][8]) { (void)serial; (void)fk; }
void mtl_sub_index(const uint64_t *h, int n) { (void)h; (void)n; }
void mtl_set_tex_pool_n(int n) { (void)n; }
void mtl_set_c2_layout(int tex, int ntex, int samp, int nsamp) { (void)tex; (void)ntex; (void)samp; (void)nsamp; }
void mtl_set_depth_mode(int m) { (void)m; }
void mtl_set_upscale_pass(const char *fs, int color, const char *compose_fs, int compose_color) { (void)fs; (void)color; (void)compose_fs; (void)compose_color; }
void mtl_set_frame(uint64_t f) { (void)f; }
void mtl_ts_factor(double f) { (void)f; }
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *dst, size_t row, size_t img) {
    (void)t; (void)level; (void)layer0; (void)r; (void)dst; (void)row; (void)img;
    static int said; if (!said++) LOG("[red] null backend: a CPU read of texels (ReadTexels) gets nothing; the buffer keeps what it held");
}
void mtl_write_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, const uint8_t *src, size_t row, size_t img) {
    (void)t; (void)level; (void)layer0; (void)r; (void)src; (void)row; (void)img;
}
void mtl_flush_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r) { (void)t; (void)level; (void)layer0; (void)r; }
uint64_t mtl_present(const tex_t *t, double period_s, const int crop[4]) { (void)t; (void)period_s; (void)crop; return 0; }   // ticket 0: no pending present
void mtl_present_wait(uint64_t ticket) { (void)ticket; }
double mtl_presented_time(uint64_t n) { (void)n; return 0; }
uint32_t mtl_present_qos(void) { return 0; }
int mtl_has_window(void) { return 0; }
int mtl_display_hz(void) { return 0; }   // no screen (pacing then keeps the game's present interval, as the Mac host without a window)
int mtl_fps_target(void) { const char *e = getenv("DM_FPS"); return e && atoi(e) == 30 ? 30 : 60; }   // DM_FPS=30 as on Metal
int mtl_link_active(void) { return 0; }
int mtl_link_wait_tick(uint64_t timeout_ns) { (void)timeout_ns; return 0; }
void mtl_link_set_fifo(int on) { (void)on; }
void mtl_link_set_interval(int n) { (void)n; }
void mtl_link_set_rate(int hz) { (void)hz; }
int mtl_warm_wait(int timeout_ms) { (void)timeout_ms; return 0; }   // no pipelines to warm
void dm_gfx_shot_at(long k, const char *path) { (void)k; (void)path; LOG("[red] a screenshot was asked for: the null backend draws nothing"); }
void mtl_texdump_next(const char *path) { (void)path; }

// The performance and memory lines' readings: gfx/nvn/null/readings.c (none here; the Vulkan backend's too until it has its own)
