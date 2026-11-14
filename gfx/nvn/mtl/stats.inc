// gfx/nvn/mtl/stats.inc, part of nvn_mtl.m (included there in order, one translation unit): Counts and wait points read from outside, what a present asks of this unit
void mtl_res_snapshot(mtl_res_count *out) {
    memset(out, 0, sizeof *out);
    out->pso = g_pso.n;
    out->cps = g_cps.n;
    out->dss = g_dss.n;
    out->shaders = (uint32_t)(g_shcache ? g_shcache.count : 0);
    os_unfair_lock_lock(&g_c1_lock);
    out->c1_bufs = (uint32_t)(g_c1_banks.count + g_c1_exclusive); out->c1_alloc = g_c1_alloc_bytes; out->c1_data = g_c1_data_bytes;
    os_unfair_lock_unlock(&g_c1_lock);
    pthread_mutex_lock(&g_smu);
    out->live_tex = g_by_serial.n;
    pthread_mutex_unlock(&g_smu);
    pthread_mutex_lock(&g_mu);
    out->pools = (uint32_t)g_npool;
    out->pool_adds = g_pool_adds;
    pthread_mutex_unlock(&g_mu);
    pthread_mutex_lock(&g_rmu);
    out->view_slots = (uint32_t)g_ntreg;
    for (int i = 0; i < g_ntreg; i++) if (g_treg[i].view) out->view_nonnull++;
    for (int i = 0; i < g_nsreg; i++) if (g_sview[i]) out->sview_nonnull++;
    pthread_mutex_unlock(&g_rmu);
}
void mtl_stats(uint64_t *passes, uint64_t *draws) { *passes = g_stat_pass; *draws = g_stat_draw; }
void mtl_args_stats(uint64_t *enc, uint64_t *reuse) { *enc = g_stat_args_enc; *reuse = g_stat_args_reuse; }
void mtl_use_stats(uint64_t *call, uint64_t *skip) { *call = 0; *skip = 0; }
uint64_t mtl_ds_omit(void) { return g_stat_ds_omit; }
void mtl_bw(uint64_t *load, uint64_t *store, uint64_t *full) { *load = g_bw_load; *store = g_bw_store; *full = g_pass_full_rw; }
uint64_t mtl_udc_saved_bytes(void) { return g_udc_saved_bytes; }
// Called by the watchdog (no progress): writes the state of all blocking wait points on the graphics side to the log -- who is waiting for whom is visible at a glance (after switching to the Metal 4 submit model there was once a case of "ran to 900 frames then stopped, no failure line")
void mtl_wait_state_dump(void) {
    uint64_t pq_in = 0, pq_done = 0; present_queue_state(&pq_in, &pq_done);
    MLOG("wait point state: main fence committed %llu / completed %llu; main allocator fences [%llu %llu %llu]; upload pending %d, allocators busy [%d %d %d], seq %u, unflushed batch %d; present queue in %llu / done %llu; main command buffer encoding %d, submit thread set %d",
         (unsigned long long)g_fence, (unsigned long long)g_done, (unsigned long long)g_alloc_fence[0], (unsigned long long)g_alloc_fence[1], (unsigned long long)g_alloc_fence[2],
         atomic_load(&g_up_pending), atomic_load(&g_up_alloc_busy[0]), atomic_load(&g_up_alloc_busy[1]), atomic_load(&g_up_alloc_busy[2]), g_up_seq, g_up_cb != nil,
         (unsigned long long)pq_in, (unsigned long long)pq_done, g_cb != nil, atomic_load(&g_sub_thread_set));
}
void mtl_alloc_stats(uint64_t *waits, uint64_t *grown, int *n) { *waits = g_alloc_waits; *grown = g_alloc_grown; *n = g_alloc_n; }   // Main submit allocator pool: how many times it waited, how many times it grew, how many exist now
// ---------- What a present asks of this unit (mtl/present.m; declared in mtl/mtl.h) ----------
// The texture a present shows, and the game's crop on it: on widened storage (plan item 320g) the crop is mapped by the storage's ratio into wcrop,
// so the widened picture is shown, not its left 16:9 part
id<MTLTexture> present_tex(const tex_t *t, const int **crop, int wcrop[4]) {
    id<MTLTexture> src = tex_mtl(t); const int *c = *crop;
    if (src && __builtin_expect(c && (c[2] || c[3]) && t->d.width && t->d.height && tex_is_widened(src, t), 0)) {
        widen_map_rect(c, (double)src.width / t->d.width, (double)src.height / t->d.height, (int)src.width, (int)src.height, wcrop); *crop = wcrop; }
    return src;
}
void use_present(void) { if (g_use != USE_OFF && g_use != USE_DONE) use_tick(); }   // DM_GFXUSE counts its frames by presents
void bootprof_present(uint64_t n) {   // DM_BOOTPROF's line at the first present and every 120th
        if (n == 1) g_bp.first_present = bp_now() - g_bp.t0;
        MLOG("[bootprof] t=%.3f s present #%llu: metallib loads %llu / %.1f ms; pipeline creations %llu / %.1f ms (all synchronous on the submit thread); warm-up claimed shaders %llu / render pipelines %llu / compute pipelines %llu; "
             "texture uploads %.1f MB / %llu first uploads + %llu re-uploads / %.1f ms; first frame presented t=%.3f s", (bp_now() - g_bp.t0) / 1e9, n,
             g_bp.lib_n, g_bp.lib_ns / 1e6, g_bp.pso_n, g_bp.pso_ns / 1e6, g_warm_claim_shaders, g_warm_claim_rps, g_warm_claim_cps,
             g_bp.up_bytes / 1048576.0, g_pkg_uploads, g_pkg_reuploads,
             g_up_total_ns / 1e6, g_bp.first_present / 1e9);
}
