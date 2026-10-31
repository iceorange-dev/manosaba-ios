// The game this host is built for. The profile comes from games/<id>/game.json (tools/gen_game.py writes host/gen/<id>/game_profile.c); a game directory may
// also add C sources (games/<id>/*.c) that implement the hooks below or add HLE_FORCE takeovers of their own. The core reads only this interface.
#pragma once
#include <stdint.h>

typedef struct { const char *path; long long size; } dm_game_file;
typedef struct {
    const char *id, *title, *version, *engine;
    uint64_t program_id;                      // reported by svc GetInfo (ProgramId)
    const char *modules;                      // default module list, space separated ("main sdk subsdk0")
    const char *main_build_id;                // NSO build id of the supported main (hex)
    const char *language;                     // nn::oe::GetDesiredLanguage default (DM_LANG overrides)
    const char *languages;                    // the game's languages, space-separated nn::settings codes (game.json "languages"; host/lang.h)
    int screen_w, screen_h;                   // console output size the game renders at (touch space until the game sets a crop)
    unsigned guest_heap_mb;                   // heap region on iOS; the rest of the address space reservation goes to the alias region
    int ios_pool_add_mb;                      // on iOS, MB added to the console's 3285 MB application pool (game.json ios_pool_add_mb, default 512 as on the Mac;
                                              //    Tears of the Kingdom 0: the console's pool, its heap in the reservation; DM_APP_POOL_ADD_MB overrides)
    int nvn_api_major, nvn_api_minor;         // NVN API version nvnDeviceGetInteger reports (game.json device_api "major.minor")
    struct { int tex, ntex, samp, nsamp, img, nimg; } nvn_c2;   // c[2] handle tables of the game's shaders: byte base and slot count (game.json nvn_c2)
    int nvn_depth_mode;                       // NVNdepthMode its shaders were built for (game.json depth_mode: 0 near_is_minus_w, 1 near_is_zero)
    int present_game_interval;                // 1: with a window too, present at the game's present interval (game.json present_interval "game"; default "display": one frame per display tick)
    int present_fifo;                         // 1: the display link shows every queued frame in order, each for its present period (game.json present_consume "fifo",
                                              //    a game that steps once per frame: manosaba); default "newest": the newest, older ones dropped
    int unfocused_in_focus;                   // 1: a Mac window that is visible but not key keeps the game in focus (game.json window_unfocused "in_focus"; default "out_of_focus",
                                              //    which on the console is the HOME menu: Tears of the Kingdom stops there); minimized is Background either way
    int virtual_cores;                        // 1: guest threads hold one of three virtual cores while running guest code (game.json virtual_cores; hle_thread.c): the game
                                              //    keeps per-core data without locks (Tears of the Kingdom)
    int ios_handheld;                         // 1: on iOS the operation mode starts handheld (game.json ios_mode "handheld", hle_oe.c; default "console")
    int touch_pad;                            // 1: on iOS the touchscreen is on-screen controls (game.json touch "pad", host/front/touchpad.c) for a game steered with a
                                              //    controller; default "screen": touches go to the game's touchscreen
    uint32_t npdm_sysres;                     // the NPDM's SystemResourceSize (game.json npdm.system_resource_size; GetInfo 16; 0 = the host's 32 MiB)
    uint8_t npdm_prio_hi, npdm_prio_lo;       // its ThreadInfo capability's highest / lowest thread priority (game.json npdm.thread_priority; GetInfo 1)
    const dm_game_file *romfs_files; int romfs_nfiles;          // key RomFS files with their exact sizes (also tells the version)
    const char *romfs_tree; int romfs_tree_files; long long romfs_tree_bytes;   // RomFS directory whose file count and byte total must match
    const char *const *save_sync; int save_sync_n;              // save paths (glob, relative to the save root) kept in iCloud sync
    const int *aoc; int aoc_n;                                  // owned add-on content indices reported by nn::aoc (none = no DLC)
    struct { const char *romfs, *sysfont, *product, *shader_map, *msl; } ws;   // workspace paths relative to DM_ROOT (game.json "workspace"), 0 = none
    const char *upscale_fs; int upscale_color;  // the game's own upscale of the finished scene into its canvas (game.json upscale_pass: "fs" its fragment
                                                //    shader key, "color" perceptual / linear / hdr, MetalFX's colour processing for it): with MetalFX
                                                //    upscaling on, MetalFX writes the canvas in its place (gfx/nvn/nvn_mtl.m fxp_dock, plan item 63); 0 = none
    const char *compose_fs; int compose_color;  // the draw that samples the scene at texture slot 0 into the canvas (game.json compose_pass, the same form):
                                                //    with MetalFX upscaling on and the scene smaller, MetalFX upscales it first (fxp_scene); 0 = none
} dm_game_profile;
extern const dm_game_profile dm_game;

// Workspace root: DM_ROOT, else ~/dm-work. dm_game_ws_path joins it with a relative path.
const char *dm_game_ws_path(const char *rel, char *buf, unsigned long n);
// Command-line defaults from the profile's workspace: DM_ROMFS, DM_SYSFONT, DM_NVN_SHMAP and DM_MSL_DIR are set only when unset (the iOS and Mac
// app front ends set their own before the host starts).
void dm_game_env_defaults(void);

// Hooks a game may implement (weak no-op defaults in game.c)
void dm_game_boot(void);                      // guest main thread, after the modules' constructors, right before nnMain
void dm_game_mount_save(const char *dir);     // before the save directory dir (the committed copy) is mounted
// nn::fs::Commit of the transactional save, before the working directory is swapped in: side files the hook writes into workdir are committed
// atomically with the game's own files. Called with the file system lock held: write with host file calls, not through the guest's nn::fs.
void dm_game_save_precommit(const char *workdir);
// Every nvnQueuePresentTexture, on the game's thread before the present (weak default in gfx/nvn/hle_nvn.c, which the graphics tests link without
// the host): once per game frame
void dm_game_present(void);
int dm_game_realtime_step(void);            // 1: the game's step follows the measured frame time now (uncapped pacing may pace it, gfx/nvn/nvn_pace.h)
// nn::oe::SetUserInactivityDetectionTimeExtended(on) from the guest return address ra (host/hle/hle_oe.c): the frame rate to hold until the next call,
// 0 = none (weak default: 0). Tears of the Kingdom holds its pre-rendered movies at 30 (games/totk/movie_fps.c, plan item 130)
int dm_game_inactivity_fps(int on, uint64_t ra);
// A command channel line the host does not know (host/front/cmdchan.c): 0 not the game's, 1 the game's (run = 0: only checked when the line is read;
// run = 1: run, the game logs the outcome). Tears of the Kingdom's survey (games/totk/survey.c, plan item 167)
int dm_game_command(const char *line, int run);
// The setting "Native" is on and the front end's window is now on a screen of w x h pixels (plan item 302: the iPhone Duo's window moves between its
// two displays while the game runs). Any thread; weak default: nothing. A game that can change its resolution while it runs renders at that screen's
// native size (host/native_size.h) from a later frame: manosaba (games/manosaba/graphics.c)
void dm_game_native_screen(unsigned w, unsigned h);
// On the game's thread right after it waited for its window image (nvnSyncWait on the sync of the last nvnWindowAcquireTexture, gfx/nvn/hle_nvn.c,
// whose weak default this is): between two frames, where a game may act on its own objects
void dm_game_frame_sync(void);

// Services for game hooks (hle_fs.c)
const char *dm_save_workdir(void);            // working directory of the transactional save (the copy the game reads and writes); NULL when none is mounted
void dm_fs_save_poison(int err);              // a side file failed to write: refuse this transaction's commit like a failed game write
