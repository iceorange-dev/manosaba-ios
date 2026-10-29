// gfx/nvn/hle/slots.inc, part of hle_nvn.c (included there in order, one translation unit): The slot table, thunks and unimplemented stubs; boolean parameters
// ---------- Slot table, thunks, unimplemented stubs ----------
typedef struct { _Atomic uint64_t cnt; void *fn; } nvn_slot_t;
nvn_slot_t nvn_slot[NVN_NFUNC];                 // The thunk accesses it by i*16; the layout must not change
_Static_assert(sizeof(nvn_slot_t) == 16, "the thunk addresses slots with a 16-byte stride");
extern void *nvn_thunk[NVN_NFUNC];
extern const char *nvn_names[NVN_NFUNC];
extern void nvn_unimpl_entry(void);
extern void nvn_prof_entry(void);
void *nvn_real[NVN_NFUNC];               // DM_NVN_PROF: the real implementation called by the timing wrapper
_Atomic uint64_t nvn_prof_ticks[NVN_NFUNC]; // DM_NVN_PROF: cumulative time of each function (cntvct counts)
static uint8_t g_unimpl_seen[NVN_NFUNC];

uint64_t nvn_unimpl_c(nvn_slot_t *slot, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t lr, uint64_t x8) {
    unsigned i = (unsigned)(slot - nvn_slot);
    if (!g_unimpl_seen[i]) {   // Report once (set to 1, no increment: a uint8 increment wraps and re-reports after 256 calls)
        g_unimpl_seen[i] = 1;
        module_t *m = module_of((void *)lr);
        LOG("[red] unimplemented NVN function called: %s (x0=%#llx x1=%#llx x2=%#llx x8=%#llx) call site %s+%#llx", nvn_names[i], x0, x1, x2, x8,
            m ? m->name : "?", m ? lr - (uint64_t)m->base : lr);
    }
    return 0;
}

typedef struct { const char *name; void *fn; } nvn_impl;
#define NVN(name, ret, ...)                                                                       \
    static ret h_##name(__VA_ARGS__);                                                             \
    static const NVNT_##name chk_##name __attribute__((unused)) = h_##name;                       \
    DM_TABLE(swnvn) static const nvn_impl e_##name = {#name, (void *)h_##name}; \
    static ret h_##name(__VA_ARGS__)
// Newer interfaces without prototypes: no signature check
#define NVN_NOPROTO(name, ret, ...)                                                               \
    static ret h_##name(__VA_ARGS__);                                                             \
    DM_TABLE(swnvn) static const nvn_impl e_##name = {#name, (void *)h_##name}; \
    static ret h_##name(__VA_ARGS__)

static int g_log;
uint64_t nvn_new_serial(void) { static _Atomic uint64_t n; return ++n; }
static unsigned g_nsem;                          // Number of semantic implementations (filled by nvn_init_slots)
#define OLOG(...) do { if (g_log) LOG("[nvn] " __VA_ARGS__); } while (0)

static void nvn_init_slots(void) {
    static int done; if (done) return; done = 1;
    g_log = getenv("DM_NVN_LOG") != NULL;
    unsigned long sz;
    const nvn_impl *im = (const nvn_impl *)DM_TABLE_GET(swnvn, &sz);
    unsigned nim = im ? (unsigned)(sz / sizeof *im) : 0, hit = 0;
    for (unsigned i = 0; i < NVN_NFUNC; i++) nvn_slot[i].fn = (void *)nvn_unimpl_entry;
    for (unsigned k = 0; k < nim; k++) {
        unsigned i = 0;
        while (i < NVN_NFUNC && strcmp(nvn_names[i], im[k].name)) i++;
        if (i == NVN_NFUNC) die("NVN implementation %s is not in main's name table", im[k].name);
        nvn_slot[i].fn = im[k].fn; hit++;
    }
    g_nsem = hit;
}

static void *nvn_lookup(const char *name) {
    nvn_init_slots();
    for (unsigned i = 0; i < NVN_NFUNC; i++)
        if (!strcmp(nvn_names[i], name)) return nvn_thunk[i];
    // Not a failure: the game looks up names this host's table does not have (manosaba: 13 newer ones, e.g. the *Deferred binds, each looked up
    // twice at startup) and copes with NULL. Logged once per name, not red; a call to a name the table has but the host does not implement is red
    // (nvn_unimpl_c)
    static char seen[64][64]; static int nseen; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    int again = 0; for (int i = 0; i < nseen; i++) if (name && !strcmp(seen[i], name)) again = 1;
    if (!again && name && nseen < 64) snprintf(seen[nseen++], sizeof seen[0], "%s", name);
    pthread_mutex_unlock(&mu);
    if (!again) LOG("[nvn] address lookup of %s: not in this host's NVN table, answered NULL", name ? name : "(null)");
    return NULL;
}

// nvnBootstrapLoader is imported by main (defined in sdk); after it is forcibly taken over, the NVN driver in sdk is no longer referenced
HLE_FORCE("nvnBootstrapLoader", "the whole NVN graphics interface is replaced by the host null device (backed by Metal); the sdk NVN driver does not run",
          void *, h_nvnBootstrapLoader, const char *name) { return nvn_lookup(name); }

// ---------- Boolean parameters: Switch callers do not extend them, only look at the low 8 bits ----------
#define B(x) ((x) & 0xff)

#include "../nvn_obj.h"
#include "../nvn_hmap.h"

