# Development notes

Where the work stands and how to pick it up again, on this machine or a new one. The repository holds all code, tools and notes; what it
does not hold (and must not: keys, dumps, anything derived from the game) is listed below with the way to get it back. Two more files:
[docs/sources.md](docs/sources.md) (the projects we draw from, how far each was read, what was taken and what not) and
[docs/plan-archive.md](docs/plan-archive.md) (plan items 1-259 with their measurements, as they stood on 2026-10-23; code comments and commit
subjects cite them as "plan item N").

## Restoring a working copy

```sh
git clone https://github.com/iceorange-dev/manosaba-ios.git "manosaba ios" && cd "manosaba ios"   # work on main, which tracks origin/main
git remote add upstream https://github.com/jhaizhou-ops/d2r-mobile.git      # fetch only, never push there
```

Then the machine: [PORTING.md § 0](PORTING.md#0-machine-and-workspace) (Xcode, Homebrew packages, the Python venv, the shader translator).
This working copy uses `DM_ROOT=<repo>/work` (git ignores `work/`).

| Not in git | Where it lived | How to get it back |
|---|---|---|
| Console keys | `keys/` | From your own console; keep your own backup |
| Game dump (NSP, ExeFS, RomFS) | `dump/`, `extracted/2.0.4b/` | [PORTING.md § 1](PORTING.md#1-dump-the-game-from-your-console): `tools/game_extract.sh keys/prod.keys <out dir> dump/*.nsp` rebuilds ExeFS and RomFS from the two NSPs. `work/data/switch/manosaba-2.0.4b/RomFS` was a link to `extracted/2.0.4b/RomFS` |
| System fonts | `work/data/switch/sysfont` | PORTING.md § 1 (nxdumptool, system data titles 0100000000000810–814) |
| Console and SD card backups | `sd_backup_*/` | Only your own backup |
| Product (rewritten modules) | `work/build/cache/dm/out/…`, `work/build/totk/product`, `work/build/totk/product60` | `tools/build_product.sh && verify/regress.sh` |
| Shaders, stage map | `work/build/…` (paths in `games/<id>/game.json`) | PORTING.md § 5 |
| App icon | `workspace.icon` | Your own 1024×1024 PNG of the game's icon |
| Device saves | Device `Documents/save`; copies in `work/run/ios/*_save_*` | Files app → this app → `save`, or `devicectl device copy from` |
| iOS signing settings | `ios/local.env` | From `ios/local.env.example`. Keep the bundle ID of the installed app (`xcrun devicectl device info apps --device <id>`): another bundle ID is another app with an empty data container |

Keep `keys/`, the dump, the backups and the device saves in a separate private backup (for example an encrypted external disk); they stay
out of git by design.

## Testing

Check a working copy with `make -C host/tests run`, `make -C host/tests run-ubsan`, `make -C gfx/tests run`, `make -C gfx/tests run-m3`,
`make -C gfx/tex/tests run`, the tool tests (`for t in tools/tests/t_*.py; do python -I "$t"; done` in the venv), `verify/regress.sh --fuzz`,
`tools/audit_public.py`, then a Mac run (README, Getting started) and `ios/run.sh build`. For Android (plan item 301): `make -C android
GAME=manosaba` and `make -C android test` (NDK r30), and with the test phone connected `make -C android run-tests` and a headless run of
manosaba under adb (`work/scripts/android_smoke.sh <dir> <seconds>`: the RomFS and system font pushed once to `/data/local/tmp/dm`).

**How a change is made.** One plan item at a time, each with a unit test and a broken version of the change that must fail it. A batch of
items ends with the gfx tests (Metal 4 and Metal 3), the texture tests, the host tests and their sanitizer run, the public audit and the tool
tests; `verify/regress.sh --fuzz` when the rewriter, a game's rules or `verify/equiv.py` changed; both IPAs (`ios/run.sh ipa`, `DM_IOS_M3=1
ios/run.sh ipa`); the Android host and its tests, on the phone when it is connected, with a 40 s headless run; a short Mac run of each game
against the previous batch's screenshots and red lines; then one commit whose subject starts with
"Plan batch N". A change to `tools/patch_nso.py` or a game's `rules.py` changes the rewriter stamp in the products, so they are built again.
The repository is public: `tools/audit_public.py` checks the git index before every push.

- **Memory over repeated loads on the Mac**: `games/manosaba/reload_loop.sh [--stay] <run dir> <save dir> <cycles> [host binary] [VAR=value …]`
  loads a save's newest autosave every 40 s by tapping through the game's own menus; `tools/memstat_cycles.py <run dir>/mem.tsv` prints one
  row per load. With `DM_GUEST_ENV="GC_PRINT_STATS=1" DM_DBG=1` the IL2CPP collector logs every collection; `tools/gc_stats.py <run dir>/log.txt`
  summarizes it. The managed heap census (`DM_HEAP_CENSUS`, `DM_HEAP_WATCH`, `DM_HEAP_REFS`, …) is in [host/README.md](host/README.md).
- **A memory limit kill on iOS** leaves no crash report as a rule (JetsamEvent reports are rate-limited, the crash is "non-actionable"). With the
  device connected: `sudo /usr/bin/log collect --device-udid <UDID> --start '<yyyy-mm-dd hh:mm:ss>' --output kill.logarchive`, then
  `/usr/bin/log show kill.logarchive | grep -E 'ReportSystemMemory|killed by jetsam'`. `DM_MEMSTAT` (relative paths land in `Documents`) and the
  low-memory watch in `host/memstat.m` show what grew.
- **Performance on the iPhone**: compare builds in pairs run one after the other, each after a rest, and judge the sustained rate after the
  first minute (the phone allows a burst of about 60 s, see below). Capped frames lower the clocks, so GPU milliseconds at a held rate
  understate a saving (it shows as a lower clock); the sky over Tears of the Kingdom's island changes with the game's clock, so pin the frame step
  (`DM_FRAME_STEP_MS`) or use a check mode before blaming a picture difference on a render change.
- **Pitfalls**: zsh has a `log` builtin (use `/usr/bin/log`); zsh does not split words (`set -- $var` does not do what bash does); make compares
  modification times, so two edits within one second can skip a rebuild; copy a host binary elsewhere before a long test if you will rebuild
  the host meanwhile; Apple's Metal HUD crashes with `CAMetalDisplayLink` on iOS 18.6 (keep it off there).

## Where the work stands (2026-10-23)

What plays is in the README's status. Supported systems: macOS 26 and iOS 26 (Metal 4); iOS 18 and macOS 15 through the Metal 3 builds
(`DM_IOS_M3=1`, `DM_MAC_M3=1`); iOS 17 and iOS 27 are not supported. Devices used: iPhone 17 Pro (iOS 26.4, free developer account, the increased memory limit: 6.1 GB), a Mac with an M5; manosaba also on an
iPad mini 7 (iOS 18.6.2) and an iPad Pro M4 (iOS 18.5, the unsigned IPA) up to 2026-10-16.

**manosaba 2.0.4b** plays on the Mac and the iPhone. Open:
1. **The game's own memory leak.** Every save load builds one more set of Naninovel's `ScriptManager` and `ResourceProviderManager`
   (`DM_HEAP_WATCH`); the old sets stay referenced. Live data grows about 20 MB per load, the heap about 28 MB (`GC_FREE_SPACE_DIVISOR=8` saves
   35–50 MB, not the rate). Root reference not found. Next: trace all old instances of the two managers (not samples), bound each object by its
   class's `instance_size` (`host/hle_svc.c`), scan the guest stacks too; then clear the root from a `games/manosaba` hook after a load.
2. **A trial's loading peak on 8 GB iPads.** Without the increased memory limit an iPad mini 7 gives about 3.35–3.4 GB; a trial's start adds
   about 1.4 GB in 0.2 s (about 116 character render targets, 0.9 GB of Metal memory). A fresh session keeps about 725 MB of headroom, a two-hour
   one does not (the leak). A free account signs the increased memory limit since plan item 65 (the iPhone 17 Pro went from 3.4 to 6.1 GB), and
   `ios/tools/ipa_sign.py` signs the unsigned IPA with it (211); neither was tried on the iPad mini. 2026-10-25, an A16 iPad (iPad15,7, iOS
   27.0.1, the 10-23 IPA, signer not known): a limit of 3071 MB, killed at a trial's start 52 s into a fresh session (footprint 2069 → 2951 MB
   in 0.5 s; 82 colour targets 588 MB and 42 depth / stencil targets 546 MB, the largest 1372x2930 Depth32Float_Stencil8).
3. Smaller: 13 NVN functions looked up but unknown (NULL, never called); compute dispatch never seen; keyboard, mouse and controller untested
   with this game.

**Tears of the Kingdom 1.4.3** plays on the Mac and on the iPhone 17 Pro (the prologue, its movies, the dive, the Great Sky Island and after).
The iPhone build embeds `product60` and runs at 120 Hz; on the island it holds about 59.5 fps after the first minute at a 1280x720 scene (plan
item 239). What limits it there (230, measured with a plain load instead of the game): the phone allows a burst of about 60 s, then a sustained
power budget that goes to a busy GPU first, and the CPU's clock is cut (TotK: GPU 75-80 % busy, CPU 1.4-1.7 GHz). The frame is one chain (the
game waits every frame for the GPU to finish the frame it handed over), so only less energy a frame turns into fps: less GPU work gives the CPU
its clock back (239: 55.4 / 56.4 → 59.5 / 59.6 fps), saved translation cycles turn half into fps (225); keeping the GPU busy and the GPU a frame behind the
game were measured and dropped (the archive's Tier 9 notes, "Why the CPU's clock falls"). A 960x540 scene holds 59.6 fps with 98 % of frames at
16.7 ms; the iOS default stays 1280x720 (the user's choice; the app's settings offer 540p with MetalFX).

## Tears of the Kingdom

`games/totk` (1.4.3, program 0100F2C0115B6000, main build 277178b7dba1b6d4949a84778d4abc58), data from your NSPs with `tools/game_extract.sh`
into `work/data/switch/totk-1.4.3`. SDK 20.5.22; `sdk` and `subsdk0` have the same build IDs as nxnative's Breath of the Wild 1.9.0 (the same
system layer). The parts below are the reference; each plan item's details are in the archive.

**Running it.** After `make -C host GAME=totk`:
`DM_GAME=totk DM_ROOT=$PWD/work DM_SHDUMP=$PWD/work/build/totk/shader_capture DM_SAVE=… DM_TEMP=… DM_CACHE=… DM_WATCH=120 host/dmhost` (stage
map and library from the profile; rebuild them with `shmap_from_capture.py` and `DM_GAME=totk dmsh_build.py`).
- Products: `work/build/totk/product` stays at 30 fps (the batches' smoke runs use it); `work/build/totk/product60`
  carries the frame-rate rules and the code options (57): `fps60`, `realtime`, `scene` (the iPhone's set: `DM_PRODUCT=build/totk/product60
  DM_OPTIONS=fps60,realtime,scene ios/run.sh …`, 62l), `output` and `aspect`; `options.default` all of them, `DM_OPTIONS=none` none.
  `DM_RENDER_RES=<w>x<h>` sets the scene size (52), `DM_RENDER_HEIGHT` its lines at 16:9, `DM_OUTPUT_RES=<w>x<h>` the output (63); the command
  channel's `scene <w>x<h>` changes the scene size while the game runs (240).
- RomFS overlay for 60 fps and up: `work/build/cache/dm/venv/bin/python -I games/totk/romfs_overlay.py work/data/switch/totk-1.4.3/RomFS
  work/build/totk/romfs_overlay` (quick menus' background, the game over menu's one-frame wait); served first when it exists
  (`games/totk/defaults.c` sets `DM_ROMFS_OVERLAY`; empty turns it off). Test: `games/totk/tests/t_romfs_overlay.py <RomFS>`. On iOS
  `ios/run.sh push-data overlay` copies it in and the app serves it when it was made from the phone's RomFS (194).
- The TBDR rewrites are off on the Mac and on iOS (`games/totk/defaults.c`; `DM_NVN_TBDR=1` on): on the M5 the GPU p50 is 8-10 % lower without
  them, and on the iPhone they drew a glow over the prologue's cave (62j).
- Window: game.json `present_interval` "game" (the display-link wait takes the game's interval: it steps per frame), `window_unfocused`
  "in_focus" (OutOfFocus is the console's HOME menu); `DM_PRESENT_HZ=30..120` sets the display link's rate (mode 1).

**Rewrite** (`tools/patch_nso.py`; `verify/regress.sh --fuzz` OK for all three modules):
- `blr x18` whose x18 was loaded in an earlier block: the K18 slot holds it wherever it was loaded (nxnative's blr rewrite has no such check).
- Jump tables with explicit targets (`adr; ldrsw / ldrh / ldrb; add; br`): targets derived over the function's control flow to a fixpoint, then a
  compare chain with landing pads as nxnative's `site_br_tab` (`sub` + `cbz`, NZCV kept); an index outside stops at `brk #0x18`. Row `JT`.
- Exclusive sections by control flow (`exclusive_regions`): TotK's compare-and-swap retries sit in cold blocks, giving sections in pieces copied
  by `site_region` (row `R`). Any other entry stops the rewrite. Load-exclusives become acquire loads (232).
- `mrs xN, ctr_el0` (SIGILL on Apple systems) → `movz xN, #4, lsl #16` in place, as nxnative (row `ctr`); also two words of manosaba's `sdk`.
- `HOSTREFS`: instructions that address a 16 KiB writable host page after the key page (fixups `HOST_PAGE_OFF`, only in modules that use it),
  resolved into cuts; the loader reads it (`dm_host_page`).
- Verifiers: `structck` ⑪ (windows, sections, explicit tables, single entry), ⑫ (`ctr`) and ⑭ (acquire loads); `equiv` runs sections from each
  piece and skips a trial reading below sp (the red zone); `fnfuzz` gives both images the `ctr` constant. Upstream PR #55 (x18 scan prefilter,
  n3crosis) is in.

**Host** (most from nxnative, credited in the code): oe (`GetPreviousProgramIndex` = -1, launch parameter, startup logo,
`PopNotificationMessage`, volume balance, ARUID), profile field `device_api` (TotK 55.15, manosaba 53.311), hid mouse / vibration batch, the
in-process audio renderer (`host/hle_audren.c`, `host/hle_guestthread.c`, `dm_pcmout_*`), the hardware Opus C API, indexed cache storage
(`$DM_CACHE`), another application's save not existing (TotK looks for BotW's), svc GetInfo 20, play reports, amiibo (`host/hle_nfp.c`), clock
snapshots and steady / network clocks (from Ryujinx), NVN functions (13 names in `gfx/nvn/names.txt`).
- Cores (`host/hle_thread.c`): the job system keys per-worker output by `nn::os::GetTlsValue` = core + 1. Core mask calls follow the Horizon kernel
  (Atmosphère mesosphere: -1 don't care, -2 the ideal core, -3 keep it), ideal core and mask from main.npdm. Virtual cores (game.json
  `virtual_cores`, from MacNativeGames' runtime/threads.c): a guest thread holds one of three while it runs guest code; host-work threads
  (`dm_guest_thread_spawn`, the audio renderer) hold none.
- Command memory: a disabled or short callback continues in host segments. TotK calls cached 512-byte bind windows every frame, so `cmd_t`
  leaves +0x10 to the game (`guest_flags`: its NVN 55 inline wrappers store a dirty byte there), a host segment lives as long as the JUMP that
  links it, and the JUMP counts as used (`t_nvn` 5a).
- Movies: the 28 in `Event/Movie` are WebM, one VP9 track, no sound. `SetDataSource(path)` (`dm_fs_host_file`), a WebM / Matroska demuxer
  (`host/movie_webm.c`, RFC 9559 / 8794), a VP8 / VP9 decoder on libvpx with MacNativeGames' behaviour and pacing (NV12, at most one frame per
  game frame, late frames dropped); the second GetOutputBufferSize output is the alignment, 256; status codes from the SDK's `movie/Status.h`.
  End-of-stream inputs stay in the decoder until Flush / Reset / Configure, as ACodec (handed back, as MacNativeGames does, the game stayed
  black after the movie). libvpx from Homebrew on the Mac, built for iOS with NEON into `$DM_ROOT/build/ios/libvpx` (62b, 62b2); pre-rendered
  movies are held at 30 fps (130). `t_movie` (`t_movie <file>` probes a movie).

**Shaders.**
- Keys are sha256(control section + code area) everywhere (`shader_key` in `hle_nvn.c`; the 0x880-byte control section alone collides).
- game.json `nvn_c2` (texture handles in c[2]: 0x20 for TotK, 0x168 for manosaba) and `depth_mode` (TotK `near_is_minus_w`: z' = z / 2 + w / 2
  before every vertex return); both in the translator job, the cache key and the reflection. `dmsh_build.py` needs `DM_GAME`, the translator
  `DOTNET_ROOT` with Homebrew's .NET (`/opt/homebrew/opt/dotnet/libexec`).
- `QueryHostSupportsShaderFloat64` false: Ryujinx's `DoubleToFloat` runs DMUL / DFMA in float. Integer pixel outputs: a failed pipeline writes
  `<DM_SHDUMP>/fout.tsv`, which `dmsh_build.py` passes to the translator. Vertex shaders padded for their pixel shaders (`vs_pad`). Early fragment
  tests where the control section asks for them (239). min / max / clamp with Maxwell's results for a NaN operand (`precise::`, 260). The chain in
  use: ae6a8bd1acdd.
- `engines/bnsh/shader_extract.py` reads the shipped BNSH (`Pack/Bootup`, `Lib/**`, `Shader/**`, as MacNativeGames' prepare_shader_seed.py),
  plus `Effect/**.esetb.byml.zs`, 0x88c-byte control sections and the sharcb archives, into `work/build/totk/shader_capture` (with the
  run-time captures 28,652 shaders; every one has a Mac and an iOS library, checked 2026-10-22). Shaders in main stay run-time captures.
  `c1_guard`: 49 shaders whose c1 index is not proven bounded.

**Replay** (`gfx/nvn`):
- 8-bit index buffers widened to 16-bit; RGB10A2SN; a view swizzle replaces the texture's (`view_t` stays 10 bytes); views Metal cannot make are
  sampled as the texture.
- CPU-filled textures (as LASRecomp): a texture in a CPU-visible, non-compressible pool that the GPU has not written is uploaded once the CPU has
  filled it; the first GPU write makes it the GPU's (`tex_gpu_write`). Linear textures in CPU-visible pools live on the pool buffer
  (`tex_linear`, nxnative). Small textures are checked by content (06c80bd).
- Counters: reports other than samples passed write their counter half in command order (TIMESTAMP ones 0), their timestamp at completion;
  conditions are evaluated at replay from guest memory, look-aheads treat a render enable change conservatively; occlusion counting started inside a
  pass splits it. Counter timestamps run on the guest clock (`guest_ns`): `CLOCK_MONOTONIC` counts the Mac's sleep, and the game's frame-rate
  controller reads them.
- From MacNativeGames (MIT, credited in the code): a registration without a view clears the old view; partial clears as one triangle
  (`clear_quad` / `clear_ds_quad`); BC render targets through a one-texel-per-block view (`blk_aux`); storage buffers addressed from a uniform
  buffer (`sbuf_ubo`, `NVN_RB_SBUFU`, `pend_read`); the storage buffer write flag is bit 0.
- A draw without attachments gets a pass sized by the scissor. Also: `rt_dup_slot` (nxnative / tomodachi-native), inline uniform data in a host
  ring (nxnative), `BindProgram` as recorded, `SetRenderEnable`; `Read|WriteTexels`, `QueueFinish`, `SubmitCommands`, `PresentTexture`
  under one recursive lock.
- Uniform snapshots (`mtl_sub_snapshot`, MacNativeGames' usnap method, MIT, written anew): the game rebuilds per-object constant pools once a
  frame is submitted, so each submission copies the uniform ranges it binds into its argument arena when it starts (merged within 256 bytes, at
  most 1 MiB) and binds and reads the copies; a GPU write within the submission drops the copies it reaches. `DM_NVN_UBO_SNAPSHOT=0` reads live
  memory. `gfx/tests/t_usnap.m`.
- Threads: replay on a translation thread of its own (61), the render encoders on a second one on iOS (`DM_NVN_RENDER_STREAM`, 245).

**Frame rate** (from MacNativeGames' `game_patches_v143.c`, MIT):
- The controller (main 0x2db174) sets present interval clamp(int(max(CPU, GPU) × 60 + bias) + 1, 2, 3); `games/totk/rules.py` CUTS its seven
  words to allow 1. At a 120 Hz present rate the intervals 1 / 2 / 3 are up to 120 / 60 / 40 fps. `nvnWindowSetPresentInterval` logs a backtrace
  on every change.
- Real-time step: the two producers (0x2daa54 ratio, 0x3e5638 ticks) load +0 (seconds × 30) and +4 (seconds × 60) from main's host page;
  `games/totk/frame_step.c` writes there at every present (`dm_game_present`) the mean of the last five present intervals on the guest clock;
  `DM_FRAME_STEP_MS` pins it. Checks: `games/totk/check_frame_rate.py <product> <main NSO>`, `tests/t_frame_step.c`.

**Diagnostics**: `DM_WATCHPOINT` (`host/diag.c`), `DM_CRASH_MEM` (`host/crash.c`, `*xN`), `DM_NVN_PASSDUMP_ALL`, `DM_NVN_GPUWATCH=<first>-<last>`,
`DM_NVN_LIVEROWS=<file>` (single wrong frames in window play), `DM_NVN_LIVESHOT=<first>,<count>:<path with %d>`, `DM_NVN_NOCULL`,
`DM_NVN_RTCLEAR=<serial>`, `DM_NVN_TEXSNAP`, `DM_NVN_TEXNZ`, `DM_NVN_TEXCLASS`, `DM_NVN_REUPLOG`; the command channel `DM_CMD=<file>` (47); per-pass
GPU time `DM_NVN_PASSGPU` (49); the frame timeline `DM_NVN_TRACE` with `tools/trace_report.py`; on iOS, switches in `Documents/env.txt` (22). The
cliff repro: a manual save at the cliff edge, `DM_KEYS="A@26.5,A@31,A@35,LSU@f1380+240,X@f1400"`. Lesson: MacNativeGames' `nvn_mtl.m` described
the dive's flicker and its fix; search the references for a symptom before building probes, then measure to confirm the cause here.

Known gaps: depth-stencil (stencil) views are not modelled; an integer render target's partial clear is not drawn (red line); other iPhones and
iPads are not tried (GPUs without BC turn BC1 into ASTC and decompress the other BC formats, 62c / 62c2).

## Open work

The open plan items, shortened (their history: the archive under the same number), by what they need. New items continue from 260.

The codebase (2026-10-24, the user: the Metal backend in one 10,000-line file is the first thing to fix, before Android; what stays is mine to
decide):
- 264 the replay core and the Metal backend apart: what the replay decides (command walk, state, uniform snapshots, CPU-filled textures,
  counters, pass planning) in C behind an interface, the Metal calls behind it, so an Android Vulkan backend takes the same fixes. In steps,
  each into `gfx/nvn/core/` with its header and a test that needs no GPU. The parts that use little Metal are done (batches 88-94: the store
  index, uniform snapshots, the TBDR switches and validity ledger with the pass dump, pending writes and frame measurements, the live
  texture, the submit call's capture, inline uniform updates). Next the mixed parts (the replay loop, lookahead, tile merge, draw) split into
  what is decided and what is executed: the replay state (`S`, `R`) and the commands that only set it (batch 96), pass planning's own
  decisions (batch 97: where the replay is, the ops run early, depth / stencil need, store decisions, target comparisons), the clear-only
  hoist's plan and the deferral's decisions (batch 98), the tile merge's plan (batch 99), the execution interface (`nvn_backend.h`) with the
  replay loop in `core/exec.c` (batch 100), SetRenderTargets, the clears and conditional rendering (batch 101), `mtl/draw.inc` split (batch
  102), the backend's private header with the first part a translation unit of its own (batch 103), three more (batch 104); next the other
  parts outside the draw's path as translation units of their own (the draw's path stays one for inlining): the rest use the encoder layer,
  the texture registry or the draw's state, which want headers of their own first (next, the user 2026-10-24: worth it, an Android version of
  manosaba is planned; design them with a Vulkan backend in mind). `hle_nvn.c` in parts by its sections (batch 105), `host/` in directories
  (batch 106), the core tests' shared fakes (batch 107). The Vulkan backend later (the user, 2026-10-24: no Android for now, a clean base
  first); from 2026-10-30 the Android version of manosaba is under way (the user), item 301.

The iPhone's frame rate (the current focus; the power split above decides what helps):
- 190 a scene size driven by dropped frames, now that the scene size can change while the game runs (240; 960x540 holds 59.6 fps on the island
  against 56.0). Upstream's controller counts only the first 3 frames of a run of dropped frames unless the run lasts 1 s; its GPU-time rule does
  not fit, since under the power split the frame looks CPU-bound.
- 218 the G-buffer split by compute: two depth-probe dispatches (3c8e9ba061067c07_b, bc58d5dec860c367_b) sample the G-buffer's depth in its middle,
  and each split stores and reloads four colour targets and the depth (about 24 MB at 1024x576).
- 219 the TBDR rewrites that merge or defer passes, with mid-submission commits (228 did the others): on the phone GPU 24.3 → 22.8 ms and 15 %
  less traffic, but 66.6 ms frames without the commits. 186: the leads 62j left in those rewrites (shared storage tracked by serial, `attr_read`
  without `pend_hits`, the load probe's readback order on Metal 4, a run under Metal API validation).
- 229 the guest main thread's QoS: `DM_MAIN_QOS=ui` exists (batch 79); the A/B on the phone is not run.
- 220 MetalFX frame interpolation: the G-buffer has no motion target, so the camera's motion would come from the depth and two frames' matrices,
  and moving things would ghost.
- 221 fewer triangles through the game's own LOD and draw distance settings: the user's call on picture against frame rate.

Correctness that needs play, saves or the console:
- 94a GPU write-back of idle textures (`DM_NVN_WB_IDLE=1`, totk-mobile's rule): it makes Link close to the camera solid instead of a translucent
  ghost. Needs console footage of Link close to the camera and of the island's grass, and what the RG8 256x256 texture the CPU rewrites is; then
  on by default or a narrower rule.
- 132 the wave program's wide lines (totk-mobile expands them by editing its shaders): needs a save where the game draws them.
- 71c product60's 30 fps systems in play (stamina, arrow speed, shield surfing, rune aiming, flurry rush; BotWRecomp's BotW list as the guide).
- Checks of finished items, for the user: grass that stands up and can be cut (161), hair and cloth in wind at 60 and 120 fps (163),
  `DM_NVN_FBX=split` with a turning camera over paved ground (165), an overworld save for towns (167), the game's text in the app's language (170),
  the settings panel on a device (148), stick diagonals against a console (124).

At the Mac or with hardware:
- 97 a present with a deadline against the GPU clock drop: needs a window and `powermetrics` (the password).
- 71b the covered window (`DM_WINDOW=1 DM_TEST_COVER=30,10`), the Metal 4 screen on a device (`DM_TEST_NO_METAL4=1` in env.txt), the backup
  exclusion in Settings, uncapped pacing in a window; the window checks of 123 and 127; 153's latency default (`DM_PRESENT_LATENCY=auto`) after
  a check of the 30 fps setting.
- 71d displays above 120 Hz (macOS rounds a 60 Hz request to 72 on a 144 Hz panel).
- 66 a Switch Pro controller with the printed-letter face buttons (2); 67 gyro and rumble with a controller and the phone.
- 62 other devices: the cold-texture census (56) under a device's memory limit; 62c2 BC2 / BC3 / BC7 → ASTC and BC4 / BC5 → EAC on GPUs without
  BC (now decompressed; with BC1 as ASTC the prologue still takes 330 MB more than with BC); 62g one varying interface as a fallback library if
  a device's log shows a "not written by vertex shader" failure.

The user's choices: 192 touch controls after Touchable (its 8-way D-pad, Liquid Glass, face-button slides and layout; the stick, the camera
drag and opacity are open); 221 above; `DM_FRAME_STEP=vsync` as iOS's default (133: 35 % less CPU than the mean step on the Mac).

From the ninth review (2026-10-27, [docs/sources.md](docs/sources.md)); the test phone is the iPhone Air (iOS 27.0.1) from 2026-10-27:
- 285 Tears of the Kingdom on the iPhone Air (iOS 27.0.1; its A19 Pro has 5 GPU cores against the 17 Pro's 6, and no vapor chamber): on Great Sky
  Island the game's own controller picks interval 2 within the first minute (thermal state 0-1; the 17 Pro went back to 1 within 0.1-0.7 s each
  time), the GPU's time then follows its lowered clock (25-28 ms), and from thermal state serious, 1-2.5 minutes in with the phone on USB power,
  our hold keeps 30 (135). Sustained after the first minute 30 fps in every variant: the native and a 720-row drawable, a 960x540 scene (37 fps
  for its first 25 s, GPU 13.6 ms), `DM_PRESENT_INTERVAL=display` (38.7 fps for 25 s). The phone has a cooler on it (the user, 2026-10-28: it
  cannot be made cooler), so these runs are the cool case. What holds 60 for the first minutes: the option hold60 (batch 118) keeps the controller at 1, about 60 fps until thermal state serious
  (720 rows: p50 17.7-18 ms, p95 25 ms, GPU 14.6-15.4 ms, for a minute and more on USB power; 540 rows: 16.6-16.9 ms for 35 s), then our hold
  and the GPU's lowered clock (27 ms) give 30. Left: a run off USB power (the runs here were on it).
- 271 CPU per draw, 226's area. Batch 116 took the texture views made again on the same base texture (about 35 a frame) off, batch 119 the memcmp
  call of 8-byte hash keys (`nvn_hmap.h`, one word). The translation thread standing on Great Sky Island (M5, a Time Profiler run of batch 120):
  render encoders made 20.5 % (the driver's, one a pass: fewer passes are 219's), dispatches 4.3 %; encode_args 15.5 %, of it the uniform updates'
  private copies 5.7 % (upd_shadow_at, 4.3 % memmove: each copy is the whole bound range, 32-36 KB on average and 64 KB the largest, `[nvn-mtl]
  renamed uniform updates`, about 88 copies a frame) and the texture IDs 5.6 % (tex_by_id's slow path 4.4 %, half of it tex_mtl's
  once-a-submission check of textures that follow guest memory); pso_get 3.1 %, pc_get 1.2 %. Copies of only what a shader reads: in 6,000
  translated shaders a uniform binding read at constant indices only is read in its first 31 vec4s (median) or 144 (90th percentile) of 4,096, but
  1,371 of the shaders index one at run time; the extents would come from the reflection of `gfx/tools/dmsh_build.py`, and a change there changes
  its code fingerprint, so every shader is translated and compiled again (Mac and iOS libraries). Not taken from MacNativeGames PR #750 (all its
  switches together +11 % fps on an M4 MacBook Air): resolved uniform addresses cached (`TOTK_CBUF_CACHE`; pc_get above), argument bytes equal to
  the shader's last block bind that block (`TOTK_AB_SAME`), a 256-entry pool table by page (`TOTK_POOL_HASH`), dispatch arguments before the
  encode stage's wait, generation maxima per 64 pages (`TOTK_DIRTY_FAST`); the border key kept per shader is not worth it here (border_variant 0.0
  % of the thread). 4ppp44 cb21bec / d0d3cb1: argument blocks reused across the passes of a submission, the driver segment filled only at the vec4
  offsets a shader reads (`mtl/args.inc` fills all).
- 280 manosaba's trial peak, after batch 114 (below): about 3.3 GB of footprint for some 6 s at the first trial's start on the Mac (Metal: 105
  colour render targets 795 MB, 44 depth / stencil ones 574 MB, mostly 1372x2930 Depth32Float_Stencil8, 20 MB each; the guest's own memory
  1.3 GB), past an 8 GB iPad's 3071 MB without the increased memory limit (the signer: ios/README.md). The save: `work/run/mano/save_before_trial1`,
  Load Game → the Auto page → A1 of 2026-10-27 22:12, then R, fast-forward; `work/scripts/mano_trial_dev.sh` runs it on the phone (the Air,
  1920x1080: RSS 3240 MB and GPU 2949 MB at the peak). Left: the characters' pixels per unit lowered in the RomFS's `resources.assets` (PR #2's
  `patch_character_ppu.py` for the Steam build: 1500x3000 → 1125x2250 at 75 instead of 100, the Switch's file has the same 50 entries; a 2930-row
  target is drawn at most 1260 rows high on a phone), an asset change for the user to decide; the depth / stencil targets made memoryless, which
  needs each pass's store decided at its begin (now at its end), as 264's Vulkan backend needs too. Heaps buy little: in the census the targets'
  pools overlap in two pairs (9 MB).
- 281 native resolution: done for manosaba in batch 109 and for Tears of the Kingdom up to 1920x1080 in batch 121 (below). Left: Tears of the
  Kingdom above 1080 (the heap overlay of `games/totk/heapdef.py`, which on iOS also needs a larger application pool than `ios_pool_add_mb` 0 and a
  heap region above `guest_heap_mb` 2176 inside the 6144 MB reservation: none of it tried on a device), and for other games the backend route:
  render targets of the screen's shape made at the screen's size while the game keeps its size (LASRecomp's "Match display", AnySwitch's
  `DM_INTERNAL_RES`; our support buffer's `render_scale` is 1.0, `mtl/args.inc`), with a headroom guard (AnySwitch: none below 768 MB).
- 284 for 264's Vulkan backend: nxport's frame capture replayed pass by pass on both backends; Adreno 740-799 without the quad / warp32
  rewrites, subgroup helpers inlined, BC4 / BC5 as 8-bit where R16G16 does not filter (upstream 2461649, 758f5d4, 17b4baa); nx2apk's Darwin
  shim headers.

From the tenth review (2026-10-30, [docs/sources.md](docs/sources.md); 288 and 289 done in batch 122, the user's order: 301 next):
- 290 texture reads of guest memory the game unmapped, guarded (MacNativeGames f3bb714, PR #1232, MIT: SIGBUS in the deswizzle, the Depths):
  `gfx/nvn/mtl/upload.inc` reads `t->mem` unguarded on the translation thread.
- 291 quad strips as triangles, plain and indexed (MacNativeGames bff585f, PR #1216: the Hero's Path); ours skips the draw (`mtl/draw.inc`).
- 292 fade steps that truncate to 0 above about 63.5 fps (MacNativeGames 3e9e7f67 / PR #1112, #1134's rule: a positive step at least 1, zero
  stays zero): main 0x0a7ac4, 0x95dd24, 0x6155e4, 0x615660, met at `DM_PRESENT_HZ` 90 / 120 with realtime.
- 293 cutscenes at 60 fps (71c; mamcjr/TOTKRecompAndroid da06d5a, facts only): four functions run once a frame only in cutscenes (main 0x45c700,
  0x2b23c7c, 0x42c3bc, 0xa5a188); while one ran in the last 200 ms the interval chosen at 0x2db238 is at least 2.
- 294 fixedres: the tier stores at 0x584a70, 0x584b70, 0x584b94 held too (TOTKRecompAndroid 1e56bfb, facts only); ours holds the scale at 0x5848a8.
- 295 the translator's BRX chained form (nx2android 02e336f, MIT): 86 of Tears of the Kingdom's 28,652 shaders and 5 of manosaba's 2,740 have a
  BRX; translate them both ways and compare. The other two nx2android translator fixes are not needed with our `XLATE_DEBUG=1` (plan item 200),
  which the Android chain keeps.
- 296 smaller correctness from nxnative: argument blocks re-encoded when the serial generation changes (971d336), a draw after its pass ended
  (81ccacf, #600), a `tex_fit` view built from the root texture (d9c2d5e), a storage buffer of size 0 as the rest of the buffer (f293a70),
  vertex streams bounded (1c54ea0), a 2D texture in a 3D slot (2bccdcc), three host races (ce99604).
- 297 the virtual core released while waiting: write-back waits (MacNativeGames 0fd9358e, PR #1141), `nn::fs` and memory-pool setup
  (valentderah/totk-pc-recomp 101f2c8, MIT: loader threads held it up to 20 ms).
- 298 the crash handler with a time limit (jhaizhou-ops/d2r-mobile 3ebb29bd, n2ray f697868f): ours is also the SIGALRM handler and runs dladdr,
  fprintf and the dump hook with none.
- 299 iOS input: Home and Capture kept from the system (nxnative 062cd06), motion from the phone and controllers (659c69d, cca3333), rumble
  through `GCHapticsLocalityHandles` (2e07a88).
- 300 texture filtering at 1/256 of a texel as the Switch does, for vertex-shader lookups and gathers (TOTKRecompAndroid 8adbc5c, c02a43f, facts
  only): look at the terrain in motion for cracks and dark dots first.
- 301 manosaba on Android, on a Redmi K100 Pro Max (SM8850, Adreno 840, Vulkan 1.4.295, Android 17, 4 KB pages; adb shell: x18 kept,
  `cntvct_el0` at 19.2 MHz, `cntpct_el0` and a misaligned sp fault, `tpidrro_el0` reads 0, RW → RX and RWX allowed, 256 GB of PROT_NONE), the
  replay core shared with iOS, a Vulkan backend behind `nvn_backend.h`, in steps:
  301a the pool memory: AHardwareBuffer mapped over the pool (nx2android; HyperOS's gralloc refused it on a POCO F7 Ultra, BOTWRecompAndroid
  4fc0d28), an exported fd mapped at the pool's address (PKShield-Android `nvn_vk.c`), `VK_EXT_map_memory_placed` (no project uses it), copied
  pools with write-back last. Measured on the K100 in the adb shell domain (2026-10-30, a probe of ours): all four work at a fixed 4 KB-aligned
  guest address, a GPU copy of 32 MB right with no flush, the CPU as fast through each mapping as through plain memory (64 MB read 4.2-6.1 ms
  against 5.9); `VK_EXT_map_memory_placed` (memoryMapPlaced 1, memoryMapRangePlaced 0, memoryUnmapReserve 1, 4 KB alignment) maps type 6
  (device-local, host-visible, coherent, cached) exactly there and unmaps keeping the range. Left: the same in an app (untrusted_app; runcon from
  the shell cannot exec there), with 301d's APK.
  301d an APK: NativeActivity, AAudio, touch and controllers, our own APK writer and signer (no Android SDK; NDK r30 on the Mac). The host becomes
  a shared library: the assembly's branches and `adrp` to global symbols (`dm_hcall`, `nvn_slot`) must bind inside it (`-Bsymbolic` or hidden
  symbols; a PLT stub would take x16 / x17, which the thunks pass). MP4 / MOV movies through MediaCodec (`android/movie_none.c` reads none).
  301e the Vulkan backend, in `gfx/nvn/vk/` in the null backend's place (`gfx/nvn/null/null.c` lists the set: `nvn_backend.h`, the core
  headers' and nvn_mtl.h / nvn_obj.h's `mtl_*`, to be named for any backend when the second one lands): the NVN fence on the game's last own
  submission (LASRecompAndroid 54686f6), the swapchain in the window texture's sRGB format with a present semaphore per image (nx2android
  e27a2d4, nxport 426ec48), storage buffers at 64-byte alignment (the translator's `sb_align`), subgroups (64 lanes, none in vertex shaders on
  the 840: the warp32 rewrite), 284's items.
  301f frame rate and heat on the phone (nx2android #8 / #11: pinning to the prime core, the present wait counted as work): plat.h's
  interactive class and deadline declaration (Android's performance hint sessions), its readings (energy, per-core-kind counts) and
  `dm_thermal_now` (`android/diag_android.c`), which say nothing on Android yet.

With the next game (64): 64a2 ClearBuffer, the indirect and IndirectCount draws and dispatches, Downsample, the deferred uniform / storage binds;
64f tessellation; 64g HSETP2 `.H_AND` and translator call arguments (with the next change of the translation chain); 64h2 a SystemEvent kept while
its detached read end waits (UE4); 64j2 AVC / AAC decoded from the game's bytes; 64n2 LDN. Also [docs/sources.md § Port when a game needs
it](docs/sources.md#port-when-a-game-needs-it).

## Plan items from 260

Finished items from 260 on, in the archive's form (`[x] batch N` is the commit whose subject starts with "Plan batch N").

- [x] batch 85 · 260 min / max / clamp with Maxwell's results for a NaN operand on every GPU (seedds/nxnative 56704a0, MIT, written anew):
  spirv-cross writes the translator's FMin / FMax / FClamp as `fast::min` / `max` / `clamp`, which compile to `air.fast_fmin` / `fast_fmax` /
  `fast_clamp` in every Metal math mode, safe included, and leave a NaN operand's result to the GPU. The M5 happens to agree with Maxwell (the
  check of 2026-10-23 evening), an A20 Pro does not (nxnative: Super Mario Galaxy 2's `max(0, NaN)` lighting turned half of Mario white).
  `gfx/tools/msl_helpers.py` `precise_minmax` writes them as `precise::` (IEEE `fmin` / `fmax`), applied by `dmsh_build.py` after spirv-cross;
  every plain min / max / clamp left is an integer one (no fast intrinsic call in a 300-shader sample). Chain f9e7d3ecf49e → ae6a8bd1acdd
  (Tears of the Kingdom 28,675 shaders, 25,081 rewritten; its c1 list 5 as before), 217's 48 kernels made again. `tools/tests/t_msl_helpers.py`
  +7 (the rewrite, and a library compiled with iOS's flags calling `air.fmin` / `fmax` / `clamp` and no fast one); `T_MSL_PRECISE_OFF=1` fails 3.
  Smoke: Tears of the Kingdom's two red lines as before and its prologue frames the same by eye, manosaba's shots identical. The iPhone 17 Pro's
  (A19 Pro) own `fast::` results were not measured.
- [x] batch 86 · 261 the Metal backend split by subject: `gfx/nvn/nvn_mtl.m` (10,113 lines, 914 KB, 356 file-scope variables) keeps the
  device, queues, residency and deferred releases (295 lines) and includes 38 parts from `gfx/nvn/mtl/` in the original order, each a
  contiguous run of the old file cut where conditionals, braces and `@implementation` are closed (`work/scripts/split_mtl.py`, not in git).
  One translation unit as before: preprocessed (`clang -E -P`) for the Mac on Metal 4 and Metal 3 and for iOS 26 and iOS 18, the old file and the
  split give the same text but for blank lines and the 19 `__LINE__` values of 217's `KP_NO` (a pass dump's line numbers). The parts' quoted
  includes go one directory up; `host/Makefile` and `gfx/tests/Makefile` list the parts as headers, `t_red_once.py` reads them; the file
  table of `gfx/README.md` names each part. `tools/tests/t_mtl_parts.py` (6 checks: each part included once and none left out, each opening
  with its name, closing every conditional, brace and `@implementation` it opens, its includes resolving, both Makefiles rebuilding on a
  change); a part with an `#if` left open (`T_MTL_PARTS_BROKEN=1`) fails 1. Batch tests as in batch 85; smoke: the same red lines, the
  frames as batch 85's but for the animation's timing (looked at).
- [x] batch 87 · 262 the single-draw kernel passes of 217 removed: off by default, and on the iPhone they saved no GPU time. Out: the part
  `mtl/kpass.inc`, its hooks in `tex_desc` (shader-write usage on small targets), `rp_end`, `NvShader`, `bind_args`, `draw` and the
  `hle_nvn.c` statistics line, `nvn_kpass.h`, `gfx/tools/dmsh_kernel.py`, `t_kpass`, `t_kpnvn` with `gfx/tests/kpass/`, `t_dmsh_kernel.py`,
  `games/totk/kernel_shaders.txt` and the switches' rows in `gfx/README.md`; the kernels left in the workspace's chain folders went too. The
  backend is the one before 217 again: preprocessed for the four configurations (Metal 4 and Metal 3 on the Mac and iOS), it is the same text
  as 55de9bf's `nvn_mtl.m` with today's headers but for blank lines, and `hle_nvn.c` the same file. Git history keeps all of it (b921a1e).
- [x] batch 88 · 264, its first module: the store lookahead index in plain C. `gfx/nvn/core/` starts: its own translation units, no Metal,
  Objective-C or Grand Central Dispatch, a header for all the backend may use of a module and, under "The backend provides", what the module
  asks of the backend. `core.h` / `core.c` hold `MLOG`, `MDIE` and `RED_ONCE` with `g_red_once` (the backend's, the same lines);
  `rt.h` a mip level's extent, an attachment's subresource and whether a scissor covers a target; `prog.c` `prog_bind` (the program per
  stage); `store_index.c` the index (the scan, the buckets, the replay order check `sdx_seq`, the decision `sdx_first`), its state in
  `g_sdx`. The backend gives it a program's reflection per stage (`nvn_be_prog_refl`) and a binding record's texture serials
  (`nvn_be_bind_sers`); `mtl/store_index.inc` keeps the build at the submit's start and the ledger's check, `hoist_try` moves to
  `mtl/lookahead.inc`, `tile_merge` reads `g_sdx`. Both Makefiles build `core/*.c` (the iOS app follows the host's command). Tests:
  `gfx/tests/t_core_store_index.c` (35 checks on synthetic streams, the backend faked; a set-before-the-end attachment counting as set again,
  `-DDM_TEST_SDX_ANY_RTSEQ`, fails 2), `t_core_prog.c` (11; the whole program replaced, `-DDM_TEST_PROG_BIND_WHOLE`, fails 6),
  `tools/tests/t_core.py` (6: the files compile as plain C and include no framework or backend part, both Makefiles take them, each module
  has its test; a module importing Metal, `T_CORE_BROKEN=1`, fails 2); `t_red_once.py` reads `core/`. The backend's warnings are HEAD's.
  Batch tests as in batch 87. Smoke: the same red lines and frames (manosaba's title by eye: particles and the fade's timing); the store
  statistics of manosaba's log moved within what equivalent builds show run to run (b85 → b86: 5 of 13 lines).
- [x] batch 89 · 264, uniform snapshots: `gfx/nvn/core/usnap.c` takes `mtl/usnap.inc` (the walk, the deferred handles once a submission with
  `bh_fresh` / `g_bh_gen`, the radix sort, the fault-guarded copies) and the snapshot half of `mtl/capture.inc` (`mtl_sub_snapshot`'s body as
  `us_snapshot`, `us_find`, the GPU-write notes and drops, `us_report`); its state is `g_us` (`g_us.ent`, `.n`, `.gw`, the counters), and the
  merge, the running-write filter and a copy's place in the arena are shared helpers that the capture now calls too (`us_merge`,
  `us_gw_keep`, `us_gw_reaches`, `us_add`, `us_cap_copy`). The backend provides whether a range lies in one pool, the arena, worker threads
  for the copies in parts (`dispatch_apply_f`; core has no Grand Central Dispatch) and the completed fence; `us_drop` stays the backend's
  (it asks for the arguments to be encoded again). `core.h` gains `core_now_ns` (the uptime clock on Apple's systems, monotonic elsewhere).
  `gfx/tests/t_core_usnap.c` (31 checks with the backend faked, pools of host memory and a page made unreadable; the last copy found taken
  unchecked, `-DDM_TEST_US_LAST_UNCHECKED`, fails 7); `t_usnap.m` / `t_ussort.m` read `g_us.*`, and their broken variants that now live in
  core still fail as written (`DM_TEST_US_GUARD_EACH` 1, `DM_TEST_US_LAST_UNCHECKED` 9, `t_bindfreeze`'s `DM_TEST_BH_EACH_READ` 2).
  Batch tests as in batch 88 (host `t_sleep`'s short-sleep count failed once in the batch, as twice before, and passed three runs alone).
  Smoke: the same red lines; Tears of the Kingdom's frames the same scenes with the cutscene further on (looked at), its snapshot
  statistics the same at frames 300 and 600 and within timing after, each snapshot 2-10 µs slower (0.020-0.103 against 0.018-0.099 ms);
  manosaba's shots as batch 88's.
- [x] batch 90 · 264, the TBDR switches and the validity ledger: `core.h` / `core.c` take the pass dump (`PD`, `passdump` inline,
  `mtl_passdump_next`) with the frame number `g_cur_frame` and `g_stat_pass` / `g_stat_draw`; `gfx/nvn/core/tbdr.c` takes from
  `mtl/tbdr_xv.inc` the switches (`tbdr_on` / `tbdr_part` inline over `g_tbdr` / `g_tbdr_off`, the `TBP_*` parts, the ruler's
  `g_ruler_truth`, `udc_on`) and the ledger (its state `g_xv.*`, `xv_*`), and from `mtl/store_index.inc` the ledger's check against the
  index (`xv_forward`). The part keeps the store decisions' attachments, load elision, continuation and layered passes. Fixed on the way:
  `DM_NVN_TBDR_OFF=all` set the mask to ~0, which is the "not read yet" -1, so every call read the variable again and logged it; it is the 12
  parts' mask now. `gfx/tests/t_core_tbdr.c` (30 checks: the switches, the ledger with `DM_XV_FILE` read back, `xv_forward` against a faked
  submission; a full clear of another level counting, `-DDM_TEST_XV_ANY_LEVEL`, fails 1); `t_xv.m` reads `g_xv.*`. The pass dump of
  `t_rt_continue` and `t_defer_chain` (`DM_NVN_PASSDUMP=0-1000000`) is batch 89's line for line but for two command buffer addresses. Batch
  tests as in batch 88; smoke: the same red lines and frames.
- [x] batch 91 · 264, pending writes and frame measurements: `gfx/nvn/core/pend.c` takes from `mtl/occlusion.inc` the buffer writes encoded
  but not executed yet (`pend_add`, `pend_begin`, `pend_hits` inline, state `g_pend.*`) and from `mtl/capture.inc` `pend_read_x`; the
  backend's `pend_note` adds its submission's fence. `gfx/nvn/core/frame.c` takes from `mtl/frame_timing.inc` the boot instrumentation
  (`bp_now` inline, `g_bp.*`, `g_up_total_ns`), each frame's GPU time as the union of its command buffers' spans (`gpu_tag_begin` /
  `gpu_tag_done`, `mtl_gpu_ms`, `mtl_gpu_sum_ms`, `mtl_gpu_span`; `GIVR` in the header) and DM_NVN_UB (`g_ub.*`, `ub_frame`, `ub_skip`,
  `mtl_ub_present`); the command buffers' completion handlers and DM_NVN_GPUWATCH stay. `core.h` gains `core_lock` (`os_unfair_lock` on
  Apple's systems as before, a yielding spin lock elsewhere; `frame.c` compiles without `__APPLE__` too). `gfx/tests/t_core_pend.c` (15;
  a copy reading its source as it is now, `-DDM_TEST_PEND_COPY_NOW`, fails 2), `t_core_frame.c` (15; the plain sum, `-DDM_TEST_GIV_SUM`,
  fails 3); `t_ub.m` / `t_pend.m` read `g_pend.*` / `g_ub.*`. Batch tests as in batch 88; smoke: the same red lines, Tears of the Kingdom's
  frames the same scenes a little further on (looked at), its long frames' GPU times of the same shape (0.2 ms, one of 68 ms, 5-17 ms).
- [x] batch 92 · 264, the live texture: `gfx/nvn/core/live.c` takes from `mtl/live.inc` the texture living at each storage address
  (`mtl_tex_live`, `live_finalized`, `live_released`, `live_follow`, `g_fin_gen`, `g_live_follows`) and the texture objects' host copies
  (`mtl_tex_snap`, `tex_snap_retire`, `tex_snap_reap`, `SNAP_KEEP`); the part keeps the releases (`fin_drain`, `mtl_tex_release`).
  `gfx/tests/t_core_live.c` (13; the texture there followed whatever its shape, `-DDM_TEST_LIVE_ANY_SHAPE`, fails 1). Batch tests as in batch
  88; smoke: the same red lines and frames (Tears of the Kingdom's looked at), its shared-storage log lines 75 against 73.
- [x] batch 93 · 264, the submit call's capture: `gfx/nvn/core/capture.c` takes `mtl/capture.inc`'s capture for the translation thread
  (`mtl_sub_capture`, `mtl_sub_install`, `mtl_cap_free`, `struct mtl_cap` with `g_cap_cur` in the header), the snapshot from it and
  `mtl_sub_snapshot`, and the test hooks; the pool check and the completed fence are the backend's hooks of batch 89. What the part held
  besides went beside it (`us_drop` and `pend_read` to the end of `mtl/usnap.inc`, the declarations the later parts share to the start of
  `mtl/encoders.inc`) and the part is gone: 36 parts. `gfx/tests/t_core_capture.c` (10; only the writes still running at the replay
  counting, `-DDM_TEST_XLAT_CAP_STALE`, fails 1); `t_xlat`'s broken variants now in core fail as written (`NO_TOKEN` 2, `NO_CAPTURE` 2,
  `CAP_STALE` 1). Batch tests as in batch 88; smoke (Tears of the Kingdom replays on the translation thread): the same red lines and frames.
- [x] batch 94 · 264, inline uniform updates: `gfx/nvn/core/upd.c` takes from `mtl/uniform_rename.inc` the update log (`upd_list`, `ul_add`,
  `ul_clear`, `ul_seq`, `ul_apply`, and `ul_by_seq` for the writes across two pools), the submission's updates (`g_upd`, `g_uh`, `g_nupd`,
  `upd_add`, `upd_any`, `upd_apply`, `upd_overlap_seq`, `upd_sub_end`), the private copies (`upd_shadow_at`, through the arena hook) and the
  statistics (`g_ren.*`, `ren_report`); the part keeps the argument blocks an update makes stale, `upd_log`, `upd_shadow` (with the
  submission's serial) and the write to memory (`upd_flush`). `gfx/tests/t_core_upd.c` (18; an answer kept after the log changed,
  `-DDM_TEST_UL_SEQ_STALE`, fails 3); `t_ubren.m` reads `g_ren.*`; the broken variants now in core fail as written (`t_ubren`: `UBSH_ANY_SUB`
  2, `UL_NO_REVERSE` 1, `UL_SEQ_STALE` 6, `UB_FLUSHED_SNAPSHOT` 4 as at batch 93; `t_nvn`: `UB_NEWEST_FIRST` 1); `t_core_usnap`'s fake arena
  aligned as the backend's. Batch tests as in batch 88; smoke: the same red lines and frames; Tears of the Kingdom's rename statistics at frame
  300 are one of the two counts the batches since 88 alternate between (14,012 as in batch 91).
- [x] batch 95 · 263 the two switches off by default traced and removed. `DM_NVN_RT_RANGE` (a pass continued across a SetRenderTargets that
  only narrows a colour view's levels) came with the initial release (46578a2) and has no measurement on record; it spread over ten parts
  (`rt_range_on`, `rt_range_shape` / `rt_range_same` in lookahead, the read-only reflection `rt_range_bindings_ro` / `rt_range_pr_ro` and
  `NvShader.range_ro`, its branch in `mtl_exec`, the checks in `draw`, `chunks`, `metalfx_scene` and the fbx refusal, `g_pass`'s range
  fields, the `TBP_RANGE` part). `DM_NVN_DEPTH_PLAIN` (plan item 99's A/B: the same picture either way) leaves `tex_desc`. Every removed
  branch was gated by a switch off by default or by state only the candidate set, so the default path is unchanged. `DM_NVN_TBDR_OFF` reads a
  table of names and bits (the other parts keep their bits; "range" now names nothing). Tests: `t_rt_range` becomes
  `gfx/tests/t_view_levels.m` with what its `--disabled` run covered (two passes ended by hand or by the SetRenderTargets, the same
  picture, depth read-only first draw, occlusion samples, the attachment bytes with and without unwritten DontCare; 19 checks in each of
  four runs; the first targets taken for the second's, `--same-targets`, fails 2); `t_rt_range_shape`, `t_rt_depth_ro` and `t_depth_plain`
  (only the removed switches) go; `t_core_tbdr` +1 (31: "range" names nothing, the 11 parts); `t_rt_continue` no longer sets the removed
  metadata (its cases 2 and 3 never read it with the switch off). The batch's first gfx run stopped at that compile error before any test ran
  (Metal 3) or before t_present_fx (Metal 4, where the run always stops there, so it was not seen); the tests ran again whole after the fix
  (no compile error, all as in batch 94), and the batch summary counts compile errors since. Smoke: the same red lines and frames.
- [x] batch 96 · 264, the replay state: `gfx/nvn/core/state.c` takes the draw state `S` (from `mtl/pipeline.inc`) and the render targets'
  state `R` (from `mtl/passes.inc`), the state at the start (`st_reset`) and the commands that only set state (`st_op`, from
  `mtl/lookahead.inc`). `st_op` answers what changed (`ST_PSO`, `ST_DSS`, `ST_ARGS_ALL`, `ST_ARGS(stage)`, `ST_HANDLES`) and the backend's
  `state_op` marks its own encoder state from it, notes GPUWATCH's bindings and rebinds border colours; `state_reset` marks everything dirty
  after `st_reset`. A clear colour is `nvn_rgba` (Metal's field names) in `R` and in what deferral, the clear-only hoist and the tile merge
  keep, made a Metal one where a pass descriptor takes it. `gfx/tests/t_core_state.c` (25; a depth-stencil change taken for a pipeline one,
  `-DDM_TEST_ST_DSS_AS_PSO`, fails 1); `t_fuse_check` and `t_rt_srgb_view` set `R`'s clear colour the new way (the batch's first gfx run
  stopped there; all 118 test targets build for Metal 4 and Metal 3 now and the tests ran again whole). Smoke: the same red lines and frames.
- [x] batch 97 · 264, pass planning: `gfx/nvn/core/pass.c` takes from `mtl/lookahead.inc` where the replay is (`g_exec_cur`, `g_exec_next`,
  the return stack, the remaining handles, `mtl_exec_rest`) with the cursor in replay order (`op_cursor` / `op_next`, inline; they were
  `fz_cursor` / `fz_next`), the ops run early (`g_hoist_skip`, `hoist_skipped` inline, `hoist_take`), the depth / stencil lookahead
  (`pass_ds_need`) and the store decision (`sd_decide`; the backend's `sd_finish` only sets each store action from it); from
  `mtl/tbdr_xv.inc` the store decisions' attachments (`sd_att`, `g_sd`), and from `mtl/passes.inc` the bandwidth ledger. `gfx/nvn/core/rt.c`
  takes `rt_same` (from `mtl/lookahead.inc`), `rt_same_sub` and `rt_dup_slot` (from `mtl/renderpass.inc`; the write mask read from `S`).
  `gfx/tests/t_core_pass.c` (17; a clear inside the pass taken as needing neither, `-DDM_TEST_DS_CLEAR_FREE`, fails 1) and `t_core_rt.c`
  (9; `t_rt_continue`'s `-DDM_TEST_RT_SAME_NO_VIEW` fails 1 there, and 1 in `t_rt_continue` as before). The backend's warnings as before; the
  pass dumps of 12 gfx tests through these paths (257 store decisions, 174 depth / stencil lookaheads) line for line as before.
  `gfx/README.md` no longer lists `range` among the `DM_NVN_TBDR_OFF` parts (gone in batch 95). Smoke: the same red lines and frames (Tears of
  the Kingdom's torch flame and manosaba's title fade at another moment, looked at).
- [x] batch 98 · 264, the clear-only hoist's plan and the deferral's decisions: `gfx/nvn/core/pass.c` takes the hoist's scan (`hoist_plan`:
  the groups, their ops put in the skip set; the backend's `hoist_try` runs each as a clear-only pass) and the next segment after a list's
  end (`exec_cont`, was `defer_cont`; the zero-source skip's occlusion check uses it too). `gfx/nvn/core/defer.c` takes the deferred merged
  pass's groups (`defer_t`, the slots, `defer_next`, `defer_release_one`, `g_defer_replaying`) from `mtl/lookahead.inc` and
  `mtl/chunks.inc`, and from `mtl/tile_merge.inc` its switches, the index's screen (`defer_pre`), the scan (`defer_try`), the kept draws
  (`defer_take`) and Q's targets and load actions (`defer_q_enter`; the backend's `defer_begin_q` ends the pass before Q first, and it keeps
  drawing the kept draws and the fallback). `rt_sub_eq` (was `sub_eq`) goes to `core/rt.c`. What the decisions need of the backend: a
  handle's texture serial (`nvn_be_handle_ser`) and its view (`nvn_be_handle_view`), and the program's reflection (`nvn_be_prog_refl`, as
  the store index). Two functions nothing called (`defer_release`, `defer_apply_p_clear`) are gone. Fixed on the way: at Q a slot no P
  colour maps to (`lmap` 0xff) indexed the group's clears at 255, past its 8 entries, into the high byte of its clear colour 7's green, so
  such a slot was cleared whenever an earlier clear of colour slot 7 had left a non-zero green (whether a game reaches it is not known); it
  is skipped now. `gfx/tests/t_core_defer.c` (16; the old indexing, `-DDM_TEST_DEFER_LMAP_ANY`, fails 1); `t_core_pass` +7 (24; a cleared
  texture on a slot hoisted all the same, `-DDM_TEST_HOIST_BOUND_OK`, fails 1). The backend's warnings as before; the pass dumps of 11 gfx
  tests (9 deferral groups made, 16 refused) line for line as before. Smoke: the same red lines and frames (Tears of the Kingdom's cutscene
  a moment further on, looked at; its Mac default keeps these rewrites off).
- [x] batch 99 · 264, the readable-tile pass merge's plan: `gfx/nvn/core/fuse.c` takes its state (`fuse_t`, `g_fz` and its counters) from
  `mtl/lookahead.inc` and from `mtl/tile_merge.inc` the index's screen (`fz_prefilter`), whether a program samples a texture
  (`prog_samples_ser`) and the plan (`fuse_try`; whether the last pass opened was a clear-only one is now its argument). What the backend
  still does at a merge: the coverage of the later pass's first draw, asked through `nvn_be_fuse_cover` (was `fz_probe_cov2`), the same-pixel
  proof and specialization at each later-pass draw, the in-run check. `gfx/tests/t_core_fuse.c` (11; attachments of another size taken into
  the union, `-DDM_TEST_FUSE_ANY_SIZE`, fails 1). No gfx test plans a merge and the smoke runs keep these rewrites off or never reach them, so
  Tears of the Kingdom's prologue ran with `DM_NVN_TBDR=1` and the pass dump on, this build against batch 98's (75 s each): neither merges
  nor hoists there; both refuse deferrals for the same reasons in 1815 of the 1934 frames both reached (the others around scene changes,
  reached at other moments), with the same red lines. The backend's warnings as before; the pass dumps of 11 gfx tests line for line as
  before. Smoke: the same red lines and frames.
- [x] batch 100 · 264, the execution interface and the replay loop: `gfx/nvn/nvn_backend.h` declares what the replay hands a backend, command
  by command (`nvn_be_reset`, `nvn_be_state`, `nvn_be_draw`, `nvn_be_dispatch`, the copies and the texture clear, `nvn_be_copy_b2b`,
  `nvn_be_update_ub`, `nvn_be_signal_event` (they were `mtl_sub_copy`, `mtl_sub_update`, `mtl_sub_event`), `nvn_be_sub_fence`, and the
  records whose handling still mixes decisions with the backend's own pass state: SetRenderTargets, the clears, render enable, the counters,
  barriers, the descriptor pools). `gfx/nvn/core/exec.c` takes the replay loop from `mtl/exec.inc` (`nvn_exec` and `nvn_exec_rest`, they
  were `mtl_exec` and `mtl_exec_rest`): the walk, the return stack, the ops run early, the deferred groups' P end, the commands that only set
  state (`st_op`, the backend marking what changed in `nvn_be_state`), FenceSync, the work count (`g_sub_work`); with it the continued pass's
  mark (`g_rt_merged`) to `core/pass.c` and `fz_unskip` to `core/fuse.c`. The record handlers hold the case bodies as they were.
  `gfx/tests/t_core_exec.c` (14; the call itself pushed for the caller, `-DDM_TEST_EXEC_RET_AT_CALL`, fails 1); `t_rqueue`'s
  `-DDM_TEST_FENCE_SYNC_LAST` still fails 5. The backend's warnings as before; the pass dumps of 13 gfx tests (16,455 lines of
  `t_visibility` among them) line for line as before. The submit path's instructions per draw in Tears of the Kingdom's smoke run within
  the spread of batches 96-99 at every checkpoint (frame 600: 28,061 against 28,045-28,105). Smoke: the same red lines and frames.
- [x] batch 101 · 264, SetRenderTargets, the clears and conditional rendering: `gfx/nvn/core/exec.c` decides SetRenderTargets (a waiting
  deferred group's Q, the same targets continued, another layer of a layered pass, new targets; `defer_begin_q` is gone) and the two clears
  (the next pass's load action, or drawn), asking the backend for its pass through the interface (`nvn_be_pass_mode`, `nvn_be_pass_flush`,
  `nvn_be_pass_end`, `nvn_be_layered`, `nvn_be_layer_continue`, `nvn_be_clear_quad`, `nvn_be_clear_ds_quad`); `fz_reuse_op` goes to
  `core/fuse.c`. `gfx/nvn/core/cond.c` takes conditional rendering from `mtl/args.inc` and `mtl/exec.inc`: render enable (`g_render_off`,
  `cond_render_enable`), its counts and the samples-passed destinations (`occ_note`, `occ_pending`, which reads the completed fence through
  `nvn_be_done_fence`, the same locked read). `gfx/tests/t_core_cond.c` (10; a result still on the GPU taken as complete,
  `-DDM_TEST_COND_PENDING_DONE`, fails 4); `t_core_exec` +14 (28; a clear inside an open pass taken as a load action,
  `-DDM_TEST_EXEC_CLEAR_IN_PASS`, fails 1). The batch procedure runs in about 7 minutes instead of 12, with the same tests (work/scripts:
  builds at once, each test recipe's commands several at once with a failure run again alone and still counted, the smoke runs on an idle
  machine); `make run-m3` copies the tests with their times, so make rebuilds only what changed. The backend's warnings as before; the pass
  dumps of 15 gfx tests line for line as before. Smoke: the same red lines and frames.
- [x] batch 102 · 264, the draw part split: the copies between buffers and textures, the texel reads and writes and the texture clears go from
  `mtl/draw.inc` to `mtl/copies.inc`, and the submission's failure-line context (`cctx_snap`) to the start of `mtl/exec.inc`, so the
  preprocessed backend stays byte for byte the same (`clang -E -P`, Metal 4 and Metal 3). `draw()` is three stages: what keeps the draw from
  drawing or comes before it (`draw_admit`), the render pass it draws in (`draw_pass`), its encoding (`draw_encode`); the first checks, which
  need only the state (rendering off, no program, depth bounds that admit no depth), are `exec_draw_dropped` in `core/exec.c`, still asked
  first by `draw()` (also how kept and merged draws are drawn again). `t_core_exec` +6 (34; `t_dbounds`'s `-DDM_TEST_DBOUNDS_EMPTY_DRAWN`
  fails 1 there; in `t_dbounds` still 2 without the Metal debug layer and Metal's assertion with it, as before, now noted there). The
  backend's warnings as before (24 of them now say the encoder is a `const __strong` parameter); the pass dumps of 19 gfx tests line for line
  as before; the submit path's instructions per draw within the spread of batches 96-101 (at its top at three of six checkpoints, under 0.5 %
  from the bottom). Smoke: the same red lines and frames.
- [x] batch 103 · 264, the backend's translation units: `gfx/nvn/mtl/mtl.h` is the Metal backend's private header (nvn_mtl.m's includes and
  Metal 4 / Metal 3 macros, its quoted paths one directory deeper, then what its translation units share), and the GPU keep-alive is the
  first part of its own, `gfx/nvn/mtl/keepalive.m` (`g_inflight`, `g_ka_idle`, `g_last_sub_ns`, `g_ka_n`, `g_ka_us`, `keepalive_start`
  declared in the header; `g_dev` is no longer static). Both Makefiles build `nvn/mtl/*.m` (the iOS app follows the host's command) and
  rebuild on `mtl/*.h`. `t_keepalive` (3) as before. The backend's warnings as before, keepalive.m none on Metal 4 or Metal 3. Smoke: the
  same red lines and frames; the submit path's instructions per frame within the spread (per draw they moved with the draws a frame: 502 at
  frame 600 against 521-522).
- [x] batch 104 · 264, three more translation units of the backend: `mtl/frame_timing.m` (the frame number, GPU time tags, DM_NVN_GPUWATCH),
  `mtl/passgpu.m` (DM_NVN_PASSGPU) and `mtl/present_pipe.m` (the present pipeline and its encoding). What crosses is declared in `mtl/mtl.h`:
  `gw_note` (inline, as before, over `g_gw_a`), `gw_commit`, the `pg_*` calls, the present pipeline's calls and its test switches; from the
  main unit `g_commit_blocks`, `lay_report`, `pass_dims`, the present blit's state, and the sRGB view table (`g_pfv_srgb`, a small constant
  table now in the header). The parts were chosen by compiling each alone with the header: these three use one to four names from the
  others. Their tests as before (`t_passgpu`, `t_present_*`, `t_gputime`, `t_linkfifo`); no basename clashes with the host's sources (the
  iOS build puts them in one directory). The backend's warnings as before, the three new units none; the pass dumps of 15 gfx tests line for
  line as before but `t_rstream`'s `bind_ub` lines, whose count varies from run to run (4719-4753 on the previous commit, 4707-4734 now; its
  other lines the same). Smoke: the same red lines and frames; the submit path's instructions per frame within the spread.
- [x] batch 105 · the interface layer in parts: `gfx/nvn/hle_nvn.c` (220 KB) keeps its includes and includes, in order, 17 parts in
  `gfx/nvn/hle/` (one translation unit, as nvn_mtl.m's parts): the slot table, the device, pools, textures, samplers, programs, sync objects,
  the queue, command buffers, render commands, state objects, suspend and resume, the translation thread with `nvnQueueSubmitCommands`, the
  commands with nothing to do (`g_nops`), windows, timing instrumentation, and frame pacing with `nvnQueuePresentTexture`. Cut where the
  content changes, which is not always at the old section titles (the translation thread's section went on into the no-op commands, the
  timing section into pacing and the present). The preprocessed file is the same but one empty line at its end (`clang -E -P`, Metal 4 and
  Metal 3); the two quoted includes inside a part look one directory up. Both Makefiles rebuild on `hle/*.inc`; `tools/hle_args.py` reads
  the parts (477 functions checked, as before); `gfx/README.md` lists the parts and points `shader_key` and `g_nops` to theirs. Smoke: the
  same red lines and frames.
- [x] batch 106 · `host/` in directories: its 62 sources move (`git mv`, history kept) into `hle/` (`hle.c` and the 28 `hle_*.c`),
  `runtime/` (loader, main, entry, host stack, threads, crashes, options, RomFS parts and validation), `front/` (the Mac front end, controllers,
  the touch pad, the command channel), `media/` (the movie decoders), `diag/` (diagnostics, memory snapshots and readouts, the SDK checks,
  thermal state, sead heaps), `game/` (the default hooks, one game at a time, mods, pchtxt patches, cheats, the wide picture) and `cloud/`; the
  headers stay in `host/`, found through `-I.`. Paths follow: `host/Makefile`'s list, the host tests' Makefile and the sources they include,
  the graphics tests (`-I../../host` for the host sources they link), `ios/tools/gen_build.py` (where `main.c` is), `tools/hle_args.py`
  (still 477 functions; `host/tests` left out) and `tools/hle_inventory.py` (still 239 entries), and every `host/<file>` named in the code and
  the current documents (this file's history and the plan archive keep the old names). Both hosts, the 173 host test commands and the tool
  tests as before. The batch's Metal 3 suite first ran with the batch script's own flags, which lacked the new include path (106 builds
  could not find `dm.h`); the script takes the Makefile's flags now, as `make run-m3` does, and the suite ran again: 148 commands, the
  same results as batch 105. Both IPAs built. Smoke: the same red lines and frames.
- [x] batch 107 · the core tests' shared fakes: `gfx/tests/core_fakes.c` holds every hook gfx/nvn/core asks of a backend (the modules' "The
  backend provides" and `nvn_backend.h`) as a weak default, with the host's empty module table and present-link switch; `gfx/tests/core_test.h`
  holds `check` (and `check_x`, in hexadecimal, for t_core_state). The 16 core tests drop the copies that matched the defaults (they keep the
  ones that differ); each links all the modules, the fakes and the host's crash handler through `CORE_C` / `CORE_DEP`, so a new module or test
  needs no list of its own. The 16 tests' results as before, and their 19 documented broken variants fail by their documented counts.
  `make run-m3` copies the tests' headers too (core_test.h): without it the batch's Metal 3 suite found no recipe and the batch script took
  that for no failure (it fails on it now); the suite ran again, 148 commands, the same results as batch 106. Smoke: the same red lines and
  frames.
- [x] batch 108 · 265 GPU write-back records that outlived their pool (MacNativeGames 0930478 / 873edff, MIT, written anew: after a warp, Tears
  of the Kingdom's heap blocks, an allocator node and index buffers were overwritten with an old rendering): a finalized pool takes the records of
  the textures in its memory with it (`wb_drop_range` from `mtl_pool_del`), and a dirty-tracking range keeps the clock it was registered at, so a
  stamp older than its range counts as written (`range_t.born`; a new range's generations used to start at 0, so a stale record read as
  fresh). `gfx/tests/t_wb` (+2: -DDM_TEST_WB_KEEP_DEAD fails 1, with -DDM_TEST_DIRTY_NO_BORN as well 2, the old rendering then written over the
  game's data) and t_dirty_stamp (-DDM_TEST_DIRTY_NO_BORN fails 1).
- [x] batch 108 · 266 the dirty-page stamps around an upload (seedds/nxnative 5b0c11a, MIT: Tears of the Kingdom's glyph letters missing): the
  stamp is taken before the pages are protected (`nvn_dirty_protect_stamp`, for the packaged-texture upload, the CPU-filled texture watch and
  the write-back note; a write that faulted in between took the generation the stamp then read); a packaged texture's `seen` is its upload's
  stamp, not the clock after it; a page another thread is releasing when the protection comes is waited for and protected, no longer left
  writable with a generation older than the stamp. `gfx/tests/t_dirty_stamp` stages each interleaving at two hook points nvn_dirty.c leaves
  empty: -DDM_TEST_DIRTY_STAMP_AFTER and -DDM_TEST_DIRTY_SKIP_BUSY fail 1 each.
- [x] batch 108 · 267 upload batches bounded (nxnative 7dc250e, MIT: Tears of the Kingdom peaked at 10.7 / 12.8 GiB on its Mac, an A20 Pro
  iPhone was killed at 8.09 GiB with 1.1 GB re-uploaded in one frame): an upload batch is committed once its staging would pass
  `DM_NVN_UP_BATCH_MB` (128; 0 unbounded), the three batch slots in flight holding the next back, and a committed batch's one-off blocks and
  CPU-path buffers are released when that batch completes (`up_retire`), no longer after the next render fence. `gfx/tests/t_upbound`:
  -DDM_TEST_UP_UNBOUNDED fails 3, -DDM_TEST_UP_RETIRE_AT_FENCE 1.
- [-] 268 nxnative 82ab25b's present allocator reused only after its own present completed: not reachable here. The drawable is taken before
  the allocator is waited for and reset, and each of the layer's three drawables returns only after the present that showed it, so the
  allocator of three presents back has finished. Left as it is.
- [x] batch 108 · 269 the memory limit in the iOS boot log (xinerqu/ns2ios-shell 8160029's line, MIT, written anew): `boot: memory limit <N> MB
  (footprint + available), RAM, and whether the increased memory limit is in the app's signature`, read back from the executable's own code
  signature (`ios/Sources/codesig.h`), so a low limit says whether the signer left the entitlement out. `host/tests/t_codesig` (copies signed
  ad hoc true / false / without entitlements; -DDM_TEST_CODESIG_ANY_VALUE fails 1). ios/README.md lists which signers keep it (the ninth
  review). Also: the batch script's host build takes the run target's prerequisites only (names in its recipe gave three "No rule" lines).
  gfx 150 commands on Metal 4 and 150 on Metal 3, host 175, as before plus the new tests; both IPAs built. Smoke: the same red lines, frames
  within the usual spread, instructions a draw as before.
- [x] batch 109 · 281 native resolution for manosaba (the user, 2026-10-27: "game default" is 1920x1080, the portraits looked soft at 720p).
  Unity's Switch player answers its display size with the constant 1920x1080 in two places (the screen manager's vtable +0x180 at
  main+0x90c990, the graphics device's at main+0x99bcc0), and its window textures and Screen.SetResolution follow it; it never asks
  `nn::oe::GetDefaultDisplayResolution` (reporting 2560x1440 there changed nothing). Both are cut to host call 0x81 (`rules.py`,
  `graphics.c` `hc_display_size`), which answers `DM_DISPLAY_SIZE=<w>x<h>` (unset: 1920x1080 as before). With 2560x1440 on the Mac the
  window textures are 2560x1440 and the game renders its own 2560x1440 picture, menus and text box in place, the portraits' lines sharper than
  at 1080 (with only the first cut, the picture reached the window's top-left 1920x1080). The iOS setting "Native (manosaba)" (Settings and the
  panel; `dm.render_height` -1) reports the 16:9 picture the screen's pixels hold and renders at it (an iPhone Air: 2240x1260). Memory grows with
  the picture (the 3071 MB A16 iPad above has no room for it). `host/tests/t_mano_display` (unset, 2241x1261 → 2240x1260, malformed;
  -DDM_TEST_DISPLAY_CONST fails the second); `verify/regress.sh --fuzz` OK (main: 6 cuts, 3 gates). Also 270's `DM_IOS_DRAWABLE_H`. gfx 150
  commands on Metal 4 and Metal 3, host 179; both IPAs; smoke as batch 108. On the iPhone Air after the commit: Native renders 2240x1260,
  the picture whole and in place, GPU memory 515 against 496 MB at the game's size.
- [x] batch 110 · 226 the submit call's capture walks its lists breadth first (`gfx/nvn/core/capture.c`). Tears of the Kingdom submits one
  list a call, which reaches about 3,300 lists of two to five records through four levels of calls (by depth 1 / 13 / 58 / 2,301 / 951), and
  the walk, going into each call as it met it, waited on memory at every list. On the M5 Mac standing on Great Sky Island (means of 3,000
  calls): the capture 350 us, of it the lists 246, sort and merge 29, the allocation and the uniforms' copy 77 (0.9 MB). Calls now go on a
  queue and the list 48 places on is fetched as one is taken; in one build the lists took 215 us depth first, 190 with the queue, 167 fetched
  16 ahead, 161 fetched 48 ahead (the capture 322 → 265 us). Tried and left: notes made at recording of where the records the capture needs are
  (with a table big enough every list found its notes and a check against the walk found 0 of 3,000 calls differing, but 359 against 347 us:
  each list's lookup waits on memory as its walk did); 272, each list's walk kept while it lives (nxport 9c5463d; the lists that cost are
  recorded anew every frame); no de-duplication of the uniform ranges before the sort (364 us). `t_core_capture` +3 (breadth-first order, a
  list called twice, call depth 32 and not 33; -DDM_TEST_CAP_DEPTH_FIRST fails 1). gfx 150 commands on Metal 4 and Metal 3; host 179, of which
  t_fs_kill failed once under the run's load (its writer got through 59 commits; the check wants more than 60) and passed alone; both IPAs. Smoke: the
  same red lines, Tears of the Kingdom's frames the same by eye, manosaba's within 1.1, instructions a draw as before.
- [x] batch 111 · 279 the GPU memory budget waits before it stops (MacNativeGames 4de7b4e, MIT, written anew; `mtl/commit.inc`
  `gpu_budget_settle`): over the budget, the deferred releases and the upload staging of completed batches are released while the running work
  completes, at most 2 s, ending early after 20 ms with nothing running or completing; a spike it recovers from is logged, and only memory still
  allocated stops the process. A zero-copy pool buffer Metal refuses is retried 8 times, 2 ms apart doubling (MacNativeGames 11ea252; the
  pages stay protected, so no write escapes the tracker; never seen in our logs). `t_poolcheck` +4 (-DDM_TEST_BUDGET_NO_WAIT fails 3;
  -DDM_TEST_POOL_NO_RETRY stops at the refused buffer). 282 res_add / res_remove in their own autorelease pool (nxnative 7d368c4): the weak
  table's containsObject: autoreleased the object on guest threads, whose pool never drains; Tears of the Kingdom on the Mac (DM_RES_CENSUS=1,
  standing): retired but not destroyed 25 textures → 0, RSS 12 MB less, GPU allocated the same (they were heap members). `t_respool` (a thread
  with no pool; -DDM_TEST_RES_NO_POOL fails 1). [-] 277 measured, nothing changed: heap members keep lossless compression (an M5 probe read
  cleared and blitted texels right through an R32UI target at the same heap offset, now `t_rtheap` +2; GPU p50 with it off 7.21 / 7.37 ms
  against 6.66 / 9.83 on, within the runs' spread, one run to the next differing by 1.3 ms with the same code); no
  `MTL4VisibilityOptionResourceAlias`: nxnative's case is two Metal buffers over the same bytes, which ours never makes (MapVirtual points the
  virtual pool at the physical pool's memory; of Tears of the Kingdom's 2,686 pools none overlaps another's guest range, 11 share a 16 KiB page
  only, 4 KiB-aligned; manosaba's 25 none), and heap textures at one offset have one GPU address. 226 closed (its rest under 271). gfx 151
  commands on Metal 4 and Metal 3, host 179; both IPAs. Smoke: the same red lines, frames the same by eye, instructions a draw as before.
- [x] batch 112 · 278 GPU results written back after the game's fence waits (MacNativeGames 1bffa28, MIT, written anew; `mtl/writeback.inc`
  `mtl_wb_fence_done`, from nvnSyncWait when signalled and nvnQueueFinish): a target up to 64 KiB in a CPU-visible pool whose rendering has
  completed is written back with no invalidate, as on the console, where the bytes are in the pool once the fence has passed; Tears of the
  Kingdom reads its grass cut map (128x128 RGBA8) that way (MacNativeGames: no blades, no insects, the grass not cut). The CPU's newer bytes
  stay as on an invalidate (the invalidate's checks now in `wb_write_list`); 3D, multisampled and BC targets stay for an invalidate; each new
  shape written back this way is logged; DM_NVN_WB_FENCE=0 turns it off. Standing on Great Sky Island (M5): one fence wait in 18 writes back a
  96x96 target, 0.6-1 ms on the game's thread for the GPU's round trip (286 is the follow-up). Not yet seen in a grass field here: the scenes
  reached (Great Sky Island's platform and cliff, a shrine, a cave) render no cut map. `t_wb` +6 (-DDM_TEST_WB_INVALIDATE_ONLY fails 3).
  Also: `host/tests/t_fs_kill` kills on past 60 times until its writer has got through more than 60 commits (at most 180; it failed under the
  batch's load twice with 59). gfx 151 commands on Metal 4 and Metal 3, host 179; both IPAs. Smoke: the same red lines, frames the same by
  eye, instructions a draw as before.
- [x] batch 113 · 287 a depth / stencil texture bound as a colour target is left out (`mtl/renderpass.inc` rt_color_fmt, red line 116), and
  the parallel precompile predicts a depth attachment only for a depth-renderable format (`mtl/lookahead.inc`, as the replay's 64m does):
  manosaba, 26 minutes into a session on the Mac with several loads, at the first trial's start bound a Depth32Float_Stencil8 target as colour 0
  and an RGBA8 one as depth, and the precompile's background compile of that pass's pipeline hit Metal's descriptor validation, which ends the
  process (`depthAttachmentPixelFormat MTLPixelFormatRGBA8Unorm is not depth renderable`; the replay itself had left the pass out). A fresh
  session from the save before the trial reached the trial without that binding. `t_mtlguard` +2 (-DDM_TEST_COLOR_ANY_FORMAT and
  -DDM_TEST_PREDICT_ANY_DEPTH fail 1 each, their child ending on SIGABRT). Also: the save before manosaba's first trial for 280; its
  controls: R fast-forward on / off (Options → 文本快进模式 全部 for unread text), X the phone menu (save, load, options, help), Y the
  encyclopedia, B hides the text box; the investigation map takes UP to a location, LEFT / RIGHT between floors, A to go there; chapter 1's
  choices that end the game: the first option of the first two, the second of the ones at Noah's room and in the shower room.
- [x] batch 114 · 280, part: a shared texture whose pool was finalized goes STOR_GONE_IDLE (16) submissions after its last holder left, instead of
  STOR_IDLE (600; `mtl/textures.inc` stor_drop_range from mtl_pool_del). manosaba gives each render target of a trial's start a pool of its own and
  finalizes both together; those entries held the peak up: on the Mac (`DM_TEST_MEM_LIMIT_MB=3600`'s steps) 3572 MB of footprint or more → 3216-3344
  MB, GPU allocated 3927 → 2338 MB in the memory line (sharing off altogether: 2955 MB of footprint, so the rest the game holds). A texture ID used
  within the window, or a new object at that storage, still finds it, and holding it makes it live again. `DM_NVN_RT_CENSUS=1` (the render targets
  of recorded commands by pool: Metal bytes, the union of their guest ranges, overlaps). `t_stor` +3 (-DDM_TEST_STOR_KEEP_GONE fails 1).
- [x] batch 115 · 286 278's write-back without a GPU round trip on the game's thread: the submission that renders such a target (up to 64 KiB, not
  3D, multisampled or BC) copies it to a buffer of its own at its end (`mtl/writeback.inc` wb_stage_encode, from mtl_sub_commit), and the write-back
  after a fence wait puts those bytes into the storage when they are of the newest rendering (else it copies on the GPU as before; an invalidate
  still does). Standing on Great Sky Island (M5): 0.6-1 → 0.043 ms a write-back, all 400 from copies, one copy for each; the frame time's p95 17.2
  → 16.8 ms, as before 278. `t_wb` +2 (-DDM_TEST_WB_STAGE_OFF fails 2).
- [x] batch 116 · 283 the pipeline guesses compile on at most two threads, the oldest kick first, and a draw needing one no thread has started yet
  compiles it on the submit thread (4ppp44 cb21bec, MIT, written anew; `mtl/pipeline.inc`; each kick was a dispatch_async of its own, a take
  waited even for one not started; `DM_NVN_PP_WORKERS=<n>`, 0 as before). The iPhone Air, manosaba's first trial from a cold shader cache
  (`DM_CLEAR_SHADER_CACHE=1`, no learned list), two runs each: long frames 37 for 2,688 ms in all against 60 for 3,338 ms; the submit thread
  waits longer on guesses already compiling (118 against 18 ms) and compiled none itself. The prediction line gives the waits. The tile-read
  pipelines MacNativeGames 59e240a added to its seed are in ours already (R rows carry the merged and tile-read keys, the warm-up builds them;
  no pipeline made in play standing on Great Sky Island). 271, part: texture views by their base texture and how they view it
  (`mtl/descriptors.inc`, let go VIEW_IDLE (8) submissions after their last use): Tears of the Kingdom made about 35 a frame again for new serials
  on the same storage, so the same MTLTexture; tex_by_id 6.8 → 5.0 % of the translation thread, encode_args 20 → 17 %. `t_ppred` +3
  (-DDM_TEST_PP_UNBOUNDED fails 2), `t_stor` +2 (-DDM_TEST_VIEW_NOCACHE fails 2). [-] 274 measured: of the 4 render passes a frame a dispatch ends
  on Great Sky Island, 1.2 are taken up again with the same targets, and the one that costs (17.6 MB loaded again a frame) is 218's depth probe
  bc58d5dec860c367_b, which reads the depth the pass wrote, so it cannot run before the pass (the other, db0984679ece41d3_b, 0.1 MB). [-] 275
  measured: the completion handler runs 0.19 ms after the GPU's end on the M5, and Tears of the Kingdom waits on a submission still on the GPU
  0.00 times a frame on the Mac, 0.16-0.19 on the Air (0.42 ms a frame waiting): the event's earlier wake would give about 0.03 ms a frame.
- [x] batch 117 · 276 / 131a the game's own dynamic resolution: the controller main+0x584300 (MacNativeGames 2279d4b, MIT, written anew) steps the
  scene through 100 / 90 / 80 % by predicted frame time and returns the tier's scale through one word of its epilogue (0x5848a8, mov w9, w23); the
  option fixedres (`games/totk/rules.py`, off by default; on iOS it must be built in, DM_OPTIONS at the build) makes it 1.0. `[scene size] the
  game's dynamic resolution over 600 presents` now gives the values the renderer holds at +0xccc when they vary (`games/totk/frame_step.c`
  dr_note): on the iPhone Air at 30 fps the game sits at 90 % or 80 % much of the time (one window 519 of 600 presents at 0.9), on the Mac at a
  1920x1080 scene 80 % in 10-20 % of the presents (1.2 of 1.5), and with fixedres 1.5 throughout. Both products rebuilt (binaries the same, the
  option row and the rewriter stamp new; the old ones kept as `.prev276`); `verify/regress.sh --fuzz` OK for the three modules.
  `tools/tests/t_totk_fixedres.py` (the epilogue in Unicorn: a tier of 0.8 comes out as 1.0, bit 33 kept; T_FIXEDRES_ORIGINAL=1 fails 1).
  [-] 273 measured on the iPhone Air (sustained, standing on Great Sky Island): 30 fps with the translation thread and without (frame p50 33.3
  / 33.5 ms, GPU 28.2 / 28.0 ms); the thread takes 2 ms of the game's submit and present (7.5 against 9.6 ms), and the GPU sets the rate, so
  the default stays on.
- [x] batch 118 · 285, part: the option hold60 (`games/totk/rules.py`, off by default; with fps60): the frame-rate controller's raising branch
  (0x2db214, b.gt) made a nop, so the interval comes down to 1 and stays there. The iPhone Air, a test build with it, standing on Great Sky
  Island: about 60 fps for the first minute and more at 720 rows (p50 17.7-18 ms, p95 25 ms against the game's controller, which chose 2 within
  the minute), 35 s at 540 rows, until thermal state serious, where the hot hold and the GPU's lowered clock (27 ms) give 30. Not a default:
  its frames are less even than the game's 30 while the GPU cannot keep 60. Both products rebuilt (binaries the same; the old as `.prev285`),
  `verify/regress.sh --fuzz` OK. `tools/tests/t_totk_options.py` (was t_totk_fixedres.py) +3 (T_HOLD60_ORIGINAL=1 fails 1).
- [x] batch 119 · 271, part: 8-byte hash keys compared as one word (`gfx/nvn/nvn_hmap.h` nvn_hm_keq; MacNativeGames PR #750, MIT, written anew),
  longer keys by memcmp as before. The tables keyed by serial or object address use it (the texture registry by serial, the live and finalized
  tables, write-back, the store's objects, the compute pipelines, the programs' functional keys). A lookup in a 4,000-key table 4.35 → 1.68 ns on the M5; in
  batch 116's profile the memcmp of these tables was 0.2 % of the translation thread (pso_get's long key, 0.8 %, is unchanged). `t_hmap` +1:
  2,000 keys apart only in their high half (-DDM_TEST_HM_KEQ_HALF, the low half compared: fails 1).
- [x] batch 120 · 271, measured: `[nvn-mtl] renamed uniform updates` also gives the bytes copied a private copy and the largest range copied
  (`core/upd.c`): standing on Great Sky Island 32-36 KB a copy, 64 KB the largest, about 88 copies a frame; the translation thread's profile and
  what copies of only the read part would take are under 271 above (not done: every shader translated and compiled again). `t_ubren` +1 (two
  copies, 512 bytes; -DDM_TEST_UB_COPIED_FULL_ONLY, the copy from the previous one not counted: fails 1). `t_upwait` judged by the median of its
  30 rounds, not the mean: in the first run of this batch, beside the IPA builds, one or two rounds preempted for a millisecond put the mean at
  0.180 ms against 0.1 (it passed alone); -DDM_TEST_UPWAIT_POLL (the usleep(1000) polling it replaced, `nvn_mtl.m` up_wait_zero) fails 1 with a
  median of 1.04 ms.
  [-] 270 measured on the iPhone Air after the batch: a 720-row drawable (`DM_IOS_DRAWABLE_H=720`, the compositor scales it) against the
  native one, standing on Great Sky Island, a b b a with 300 s rests. At 60 fps (a test build with hold60, frames 1800 on until thermal state
  serious): GPU p50 15.57 / 15.13 ms native against 15.64 / 15.57 ms, frame p50 18.6 / 18.5 against 18.6 / 18.3 ms; at 30 fps (the default
  build, frames 2100-6000): GPU p50 27.62 / 28.25 against 27.60 / 27.07 ms. No gain the runs tell apart from their spread: the switch stays, off.
- [x] batch 121 · 281 the iOS setting "Native" (was "Native (manosaba)") for Tears of the Kingdom: docked, a 1920x1080 scene and canvas.
  Native sets `DM_RENDER_HEIGHT` and `DM_DISPLAY_SIZE` to the screen's 16:9 picture (an iPhone Air: 1260, 2240x1260); the game refused the 1260
  lines (above the 1920x1080 its heap holds) and drew its own 1280x720. Now `games/totk/frame_step.c` takes the largest 16:9 the heap holds
  (1920x1080 without the heap overlay, the note's with it) for the scene from `DM_RENDER_HEIGHT` and for the output from `DM_DISPLAY_SIZE` (when
  the option output is on and `DM_OUTPUT_RES` unset), and a display size without `DM_OPMODE` runs the game docked (`dm_set_opmode`). Docked
  because handheld's heap does not hold the sizes: on the Mac in handheld a 1920x1080 canvas beside a 1920x1080 or a 1600x900 scene crashed at
  main+0x1c2f20 (the texture heap run out) within seconds, while a 1920x1080 canvas beside the game's 1280x720 scene and a 1920x1080 scene beside
  handheld's 1280x720 canvas held; docked, a 1920x1080 scene beside its 1920x1080 canvas held (GPU p50 7.11 against the docked default's 7.05 ms).
  The phone's build keeps the options fps60,realtime,scene: docked, the game's own canvas is 1920x1080 (the option output, on or off, presented
  1920x1080 in handheld with GPU p50 6.11 against 6.22 ms, so 62l's note that it makes the handheld swapchain 1080p did not hold). The iPhone
  Air standing on Great Sky Island: Native docked at 1920x1080, frame p50 46.4 ms (the game's controller at 3 intervals, about 21 fps), GPU p50
  30.6 ms, peak RSS 3552 MB; the default 33.3 ms, GPU 28.2 ms, 3474 MB. `t_frame_step` +9 (-DDM_TEST_SCENE_HEIGHT_NO_FIT fails 2,
  -DDM_TEST_OUTPUT_NO_DISPLAY fails 3, -DDM_TEST_NATIVE_HANDHELD fails 1). `games/totk/check_frame_rate.py` applied every option, hold60 too, and
  3 of its checks failed since batch 118: hold60 left out now (its own check is `tools/tests/t_totk_options.py`); the five
  `games/totk/check_*.py` run in the batch's tool step from now on (`work/scripts/batch_fast.sh`).
- [x] batch 122 · 288 SDK functions with the wrong return type (seedds/nxnative 971d336, MIT, written anew; the SDK reference checked):
  `nn::time::CalculateSpanBetween` returns bool, true when computed, and ours returned a Result (0 on success), so every span read as failed and a
  failure as computed; `nn::audio::AcquireAudioDeviceNotificationForOutput` returns a Result, and ours was void, which left the caller x0 as it
  came, the event's address: a failed Result. Tears of the Kingdom's main imports both, manosaba's neither. `t_time` +3 (-DDM_TEST_SPAN_RESULT,
  the old Result answers: 3 fail), `t_audren` +1 (-DDM_TEST_ADNOTIFY_VOID, the event's address as the answer: 1 fails).
  289 the SDK's address-region node area cut from 0x3fe00000 (1022 MB) to 0x3e00000 (62 MB) for both games' sdk builds (nxnative 7deea8e /
  d1b7444, nx2android 8056043, MIT; the words and the constructor checked here): `games/totk/rules.py` sdk 574284b40c06 +0x145d0c / +0x145d78,
  `games/manosaba/rules.py` sdk ab3835b2a37b +0x14fbf0 / +0x14fc5c; both games' AllocateAddressRegion align to 64 KB or 2 MB only, so no
  alignment cut. The rebuilt products differ from the old in those two words of sdk and the rewriter stamp alone. On the Mac smoke runs the
  highest offset used fell with the same use: Tears of the Kingdom 1715 → 749 MB (512 MB used), manosaba 2460 → 1518 MB (1166 MB used); on the
  Air manosaba's trial reached +4904 MB of 5376 before. `hle_svc.c` passes nn::os::AllocateAddressRegion to the SDK and logs the highest region
  end at each 512 MB and a refusal red (it had no line). `tools/tests/t_node_area.py` (8 checks: each pair, its words' encodings, and with the
  game files each module's original words in the constructor's shape; T_NODE_AREA_BROKEN=1, a cut that changes nothing: 1 fails),
  `t_guest_mem` +3 (-DDM_TEST_AR_UNLOGGED, a refusal unseen: 1 fails). verify/regress.sh --fuzz for both games: all OK. Smoke: Tears of the
  Kingdom's two red lines as before, frame 1300 the same scene a few frames of animation apart (looked at), manosaba's shots as batch 121's.
- [x] batch 123 · 301b the rewriter's android target (`DM_TARGET=android`, `tools/patch_nso.py`; after shirai91/nx2android's DM_TARGET and
  jhaizhou-ops/d2r-mobile's NOREDZONE / sp_frame, MIT, written anew): the slot block at bionic's TLS_SLOT_APP (`mrs xT, tpidr_el0; ldr xT, [xT, #16]`,
  bionic's tls_defines.h: slot 2, "available for use by apps in API level 29 and later"), and since Linux has no red zone (a signal whose handler
  lacks SA_ONSTACK builds its frame right below sp) every block moves sp down before it saves a register and back as it restores it; an x18
  instruction on sp is re-addressed by 16 (`sp_moved`: ldp / stp / ldpsw at signed offsets, pre-index pushes with the pair below what they write,
  post-index pops, ldr / str at unsigned and unscaled offsets, a pair past its reach as two single-register accesses, add / sub on sp), and a site
  that cannot be encoded keeps the red-zone form, listed in `<name>.belowsp.tsv`: manosaba 0 of its 55,245 sites, Tears of the Kingdom 7 (its code
  caves and three exclusive sections on sp). The apple products are byte for byte as before but for the rewriter stamp. The verification knows the
  target from the header (`TARGET_ANDROID`): structck reads the android forms (the slot base, sp followed through a block for the temporary
  register pairs, the window and jump-table templates, the listed red-zone sites); equiv and fnfuzz give the patched side tpidr_el0 → slot block
  and write a signal frame (256 bytes) below the current sp before every patch-area instruction, a patched access below that sp differs, and
  fnfuzz leaves stack that frame overwrote and nobody wrote since out of the memory comparison (stale on both sides). verify/regress.sh --fuzz on
  the android manosaba product (`work/build/android/manosaba.b301`): STRUCT, EQUIV, FNFUZZ and GATEFN OK, the mutation self-checks as on apple
  (subsdk0 24 executed, 11 caught on both); the red-zone forms under android's slots (DM_TEST_NRZ_OFF=1) fail equiv at all 9,625 sites of subsdk0
  and fnfuzz at 80 functions. `tools/tests/t_rewriter_synth.py` +10 (a fourth module with x18 on sp in every form; DM_TEST_NRZ_OFF=1 for the run:
  6 fail). Both apple products' regress as before. Smoke: Tears of the Kingdom's two red lines, manosaba's shots as batch 122's (the images are
  batch 122's word for word).
- [x] batch 124 · 301c the host under adb on the phone (`android/`: `make -C android GAME=manosaba`, NDK r30 at API 35): manosaba runs headless at
  60 fps from its 600th frame (frame time p50 16.6 ms), its saves committed and swapped in atomically, no red line in 40 s. What differs by
  operating system goes through new host headers, with Apple's side as the shared sources called it before:
  - `plat.h` (`runtime/plat_apple.c`, `android/plat_linux.c`): memory reads and writes that may fault (process_vm_readv / writev), the save tree
    copy (file by file, the `user.` attributes only: writing `security.selinux` is refused, which failed the first save with a stale ENOENT), the
    swap (renameat2 RENAME_EXCHANGE), a descriptor's path, xattrs in `user.` (jhaizhou-ops `hle_fs.c`), the thread stack without bionic's
    guard page (RogelioLB/nxnative-android), the process's age, fixed reservations (MAP_FIXED_NOREPLACE), regions (/proc/self/maps), deferred
    calls and signals, a semaphore; host threads stopped by a real-time signal whose handler waits on a futex (the registers and FPSIMD from the
    signal context, suspensions counted as Mach's, a listed record held past its thread's exit); scheduling (the interactive class and the
    deadline declaration: none for an app yet, 301f), the footprint (RssAnon + RssShmem + VmSwap) and the performance lines' readings.
  - `slots.h` (the slot block at TLS_SLOT_APP, `android/slots.c`), `uc.h` (signal contexts), `table.h` (the takeover tables: Mach-O sections or
    ELF `__start_` / `__stop_`), `runtime/asm.h` (entry.S, hstack.S and gfx/nvn's nvn_entry.S and the thunks `gen_protos.py` writes: the Apple
    objects identical section by section, relocations and symbols); `android/compat/` for Darwin names (TargetConditionals, OSCacheControl,
    CommonCrypto's SHA-256, mach_time on CLOCK_MONOTONIC, `CLOCK_UPTIME_RAW` the same clock, the one-argument `pthread_setname_np`).
  - The time zone by bionic's tzalloc / localtime_rz and `persist.sys.timezone`; sound to the silent sink (AAudio: 301d); movies WebM only.
  - Graphics: the nn::vi and nv:: takeovers, NVN's takeover and the replay core built for Android on `gfx/nvn/null/null.c`, the null backend:
    a submission completes when committed, its event writes and counter reports land then (timestamps from the guest clock, 0 samples
    passed), nothing is drawn, no screen (pacing as the Mac host without a window). Without NVN's takeover the sdk's own driver ran and aborted
    in NvOsDrvOpen; without vi's, `ConnectToNamedPort`.
  - The NVN layer's Mach calls (deadline declarations, footprint, QoS, power, per-core-kind counts, thread policy) through plat.h; the main
    heap readout's weak default moved beside its caller (its provider left with D2R). The loader refuses a product of the other target.
  Tests: on the phone (`make -C android run-tests`) `t_sha256` 5 (`-DDM_TEST_SHA256_ROUND63`: 4 fail), `t_hthread` 12 (`-DDM_TEST_STOP_NO_V`:
  1 fail), `t_slots` 8; `gfx/tests/t_null_be.c` 20 (`-DDM_TEST_NULL_EARLY`, landing at replay: 4 fail, checked in `make run`). The host tests
  link plat_apple.c, `t_fs_case` / `t_fs_index` inject through `dm_xattr_set` / `dm_xattr_get`. The batch builds the Android host and its tests
  and, with the phone connected, runs them and 40 s of manosaba headless (`work/scripts/android_smoke.sh`). gfx 153 / 153 on Metal 4 and Metal
  3, host 182 / 182, sanitizer, texture, audit and tool tests as before, both IPAs. Smoke: Tears of the Kingdom's two red lines and its shots in
  the spread of the last batches (s1300 against batch 123 8.6, batch 123 against 122 9.7), manosaba's shots as batch 123's, no red line.
