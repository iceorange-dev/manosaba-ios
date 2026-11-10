// gfx/nvn/mtl/tbdr_xv.inc, part of nvn_mtl.m (included there in order, one translation unit): Load elision, same-target continuation, layered passes (their state and declarations)
// The TBDR switches and the cross-submission attachment validity ledger are gfx/nvn/core/tbdr.c (plan item 264)
// The store lookahead (sd_att, g_sd, sd_decide): gfx/nvn/core/pass.h
static void sd_finish(void);
// Load elision (fully covering first draw): if the draw that opens the pass can be proven to cover a color attachment in full (first_draw_covers), that attachment's load becomes DontCare -
// the first draw does not blend and writes all channels, every covered pixel is rewritten by it, and the original content is not visible (Switch immediate-mode rendering has no such cost; on TBDR load = reading the whole thing from memory into the tile)
static const p_draw *g_fd_dw; static const p_draw_idx *g_fd_di;   // The draw that opens the pass (one of non-indexed / indexed); non-null only while draw() is opening a pass
static uint64_t g_fd_n;         // Cumulative number of attachments judged elidable
static int g_fd_why;            // Reason for the most recent failure (Q line of PASSDUMP: 1 no program 2 shader 3 attachment 4 blend/mask/raster 5 depth-stencil test 6 vertex count 7 position attribute 8 format/stream 9 out of bounds 10 address 11 geometry 12 vertices or indices the GPU writes, pending)
static int first_draw_covers(int i, id<MTLTexture> t, NSUInteger level);
static int pf_is_float(MTLPixelFormat f) {   // Float color formats that can store NaN (sentinel choice for the load-elision probe; RGB9E5 cannot store NaN and does not count)
    switch (f) { case MTLPixelFormatR16Float: case MTLPixelFormatRG16Float: case MTLPixelFormatRGBA16Float: case MTLPixelFormatR32Float:
        case MTLPixelFormatRG32Float: case MTLPixelFormatRGBA32Float: case MTLPixelFormatRG11B10Float: return 1; default: return 0; }
}
static int fz_load_covers(int i, id<MTLTexture> t, NSUInteger level);
static _Atomic uint64_t g_lp_ok, g_lp_bad;   // the load probe's readbacks: fully covered / with sentinel pixels left (lp_finish; t_xv)
static NSMutableArray *g_lp;    // DM_NVN_LOADPROBE=1 / 2 / 3 (two constant sentinel values / NaN - under float formats normal shader output cannot reach NaN, ruling out numeric coincidence): attachments judged elidable are cleared to the sentinel color instead, and at pass end the remaining sentinel pixels are read back and counted (diagnostic: should be 0)
static void lp_finish(void);
// Same-target continuation: when the game calls SetRenderTargets again on the same set of targets (with no copy / dispatch / clear in between) the pass is not ended - on TBDR this saves one full write-back + load.
// Basis for equivalence: NVN's Barrier between two passes requires the latter to see all writes of the former. Within one Metal render encoder, later draws' reads and writes of the attachment itself (blend, depth / stencil test)
// are ordered with earlier draws in submission order; but 'sampling' the attachment texture is not covered (reading and writing the same attachment within one pass is undefined). So continuation happens only when later draws do not sample these attachments:
// if during continuation a draw needs to sample them, or a clear arrives, the pass is ended before it, returning to the separate semantics. Copies / dispatches end the render encoder anyway
static int g_enc_mode;          // How the current encoder was opened (rp_begin_ex argument: 0 all attachments 1 only for clears 2 depth omitted)
// Layered render passes (lay_decide): a pass opened on one layer of a small cube / 2D array level covers the layers that the following SetRenderTargets
// of the same texture, level and view draw into; each draw goes to its layer through the vertex amplification view mapping (no shader change). Method
// from MacNativeGames' layered passes (ly05010419/MacNativeGames, MIT; it patches the vertex functions to write the layer instead), written anew here
enum { LAY_MAX_EXTENT = 256, PSO_LAYERED = 1u << 8 };   // PSO_LAYERED: in pso_key.a2c (bit 0 stays alpha to coverage)
static struct { int active, first, count, cur, amp, refuse; uint64_t ser; view_t v; uint64_t passes, merges, splits; } g_lay = {.amp = -1};
static int lay_decide(int clear_only, int *first);
