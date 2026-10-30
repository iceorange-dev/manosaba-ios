// gfx/nvn/vk/vk_ptime.c: each pass's GPU time (plan item 301e3's readings; DM_VK_PASS_GPU=1). A timestamp before the pass begins and one after it
// ends (each pass ends with a full barrier, so the span is that pass's: its loads, draws and stores), PQ_MAX passes a submission; at completion
// the spans are summed by the pass's targets (the first colour attachment's size and format, the depth attachment's format), and the stats line
// every 600 presents lists the costliest per present, then starts the sums again.
#include "vk.h"
#include <stdlib.h>
#include <string.h>

enum { PQ_MAX = 64, ROWS = 32 };
static int g_on = -1; static VkQueryPool g_pq; static double g_tick_ns;
static struct { int n, full; vk_ptime_key k[PQ_MAX]; uint32_t draws[PQ_MAX]; } g_slot[VK_NSLOT];   // the passes timed in each slot's submission; full: the rest not
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static vk_ptime_row g_rows[ROWS]; static int g_nrows; static uint64_t g_untimed;   // under g_mu

static int on(void) {
    if (g_on < 0) {
        const char *e = getenv("DM_VK_PASS_GPU"); g_on = e && strcmp(e, "0") && V.ts_bits && V.props.limits.timestampPeriod > 0;
        if (g_on) {
            VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 * PQ_MAX * VK_NSLOT};
            VK(vkCreateQueryPool(V.dev, &qi, NULL, &g_pq)); g_tick_ns = V.props.limits.timestampPeriod;
            VLOG("each pass's GPU time measured (DM_VK_PASS_GPU)");
        } else if (e && strcmp(e, "0")) VLOG("[red] DM_VK_PASS_GPU: the queue has no timestamps");
    }
    return g_on;
}
void vk_ptime_slot(VkCommandBuffer cb, int slot) {
    if (!on()) return;
    g_slot[slot].n = g_slot[slot].full = 0; vkCmdResetQueryPool(cb, g_pq, 2 * PQ_MAX * (uint32_t)slot, 2 * PQ_MAX);
}
void vk_ptime_begin(VkCommandBuffer cb) {
    if (!on()) return;
    int s = vk_cur_slot(), i = g_slot[s].n;
#ifdef DM_TEST_VK_PTIME_FIRST_ONLY   // negative control of t_vk_be only: only a submission's first pass timed
    if (i > 0) g_slot[s].full = 1;
#endif
    if (i >= PQ_MAX) g_slot[s].full = 1;
    if (g_slot[s].full) return;
    vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, g_pq, (uint32_t)(2 * (PQ_MAX * s + i)));
}
void vk_ptime_end(VkCommandBuffer cb, const vk_ptime_key *k, uint32_t draws) {
    if (!on()) return;
    int s = vk_cur_slot(), i = g_slot[s].n;
    if (g_slot[s].full) { pthread_mutex_lock(&g_mu); g_untimed++; pthread_mutex_unlock(&g_mu); return; }
    vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, g_pq, (uint32_t)(2 * (PQ_MAX * s + i) + 1));
    g_slot[s].k[i] = *k; g_slot[s].draws[i] = draws; g_slot[s].n = i + 1;
}
void vk_ptime_done(int slot) {   // (the completion thread, the slot's submission done)
    if (g_on <= 0) return;
    int n = g_slot[slot].n; g_slot[slot].n = 0; if (n <= 0) return;   // (an empty submission next in the slot: nothing read again)
    uint64_t t[2 * PQ_MAX];
    if (vkGetQueryPoolResults(V.dev, g_pq, 2 * PQ_MAX * (uint32_t)slot, 2 * (uint32_t)n, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) return;
    uint64_t mask = V.ts_bits >= 64 ? UINT64_MAX : (1ull << V.ts_bits) - 1;
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < n; i++) {
        const vk_ptime_key *k = &g_slot[slot].k[i]; int r = 0;
        while (r < g_nrows && memcmp(&g_rows[r].k, k, sizeof *k)) r++;
        if (r == g_nrows) { if (g_nrows == ROWS) { g_untimed++; continue; } memset(&g_rows[r], 0, sizeof g_rows[r]); g_rows[r].k = *k; g_nrows++; }
        g_rows[r].ns += (double)(((t[2 * i + 1] & mask) - (t[2 * i] & mask)) & mask) * g_tick_ns; g_rows[r].passes++; g_rows[r].draws += g_slot[slot].draws[i];
    }
    pthread_mutex_unlock(&g_mu);
}
int vk_ptime_take(vk_ptime_row *out, int max, uint64_t *untimed) {
    pthread_mutex_lock(&g_mu);
    int n = g_nrows < max ? g_nrows : max; memcpy(out, g_rows, (size_t)n * sizeof *out); if (untimed) *untimed = g_untimed;
    g_nrows = 0; g_untimed = 0;
    pthread_mutex_unlock(&g_mu);
    return n;
}
static int by_ns(const void *a, const void *b) { double x = ((const vk_ptime_row *)a)->ns, y = ((const vk_ptime_row *)b)->ns; return x < y ? 1 : x > y ? -1 : 0; }
void vk_ptime_line(uint64_t presents) {
    if (g_on <= 0) return;
    vk_ptime_row r[ROWS]; uint64_t un; int n = vk_ptime_take(r, ROWS, &un); if (!n || !presents) return;
    qsort(r, (size_t)n, sizeof r[0], by_ns);
    char b[1400]; size_t o = 0; double all = 0; for (int i = 0; i < n; i++) all += r[i].ns;
    for (int i = 0; i < n && i < 8 && o < sizeof b - 160; i++)
        o += (size_t)snprintf(b + o, sizeof b - o, "%s%ux%u fmt %d%s: %.2f ms (%.1f passes, %.1f draws each)", i ? "; " : "", r[i].k.w, r[i].k.h, r[i].k.cfmt,
                              r[i].k.dfmt ? " + depth" : "", r[i].ns / 1e6 / (double)presents, (double)r[i].passes / (double)presents,
                              (double)r[i].draws / (double)r[i].passes);
    VLOG("GPU time in passes over the last %llu presents: %.2f ms a present; by target, a present: %s%s; %llu passes untimed", (unsigned long long)presents,
         all / 1e6 / (double)presents, b, n > 8 ? "; ..." : "", (unsigned long long)un);
}
