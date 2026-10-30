// gfx/nvn/mtl/uniform_rename.inc, part of nvn_mtl.m (included there in order, one translation unit): Inline uniform updates' backend side: the argument blocks they make stale, the log's entry point, writing them to memory
// The log and the private copies are gfx/nvn/core/upd.c (plan item 264)
static void upd_dirty_stages(uint64_t lo, uint64_t hi) {   // the argument blocks of stages binding an updated range are encoded again (they point at the old copy)
    for (int st = 0; st < 6; st++) for (int k = 0; k < 16; k++) { const p_bind_buf *u = &S.ubo[st][k]; if (u->addr && u->addr < hi && u->addr + (u->size ? u->size : 1) > lo) { g_args_dirty[st] = 1; break; } }
}
static void upd_log(uint64_t lo, uint64_t hi, const uint8_t *data) {
    upd_add(lo, hi, data); if (g_enc) g_ren.pass = g_pass.serial;
    upd_dirty_stages(lo, hi);
}
static uint64_t upd_shadow(uint64_t lo, uint64_t size, void **buf) { return upd_shadow_at(lo, size, buf, g_sub_serial); }
static void upd_flush(void) {   // the updates not in memory yet into memory on the GPU timeline (merged; ends a render pass if one is open)
    if (!g_upd.n || g_upd_flushing) return;
    g_upd_flushing = 1;
    upd_list *l = &g_upd;
    for (int i = 0; i < l->n;) {
        uint64_t lo = l->e[i].lo, hi = l->e[i].hi; int j = i + 1;
        for (; j < l->n && l->e[j].lo <= hi; j++) hi = MAX(hi, l->e[j].hi);   // overlapping or touching: covered whole by updates, one copy
        void *db; uint64_t dof;
        if (mtl_addr(lo, hi - lo, &db, &dof)) {
            ARENA_OUT id<MTLBuffer> sb; size_t so = arena_alloc((size_t)(hi - lo), &sb);
            ul_apply(l, lo, (uint8_t *)sb.contents + so, hi - lo, 0);
            enc_b_copy_buffer(blit(), sb, so, (__bridge id<MTLBuffer>)db, dof, hi - lo);
        } else {   // touching updates in two pools: each on its own, oldest first (blit() orders them)
            const int *ord = ul_by_seq(l, i, j);
            for (int q = 0; q < j - i; q++) { const upd_ent *e = &l->e[ord[q]]; void *eb; uint64_t eo; __unsafe_unretained id<MTLBuffer> rb; size_t ro;
                if (!mtl_addr(e->lo, e->hi - e->lo, &eb, &eo)) MDIE("renamed uniform update %#llx+%#llx is no longer in a memory pool", (unsigned long long)e->lo, (unsigned long long)(e->hi - e->lo));
                ring_put(e->data, e->hi - e->lo, &rb, &ro); enc_b_copy_buffer(blit(), rb, ro, (__bridge id<MTLBuffer>)eb, eo, e->hi - e->lo); }
        }
        if (us_on()) us_gw_note(lo, hi - lo, g_fence + 1, 1);   // a GPU write for the next submissions, its bytes the pending updates'; the snapshot stays (private copies add every update to it)
        i = j;
    }
    ul_clear(l); g_nupd = 0; g_ren.flushes++;
    g_upd_flushing = 0;
}
