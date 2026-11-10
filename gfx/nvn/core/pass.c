// gfx/nvn/core/pass.c: pass planning (see pass.h; plan item 264 moved it here from the Metal backend's mtl/lookahead.inc and mtl/tbdr_xv.inc)
#include "pass.h"
#include "rt.h"
#include "state.h"
#include "store_index.h"
#include "tbdr.h"
#include "upd.h"
#include "usnap.h"

const uint8_t *g_exec_cur, *g_exec_next;
const uint8_t *g_ret[16]; int g_ret_n;
const uint64_t *g_rest; int g_nrest;
int g_rt_merged; uint64_t g_rt_merge_n;
void nvn_exec_rest(const uint64_t *h, int n) { g_rest = h; g_nrest = n; }
const uint8_t *exec_cont(int *ci) {
    for (;;) { int k = (*ci)++;
        if (k < g_ret_n) { if (g_ret_n > 16) return NULL; return g_ret[g_ret_n - 1 - k]; }
        k -= g_ret_n; if (k < g_nrest) return (const uint8_t *)g_rest[k];
        return NULL; }
}
const uint8_t *g_hoist_skip[HOIST_MAX]; int g_hoist_n; uint64_t g_hoist_groups;
int hoist_take(const uint8_t *p) { for (int i = 0; i < g_hoist_n; i++) if (g_hoist_skip[i] == p) { g_hoist_skip[i] = g_hoist_skip[--g_hoist_n]; return 1; } return 0; }

// ---- The clear-only pass hoist ----
int hoist_plan(hoist_grp G[4]) {
    if (g_hoist_n || !g_exec_next) return 0;
    int ng = 0, in_grp = 0; p_scissor sc = R.sc; int has_sc = R.has_sc;
    // The binding set needed for "the cleared texture is not on any slot": the table is looked up only when a hoistable clear group is really found and we are back to this render pass's targets (the vast majority of render passes never get that far).
    // Handles bound within the scan range are noted first and converted to texture serials together with the current slots at lookup time; the scan only reads commands and the current slots S do not change meanwhile, so the result is the same as checking at the start
    uint64_t bh[512]; int nbh = 0;
    const uint8_t *p = g_exec_next;
    for (int guard = 0; guard < 200000; guard++) {
        op_hdr h; memcpy(&h, p, sizeof h); const void *a = p + 8;
        switch (h.op) {
            case OP_END: case OP_CALL: case OP_COPY_T2T: case OP_COPY_B2T: case OP_COPY_T2B: case OP_CLEAR_TEX: case OP_DISPATCH: case OP_RENDER_ENABLE: return 0;   // clears are not moved across a render enable change
            case OP_JUMP: { const uint8_t *t; memcpy(&t, a, 8); p = t; continue; }
            case OP_DRAW: case OP_DRAW_IDX: if (in_grp) return 0; break;
            case OP_SCISSOR: sc = *(const p_scissor *)a; has_sc = 1; break;
            case OP_BIND_TEX: case OP_BIND_IMG: { const p_bind_handles *b = a; bh_fresh(b); for (int k = 0; k < b->n && k < 32 && nbh < 512; k++) if (b->h[k]) bh[nbh++] = b->h[k]; break; }
            case OP_CLEAR_COLOR: case OP_CLEAR_DS: {
                if (!in_grp || G[ng - 1].nops >= 8) return 0;
                hoist_grp *g = &G[ng - 1];
                if (h.op == OP_CLEAR_COLOR) { const p_clear_color *c = a;
                    if (c->index < 0 || c->index >= g->rt->n || !g->rt->color[c->index] || c->mask != 0xf) return 0;
                    const tex_t *t = g->rt->color[c->index];
                    if (has_sc && !(sc.x <= 0 && sc.y <= 0 && sc.x + sc.w >= (int)t->d.width && sc.y + sc.h >= (int)t->d.height)) return 0;
                    g->clear_c[c->index] = 1; g->cc[c->index] = nvn_rgba_make(c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3]); }
                else { const p_clear_ds *c = a; if (!g->rt->depth) return 0;
                    if (!scissor_full_of(&sc, has_sc, g->rt->depth, &g->rt->dview, g->rt->has_dview) || ((c->stencil_mask & 255) && (c->stencil_mask & 255) != 255)) return 0;   // drawn as a triangle (clear_ds_quad), not a load action
                    if (c->depth_mask) { g->clear_d = 1; g->cd = c->depth; } if (c->stencil_mask) { g->clear_s = 1; g->cs = (uint32_t)c->stencil; } }
                g->sc = sc; g->has_sc = has_sc; g->ops[g->nops++] = p; break; }
            case OP_SET_RT: { const p_set_rt *r = a;
                if (rt_same(&R.rt, r)) {   // Back to this render pass's targets: execute if hoisting holds
                    if (!ng) return 0;
                    uint64_t bound[512]; int nb = 0;   // The order of the original "check at the start": the current slots (stage x textures 128 + images 32), followed by those bound within the scan range
                    for (int st = 0; st < 6; st++) { for (int k = 0; k < 128 && nb < 512; k++) if (S.tex[st][k]) bound[nb++] = nvn_be_handle_ser(S.tex[st][k]);
                                                     for (int k = 0; k < 32 && nb < 512; k++) if (S.img[st][k]) bound[nb++] = nvn_be_handle_ser(S.img[st][k]); }
                    for (int k = 0; k < nbh && nb < 512; k++) bound[nb++] = nvn_be_handle_ser(bh[k]);
                    for (int gi = 0; gi < ng; gi++) {   // Disjoint and not bound
                        hoist_grp *g = &G[gi]; if (!g->nops) return 0;
                        if (rt_any_shared(g->rt)) return 0;   // bound through another object of its storage, its serial is on no slot (rt.h)
                        for (int k = 0; k < 9; k++) { if (k < 8 ? !g->clear_c[k] : !(g->clear_d || g->clear_s)) continue;
                            uint64_t ser; int lv, sl; rt_sub(g->rt, k, &ser, &lv, &sl); if (!ser) return 0;
#ifndef DM_TEST_HOIST_BOUND_OK   // negative control of t_core_pass only: a cleared texture on a slot hoisted all the same
                            for (int j = 0; j < nb; j++) if (bound[j] == ser) return 0;
#endif
                            for (int m = 0; m < 9; m++) { uint64_t s2; int l2, sl2; if (m < 8 && m >= R.rt.n) continue; rt_sub(&R.rt, m, &s2, &l2, &sl2);
                                if (s2 == ser && l2 == lv && sl2 == sl) return 0; } } }
                    // Register all skip entries before the backend runs the groups: when a hoisted render pass ends the store lookahead scans forward, and the clears at their original positions must already be in the skip set
                    for (int gi = 0; gi < ng; gi++) { g_hoist_skip[g_hoist_n++] = G[gi].rt_op; for (int k = 0; k < G[gi].nops; k++) g_hoist_skip[g_hoist_n++] = G[gi].ops[k]; }
                    return ng;
                }
                if (in_grp && !G[ng - 1].nops) return 0;   // The previous group has no clear (only set targets): not recognized
                if (ng >= 4) return 0;
                memset(&G[ng], 0, sizeof G[ng]); G[ng].rt_op = p; G[ng].rt = r; ng++; in_grp = 1; break; }
            default: break;
        }
        p += h.size;
    }
    return 0;
}

// ---- Does a pass use depth / stencil ----
uint64_t g_stat_ds_omit, g_stat_s_only, g_stat_d_only;
enum { DS_FREE = 0, DS_USED = 1, DS_RET = 2 };
static int g_ds_why; static int g_ds_scanned; static int g_ds_need;   // g_ds_need: need accumulated during the scan, 1 depth 2 stencil   // Diagnostics: the opcode that triggered the 'used' verdict (for a draw, records 1000 + dt*100 + dw*10 + st), and the number of ops scanned
static int ds_scan(const uint8_t *p, ds_st *ds, int depth) {
    for (int guard = 0; guard < 1000000; guard++) {
        op_hdr h; memcpy(&h, p, sizeof h);
        const void *a = p + 8; g_ds_scanned++;
        if (g_hoist_n && hoist_skipped(p)) { p += h.size; continue; }   // Clear pass already executed early
        switch (h.op) {
            case OP_END: return DS_RET;
            case OP_JUMP: { const uint8_t *t; memcpy(&t, a, 8); p = t; continue; }
            case OP_CALL: { uint64_t t; memcpy(&t, a, 8); if (depth >= 8) return DS_USED;
                int r = ds_scan((const uint8_t *)t, ds, depth + 1); if (r != DS_RET) return r; break; }
            case OP_UPDATE_UB: if (ub_rename_on()) break; return DS_FREE;   // renamed: the pass goes on past it
            case OP_SET_RT: case OP_COPY_B2B: case OP_COPY_B2T: case OP_COPY_T2B: case OP_COPY_T2T: case OP_CLEAR_TEX: case OP_DISPATCH: return DS_FREE;
#ifndef DM_TEST_DS_CLEAR_FREE   // negative control of t_core_pass only: a clear inside the pass taken as needing neither
            case OP_CLEAR_COLOR: case OP_CLEAR_DS: g_ds_why = h.op; return DS_USED;
#else
            case OP_CLEAR_COLOR: case OP_CLEAR_DS: break;
#endif
            case OP_BIND_STATE: { const p_bind_state *b = a; if (b->kind == 5) memcpy(ds, b->bytes, sizeof *ds); break; }
            case OP_DRAW: case OP_DRAW_IDX:   // Depth test / write → depth needed; stencil test → stencil needed; can stop once both are needed
                g_ds_need |= (ds->dt || ds->dw ? 1 : 0) | (ds->st ? 2 : 0);
                if (g_ds_need == 3) { g_ds_why = 1000 + ds->dt * 100 + ds->dw * 10 + ds->st; return DS_USED; } break;
            default: break;
        }
        p += h.size;
    }
    return DS_USED;
}
// Continue scanning in real replay order: current list -> callers at each level (return stack from the top down) -> remaining handles of this submit; the render pass necessarily ends when the submit ends (rp_flush before commit)
// The whole render pass's need for depth / stencil (bits: 1 depth 2 stencil; 3 = both needed or cannot be proven). Starting from the draw that opens the render pass, scan in real replay order to the end of the render pass:
// the attached clears and this draw's state are counted first; later draws accumulate by their depth-stencil state; anything unprovable such as a mid-pass clear or nesting too deep is treated as both needed
int pass_ds_need(void) {
    int need = (R.clear_d || S.ds.dt || S.ds.dw ? 1 : 0) | (R.clear_s || S.ds.st ? 2 : 0);
    if (!R.rt.depth || need == 3 || !g_exec_next || g_ret_n > 16) {
        PD("S %llu no lookahead depth=%d clear_d=%d clear_s=%d dt=%d dw=%d st=%d next=%d ret=%d", g_stat_pass + 1, R.rt.depth != NULL, R.clear_d, R.clear_s, (int)S.ds.dt, (int)S.ds.dw, (int)S.ds.st, g_exec_next != NULL, g_ret_n);
        return 3;
    }
    ds_st ds = S.ds; g_ds_why = -1; g_ds_scanned = 0; g_ds_need = need;
    int r = ds_scan(g_exec_next, &ds, 0);
    for (int k = g_ret_n - 1; r == DS_RET && k >= 0; k--) r = ds_scan(g_ret[k], &ds, 0);
    for (int k = 0; r == DS_RET && k < g_nrest; k++) r = ds_scan((const uint8_t *)g_rest[k], &ds, 0);
    int out = r == DS_USED ? 3 : g_ds_need;
    PD("S %llu lookahead result=%d need=%d (1 depth 2 stencil) why=%d scanned %d ops return stack %d remaining handles %d", g_stat_pass + 1, r, out, g_ds_why, g_ds_scanned, g_ret_n, g_nrest);
    return out;
}

// ---- Store lookahead ----
sd_att g_sd[SD_MAX]; int g_nsd;
uint64_t g_sd_dead_bytes, g_sd_n_dead, g_sd_n_all, g_sd_n_shared;
uint64_t g_bw_load, g_bw_store, g_pass_full_rw, g_udc_saved_bytes;
static void sd_need(sd_att *t, int why) { if (!t->state) { t->state = 1; t->why = (uint8_t)why; } }   // why: 1 a draw reads it as a texture 2 as an attachment, drawn before cleared 3 copy 4 texture clear 5 scanned to the end undecided 6 no start point / index unavailable
static int sdx_decide(uint8_t st[SD_MAX]) {   // Store lookahead decision by the index (gfx/nvn/core/store_index.c; built by mtl_sub_index): st[i] 2 redundant, otherwise why << 2 (the reason it is needed, same as sd_need). Returns 0 = index unavailable
    uint32_t P; if (!sdx_pos(g_exec_cur, &P)) return 0;
    for (int i = 0; i < g_nsd; i++) st[i] = sdx_first(g_sd[i].ser, g_sd[i].lv, g_sd[i].sl, g_sd[i].depth, P, g_hoist_skip, g_hoist_n);
    return 1;
}
void sd_decide(uint8_t store[SD_MAX]) {
    if (!tbdr_part(TBP_STORE)) {   // the store rewrites off (Tears of the Kingdom's default on the Mac and iOS): written back, but for an attachment loaded and not
        // written (udc: memory holds it already). It was in the rewrites' set, so with them off it never acted: the Air stored 324-361 MB a frame, "0.0 MB
        // of stores saved" (plan item 315a, after S2Arm 4c0bec5, Apache-2.0, written anew); DM_NVN_UNWRITTEN_DC=0 turns it off
        for (int i = 0; i < g_nsd; i++) {
            sd_att *t = &g_sd[i];
#ifndef DM_TEST_UDC_TBDR_ONLY   // negative control of t_core_pass only: with the rewrites only, as before
            int dead = udc_on() && t->load && !t->wrote;
#else
            int dead = 0;
#endif
#ifndef DM_TEST_NOMEM_STORED   // negative control of t_core_pass only: a target without memory decided like any other
            if (t->nomem) dead = 1;
#endif
            store[i] = !dead; g_sd_n_all++;
            if (dead) { g_sd_n_dead++; g_sd_dead_bytes += t->bytes; g_bw_store -= t->bytes; g_udc_saved_bytes += t->bytes; }
        }
        return;
    }
    uint8_t nst[SD_MAX];
    if (sdx_decide(nst)) { for (int i = 0; i < g_nsd; i++) if (nst[i] == 2) g_sd[i].state = 2; else sd_need(&g_sd[i], nst[i] >> 2); }
    else for (int i = 0; i < g_nsd; i++) sd_need(&g_sd[i], 6);   // Pass ended outside the list / index unavailable: always write back
    for (int i = 0; i < g_nsd; i++) {
        sd_att *t = &g_sd[i]; int udc_dead = udc_on() && t->load && !t->wrote;
        // A color attachment written in the same pass as the window texture does not enter the ledger: it is a link in the present chain, not a transient attachment that is 'reset every frame before use'.
        // The in-run 'upscale' pass writes two textures at once - scene texture + window texture - and the UI composite was merged into the same pass, so in-run nobody reads back the scene texture; as soon as the game stops drawing 3D (loading screen)
        // a separate composite pass samples it afterwards instead. 'About to enter the loading screen' is only known after the previous frame was submitted, so prediction cannot stop it: on iPad true fullscreen every install hits it once on the first act change
        // (the 2732×2048 attachment of frame N was not written back and is sampled for 4 consecutive frames starting at frame N+1; the key is permanently disabled). At the iPhone aspect ratio the same-size key already sees a reader during the trial period and is silently disabled.
        int xv_win = 0; for (int k = 0; k < R.rt.n && k < 8; k++) if (R.rt.color[k] && xv_present(R.rt.color[k]->serial)) xv_win = 1;
        uint64_t xtag = 0; int xvcand = xv_on() && !xv_win && t->depth == 0 && t->state != 2 && t->why == 5 && xv_first_good(t->ser, t->lv, t->sl) && !xv_present(t->ser)
                                        && !xv_disabled(xtag = xv_tag_pass(R.rt.color[t->idx], t->lv, t->idx, &R.rt));
        // Another live object on its storage (rt.h tex_shared): a read or a clear through that one is under another serial, so neither the index's
        // "cleared before it is read" nor the ledger's holds for it. Written back
        if (t->state == 2 || xvcand) { const tex_t *tt = t->depth ? R.rt.depth : t->idx >= 0 && t->idx < 8 ? R.rt.color[t->idx] : NULL;
            if (tex_shared(tt)) { if (t->state == 2) { t->state = 1; t->why = 7; } xvcand = 0;
                uint64_t n = ++g_sd_n_shared; if (n <= 4 || !(n & (n - 1))) MLOG("store lookahead: texture %llu's storage is another texture object's too, written back (pass %llu; %llu so far)", (unsigned long long)t->ser, (unsigned long long)g_stat_pass, (unsigned long long)n); } }
        int xvdead = xvcand && xv_trial_get(xtag, 0) >= XV_TRIAL;   // Only after the trial period is the write-back really skipped
        if (xvcand && !xvdead) xv_set(t->ser, t->lv, t->sl, XV_PROBE, xtag);   // Trial period: write back as usual, only track the next touch
        int dead = t->state == 2 || udc_dead || xvdead;
        t->lost = (uint8_t)(t->state == 2 || xvdead);
#ifndef DM_TEST_NOMEM_STORED
        if (t->nomem) dead = 1;   // (plan item 280)
#endif
        if (xvdead) { xv_set(t->ser, t->lv, t->sl, XV_INVALID, xtag); g_xv.dc++; }
        store[i] = !dead;
        g_sd_n_all++; if (dead) { g_sd_n_dead++; g_sd_dead_bytes += t->bytes; g_bw_store -= t->bytes; if (udc_dead) g_udc_saved_bytes += t->bytes; }
        if (!t->state) t->why = 5;
        PD("Z %llu %s ser=%llu lv%u sl%u %s why=%d by=%s slot=%d", g_stat_pass, t->depth == 1 ? "d" : t->depth == 2 ? "s" : "c", (unsigned long long)t->ser, t->lv, t->sl, xvdead ? "dontcare-xv" : dead ? "dontcare" : "store", dead ? 0 : t->why, t->why == 1 && t->wkey ? t->wkey : "-", t->wslot);
        if (udc_on()) PD("U %llu %s ser=%llu lv%u sl%u runtime=%s reason=%s state=%d load=%d", g_stat_pass, t->depth == 1 ? "d" : t->depth == 2 ? "s" : "c", (unsigned long long)t->ser, t->lv, t->sl, t->wrote ? "wrote" : "not-written", t->wwhy == 1 ? "image-write" : t->wwhy == 2 ? "color-write-mask" : t->wwhy == 3 ? "fragment-output" : t->wwhy == 4 ? "depth-write" : t->wwhy == 5 ? "stencil-write" : t->wwhy == 6 ? "clear-quad" : "none", t->state, t->load);
    }
}
