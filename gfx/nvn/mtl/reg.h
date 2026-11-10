// gfx/nvn/mtl/reg.h: the registries for the Metal backend's translation units (plan item 264; included by mtl/mtl.h): what a texture ID and a
// sampler ID name (mtl/descriptors.inc), the shader and program records (mtl/shaders.inc), and the few flags and calls the units share with the
// draw's path. The tables are read and written under g_rmu (game threads register while the replay draws)
#pragma once
// ---- Descriptor pool: texture ID / sampler ID → object (mtl/descriptors.inc) ----
// At registration the whole texture object is snapshotted (NVN semantics: the descriptor has been written into the pool); view: the MTLTexture for that ID (a view or the base texture, held via CFBridgingRetain) and the serial-table / guest-write generations at the time it was obtained
// fin: the texture this entry names was released after nvnTextureFinalize (fin_drain) while the ID stayed registered, at submission fin_sub
typedef struct { tex_t t; int ok, fin; view_t v; int has_v; void *view; uint64_t view_id, view_rid, view_ser, view_dirty, fin_sub, live_gen; int view_kind;
                 void *alt, *alt_of; uint64_t alt_rid; uint8_t alt_type;
                 float wr_x, wr_y; uint64_t wr_rid, wr_gen; } treg_t;   // wr_*: the bound texture's storage ratio (the widening, plan item 320g), for the texture wr_rid while g_ser_gen is wr_gen   // alt: a view of another texture type for shaders that declare one (tex_fit), built from the view alt_of
extern treg_t *g_treg; extern int g_ntreg;
extern samp_par *g_sreg; extern uint8_t *g_sreg_ok; extern void **g_sview; extern uint64_t *g_srid; extern int g_nsreg;   // g_sview: sampler ID → MTLSamplerState (retained)
extern uint8_t *g_sreg_lb;           // the sampler's LOD bias is carried by the shaders, not the sampler (plan item 318)
extern int g_lb_any, g_lb_force;     // such a sampler has been registered; DM_SAMP_LB_EMU (1 on, 0 off, -1 not set)
extern pthread_mutex_t g_rmu;        // the tables' lock (mtl/renderpass.inc)
// The texture / sampler pool a command buffer set last (plan item 64e): handle IDs resolve at its base in the tables
extern int g_tex_ns, g_samp_ns;
static inline int h_tid(uint64_t h) { int t = (int)(h & 0xfffff); return t ? t + g_tex_ns : 0; }
static inline int h_sid(uint64_t h) { int s = (int)(h >> 20); return s ? s + g_samp_ns : 0; }
// ---- Shaders and programs (mtl/shaders.inc) ----
@interface NvShader : NSObject { @public id<MTLFunction> fn; nvn_refl rf; id<MTLArgumentEncoder> ae; NSUInteger arg_len; int stage; char key[24]; id<MTLBuffer> c1buf; uint64_t c1_gpu; size_t c1off;
    id<MTLLibrary> vi_lib; NSString *vi_entry; uint32_t vi_mask; NSMutableDictionary<NSNumber *, id<MTLFunction>> *vi_fns;   // integer vertex input variant (dmsh_build.py vi_variant): locations that can be specialized
    // The argument buffer encoded last time
    id<MTLBuffer> ab_buf; uint64_t ab_gpu; size_t ab_off; uint64_t ab_pass; int ab_wfl;   // ab_wfl: what its uniform blocks held for the widening (320d), kept with the reused block;   // ab_pass: serial of the render pass that encoded this argument block - reused only within the same pass (the arena holding a block from another pass / another submission may already have been reclaimed and rewritten)
    id<MTLLibrary> bflib; int bflib_tried; NSMutableDictionary<NSString *, id<MTLFunction>> *bffn;   // bflib: readable-tile version <key>_bf (merge B step 2)
    id<MTLLibrary> alib; NSMutableDictionary<NSNumber *, id<MTLFunction>> *afn;
    NSMutableDictionary<NSString *, NvShader *> *bdv;
    NSMutableDictionary<NSString *, NvShader *> *lbv; NSString *msl;   // lbv: variants carrying the bound samplers' LOD biases (lb_variant, plan item 318); msl: a variant's source
    id<MTLFunction> ifn; int ifn_tried;
    int kw; } @end   // kw: DM_NVN_KEY_WATCH (plan item 322): 0 not looked at yet, 1 watched and its first use logged, 2 not watched   // ifn: the integer-output variant <key>_i_b (fout_fn, plan item 162)   // bdv: variants that return a sampler's border colour Metal lacks (border_variant, plan item 161)   // alib: a fragment shader with the alpha test epilogue (dmsh_build.py alpha_test); afn: its specializations
@interface NvProg : NSObject { @public NvShader *sh[6]; int sum_ok, sum_img, sum_imgw; uint32_t sum_cmask; } @end   // sum_*: udc_note_draw's per-program summary
extern NSString *g_shmap_path, *g_msl_dir;   // the shader map and the directory of this chain's Metal libraries
id<MTLFunction> alpha_plain(id<MTLLibrary> lib, NSString *entry);   // a fragment function as drawn without an alpha test (mtl/pipeline.inc)
extern int g_pso_dirty, g_dss_dirty; // pipeline / depth-stencil related state was changed during the replay (mtl/passes.inc)
// ---- Sampler LOD bias in the shader (mtl/lod_bias.m) ----
NSString *lb_src(NSString *src, NSArray<NSString *> *names, const float *bias, int frag, int *count, int *left);   // the MSL with the named samplers' biases in its sample calls
void lb_rebind(int st, const uint64_t *h, int n);   // a stage's sampler bindings changed
NvShader *lb_variant(NvShader *sh, int st);         // the stage's shader with the bound samplers' biases (itself when none)
