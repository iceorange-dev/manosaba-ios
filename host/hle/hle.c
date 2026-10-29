// Import resolution (implemented ones go to the host, the rest go to named stubs) and host gate dispatch.
#include "dm.h"
#include "table.h"
#include <dlfcn.h>
#include <stdatomic.h>
#include <time.h>

typedef struct { const char *name; void *fn; int direct; } hle_ent;   // = hle.h

// ---------- Unimplemented imports: named stubs (the statically assembled stub table in entry.S, the k-th = movz x9,#k; b unimpl_entry) ----------
#define MAX_STUB 4096                 // Matches the .rept count in entry.S
static unsigned g_nstub;
static const char *g_stub_name[MAX_STUB];
static uint8_t g_stub_seen[MAX_STUB];
extern uint64_t unimpl_stubs[MAX_STUB];   // 8 bytes per stub

uint64_t unimpl_c(unsigned idx, uint64_t *regs) {
    if (!g_stub_seen[idx]++) {   // red with its caller: what the next game would otherwise hit silently (bayonetta3-mobile's DM_UNIMPL_ABORT shows the call site too)
        uint64_t ra = DM_UNIMPL_GUEST_RA(); module_t *m = module_of((void *)ra);
        LOG("[red] UNIMPL %s (x0=%#llx x1=%#llx x2=%#llx), called from %s+%#llx: returns 0", g_stub_name[idx], regs[0], regs[1], regs[2], m ? m->name : "?", m ? ra - (uint64_t)m->base : ra);
    }
    return 0;
}

// weak: undefined weak symbol (STB_WEAK). The real rtld fills in 0 when no definition is found, and callers skip on that (e.g. the thread_local init function _ZTH*); the host fills in 0 too and allocates no stub.
uint64_t resolve_import(module_t *m, const char *name, int weak) {
    static const hle_ent *sec; static unsigned long secsz;
    if (!sec) sec = (const hle_ent *)DM_TABLE_GET(swhle, &secsz);
    for (unsigned i = 0; i < secsz / sizeof(hle_ent); i++)
        if (!strcmp(sec[i].name, name)) {
            if (g_flag.imports) { Dl_info di; dladdr(sec[i].fn, &di); LOG("import %s -> %s", name, di.dli_sname); }
            // On the thread's host stack (hstack.S), except the libc functions hle_table.c calls direct; with DM_HLECOUNT a counting stub in front
            void *hlecount_wrap(const char *, const char *, void *); return (uint64_t)hlecount_wrap("HLE", name, sec[i].direct ? sec[i].fn : dm_hwrap(sec[i].fn));
        }
    if (weak) { if (g_flag.imports) LOG("import %s: undefined weak symbol, 0", name); return 0; }
    // Reuse the stub if one was already allocated for the same name
    void *hlecount_wrap(const char *, const char *, void *);
    for (unsigned k = 0; k < g_nstub; k++)
        if (!strcmp(g_stub_name[k], name)) return (uint64_t)hlecount_wrap("STUB", name, (void *)&unimpl_stubs[k]);
    if (g_nstub >= MAX_STUB) die("stub overflow");
    unsigned k = g_nstub++;
    g_stub_name[k] = name;
    (void)m;
    return (uint64_t)hlecount_wrap("STUB", name, (void *)&unimpl_stubs[k]);
}

void stubs_report(void) { LOG("stubs=%u", g_nstub); }

// ---------- Host gate dispatch ----------
// The rewriter replaces every svc #n / mrs cntpct with a call to dm_gate; here we remember, by gate block return point (absolute address), what it originally was.
// Lookup: open-addressing hash, one hit (it used to do module_of + a linear compare over 110 entries on every gate call, keyed by in-module offset, which would collide across modules).
enum { G_SVC, G_CNTPCT, G_CNTVCT, G_OBS };
// Observation gates (inserted offline from the rewriter's OBS table): number → read-only callback; with no callback attached the gate returns after one pass in and out
static void (*g_obs_fn[OBS_MAX])(const uint64_t *regs); static int g_obs_have[OBS_MAX];
void gate_obs_set(int id, void (*fn)(const uint64_t *regs)) { if (id <= 0 || id >= OBS_MAX) die("observation gate number out of range: %d", id); g_obs_fn[id] = fn; }
int gate_obs_present(int id) { return id > 0 && id < OBS_MAX && g_obs_have[id]; }
static uint8_t g_svc_have[256];   // svc numbers registered in the build output (used to tell whether the host call numbers from 0x80 up, introduced by cuts, are present)
int gate_svc_present(int num) { return num >= 0 && num < 256 && g_svc_have[num]; }
typedef struct { uint64_t ret; int kind, arg; uint64_t orig; } gate_t;
#define GATE_BITS 12                     // 4096 slots, gate count ≤ 1024, load factor ≤ 1/4
static gate_t g_gate[1u << GATE_BITS];
static int g_ngate;
static inline unsigned gate_h(uint64_t a) { return (unsigned)(((a >> 2) * 0x9E3779B97F4A7C15ull) >> (64 - GATE_BITS)); }

void gate_register(uint64_t ret_abs, const char *kind, int arg, uint64_t orig) {
    int k = !strcmp(kind, "svc") ? G_SVC : !strcmp(kind, "cntpct") ? G_CNTPCT : !strcmp(kind, "cntvct") ? G_CNTVCT : !strcmp(kind, "obs") ? G_OBS : -1;
    if (k < 0) die("unknown gate kind %s", kind);
    if (k == G_OBS) { if (arg <= 0 || arg >= OBS_MAX) die("observation gate number out of range: %d", arg); g_obs_have[arg] = 1; }
    if (k == G_SVC && arg >= 0 && arg < 256) g_svc_have[arg] = 1;
    if (++g_ngate > (1 << GATE_BITS) / 4) die("gate overflow");
    unsigned i = gate_h(ret_abs);
    while (g_gate[i].ret) { if (g_gate[i].ret == ret_abs) die("gate registered twice: %#llx", ret_abs); i = (i + 1) & ((1u << GATE_BITS) - 1); }
    g_gate[i] = (gate_t){ret_abs, k, arg, orig};
}

uint64_t svc_dispatch(int num, uint64_t *regs, uint64_t orig_addr);   // hle_svc.c

// Gate entries by kind when DM_NVN_COUNTS is set (plan item 45: how often the guest reads the system counter), for the power summary (hle_nvn.c)
static int g_gate_count_on = -1; static _Atomic uint64_t g_gate_n[4];
void dm_gate_counts(uint64_t out[4]) { for (int k = 0; k < 4; k++) out[k] = atomic_load_explicit(&g_gate_n[k], memory_order_relaxed); }
uint64_t gate_dispatch(uint64_t ret_addr, uint64_t *regs) {
    for (unsigned i = gate_h(ret_addr);; i = (i + 1) & ((1u << GATE_BITS) - 1)) {
        const gate_t *g = &g_gate[i];
        if (g->ret == ret_addr && g_gate_count_on) {
            if (g_gate_count_on < 0) g_gate_count_on = getenv("DM_NVN_COUNTS") && strcmp(getenv("DM_NVN_COUNTS"), "0");
            if (g_gate_count_on) atomic_fetch_add_explicit(&g_gate_n[g->kind & 3], 1, memory_order_relaxed);
        }
        if (g->ret == ret_addr) switch (g->kind) {
            case G_CNTPCT: case G_CNTVCT: {
                uint64_t t = guest_ns() * 12 / 625;    // 19.2 MHz system counter; original instruction mrs xN, cntpct_el0, arg = N
                regs[g->arg] = t;
                return t;
            }
            case G_OBS: if (g_obs_fn[g->arg]) g_obs_fn[g->arg](regs); return 0;   // Registers are read-only; the x0–x8 that dm_gate writes back are the original values
            default: {   // svc: registered on entry / exit (thread pausing treats it as "in the kernel" and stops it on the way out; hle_thread.c)
                void thr_svc_enter(uint64_t *regs, uint64_t pc); void thr_svc_exit(void);
                module_t *m = module_of((void *)ret_addr);
                thr_svc_enter(regs, m ? (uint64_t)m->base + g->orig : 0);
#ifndef DM_TEST_GATE_ORIG_REL   // negative control of t_gate_svc only: the offset in the module, as before
                uint64_t pc = m ? (uint64_t)m->base + g->orig : g->orig;   // the svc instruction's address (plan item 125: its offset alone, which
#else                                                                     // module_of cannot place, made every svc log line say "?")
                uint64_t pc = g->orig;
#endif
                uint64_t r = svc_dispatch(g->arg, regs, pc);
                thr_svc_exit();
                return r;
            }
        }
        if (!g->ret) die("unknown gate at %#llx", ret_addr);
    }
}
