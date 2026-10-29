// gfx/nvn/null/readings.c: the readings of the performance and memory lines a backend gives (gfx/nvn/hle/timing.inc and the replay's lines), none:
// the null backend's (plan item 301c) and the Vulkan one's until it has readings of its own (plan item 301e). Zeros, and the memory line from the
// footprint alone.
#include "../../../host/dm.h"
#include <stdint.h>
#include <string.h>

void mtl_mem_stat(unsigned long long frame, unsigned long long rss_mb, int heap_ok, unsigned long long heap_mb, unsigned long long heap_free_mb, unsigned long long heap_maxblk_kb) {
    (void)heap_ok; (void)heap_mb; (void)heap_free_mb; (void)heap_maxblk_kb;
    static unsigned long long last, at;   // as the Metal backend's line: on a change of 64 MB or every 3000 frames
    if (last && (rss_mb > last ? rss_mb - last : last - rss_mb) < 64 && frame - at < 3000) return;
    last = rss_mb; at = frame; LOG("[nvn] memory at frame %llu: footprint %llu MB (no GPU memory reading from this backend)", frame, rss_mb);
}
void mtl_stats(uint64_t *passes, uint64_t *draws) { *passes = *draws = 0; }
void mtl_args_stats(uint64_t *enc, uint64_t *reuse) { *enc = *reuse = 0; }
void mtl_use_stats(uint64_t *call, uint64_t *skip) { *call = *skip = 0; }
void mtl_alloc_stats(uint64_t *a, uint64_t *b, int *n) { *a = *b = 0; *n = 0; }
void mtl_border_census(uint64_t *a, uint64_t *b) { *a = *b = 0; }
void mtl_bw(uint64_t *load, uint64_t *store, uint64_t *full_rw_passes) { *load = *store = *full_rw_passes = 0; }
void mtl_clear_only_stats(uint64_t *n, uint64_t *bytes) { *n = *bytes = 0; }
void mtl_counter_stats(uint64_t rep[32], uint64_t rst[32]) { memset(rep, 0, 32 * sizeof *rep); memset(rst, 0, 32 * sizeof *rst); }
void mtl_counts(uint64_t out[13]) { memset(out, 0, 13 * sizeof *out); }
void mtl_create_ns(uint64_t *lib_ns, uint64_t *pso_ns) { *lib_ns = *pso_ns = 0; }
void mtl_create_stats(uint64_t *lib, uint64_t *pso) { *lib = *pso = 0; }
uint64_t mtl_drawable_ns_take(void) { return 0; }
uint64_t mtl_ds_omit(void) { return 0; }
void mtl_fbx_stats(uint64_t out[3]) { memset(out, 0, 3 * sizeof *out); }
void mtl_fbx_split_stats(uint64_t out[2]) { memset(out, 0, 2 * sizeof *out); }
void mtl_fbx_fetch_stats(uint64_t out[1]) { out[0] = 0; }
void mtl_pkg_age(uint64_t out[5]) { memset(out, 0, 5 * sizeof *out); }
uint64_t mtl_pkg_miss(void) { return 0; }
void mtl_pkg_stats(uint64_t *uploads, uint64_t *reuploads) { *uploads = *reuploads = 0; }
void mtl_pp_stats(uint64_t out[12]) { memset(out, 0, 12 * sizeof *out); }
uint64_t mtl_udc_saved_bytes(void) { return 0; }
uint64_t mtl_upload_bytes(void) { return 0; }
uint64_t mtl_upload_gpu_ns_take(void) { return 0; }
uint64_t mtl_upload_ns_take(void) { return 0; }
void mtl_vis_state(int *open, uint64_t *folded) { *open = 0; *folded = 0; }
