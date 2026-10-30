// NVN interface layer: takes over the nvnBootstrapLoader imported by main; the NVN driver in sdk is not executed at all.
// All 523 NVN names return counting thunks (nvn_thunks.S), and the thunks forward to the implementations here:
//   - those with semantics (device / memory pools / textures and samplers / queue / window / sync / events / command buffer memory) are implemented per NVN semantics, and the objects are real;
//   - those that only change GPU state and have no CPU-visible result (state objects, bindings, draws, clears...) are recorded as commands and replayed at submit;
//   - the rest (the 307 that main only loads but never calls + those without a prototype in the header) go to nvn_unimpl_entry: reports a failure and prints the name when called.
// GPU address = CPU address (memory pools use the real memory given by the game, also built as zero-copy MTLBuffer). Commands are recorded into the command memory given by the game and replayed at submit into a
// Metal command buffer (nvn_mtl.m): buffer copies / uniform updates go through blit (GPU ordering), event value writes / counters are written after the GPU completes;
// sync object = submit fence value, and waits are released only after the GPU completes.
// Every implementation gets a compile-time signature check against the prototypes in nvn_protos.h (NVNboolean parameters are received as uint32_t, only the low 8 bits are looked at).
// Environment switches: DM_NVN_FRAMES=N prints the call table per frame for the first N frames (default 3); DM_NVN_TSV=path writes the per-frame call table and timing to a TSV (DM_NVN_TSV_CALLS=0 writes timing only); DM_SLOWFRAME=1 writes only the per-frame breakdown of dropped frames to the log; DM_NVN_LOG=1 logs object creation.
#include "../../host/hle.h"
#include "nvn_protos.h"
#include "nvn_count.h"
#include "nvn_tex.h"
#include "../tex/nvn_fmt_mtl.h"
#include "nvn_mtl.h"
#include "nvn_pace.h"
#include "nvn_fstep.h"
#include "nvn_fptrial.h"
#include "../../host/front.h"
#include "../../host/game.h"
#include "nvn_dirty.h"
#include "nvn_perfline.h"
#include "nvn_trace.h"
#include "../../host/plat.h"
#include <mach/mach_time.h>
#include <TargetConditionals.h>
#include <unistd.h>
#if TARGET_OS_OSX
#define DM_MEM_CAP_DEFAULT_GB 24
#else
#define DM_MEM_CAP_DEFAULT_GB 0
#endif
#include <sys/resource.h>
#include <assert.h>
#include <stdatomic.h>

// A guest thread waiting in an NVN call (GPU, display, replay, shader loading, pool setup) gives its virtual core back meanwhile (host hle_thread.c, game.json virtual_cores; as
// MacNativeGames' NVN_BLOCKING). Weak defaults for the graphics unit tests, which have no guest threads
#ifndef DM_CORE_AWAY_WEAK   // (once in a unit that includes both the backend and hle_nvn.c: the tests)
#define DM_CORE_AWAY_WEAK
__attribute__((weak)) int dm_core_away(void) { return 0; }
__attribute__((weak)) void dm_core_back(int *away) { (void)away; }
#endif
#define NVN_BLOCKING() __attribute__((cleanup(dm_core_back))) int nvn_core_away_ = dm_core_away()
// The translation thread (plan item 61, with nvnQueueSubmitCommands below)
static int xlat_on(void); static void x_drain(void); static uint64_t x_token(void);
static int x_token_fence(uint64_t k, uint64_t timeout_ns, uint64_t *fence);

#include "hle/slots.inc"
#include "hle/device.inc"
#include "hle/pools.inc"
#include "hle/textures.inc"
#include "hle/samplers.inc"
#include "hle/programs.inc"
#include "hle/sync.inc"
#include "hle/queue.inc"
#include "hle/cmdbuf.inc"
#include "hle/render_cmds.inc"
#include "hle/state_objs.inc"
#include "hle/lifecycle.inc"
#include "hle/xlat.inc"
#include "hle/nops.inc"
#include "hle/window.inc"
#include "hle/timing.inc"
#include "hle/pacing.inc"
