// HLE common: register host implementations by Switch mangled name.
// Note the differences between Apple AAPCS64 and Switch: Apple requires the caller to extend <32-bit arguments (bool/char/short), Switch code does not extend ⇒
// such parameters are always declared as int/unsigned and truncated by ourselves. Variadic arguments go on the stack on Apple and in registers on Switch ⇒ variadic functions cannot use C's "..." directly.
#pragma once
#include "dm.h"
#include "table.h"
typedef struct { const char *name; void *fn; int direct; } hle_ent;   // direct: called as it is, not on the host stack (hle_table.c)
#define HLE(mangled, ret, cname, ...)                                                             \
    static ret cname(__VA_ARGS__);                                                                \
    DM_TABLE(swhle) static const hle_ent hle_ent_##cname = {mangled, (void *)cname, 0}; \
    static ret cname(__VA_ARGS__)

typedef uint32_t Result;   // nn::Result: w0, 0 = success
#define RES_OK 0u
#define RES_FAIL 0x2A2u      // Generic failure (cut features such as networking uniformly return this)

// Guest address → host object side table
void *objmap_get(const void *guest);
void objmap_put(const void *guest, void *host);
void *objmap_del(const void *guest);
uint64_t now_ns(void);
uint64_t guest_ns(void);              // Guest clock (hle_os.c; does not advance while frozen)
int dm_detclock_enabled(void);
void dm_guest_clock_advanced(void);
uint64_t dm_guest_frame(void);        // Incremented each time the host presents a frame; unaffected by machine load
void guest_clock_freeze(int on);
void guest_clock_freeze_for(int reason, int on);   // Freeze by reason (1 background, 2 system keyboard); see hle_os.c
int dm_fs_host_file(const char *path, char *out, size_t n);   // Host file of an existing guest file path ("content:/Event/Movie/x.webm"), for host-side readers; 0 when it does not resolve (hle_fs.c)

// Forced takeover: even if the SDK module has a definition, all references to that symbol at load time (JUMP_SLOT/GLOB_DAT/ABS64) are resolved to the host implementation.
// Used only for the service-layer nn:: (service clients of fs/oe/aoc/pctl...); the SDK's libc++ and nn::os internals do not go into this table.
// Every entry states why it is taken over. HLE_FORCE entries no module references are listed with DM_IMPORTS=1.
typedef struct { const char *name; void *fn; const char *why; int optional; } hle_force_ent;
#define HLE_FORCE(mangled, why, ret, cname, ...)                                                   \
    static ret cname(__VA_ARGS__);                                                                \
    DM_TABLE(swforce) static const hle_force_ent hle_force_##cname = {mangled, (void *)cname, why, 0}; \
    static ret cname(__VA_ARGS__)
// Same, but an entry no module references is not an error (SDK APIs only some games use)
#define HLE_FORCE_OPT(mangled, why, ret, cname, ...)                                               \
    static ret cname(__VA_ARGS__);                                                                \
    DM_TABLE(swforce) static const hle_force_ent hle_force_##cname = {mangled, (void *)cname, why, 1}; \
    static ret cname(__VA_ARGS__)

// Operation mode (0 handheld / 1 docked): owned by hle_oe.c, and the hid controller style follows it; the front end can switch it at runtime
int dm_opmode(void);
int dm_overlay_file(const char *rel, char *out, size_t n);   // File in the overlay: DM_ROMFS_OVERLAY may have several layers (colon-separated, earlier ones take priority); look for <layer>/<rel> layer by layer, and on a hit write it to out and return 1 (hle_fs.c)
void dm_set_opmode(int m);          // On change, push notification 30 / 31 according to the switches the game has turned on
// Focus (1 foreground / 2 focus lost / 3 background): called by the front end on lifecycle changes; a change pushes notification 15
void dm_focus_set(int st);
// Audio pause / resume following the lifecycle (called by the front end on going to background / returning to foreground)
void dm_audio_suspend(int on);
void dm_audio_reset(void);           // iOS media services reset: every AudioUnit made again, playing ones restarted (hle_audio.c)
uint64_t dm_audio_render_calls(void);   // Cumulative sound card callback count (the silent sink is not counted)

// ---------- Host calls ----------
// A rewriter cut (games/<id>/rules.py) can replace a function entry or a call site in the guest with `svc #n` (n = 0x80–0xff; Horizon's own SVCs end
// at 0x7f), and the rewriter turns that into a host gate. The game's hook code registers the handler for n (dm_game_boot or earlier); the handler gets
// x0..x30, may change them, and returns 1 when it handled the call. An unregistered number aborts: the product and the hooks disagree.
void dm_host_call_register(int num, int (*fn)(uint64_t *regs));
