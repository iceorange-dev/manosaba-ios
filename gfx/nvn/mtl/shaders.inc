// gfx/nvn/mtl/shaders.inc, part of nvn_mtl.m (included there in order, one translation unit): The shader mapping table, programs, pipeline lists
// ---------- Shader mapping table: Switch shader key (control section + code area, hle_nvn.c shader_key) → PC metallib ----------
static NSMutableDictionary<NSString *, NSString *> *g_shmap;
static NSString *g_shmap_path, *g_msl_dir;
static NSMutableDictionary<NSString *, NSString *> *g_fmap;   // functional key → the first key of the map with it (the map's third column, plan item 169)
// Libraries an app packs once (gfx/tools/shader_pack.py, plan item 311b): <library>\t<the library of the same bytes packed in its place>, the
// directory's twins.tsv (none on the Mac, whose build directory has every file). A library is opened by msl_lib_path
static NSMutableDictionary<NSString *, NSString *> *g_twins;
static NSString *msl_lib_path(NSString *name) {
    NSString *t = nil;
#ifndef DM_TEST_TWINS_IGNORED   // negative control of t_shmap only: the table not read
    t = g_twins[name];
#endif
    return [g_msl_dir stringByAppendingPathComponent:t ?: name];
}
// Shader stage map (gfx/tools/shmap_from_capture.py): Switch shader key → stage; the shaders themselves are translations of the game's own
// Switch shaders built offline by gfx/tools/dmsh_build.py (<key>_b.metallib + reflection JSON)
static void shmap_load(void) {
    if (g_shmap) return;
    const char *e = getenv("DM_NVN_SHMAP");
    if (!e) MDIE("DM_NVN_SHMAP is not set: it names the game's shader stage map (gfx/tools/shmap_from_capture.py)");
    NSString *path = @(e);
    NSString *txt = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    if (!txt) MDIE("cannot read the shader stage map %s", path.UTF8String);
    g_shmap = [NSMutableDictionary dictionary];
    for (NSString *ln in [txt componentsSeparatedByString:@"\n"]) {   // "<key>\t<stage>" per line, # comments
        if (!ln.length || [ln hasPrefix:@"#"]) continue;
        NSArray *c = [ln componentsSeparatedByString:@"\t"];
        if (c.count >= 2) g_shmap[c[0]] = c[1];
        if (c.count >= 3 && [c[2] length] == 16) { if (!g_fmap) g_fmap = [NSMutableDictionary dictionary]; if (!g_fmap[c[2]]) g_fmap[c[2]] = c[0]; }
    }
    g_shmap_path = path;
    const char *m = getenv("DM_MSL_DIR");
    // Library directory = <DM_MSL_DIR or default cache>/b-<translation chain version>: the version comes from this source tree's dmsh_chain.h (tools/dmsh_chain.py); a host built from an old source tree only accepts libraries of the old version
    g_msl_dir = [NSString stringWithFormat:@"%@/b-%s", m ? @(m) : [@(getenv("DM_ROOT") ?: getenv("HOME")) stringByAppendingPathComponent:getenv("DM_ROOT") ? @"build/cache/metal_shader_cache" : @"dm-work/build/cache/metal_shader_cache"], DMSH_CHAIN];
    NSString *tw = [NSString stringWithContentsOfFile:[g_msl_dir stringByAppendingPathComponent:@"twins.tsv"] encoding:NSUTF8StringEncoding error:nil];
    for (NSString *ln in tw ? [tw componentsSeparatedByString:@"\n"] : @[]) {
        NSArray *c = ln.length && ![ln hasPrefix:@"#"] ? [ln componentsSeparatedByString:@"\t"] : nil;
        if (c.count == 2) { if (!g_twins) g_twins = [NSMutableDictionary dictionary]; g_twins[c[0]] = c[1]; }
    }
    if (![NSFileManager.defaultManager fileExistsAtPath:g_msl_dir]) MLOG("[red] shader library directory %s does not exist: translation chain version %s of this source tree has not been packaged yet (gfx/tools/dmsh_build.py)", g_msl_dir.UTF8String, DMSH_CHAIN);
    NSString *cp = [[path stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"c1_full.tsv"];   // Same directory as the mapping table (Mac: gfx/nvn; iOS: dm/ inside the App, copied along by run.sh)
    NSString *ct = cp ? [NSString stringWithContentsOfFile:cp encoding:NSUTF8StringEncoding error:nil] : nil;
    NSString *head = ct ? [[ct componentsSeparatedByString:@"\n"] firstObject] : nil;
    NSString *want = [NSString stringWithFormat:@"# chain b-%s", DMSH_CHAIN];
    if (!head || ![head hasPrefix:want]) {
        MLOG("[red] c1_full.tsv list is stale or missing, every c1 gets an exclusive 64 KiB");
    } else {
        g_c1_full_valid = 1; g_c1_full = [NSMutableSet set];
        for (NSString *line in [ct componentsSeparatedByString:@"\n"]) if (line.length && ![line hasPrefix:@"#"]) [g_c1_full addObject:[line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]];
    }
    MLOG("shader route B (translated from the original Switch shaders): map has %lu entries (%s), metallib directory %s%s", (unsigned long)g_shmap.count, path.UTF8String, g_msl_dir.UTF8String,
         g_twins.count ? [NSString stringWithFormat:@" (%lu libraries packed once, twins.tsv)", (unsigned long)g_twins.count].UTF8String : "");
    warm_start();
}

@interface NvShader : NSObject { @public id<MTLFunction> fn; nvn_refl rf; id<MTLArgumentEncoder> ae; NSUInteger arg_len; int stage; char key[24]; id<MTLBuffer> c1buf; uint64_t c1_gpu; size_t c1off;
    id<MTLLibrary> vi_lib; NSString *vi_entry; uint32_t vi_mask; NSMutableDictionary<NSNumber *, id<MTLFunction>> *vi_fns;   // integer vertex input variant (dmsh_build.py vi_variant): locations that can be specialized
    // The argument buffer encoded last time
    id<MTLBuffer> ab_buf; uint64_t ab_gpu; size_t ab_off; uint64_t ab_pass;   // ab_pass: serial of the render pass that encoded this argument block - reused only within the same pass (the arena holding a block from another pass / another submission may already have been reclaimed and rewritten)
    id<MTLLibrary> bflib; int bflib_tried; NSMutableDictionary<NSString *, id<MTLFunction>> *bffn;   // bflib: readable-tile version <key>_bf (merge B step 2)
    id<MTLLibrary> alib; NSMutableDictionary<NSNumber *, id<MTLFunction>> *afn;
    NSMutableDictionary<NSString *, NvShader *> *bdv;
    id<MTLFunction> ifn; int ifn_tried; } @end   // ifn: the integer-output variant <key>_i_b (fout_fn, plan item 162)   // bdv: variants that return a sampler's border colour Metal lacks (border_variant, plan item 161)   // alib: a fragment shader with the alpha test epilogue (dmsh_build.py alpha_test); afn: its specializations
@implementation NvShader @end
#include "../nvn_pso_skey.h"
#include "../nvn_warm_list.h"
#include "../nvn_aniso.h"
#include "../nvn_retry.h"
@interface NvProg : NSObject { @public NvShader *sh[6]; int sum_ok, sum_img, sum_imgw; uint32_t sum_cmask; } @end   // sum_*: udc_note_draw's per-program summary
@implementation NvProg @end
// What udc_note_draw needs of a program, worked out once (upstream d2r-mobile 44a6770 caches it per program): any image binding, any writable one,
// the pixel shader's colour outputs. Its shaders are fixed once the program is built, so the summary never goes stale
static void prog_summary(NvProg *pr) {
    if (pr->sum_ok) return;
    for (int st = 0; st < 6; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (!sh) continue;
        if (st == 1) pr->sum_cmask = sh->rf.color_mask;
        for (int i = 0; i < sh->rf.nb; i++) if (sh->rf.b[i].kind == NVN_RB_IMG) { pr->sum_img = 1; if (sh->rf.b[i].rw) pr->sum_imgw = 1; } }
    pr->sum_ok = 1;
}
// Warm-up entry: built in the background, waiting to be taken by the submit thread. vs / fs are strong references (in pso they are only raw pointers, and the background dictionary is released when the task ends)
@interface WarmItem : NSObject { @public int kind; NSString *key, *vs_key, *fs_key; NvShader *shader, *vs, *fs; pso_key pso; id<MTLFunction> fsfn; id object; } @end
@implementation WarmItem @end
static NSMutableDictionary<NSString *, NvShader *> *g_shcache;

static void c1_store(NvShader *o) {
    size_t len = o->rf.c1_len;
    NSString *guardKey = [NSString stringWithUTF8String:o->key];
    if ([guardKey hasSuffix:@"_b"]) guardKey = [guardKey substringToIndex:guardKey.length - 2];
    os_unfair_lock_lock(&g_c1_lock);   // Both the warm-up thread and the submit thread load shaders
    if (!g_c1_full_valid || [g_c1_full containsObject:guardKey]) {   // Runtime index not proven bounded: exclusive 64 KiB of all zeros (same as the original implementation)
        o->c1buf = [g_dev newBufferWithLength:C1_DECL_SIZE options:MTLResourceStorageModeShared]; o->c1_gpu = o->c1buf.gpuAddress;
        if (!o->c1buf) { os_unfair_lock_unlock(&g_c1_lock); MDIE("c1 exclusive buffer allocation failed"); }
        res_add(o->c1buf);
        memset(o->c1buf.contents, 0, C1_DECL_SIZE); memcpy(o->c1buf.contents, o->rf.c1, len);
        g_c1_alloc_bytes += C1_DECL_SIZE; g_c1_data_bytes += len; g_c1_count++; g_c1_exclusive++;
        os_unfair_lock_unlock(&g_c1_lock);
        return;
    }
    size_t need = (len + 255u) & ~((size_t)255u);
    if (!g_c1_banks) g_c1_banks = [NSMutableArray array];
    if (!g_c1_banks.count || g_c1_bank_off + need + 256u + C1_DECL_SIZE > C1_BANK_SIZE) {
        id<MTLBuffer> b = [g_dev newBufferWithLength:C1_BANK_SIZE options:MTLResourceStorageModeShared];
        if (!b) { os_unfair_lock_unlock(&g_c1_lock); MDIE("c1 buffer block allocation failed"); }
        res_add(b);
        memset(b.contents, 0, C1_BANK_SIZE);
        [g_c1_banks addObject:b];
        g_c1_bank_off = 0;
        g_c1_alloc_bytes += C1_BANK_SIZE;
    }
    id<MTLBuffer> b = g_c1_banks.lastObject;
    size_t off = g_c1_bank_off;
    memcpy((uint8_t *)b.contents + off, o->rf.c1, len);
    g_c1_bank_off += need + 256u;
    g_c1_data_bytes += len;
    g_c1_count++;
    o->c1buf = b;
    o->c1_gpu = b.gpuAddress;
    o->c1off = off;
    os_unfair_lock_unlock(&g_c1_lock);
}

static NSMutableArray<WarmItem *> *g_warm_items;
static os_unfair_lock g_warm_lock = OS_UNFAIR_LOCK_INIT;
static _Atomic int g_warm_ready;
static int g_warm_started;
static uint64_t g_warm_t0;
static _Atomic int g_warm_running;   // Background warm-up has not finished yet (the loading screen on entering a run waits a little longer based on this, see mtl_warm_wait)
static uint64_t g_warm_shaders, g_warm_rps, g_warm_cps, g_warm_skip_fz, g_warm_old_rows, g_warm_folded;
static uint64_t g_warm_claim_shaders, g_warm_claim_rps, g_warm_claim_cps;
static id<MTLFunction> bf_fn(NvShader *fs, const int *fx, uint8_t fbmask, const uint8_t *lmap);
static id<MTLFunction> alpha_plain(id<MTLLibrary> lib, NSString *entry);
static FILE *g_pso_record; static int g_pso_record_checked;
static FILE *pso_record_file(void) {
    if (g_pso_record_checked) return g_pso_record;
    g_pso_record_checked = 1;
    const char *path = getenv("DM_PSO_RECORD");
    if (path && *path) g_pso_record = fopen(path, "a");
    return g_pso_record;
}
static void pso_record(const char *fmt, ...) {
    FILE *f = pso_record_file();
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fflush(f);
}
// User manifest: shaders / pipelines created synchronously on the submit thread while running on this machine, warmed up at the next launch together with the seed manifest.
// Path = DM_PSO_USER, or pso_user.tsv under the parent of the save directory (DM_SAVE, Documents/save on iOS); tests use a fresh save copy per run, so they do not pollute each other
static NSString *pso_user_path(void) {
    const char *u = getenv("DM_PSO_USER"), *save = getenv("DM_SAVE");
    if (u && *u) return @(u);
    return save && *save ? [[@(save) stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"pso_user.tsv"] : nil;
}
static FILE *g_pso_user; static int g_pso_user_checked;
static NSMutableSet<NSString *> *g_pso_user_seen;
static FILE *pso_user_file(void) {
    if (g_pso_user_checked) return g_pso_user;
    g_pso_user_checked = 1;
    NSString *path = pso_user_path();
    if (path) g_pso_user = fopen(path.fileSystemRepresentation, "a");
    return g_pso_user;
}
// The line is written on a serial queue of its own (plan item 246, after AnySwitch c483c04, MIT): the first one read the seed and the player's list
// (6,365 rows for Tears of the Kingdom) into a set, and every new one flushed the file, on the submit / translation thread
static dispatch_queue_t pso_user_queue(void) {
    static dispatch_queue_t q; static dispatch_once_t once;
    dispatch_once(&once, ^{ q = dispatch_queue_create("dm.pso_user", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0)); });
    return q;
}
void mtl_pso_user_sync(void) { dispatch_sync(pso_user_queue(), ^{}); }   // (tests: the lines queued so far are written)
static void pso_user_write(const char *line);
static void pso_user_record(const char *fmt, ...) {
    char line[8192]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
#ifndef DM_TEST_PSO_USER_INLINE   // negative control of t_psouser only: written on the calling thread, as before
    char *copy = strdup(line); if (!copy) return;
    dispatch_async(pso_user_queue(), ^{ pso_user_write(copy); free(copy); });
#else
    pso_user_write(line);
#endif
}
static void pso_user_write(const char *line) {
    FILE *f = pso_user_file();
    if (!f) return;
    if (!g_pso_user_seen) {
        g_pso_user_seen = [NSMutableSet set];
        NSString *seed = g_shmap_path ? [g_shmap_path.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"pso_manifest.tsv"] : nil;
        NSMutableArray<NSString *> *paths = [NSMutableArray array]; if (seed) [paths addObject:seed]; NSString *user = pso_user_path(); if (user) [paths addObject:user];
        for (NSString *path in paths) { NSString *txt = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
            for (NSString *old in [txt componentsSeparatedByString:@"\n"]) if (old.length && ![old hasPrefix:@"#"]) [g_pso_user_seen addObject:old]; }
    }
    size_t ll = strlen(line); NSString *s = [[NSString alloc] initWithBytes:line length:ll && line[ll - 1] == '\n' ? ll - 1 : ll encoding:NSUTF8StringEncoding];   // Compared with the manifest file line by line (without the newline)
    if (!s || [g_pso_user_seen containsObject:s]) return;
    [g_pso_user_seen addObject:s]; fputs(line, f); fflush(f);
}
// Build the library from a .metallib file without permanently occupying a file descriptor: newLibraryWithURL keeps that file open for as long as the library object lives (one per game shader - of the 258 descriptors the iOS app permanently holds in a run, 213 are these,
// while the default soft limit of an iOS process is 256; new shaders created in other acts raise it further). Here the file is mapped into memory and closed immediately, and the library is built from the in-memory data; the mapping is released when the library's data object is freed
static id<MTLLibrary> lib_from_file(NSString *path, NSError **err) {
    int fd = open(path.fileSystemRepresentation, O_RDONLY); struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_size <= 0) { if (fd >= 0) close(fd); if (err) *err = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"cannot open %@", path]}]; return nil; }
    size_t len = (size_t)st.st_size; void *m = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
    if (m == MAP_FAILED) { if (err) *err = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"mmap failed %@", path]}]; return nil; }
    // iOS (DM_NVN_LIB_COPY=1 elsewhere, for t_libcopy): the library is copied into memory and the mapping dropped at once, since a mapping holds a vnode
    // as long as the library lives and the device has one table for everything (MacNativeGames e099353, MIT: about 8,800 of Tears of the Kingdom's
    // libraries kept by its warm-up filled an A16 iPad's 11,400 and jetsam killed the game, reason vnode-limit; all of them are about 110 MB)
    static int copy = -1;
#if TARGET_OS_IPHONE
    if (copy < 0) copy = 1;
#else
    if (copy < 0) { const char *e = getenv("DM_NVN_LIB_COPY"); copy = e && atoi(e); }
#endif
    if (copy) { void *c = malloc(len); if (c) { memcpy(c, m, len); munmap(m, len); return [g_dev newLibraryWithData:dispatch_data_create(c, len, NULL, DISPATCH_DATA_DESTRUCTOR_FREE) error:err]; } }
    dispatch_data_t d = dispatch_data_create(m, len, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ munmap(m, len); });
    return [g_dev newLibraryWithData:d error:err];
}
static NvShader *shader_load(NSString *pc, int stage) {
    NSError *e = nil;
    NSString *lp = msl_lib_path([pc stringByAppendingString:@".metallib"]);
    id<MTLLibrary> lib = lib_from_file(lp, &e);
    NSData *jd = [NSData dataWithContentsOfFile:[NSString stringWithFormat:@"%@/%@.json", g_msl_dir, pc]];
    NSDictionary *j = jd ? [NSJSONSerialization JSONObjectWithData:jd options:0 error:nil] : nil;
    if (!lib || !j) { MLOG("[red] failed to load shader %s: %s", pc.UTF8String, e ? e.localizedDescription.UTF8String : "no reflection json"); return nil; }
    if (![j[@"chain"] isEqual:@DMSH_CHAIN]) { MLOG("[red] shader %s translation chain version %s does not match the %s expected by this host", pc.UTF8String, [[j[@"chain"] description] UTF8String], DMSH_CHAIN); return nil; }
    NvShader *o = [NvShader new];
    char err[160];
    if (nvn_refl_from_json(j, &o->rf, err, sizeof err)) { MLOG("[red] shader %s reflection conversion failed: %s", pc.UTF8String, err); return nil; }
    o->fn = [lib newFunctionWithName:j[@"entry"]]; o->stage = stage; strlcpy(o->key, pc.UTF8String, sizeof o->key);
    if (stage == 1 && [j[@"alpha_test"] boolValue]) { o->alib = lib; o->fn = alpha_plain(lib, j[@"entry"]); }   // the alpha test epilogue: specialized with ALWAYS (alpha_fn)
    if (!o->fn) { MLOG("[red] shader %s: no function %s", pc.UTF8String, [[j[@"entry"] description] UTF8String]); return nil; }
    if ([j[@"vi_entry"] isKindOfClass:NSString.class]) { o->vi_lib = lib; o->vi_entry = j[@"vi_entry"]; for (NSNumber *l in j[@"vi_int"]) if (l.intValue >= 0 && l.intValue < 16) o->vi_mask |= 1u << l.intValue; }
    if (o->rf.nb) {
        o->ae = [o->fn newArgumentEncoderWithBufferIndex:o->rf.abi];
        int max_aid = -1;
        for (int i = 0; i < o->rf.nb; i++) max_aid = MAX(max_aid, o->rf.b[i].aid);
        o->arg_len = o->ae.encodedLength;
        if (o->arg_len != (NSUInteger)(8 * (max_aid + 1)) || (o->arg_len & 7))
            MDIE("shader %s argument buffer layout anomaly: encodedLength=%lu maxAid=%d", o->key, (unsigned long)o->arg_len, max_aid);
    }
    if (o->rf.has_c1) c1_store(o);
    return o;
}

static int stage_of(NSString *st) {   // NVN shader stage of a shader map entry
    if ([st isEqualToString:@"vs"]) return 0; if ([st isEqualToString:@"ps"]) return 1; if ([st isEqualToString:@"cs"]) return 5;
    if ([st isEqualToString:@"hs"]) return 3; if ([st isEqualToString:@"ds"]) return 4; return -1;
}

// Programs' functional keys by serial (plan item 169; a side table: prog_t cannot grow, nvn_obj.h): set at SetShaders, dropped with the program
static nvn_hmap g_pfk = {.klen = 8}; static pthread_mutex_t g_pfk_mu = PTHREAD_MUTEX_INITIALIZER;
void mtl_prog_fkeys(uint64_t serial, const uint8_t fk[6][8]) {
    static const uint8_t zero[6][8]; if (!serial || !memcmp(fk, zero, sizeof zero)) return;
    uint8_t *v = malloc(48); if (!v) return; memcpy(v, fk, 48);
    pthread_mutex_lock(&g_pfk_mu); uint8_t *old = nvn_hm_get(&g_pfk, &serial); if (old) { nvn_hm_del(&g_pfk, &serial); free(old); } nvn_hm_put(&g_pfk, &serial, v); pthread_mutex_unlock(&g_pfk_mu);
}
static void prog_fkey(uint64_t serial, unsigned stage, uint8_t out[8]) {
    memset(out, 0, 8); if (stage >= 6) return;
    pthread_mutex_lock(&g_pfk_mu); const uint8_t *v = nvn_hm_get(&g_pfk, &serial); if (v) memcpy(out, v + 8 * stage, 8); pthread_mutex_unlock(&g_pfk_mu);
}
static void prog_fkey_drop(uint64_t serial) { pthread_mutex_lock(&g_pfk_mu); uint8_t *v = nvn_hm_get(&g_pfk, &serial); if (v) { nvn_hm_del(&g_pfk, &serial); free(v); } pthread_mutex_unlock(&g_pfk_mu); }
// The map's key for a shader: its own, else (plan item 169) the first key of the map with its functional key, the shader the game also ships under another
// key with other metadata in its control section (the same translation). log: the red line for a key that has neither, once per key, and a line for
// each twin used
static NSString *shmap_resolve(NSString *k, const uint8_t fk[8], int log) {
    if (g_shmap[k]) return k;
    static NSMutableSet<NSString *> *told; if (log && !told) told = [NSMutableSet set];
    uint64_t z = 0; NSString *t = nil;
#ifndef DM_TEST_SHMAP_NO_FKEY   // negative control of t_shmap only: no fallback
    if (memcmp(fk, &z, 8)) t = g_fmap[[NSString stringWithFormat:@"%02x%02x%02x%02x%02x%02x%02x%02x", fk[0], fk[1], fk[2], fk[3], fk[4], fk[5], fk[6], fk[7]]];
#endif
    if (log && ![told containsObject:k]) { [told addObject:k];
        if (t) MLOG("shader %s is not in the shader map: its functional twin %s is used (the same code, constants and stage)", k.UTF8String, t.UTF8String);
        else MLOG("[red] shader %s is not in the shader map", k.UTF8String); }
    return t;
}
static NvShader *shader_get(NSString *pc, int stage) {
    if (!g_shcache) g_shcache = [NSMutableDictionary dictionary];
    NvShader *o = g_shcache[pc];
    if (o) return o;
    uint64_t bt = bp_now(); o = shader_load(pc, stage);
    if (!o) return nil;
    g_shcache[pc] = o;
    pso_record("S\t%s\t%d\n", pc.UTF8String, stage);
    pso_user_record("S\t%s\t%d\n", pc.UTF8String, stage);
    g_bp.lib_n++; g_bp.lib_ns += bp_now() - bt;
    return o;
}
static __unsafe_unretained NvProg *prog_get(const prog_t *cp) {   // Called only on the submit thread
    prog_t *p = (prog_t *)cp;
    static const prog_t *lp; static uint64_t lser, lgen; static NvProg *lpr;   // Last result: same object, same serial and unchanged serial table means it is used directly
    uint64_t gen = atomic_load_explicit(&g_ser_gen, memory_order_acquire);
    if (p == lp && p->serial == lser && lpr && (gen == lgen || ser_unaffected(lgen, gen, lser))) { lgen = gen; return lpr; }   // steps for other serials leave it
    __unsafe_unretained NvProg *have = p->serial ? serial_get(p->serial) : nil;
    if (have) { lp = p; lser = p->serial; lgen = gen; lpr = have; return have; }
    shmap_load();
    NvProg *o = [NvProg new];
    for (unsigned i = 0; i < p->nstage; i++) {
        const uint8_t *md = p->key[i];   // shader key computed at SetShaders (the game may already have reclaimed the control section memory by now)
        uint8_t fk[8]; prog_fkey(p->serial, i, fk);
        NSString *k = shmap_resolve([NSString stringWithFormat:@"%02x%02x%02x%02x%02x%02x%02x%02x", md[0], md[1], md[2], md[3], md[4], md[5], md[6], md[7]], fk, 1);
        NSString *zh = k ? g_shmap[k] : nil;
        if (!zh) continue;
        int st = stage_of(zh);
        NvShader *sh = st >= 0 ? shader_get([k stringByAppendingString:@"_b"], st) : nil;
        if (sh) o->sh[st] = sh; else MLOG("[red] shader %s (%s) failed to load: run gfx/tools/dmsh_build.py to package first", k.UTF8String, zh.UTF8String);
    }
    if (p->serial) serial_put(p->serial, o);
    return o;
}
// A program for the pipeline prediction scan, without loading anything there: resolved already, or every shader of it in the library cache; else
// nil (a program not met yet would have its libraries read from disk on the submit thread, for a guess)
static __unsafe_unretained NvProg *prog_peek(const prog_t *p) {
    if (!p) return nil;
    __unsafe_unretained NvProg *have = p->serial ? serial_get(p->serial) : nil;
    if (have) return have;
    shmap_load();
    for (unsigned i = 0; i < p->nstage; i++) {
        const uint8_t *md = p->key[i];
        uint8_t fk[8]; prog_fkey(p->serial, i, fk);
        NSString *k = shmap_resolve([NSString stringWithFormat:@"%02x%02x%02x%02x%02x%02x%02x%02x", md[0], md[1], md[2], md[3], md[4], md[5], md[6], md[7]], fk, 0);
        if (!k || !g_shcache[[k stringByAppendingString:@"_b"]]) return nil;
    }
    return prog_get(p);
}
void mtl_prog_release(prog_t *p) { @autoreleasepool { if (p->serial) { serial_del(p->serial); prog_fkey_drop(p->serial); } } }

