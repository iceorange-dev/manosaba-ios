// gfx/nvn/core/tbdr.h: the switches of the TBDR rewrites and the cross-submission attachment validity ledger (plan item 264 moved them here from the
// Metal backend). Checked without a GPU by gfx/tests/t_core_tbdr.c; with the backend by t_xv.m, t_rt_continue.m and the tests that run DM_NVN_TBDR=0
#pragma once
#include <stdint.h>
#include "../nvn_obj.h"
#include "core.h"

enum { TBP_ZSKIP = 1, TBP_XV = 2, TBP_COVER = 4, TBP_STORE = 8, TBP_SDX = 16, TBP_DEFER = 32, TBP_FUSE = 64, TBP_HOIST = 128, TBP_DS = 256, TBP_CONT = 1024, TBP_RTHEAP = 2048 };   // (512 was the attachment-range continuation, removed: plan item 263)
// Diagnostic DM_NVN_TBDR=0: turns off all TBDR optimizations (store lookahead, load elision, same-target continuation, clear-only pass hoist, on-demand depth-stencil attach); replay takes the plainest 'attach all, load all, store all' path.
// Only for equivalence comparison (the same replay run on / off, comparing the picture byte by byte), not a product configuration
// DM_NVN_TBDR_OFF=<part>[,<part>...] (diagnostic, plan item 62j): with the TBDR rewrites on, the parts named stay off: zskip, xv, cover (load
// elision by coverage), store (store decisions), sdx (the store lookahead index), defer, fuse, hoist, ds (on-demand depth-stencil), cont (same-target
// continuation), rtheap; "all" is DM_NVN_TBDR=0's set of parts
// The checks are asked a few times a draw, so they stay inline; the first one reads the environment
extern int g_tbdr, g_tbdr_off;                 // -1: not read yet
extern int g_ruler_truth, g_ruler_pending;     // DM_NVN_RULER=1: the ruler's reference run with the rewrites off, then the measured mode (g_ruler_truth -1: not read yet)
void tbdr_read(void); void tbdr_read_off(void);
static inline int tbdr_on(void) { if (g_tbdr < 0) tbdr_read(); return g_tbdr && !g_ruler_truth; }
static inline int tbdr_part(int bit) { if (g_tbdr_off < 0) tbdr_read_off(); return tbdr_on() && !(g_tbdr_off & bit); }
// Attachments this pass did not write (depth / stencil test only, color write mask 0, etc.) get their write-back changed to DontCare: the content is already in memory, saving one full write-back.
// With the TBDR rewrites on or off (core/pass.c sd_decide; off, it never acted before plan item 315a). On by default: on Mac, same-process on / off screenshots follow the same distribution as the null control; on A19 Pro, private Depth32F_S8 / RGBA8 read back after DontCare differ in 0 places.
// DM_NVN_UNWRITTEN_DC=0 turns it off (for equivalence comparison)
extern int g_udc_ab, g_udc_ab_on;
int udc_on(void);

// ---------- Cross-submission attachment validity ledger ----------
// Entry = subresource (texture serial, level, layer) + state, surviving across submissions:
//   XV_INVALID       content was not written back / not generated and must not be read. The next touch must be a full Clear, or use as an attachment with load action DontCare / Clear; otherwise it is a violation.
//   XV_PENDING_CLEAR reserved for 'clear-only pending across submissions' (not implemented): accepts only full, unscissored clears with the write mask fully open; clearing again while pending = replace the clear value without landing it;
//                    as an attachment with load DontCare / Clear → the pending clear is voided, with load Load → changed to a Clear load; depth-stencil is recorded per plane.
// Violation = report a failure + that stable key (format / width / height / level) permanently disabled (in memory + persisted to DM_XV_FILE, read at the next launch) + all entries of the same texture deleted. The fallback can only detect, not undo (in that frame the attachment holds old content).
// Its first use: color attachments 'scanned to the end undecided' at the end of a submission are not written back (sd_finish) - conditions: its first touch in this submission is a full clear / DontCare load (reset every frame before use), it is not a window texture, and the key is not disabled.
// Switch DM_NVN_XV=0 turns it off; has no effect when tbdr_on()==0 (visual ruler ground-truth mode).
// Trial period (XV_PROBE): for a key's first XV_TRIAL times it is only 'shadow-accounted' - written back as usual, but its next touch is tracked all the same; only after XV_TRIAL consecutive compliant touches does the write-back really start being skipped.
//   On a read / Load during the trial period: only disable that key (within this process, not persisted, no failure), the picture is intact - in practice there are readers as early as the launch / menu screens (upscale target 36), and not every fresh install should get one wrong frame.
enum { XV_INVALID = 1, XV_PENDING_CLEAR = 2, XV_PROBE = 3 };
#define XV_TRIAL 3
typedef struct { uint64_t ser, tag; uint32_t lv, sl; uint8_t state; } xv_ent;
typedef struct { uint64_t ser; uint32_t lv, sl; uint8_t good; } xv_touch;
typedef struct {
    xv_ent ent[64]; int n, block, init;   // block: this submission adds nothing (its store lookahead index is unavailable)
    uint64_t disabled[128]; int ndis;     // Keys disabled (and read back from DM_XV_FILE)
    struct { uint64_t tag; int good; } trial[128]; int ntrial;   // Number of consecutive compliant touches for each key
    uint64_t present[128]; int npresent;  // Window textures
    uint64_t dc, viol;
    xv_touch touch[128]; int ntouch;      // This submission's first touches
} xv_state;
extern xv_state g_xv;
int xv_trial_get(uint64_t tag, int add);
uint64_t xv_tag(const tex_t *t, uint32_t lv);
uint64_t xv_tag_pass(const tex_t *t, uint32_t lv, int idx, const p_set_rt *rt);   // The stable key of an attachment in its pass
void xv_init(void);
int xv_disabled(uint64_t tag);
xv_ent *xv_find(uint64_t ser, uint32_t lv, uint32_t sl);
void xv_drop(uint64_t ser, uint32_t lv, uint32_t sl);
void xv_drop_all_of(uint64_t ser);
void xv_set(uint64_t ser, uint32_t lv, uint32_t sl, uint8_t state, uint64_t tag);
void xv_violation(const xv_ent *e, const char *why);
void xv_check(uint64_t ser, uint32_t lv, uint32_t sl, const char *why);   // A read or load of a subresource: a violation when its entry is invalid
void xv_good(xv_ent *e);
void xv_touch_add(uint64_t ser, uint32_t lv, uint32_t sl, int good);
int xv_first_good(uint64_t ser, uint32_t lv, uint32_t sl);
int xv_present(uint64_t ser);
int xv_on(void);   // DM_NVN_XV (read once) and the xv part of the TBDR switches
void mtl_xv_window_tex(uint64_t serial);
void xv_test_counts(int *dc, int *viol);
void xv_forward(void);   // At the start of a submission, after its store lookahead index: each entry's first event in it (a read violates, a full clear makes it valid)
