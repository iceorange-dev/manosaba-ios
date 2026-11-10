// gfx/tests/core_fakes.c: the backend the tests of gfx/nvn/core link against (plan item 264): every hook the core asks for, as a weak default a
// test overrides by defining its own. The defaults: no memory pool holds a range, the arena hands out nothing, parallel work runs in order, no fence
// has completed, no program is known (every bound texture counts as read), a handle is its texture serial and has no view, the execution
// interface does nothing (no pass open, the submission's fence 0); the host's module table, empty, for host/runtime/crash.c, and its
// present-link switch
#include "../nvn/core/defer.h"
#include "../nvn/core/fuse.h"
#include "../nvn/core/pass.h"
#include "../nvn/core/store_index.h"
#include "../nvn/core/usnap.h"
#include "../nvn/nvn_backend.h"
#include "../../host/dm.h"
#define W __attribute__((weak))

W module_t g_mod[MAX_MOD];
W int g_nmod;
W module_t *module_of(const void *p) { (void)p; return NULL; }
W int g_dm_present_link;   // the host's switch DM_NVN_UB's table registers (core/frame.c)
// what the core's modules ask for (their headers' "The backend provides")
W int nvn_be_pool_span(uint64_t lo, uint64_t n) { (void)lo; (void)n; return 0; }
W void nvn_be_arena(size_t n, uint8_t **cpu, uint64_t *gpu, void **buf) { (void)n; *cpu = NULL; *gpu = 0; *buf = NULL; }
W void nvn_be_parallel(int n, void (*fn)(void *ctx, size_t i), void *ctx) { for (int i = 0; i < n; i++) fn(ctx, (size_t)i); }
W uint64_t nvn_be_done_fence(void) { return 0; }
W int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]) { (void)p; (void)rf; (void)stage; return 0; }
W void nvn_be_bind_sers(const p_bind_handles *b, int n, uint64_t *ser) { for (int k = 0; k < n; k++) ser[k] = b->h[k]; }
W uint64_t nvn_be_handle_ser(uint64_t h) { return h; }
W int nvn_be_tex_shared(const tex_t *t) { (void)t; return 0; }
W int nvn_be_handle_view(uint64_t h, uint64_t *ser, view_t *v, int *has_v) { (void)h; (void)ser; (void)v; (void)has_v; return 0; }
W void nvn_be_fuse_cover(const p_set_rt *r2, const uint8_t *first, const int8_t *p2map, const uint8_t *clr, int n1, int n) { (void)r2; (void)first; (void)p2map; (void)clr; (void)n1; (void)n; }
// the execution interface (gfx/nvn/nvn_backend.h)
W void nvn_be_reset(void) {}
W void nvn_be_state(uint32_t op, const void *a, int c) { (void)op; (void)a; (void)c; }
W void nvn_be_draw(const p_draw *dw, const p_draw_idx *di) { (void)dw; (void)di; }
W void nvn_be_dispatch(const p_dispatch *d) { (void)d; }
W void nvn_be_copy_b2b(uint64_t src, uint64_t dst, uint64_t n) { (void)src; (void)dst; (void)n; }
W void nvn_be_update_ub(const void *data, uint64_t dst, uint64_t n) { (void)data; (void)dst; (void)n; }
W void nvn_be_copy_b2t(const p_copy_b2t *c) { (void)c; }
W void nvn_be_copy_t2b(const p_copy_b2t *c) { (void)c; }
W void nvn_be_copy_t2t(const p_copy_t2t *c) { (void)c; }
W void nvn_be_clear_tex(const p_clear_tex *c) { (void)c; }
W void nvn_be_signal_event(volatile uint32_t *val, uint32_t mode, uint32_t v) { (void)val; (void)mode; (void)v; }
W uint64_t nvn_be_sub_fence(void) { return 0; }
W int nvn_be_pass_mode(void) { return -1; }
W void nvn_be_pass_flush(void) {}
W void nvn_be_pass_end(void) {}
W int nvn_be_layered(void) { return 0; }
W int nvn_be_layer_continue(const p_set_rt *r) { (void)r; return 0; }
W void nvn_be_clear_quad(const p_clear_color *c) { (void)c; }
W void nvn_be_clear_ds_quad(const p_clear_ds *c) { (void)c; }
W void nvn_be_report_counter(const uint8_t *p, const void *a) { (void)p; (void)a; }
W void nvn_be_reset_counter(const uint8_t *p, const void *a) { (void)p; (void)a; }
W void nvn_be_barrier(const uint8_t *p, const void *a) { (void)p; (void)a; }
W void nvn_be_set_pool(const uint8_t *p, const void *a) { (void)p; (void)a; }
