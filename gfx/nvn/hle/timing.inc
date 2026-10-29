// gfx/nvn/hle/timing.inc, part of hle_nvn.c (included there in order, one translation unit): Timing instrumentation: DM_NVN_TSV, the fps / GPU line, thermal attribution, the summaries every 300 frames
// ---------- Timing instrumentation (per-frame rows written to DM_NVN_TSV: frame / @metric / value; every 300 frames a summary of p50 / p95 / max) ----------
#define TWIN 300
static double g_tw[6][TWIN]; static unsigned g_tn[6];
static double g_pres_iv[TWIN]; static unsigned g_presn;   // Present interval (ms; -1 = dropped by the system, 0 = no reading)
// Each frame's hold (plan item 156, nvn_pace.h nvn_hold_ms): g_pres_exp what the on-screen interval beside it is judged against; g_hold_ring the hold
// each present was given, by present number (the on-screen interval is read 8 presents late); g_tw0_hold the hold beside each frame interval
static double g_pres_exp[TWIN], g_pres_exp_last, g_tw0_hold[TWIN], g_hold_ring[16], g_hold_now = 1000.0 / 60;
__attribute__((weak)) double dm_brightness_now(void) { return -1; }
__attribute__((weak)) void dm_yield_top(char *out, size_t n) { if (n) snprintf(out, n, "none"); }   // hle_thread.c's when linked (not in the graphics unit tests)
__attribute__((weak)) void dm_thread_cpu(char *out, size_t n, double secs) { (void)secs; if (n) snprintf(out, n, "-"); }   // hle_thread.c: guest-thread CPU by Switch priority; none in graphics unit tests
__attribute__((weak)) void dm_perf_present(double iv_ms) { (void)iv_ms; }   // Default (unit tests); strong definition in the host's diag.c: always-on performance record perf.txt
uint64_t dm_nvn_sub_cpu_ns(void) { return atomic_load(&g_cum_sub_cpu_ns); }   // Cumulative CPU nanoseconds of submit (excluding present)
__attribute__((weak)) int dm_thermal_now(void) { return -1; }   // default (the graphics unit tests); the host's thermal.m overrides it (plan item 193)
// Per-frame ring for the long-frame log (GPU time only arrives 8 frames late, so store first and judge later)
#define LFN 16
typedef struct { uint64_t f; double fr, nv, t_s, make_ms, up_mb, up_ms; unsigned pso, lib, up, rup; int ls; } lf_rec;   // ls: whether the loading screen was covering the picture when this frame was presented
static lf_rec g_lf[LFN];
__attribute__((weak)) void dm_longframe_note(const char *what) { LOG("[long frame] %s", what); }   // Default (graphics unit tests); the strong definition in the host's diag.c adds scene context and writes perf.txt
extern void mtl_create_ns(uint64_t *lib_ns, uint64_t *pso_ns); extern uint64_t mtl_upload_bytes(void); extern void mtl_pkg_stats(uint64_t *uploads, uint64_t *reuploads);
static const char *const g_tname[6] = {"frame time", "frame work", "NVN submit+present", "GPU", "translation record phase", "translation submit phase"};
static void tpush(int k, double v) { g_tw[k][g_tn[k]++ % TWIN] = v; }
// The fps / GPU line (nvn_perfline.h): fed from the present below, read by any thread (the iOS overlay, the Mac window title, the diagnostics export)
static perfline g_perf;
int dm_perf_line(char *out, size_t n) { return perfline_fmt(&g_perf, now_ns(), out, n); }
// Thermal attribution (iOS DM_HEAT): per-frame GPU milliseconds are kept in a separate ring, taking the mean / p95 "since the last read" (at most the last HRN frames), without interfering with the timing summary's 300-frame window
#define HRN 1024
static double g_hr[HRN]; static uint64_t g_hn, g_hn_read;
static int dcmp(const void *a, const void *b);
void nvn_gpu_recent(double *avg, double *p95, unsigned *n) {
    uint64_t hi = g_hn, lo = g_hn_read > hi ? hi : g_hn_read; if (hi - lo > HRN) lo = hi - HRN; g_hn_read = hi;
    unsigned c = (unsigned)(hi - lo); static double v[HRN]; double s = 0;
    for (unsigned i = 0; i < c; i++) { v[i] = g_hr[(lo + i) % HRN]; s += v[i]; }
    qsort(v, c, sizeof *v, dcmp); *n = c; *avg = c ? s / c : -1; *p95 = c ? v[(c * 95) / 100 < c ? (c * 95) / 100 : c - 1] : -1;
}
static int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
// Power over the window since the last summary, for the whole process (nxnative's power line, MIT): CPU energy (task_energy, nJ on Apple silicon)
// in watts and per frame, CPU time (100 = one core) and its share on performance cores, wakeups per second, and the guest threads that yielded most;
// then render-target traffic per frame (attachment bytes loaded and stored, passes loading and storing everything, stores DM_NVN_UNWRITTEN_DC saved)
// and guest-thread CPU by Switch priority (nxnative 451e118); samples-passed reports and resets a frame, the slots a report adds up (since the last
// reset) and those folded when their visibility buffer was used again (nvn_mtl.m vis_recycle)
void mtl_counter_stats(uint64_t rep[32], uint64_t rst[32]);
void mtl_vis_state(int *open, uint64_t *folded);
void mtl_counts(uint64_t out[13]);
void mtl_pkg_age(uint64_t out[5]);
void mtl_fbx_stats(uint64_t out[3]); void mtl_fbx_split_stats(uint64_t out[2]); void mtl_fbx_fetch_stats(uint64_t out[1]);
void mtl_pp_stats(uint64_t out[12]);
__attribute__((weak)) void dm_gate_counts(uint64_t out[4]) { memset(out, 0, 4 * sizeof *out); }   // host/hle/hle.c; the graphics unit tests link without it
static void power_line(uint64_t frame) {
    static uint64_t pt, pe, pcpu, pp, piw, pf, pld, pst, pfu, pudc, prep, prst, pfold, pc[13], pg[4], pfx[3], pff[1], psp[2], psw[3], ppr[12];
    static int counts = -1; if (counts < 0) counts = getenv("DM_NVN_COUNTS") && strcmp(getenv("DM_NVN_COUNTS"), "0");
    dm_power pw; int have_pw = dm_power_now(&pw);   // (plat.h: none on Android yet, the other lines still come)
    uint64_t t = now_ns(), cpu = pw.cpu_ns, p = pw.pcpu_ns, e = pw.energy_nj, iw = pw.wakeups;
    char yt[192]; dm_yield_top(yt, sizeof yt);
    if (pt && t > pt) { double s = (t - pt) / 1e9, nf = frame > pf ? (double)(frame - pf) : 1;
        if (have_pw) LOG("[nvn] power up to frame %llu: CPU energy %.2f W, %.1f mJ a frame (%.1f frames/s); CPU %.0f%% (100 = one core), %.0f%% of it on performance cores; %.0f wakeups/s; most yields: %s",
            frame, (e - pe) / 1e9 / s, (e - pe) / 1e6 / nf, nf / s, (cpu - pcpu) / 1e7 / s, cpu > pcpu ? 100.0 * (double)(p - pp) / (double)(cpu - pcpu) : 0, (iw - piw) / s, yt);
        uint64_t ld, st, fu, udc = mtl_udc_saved_bytes(), con, cob; mtl_bw(&ld, &st, &fu); mtl_clear_only_stats(&con, &cob); static uint64_t pcon, pcob;
        LOG("[nvn] render targets up to frame %llu: %.1f MB loaded / %.1f MB stored a frame, %.1f passes a frame load and store everything, %.1f MB a frame of stores saved (DM_NVN_UNWRITTEN_DC); "
            "%.1f passes a frame only land clears, storing %.1f MB",
            frame, (ld - pld) / 1048576.0 / nf, (st - pst) / 1048576.0 / nf, (fu - pfu) / nf, (udc - pudc) / 1048576.0 / nf, (con - pcon) / nf, (cob - pcob) / 1048576.0 / nf); pcon = con; pcob = cob;
        char tc[400]; dm_thread_cpu(tc, sizeof tc, s);
        LOG("[nvn] threads up to frame %llu: guest CPU %s", frame, tc);
        uint64_t rep[32], rst[32], fold; int open; mtl_counter_stats(rep, rst); mtl_vis_state(&open, &fold);
        if (rep[NVN_COUNTER_TYPE_SAMPLES_PASSED] > prep || rst[NVN_COUNTER_TYPE_SAMPLES_PASSED] > prst)
            LOG("[nvn] occlusion up to frame %llu: %.1f reports / %.1f resets a frame, %d slots in the open count, %.1f folded a frame", frame,
                (rep[NVN_COUNTER_TYPE_SAMPLES_PASSED] - prep) / nf, (rst[NVN_COUNTER_TYPE_SAMPLES_PASSED] - prst) / nf, open, (fold - pfold) / nf);
        { uint64_t fx[3], ff[1]; mtl_fbx_stats(fx); mtl_fbx_fetch_stats(ff);
          if (fx[0] > pfx[0] || fx[2] > pfx[2]) LOG("[nvn] attachment copies up to frame %llu: %.1f draws a frame sample what their pass draws, %.1f copies, %.1f read from the tile at their pixel, %.1f not copied (merged, layered or range pass)",
                                                   frame, (fx[0] - pfx[0]) / nf, (fx[1] - pfx[1]) / nf, (ff[0] - pff[0]) / nf, (fx[2] - pfx[2]) / nf);
          uint64_t sp[2]; mtl_fbx_split_stats(sp);
          if (sp[0] > psp[0] || sp[1] > psp[1]) LOG("[nvn] pass splits up to frame %llu: %.1f a frame before draws that sample what their pass wrote, %.1f pending clears flushed (DM_NVN_FBX=split)",
                                                   frame, (sp[0] - psp[0]) / nf, (sp[1] - psp[1]) / nf); }
        { uint64_t sw[3]; void nvn_syncwait_stats(uint64_t out[3]); nvn_syncwait_stats(sw);
          if (sw[0] > psw[0]) LOG("[nvn] sync waits up to frame %llu: %.2f a frame, %.2f of them on the newest submission still on the GPU, %.2f ms a frame waiting (plan item 177)",
                                  frame, (sw[0] - psw[0]) / nf, (sw[1] - psw[1]) / nf, (sw[2] - psw[2]) / 1e6 / nf); }
        { uint64_t pr[12]; mtl_pp_stats(pr);   // the pipeline prediction (nvn_mtl.m pso_predict_rest) over the window, when it scanned
          if (pr[4] > ppr[4]) LOG("[nvn] pipeline prediction up to frame %llu: %llu scans (ended by commands %llu / keys %llu / time %llu), %llu pipelines kicked, %llu taken by draws, "
                                  "%llu freed untaken, %llu refused with the table full, %llu in the table; the submit thread waited %.1f ms on %llu and compiled %llu itself", frame, pr[4] - ppr[4], pr[5] - ppr[5], pr[6] - ppr[6], pr[7] - ppr[7],
                                  pr[0] - ppr[0], pr[1] - ppr[1], pr[2] - ppr[2], pr[3] - ppr[3], pr[8], (pr[9] - ppr[9]) / 1e6, pr[10] - ppr[10], pr[11] - ppr[11]); }
        if (counts) { uint64_t c[13], g[4]; mtl_counts(c); dm_gate_counts(g);   // DM_NVN_COUNTS (plan items 42-45)
            LOG("[nvn] counts up to frame %llu, a frame: %.1f draws sample the levels and layers their own pass draws (%.1f after other draws of the Metal pass, %.1f of them after a barrier); "
                "%.1f timestamp reports, %.2f submissions with two or more; %.1f ClearTexture; %.1f render passes without a draw; a second: %.0f counter reads, %.0f svcs",
                frame, (c[0] - pc[0]) / nf, (c[1] - pc[1]) / nf, (c[2] - pc[2]) / nf, (c[3] - pc[3]) / nf, (c[4] - pc[4]) / nf, (c[5] - pc[5]) / nf, (c[6] - pc[6]) / nf,
                (g[1] + g[2] - pg[1] - pg[2]) / s, (g[0] - pg[0]) / s);
            LOG("[nvn] counts up to frame %llu, a frame: %.1f passes with draws, %.1f of them draw only a top-left part (%.1f more with a clear on load), %.2f%% (%.2f%%) of all their pixels outside it",
                frame, (c[7] - pc[7]) / nf, (c[8] - pc[8]) / nf, (c[11] - pc[11]) / nf, c[9] > pc[9] ? 100.0 * (double)(c[10] - pc[10]) / (double)(c[9] - pc[9]) : 0,
                c[9] > pc[9] ? 100.0 * (double)(c[12] - pc[12]) / (double)(c[9] - pc[9]) : 0);
            uint64_t ag[5]; mtl_pkg_age(ag);
            LOG("[nvn] counts up to frame %llu: %llu CPU-filled textures, %.1f MB used in the last 30 submissions, %.1f MB 31-300 ago, %.1f MB 301-1800 ago, %.1f MB longer",
                frame, (unsigned long long)ag[4], ag[0] / 1048576.0, ag[1] / 1048576.0, ag[2] / 1048576.0, ag[3] / 1048576.0); } }
    else { char tc[400]; dm_thread_cpu(tc, sizeof tc, 1); }   // the first call only takes the baselines
    pt = t; pe = e; pcpu = cpu; pp = p; piw = iw; pf = frame; mtl_bw(&pld, &pst, &pfu); pudc = mtl_udc_saved_bytes();
    { uint64_t rep[32], rst[32]; int open; mtl_counter_stats(rep, rst); mtl_vis_state(&open, &pfold); prep = rep[NVN_COUNTER_TYPE_SAMPLES_PASSED]; prst = rst[NVN_COUNTER_TYPE_SAMPLES_PASSED]; }
    if (counts) { mtl_counts(pc); dm_gate_counts(pg); }
    mtl_pp_stats(ppr);
    mtl_fbx_stats(pfx); mtl_fbx_fetch_stats(pff); mtl_fbx_split_stats(psp); { void nvn_syncwait_stats(uint64_t out[3]); nvn_syncwait_stats(psw); }
}
static void tsummary(uint64_t frame) {
    char line[1024]; size_t o = 0;
    for (int k = 0; k < 6; k++) {
        unsigned n = g_tn[k] < TWIN ? g_tn[k] : TWIN; if (!n) continue;
        double v[TWIN]; memcpy(v, g_tw[k], n * sizeof *v); qsort(v, n, sizeof *v, dcmp);
        o += snprintf(line + o, sizeof line - o, " %s p50 %.2f / p95 %.2f / max %.2f ms (%u frames);", g_tname[k], v[n / 2], v[(n * 95) / 100 < n ? (n * 95) / 100 : n - 1], v[n - 1], n);
    }
    LOG("[nvn] timing up to frame %llu:%s", frame, line);
    { static uint64_t pn, pe, pw; if (g_acq_n > pn) LOG("[nvn] swapchain acquires up to frame %llu: %llu, %llu of them before the GPU finished the frame last drawn into that image%s",
          frame, (unsigned long long)(g_acq_n - pn), (unsigned long long)(g_acq_early - pe), acq_gpu_wait() ? "" : " (DM_NVN_ACQ_GPU=wait waits)");
      if (acq_gpu_wait() && g_acq_gpu_ns > pw) LOG("[nvn] ... waited %.2f ms for it in all", (g_acq_gpu_ns - pw) / 1e6);
      pn = g_acq_n; pe = g_acq_early; pw = g_acq_gpu_ns; }
    { char pl[128]; dm_perf_line(pl, sizeof pl); LOG("[nvn] fps / GPU line at frame %llu: %s", frame, pl); }   // what the iOS overlay / Mac title show
    { static uint64_t pb = UINT64_MAX; uint64_t b, off; void mtl_border_census(uint64_t *, uint64_t *); mtl_border_census(&b, &off);   // plan item 161's census
      if (b != pb) { pb = b; LOG("[nvn] samplers up to frame %llu: %llu clamp to their border colour, %llu of them a colour Metal does not have", frame, (unsigned long long)b, (unsigned long long)off); } }
    power_line(frame);
    { unsigned n = g_tn[0] < TWIN ? g_tn[0] : TWIN, over = 0; double v[TWIN];   // frame interval gets its own line (M1 baseline): frames over their hold in this window and p99
      if (n) { for (unsigned i = 0; i < n; i++) over += nvn_slow(g_tw[0][i], g_tw0_hold[i]);   // over 1.1 × each frame's own hold (plan item 156)
        memcpy(v, g_tw[0], n * sizeof *v); qsort(v, n, sizeof *v, dcmp);
        LOG("[nvn] frame interval up to frame %llu: over their hold %u / %u frames, p99 %.2f ms", frame, over, n, v[(n * 99) / 100 < n ? (n * 99) / 100 : n - 1]); } }
    { unsigned n = g_presn < TWIN ? g_presn : TWIN, k = 0, drop = 0, discard = 0; double v[TWIN];   // on-screen present: dropped = on screen over 1.5 × its hold, or discarded (nvn_dropped)
      for (unsigned i = 0; i < n; i++) { double x = g_pres_iv[i]; if (x < 0) { drop++; discard++; } else if (x > 0) { v[k++] = x; drop += nvn_dropped(x, g_pres_exp[i]); } }
      uint64_t fpb = dm_footprint(); double fp = fpb ? fpb / 1048576.0 : -1;
      double br = dm_brightness_now();   // footprint / brightness are carried on the on-screen present row: does not depend on the tick-entry observation gate (camp runs have it too)
      if (k) { qsort(v, k, sizeof *v, dcmp); LOG("[nvn] on-screen present up to frame %llu: %u frames, dropped %u (discarded %u), p99 %.2f ms; footprint %.0f MiB; brightness %.2f; thermal %d", frame, k + discard, drop, discard, v[(k * 99) / 100 < k ? (k * 99) / 100 : k - 1], fp, br, dm_thermal_now()); }
      g_presn = 0; }
    { static uint64_t pi, pc, pd, pf; uint64_t ci = atomic_load(&g_cum_sub_instr), cc = atomic_load(&g_cum_sub_cpu_ns), ps_, dr_; mtl_stats(&ps_, &dr_);   // translation layer
      if (pf && frame > pf && dr_ > pd) LOG("[nvn] translation up to frame %llu: %.0f instructions per draw, per-frame submit %.2f M instructions / CPU %.3f ms, %.0f draws per frame", frame,
          (double)(ci - pi) / (double)(dr_ - pd), (ci - pi) / 1e6 / (double)(frame - pf), (cc - pc) / 1e6 / (double)(frame - pf), (double)(dr_ - pd) / (double)(frame - pf));
      { static uint64_t py, pk, pw; uint64_t cy = atomic_load(&g_cum_sub_cyc), ck = atomic_load(&g_cum_sub_sys_ns), cw = atomic_load(&g_cum_sub_wall_ns);
        if (pf && frame > pf && ci > pi && cy > py) { double nf = (double)(frame - pf), cyc = (double)(cy - py), cpu = (double)(cc - pc);
          uint32_t qc = dm_self_qos();
          uint64_t aw = 0, ag = 0; int an = 0; void mtl_alloc_stats(uint64_t *, uint64_t *, int *); mtl_alloc_stats(&aw, &ag, &an);
          static uint64_t pyn, pyns; uint64_t yn = 0, yns = 0; dm_main_yield_stats(&yn, &yns);
          char lvl[360] = ""; { static uint64_t plc[THR_LVL_MAX], plm[THR_LVL_MAX]; static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
            double tot = 0, ms[THR_LVL_MAX], gc[THR_LVL_MAX]; size_t o = 0;
            for (int k = 0; k < thr_nlvl(); k++) { uint64_t c_ = atomic_load(&g_cum_lvl_cyc[k]), m_ = atomic_load(&g_cum_lvl_mach[k]);
                ms[k] = (double)(m_ - plm[k]) * tb.numer / tb.denom / 1e6; gc[k] = (double)(c_ - plc[k]); tot += ms[k]; plc[k] = c_; plm[k] = m_; }
            for (int k = 0; k < thr_nlvl() && tot > 0 && o < sizeof lvl; k++) {
                char nm[32] = ""; if (!dm_perf_level_name(k, nm, sizeof nm)) snprintf(nm, sizeof nm, "level %d", k);
                o += (size_t)snprintf(lvl + o, sizeof lvl - o, "%s%s %.0f%% at %.2f GHz", k ? ", " : "; the translating thread on ", nm, 100 * ms[k] / tot, ms[k] > 0 ? gc[k] / (ms[k] * 1e6) : 0); }
            // The same for this thread, the one presenting (Tears of the Kingdom's main thread, real-time but of no QoS class: plan item 229)
            static thr_lvl pme[THR_LVL_MAX]; thr_lvl me[THR_LVL_MAX]; double mt = 0, mm[THR_LVL_MAX];
            if (thr_levels(me)) { for (int k = 0; k < thr_nlvl(); k++) { mm[k] = (double)((me[k].user + me[k].sys) - (pme[k].user + pme[k].sys)) * tb.numer / tb.denom / 1e6; mt += mm[k]; }
                for (int k = 0; k < thr_nlvl() && mt > 0 && o < sizeof lvl; k++) {
                    char nm[32] = ""; if (!dm_perf_level_name(k, nm, sizeof nm)) snprintf(nm, sizeof nm, "level %d", k);
                    o += (size_t)snprintf(lvl + o, sizeof lvl - o, "%s%s %.0f%% at %.2f GHz", k ? ", " : "; this thread on ", nm, 100 * mm[k] / mt, mm[k] > 0 ? (double)(me[k].cyc - pme[k].cyc) / (mm[k] * 1e6) : 0); }
                if (mt > 0 && o < sizeof lvl) o += (size_t)snprintf(lvl + o, sizeof lvl - o, " (%.2f ms a frame)", mt / nf);
                memcpy(pme, me, sizeof pme); } }
          LOG("[nvn] translation detail up to frame %llu: per-frame submit cycles %.2f M (%.2f cycles per instruction), CPU %.3f ms of which kernel %.3f ms, wall %.3f ms; avg clock %.2f GHz (cycles / CPU time); this thread QoS %#x; allocator pools %d, cumulative GPU waits %llu; real-time declared computation %.2f ms; main thread yields per frame %.0f / %.3f ms%s", frame,
              cyc / 1e6 / nf, cyc / (double)(ci - pi), cpu / 1e6 / nf, (double)(ck - pk) / 1e6 / nf, (double)(cw - pw) / 1e6 / nf, cpu > 0 ? cyc / cpu : 0, (unsigned)qc, an, (unsigned long long)aw, g_rt_comp_ms, (double)(yn - pyn) / nf, (double)(yns - pyns) / 1e6 / nf, lvl); pyn = yn; pyns = yns; }
        if (frame % 300 == 0) { unsigned wn = 0; uint32_t wq = dm_guest_work_qos(&wn); dm_sched sc; dm_self_sched(&sc);   // (plat.h)
            LOG("[nvn] thread policy: main thread real-time computation %.2f ms / period %.2f ms, real-time policy %s, current priority %d (base %d, sched policy %d); worker threads %u, QoS %#x; present QoS %#x", g_rt_comp_ms, g_rt_period_ms, sc.rt_live ? "active" : "inactive", sc.cur_pri, sc.base_pri, sc.policy, wn, wq, mtl_present_qos()); }
        py = cy; pk = ck; pw = cw; }
      pi = ci; pc = cc; pd = dr_; pf = frame; }
    uint64_t up, re, fa, pr; mtl_pkg_stats(&up, &re); nvn_dirty_stats(&fa, &pr);
    LOG("[nvn] packaged textures: first uploads %llu, re-uploads on pool content change %llu; write-protect faults %llu, pages unprotected before host writes %llu; missed detections %llu (cumulative)", up, re, fa, pr, mtl_pkg_miss());
    memset(g_tn, 0, sizeof g_tn);
}
// DM_SLOWFRAME=1 (test switch, read over the log port on a real device): per-frame timings are first stored in a ring of the last 16 frames; dropped frames are judged 8 frames late (GPU / on-screen present times are back by then),
// and only for dropped frames is one line "[slowframe]\t<frame number>\t@<name>=<value>..." written to the log, plus one denominator line every 300 frames. No per-frame file is written: the log tee fsyncs every line, and writing line by line would disturb the readings
#define SRN 16
static struct { uint64_t f; int n; const char *k[64]; double v[64]; } g_sr[SRN];
static int g_slow = -1;
static int slow_on(void) { if (g_slow < 0) g_slow = getenv("DM_SLOWFRAME") != NULL; return g_slow; }
static int frame_rec(void) { return g_tsv || slow_on(); }
static void trow(uint64_t f, const char *name, double v) {
    if (g_tsv && !g_tsv_full) fprintf(g_tsv, "%llu\t%s\t%.3f\n", f, name, v);
    if (slow_on()) { __typeof__(g_sr[0]) *r = &g_sr[f % SRN]; if (r->f != f) { r->f = f; r->n = 0; } if (r->n < 64) { r->k[r->n] = name; r->v[r->n++] = v; } }
}
static double srow(uint64_t f, const char *name, double dflt) {
    __typeof__(g_sr[0]) *r = &g_sr[f % SRN];
    if (r->f == f) for (int i = 0; i < r->n; i++) if (!strcmp(r->k[i], name)) return r->v[i];
    return dflt;
}
