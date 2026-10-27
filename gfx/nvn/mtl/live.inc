// gfx/nvn/mtl/live.inc, part of nvn_mtl.m (included there in order, one translation unit): Texture releases (the live texture at a storage address and
// the texture objects' host copies are gfx/nvn/core/live.c, plan item 264)
static void treg_drop_views(const uint64_t *serials, int n);
static void blk_drop(uint64_t serial);   // a finalized compressed render target's auxiliary texture
static void view_sweep(void);   // descriptors.inc
static void fin_drain(void) {   // submit thread, at the start of a submission
    uint64_t due[64]; int n = 0;
    tex_snap_reap(g_sub_serial); stor_sweep(); view_sweep();
    pthread_mutex_lock(&g_fin_mu);
    for (int i = 0; i < g_nfin;) {
        if (!g_fin[i].due) g_fin[i].due = g_sub_serial + FIN_GRACE;
        if (g_fin[i].due <= g_sub_serial && n < 64) { due[n++] = g_fin[i].serial; g_fin[i] = g_fin[--g_nfin]; } else i++;
    }
    pthread_mutex_unlock(&g_fin_mu);
    // Each serial's steps in the order they always ran; the views of all of them in one walk of the registrations (one walk per serial cost 0.28 ms
    // a submission in Tears of the Kingdom: 30 releases × 33,000 registrations)
    for (int i = 0; i < n; i++) serial_del(due[i]);
    treg_drop_views(due, n);
    for (int i = 0; i < n; i++) { blk_drop(due[i]); tex_snap_retire(due[i], g_sub_serial + SNAP_KEEP); live_released(due[i]); wb_released(due[i]); }
}
void mtl_tex_release(tex_t *t) { if (t->serial) { live_finalized(t); fin_queue(t->serial); tex_forget_serial(t->serial); } }

