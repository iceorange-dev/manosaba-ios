// Common header of the native Switch host (multi-module).
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MOD 8
#define LOG(...) do { fprintf(stderr, "[dm] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define die(...) do { LOG("FATAL " __VA_ARGS__); abort(); } while (0)

typedef struct { uint32_t st_name; uint8_t st_info, st_other; uint16_t st_shndx; uint64_t st_value, st_size; } Elf64_Sym;
typedef struct { int64_t d_tag; union { uint64_t d_val, d_ptr; } d_un; } Elf64_Dyn;
typedef struct { uint64_t r_offset, r_info; int64_t r_addend; } Elf64_Rela;
#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffff)
#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_ST_VISIBILITY(o) ((o) & 3)
enum { STB_LOCAL = 0, STB_WEAK = 2 };
enum { STV_DEFAULT = 0, SHN_COMMON = 0xfff2 };
enum { DT_INIT = 12, DT_RELA = 7, DT_RELASZ = 8, DT_JMPREL = 23, DT_PLTRELSZ = 2, DT_SYMTAB = 6, DT_STRTAB = 5,
       DT_INIT_ARRAY = 25, DT_INIT_ARRAYSZ = 27 };
enum { R_AARCH64_ABS64 = 257, R_AARCH64_GLOB_DAT = 1025, R_AARCH64_JUMP_SLOT = 1026, R_AARCH64_RELATIVE = 1027 };

// Run-time state of one NSO module
typedef struct {
    char name[16];
    uint8_t *base;             // Start address of the image (including the patch area) in the host address space
    size_t size;
    uint64_t text_end, ro_off, data_off, patch_off, key_page_off, key_page_size;   // From the rewriter fixups header
    uint64_t rewriter;         // the rewriter stamp in the fixups header (tools/rewriter_stamp.py), 0 for products made before it
    uint64_t host_page_off;    // 16 KiB writable page the host fills for the game's HOSTREFS rules (fixups HOST_PAGE_OFF), 0 = none
    unsigned key_idx[8];
    const Elf64_Dyn *dyn;
    const Elf64_Sym *symtab;
    const char *strtab;
    unsigned nsym;
    uint32_t *symh; unsigned symh_mask;   // Name hash table of defined symbols (open addressing, stores symbol indices; for a duplicated name only the smallest index is kept = the first hit of a linear scan)
    uint64_t init, init_array, init_arraysz;   // init = DT_INIT (the module's _init: registers the DSO and runs this module's init_array)
} module_t;

extern module_t g_mod[MAX_MOD];
extern int g_nmod;
// Thread-local slots the rewritten guest code reads through the key page (the loader writes slot * 8 there; slots.h): K18 = the Switch's x18,
// KTLS = tpidr_el0, KTLR = tpidrro_el0 (ThreadLocalRegion), KGATE = host gate entry; g_keyparam = the four parameter slots of the rewriter's PARAMS
// rules (key page entries KPARAM0..3), for a game's own hooks; unused slots read 0
#include "slots.h"
extern dm_slot g_key18, g_keytls, g_keytlr, g_keygate;
extern dm_slot g_keyparam[4];

void load_modules(const char *dir, const char **names, int n);
const char *dm_main_build(void); // main's NSO build ID (16 bytes hex)
void run_module_inits(void);
void *module_sym(module_t *m, const char *name);
void *guest_sym(const char *name);
module_t *module_of(const void *p);
// The host page of a module (rewriter HOSTREFS rules: guest code reads values the host writes there), NULL when its product has none
void *dm_host_page(const char *module);
int dm_option_on(const char *name);   // a code option applied at load (options.c: DM_OPTIONS or the product's options.default)
// Gate dispatch: rewritten svc / timer reads come in through the common entry, ret_addr = the return point of that gate block, regs points to the saved x0..x30
uint64_t gate_dispatch(uint64_t ret_addr, uint64_t *regs);
// Observation gates: the rewriter's OBS rules (games/<id>/rules.py) number them; a game's hook code attaches a read-only callback, called on every hit.
// gate_obs_present = the product has that gate; gate_svc_present = the product has a gate for that svc number
enum { OBS_MAX = 32 };
void gate_obs_set(int id, void (*fn)(const uint64_t *regs));
int gate_obs_present(int id);
int gate_svc_present(int num);
uint64_t now_ns(void);
uint64_t guest_ns(void);              // Guest clock (hle_os.c; does not advance while frozen)
int dm_detclock_enabled(void);
void dm_guest_clock_advanced(void);
void guest_clock_freeze(int on);
void guest_clock_freeze_for(int reason, int on);   // Freeze by reason (1 background, 2 system keyboard); see hle_os.c
int guest_clock_frozen_for(int reason);           // frozen for that reason now (the watchdog does not count the background)
void dm_guest_frame_tick(void);        // Called on every present (hle_nvn.c): when DM_DETCLOCK is on, advances the guest clock by one fixed step
uint64_t dm_nvn_frame_count(void);
uint64_t dm_nvn_sub_cpu_ns(void);     // Cumulative CPU nanoseconds of submit (excluding present)
void dm_backtrace(uint64_t fp);
// Host calls on a per-thread host stack (hstack.S, hstack.c): dm_hwrap(fn) is what the guest gets in place of a host function (a wrapper that
// runs it on the thread's host stack; fn itself when DM_HSTACK=0); dm_hwrap_vt does it for every slot of a host vtable. DM_HREC = the record a
// host call or svc gate leaves at the top of the host stack (layout in hstack.S); dm_hstack_record finds it for a stopped thread
#define DM_HREC 192
void dm_hstack_init(void);
void *dm_hwrap(void *fn);
void *const *dm_hwrap_vt(void *const *vt, int n);
const uint64_t *dm_hstack_record(uint64_t tsd, uint64_t top);
// The guest return address of a host function the guest called (diagnostics): through the switch the function's own return address is the
// switch's, and the guest's is in the switch's frame record
extern char dm_hcall_ret[];
#define DM_GUEST_RA() ((uint64_t)__builtin_return_address(0) == (uint64_t)dm_hcall_ret \
                       ? ((const uint64_t *)*(const uint64_t *)__builtin_frame_address(0))[1] : (uint64_t)__builtin_return_address(0))
// The guest return address in unimpl_c, called from an unimplemented import's stub (entry.S): the stub's body keeps a frame record whose saved lr is
// dm_hcall_ret when it ran on the host stack (the guest's x29 / x30 are then in dm_hcall's record) or the guest's own with DM_HSTACK=0 (t_hstack 4)
#define DM_UNIMPL_GUEST_RA() ({ const uint64_t *b_ = (const uint64_t *)*(const uint64_t *)__builtin_frame_address(0); \
                                b_[1] == (uint64_t)dm_hcall_ret ? ((const uint64_t *)b_[0])[1] : b_[1]; })
// Frame pointer walks (crash.c, diag.c, hle_thread.c): frames go up the stack, except once, where a host call's record links the host stack to
// the guest's
static inline int dm_fp_step_ok(uint64_t fp, uint64_t nfp, int *jumps) { return nfp > fp || (nfp && !(*jumps)++); }
void crash_watch_progress(uint32_t tick, const char *stage);
void crash_watch_frame(void);
void crash_watch_pregame_done(void);
uint64_t crash_watch_progress_count(void);
void crash_dump_now(const char *why);   // Dump state on demand (threads + graphics wait points), without exiting
// Diagnostic switches: read once from environment variables when main starts (getenv takes the libc global environment lock, and on a hot path it would make all guest threads queue up)
extern uint64_t g_t0_ns;   // Host start time (now_ns), used for relative times in the log
extern struct dm_flags { int thr, svc, fs, movie, dbg, imports, bootprof; } g_flag;
// Boot timeline (DM_BOOTPROF=1, diag.c): segment marks + counters (nanoseconds / bytes, accumulated over all threads); PROF_GFX_* can be used for instrumentation
enum { PROF_FS_READ, PROF_WAIT_LOCK, PROF_WAIT_CV, PROF_WAIT_ADDR, PROF_WAIT_SYNC, PROF_SLEEP, PROF_MAIN_WAIT,
       PROF_GFX_PRESENT, PROF_GFX_SHADER, PROF_N };
void dm_prof_add(int id, uint64_t ns, uint64_t bytes);
void dm_prof_mark(const char *seg);
void *objmap_get(const void *guest); void objmap_put(const void *guest, void *host); void *objmap_del(const void *guest);
void hle_signal_system_event(void *g);   // hle_thread.c: signal a service-layer SystemEvent (any thread)
void hle_bind_system_event(void *g, int autoclear);   // On a guest thread, bind a guest SystemEvent to a kernel event
// Page fault hook / hook before the host writes guest memory (crash.c; used for write-protect dirty page tracking)
void dm_fault_hook_register(int (*fn)(void *addr, int is_write, void *uc));
void dm_yield_top(char *out, size_t n);
int dm_game_slot_acquire(const char *what);   // game_slot.c: one game at a time on the Mac (waits, or stops an agent's run for the player)   // hle_thread.c: the two threads that yielded most since the last call
void dm_guest_write_prep_register(void (*fn)(void *addr, size_t len));
void dm_guest_unmap_register(void (*fn)(void *addr, size_t len));   // hle_svc.c: called before the host gives guest memory back (UnmapPhysicalMemory, a heap shrink), the content going with it
void dm_res_report_register(void (*fn)(uint32_t tick, const char *why));   // Resource count callback (registered by the graphics layer): called every 250 ticks, when the loading screen has been dismissed, and on exit when the record is full
void (*dm_res_report_cb(void))(uint32_t tick, const char *why);   // Get the registered resource count callback (null if not registered)
void dm_guest_write_prepare(void *addr, size_t len);
// Graphics lifecycle (hle_nvn.c): on=1 suspends (afterwards submit / present block, and in-flight GPU work is drained before returning), on=0 resumes; called by the iOS front end when entering and leaving the background
void dm_gfx_suspend(int on);
void dm_gfx_shot_at(long k, const char *path);   // screenshot of the k-th frame after the current one (nvn_mtl.m)
void dm_shot_written(const char *path, int ok);   // the graphics side finished writing a screenshot (diag.c)
void dm_host_shutdown(const char *why);   // Graceful exit (crash.c): stop background threads and audio, drain graphics, then _exit(0); SIGTERM and the iOS app termination
