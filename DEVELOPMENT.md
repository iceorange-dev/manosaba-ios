# Development notes

Where the work stands and how to pick it up again, on this machine or a new one. The repository holds all code, tools and notes; what it
does not hold (and must not: keys, dumps, anything derived from the game) is listed below with the way to get it back. Beside this file:
[docs/plan.md](docs/plan.md) (what is left to do, by what each item needs), [docs/plan-done.md](docs/plan-done.md) (every finished plan item in
a few lines; code comments and commit subjects cite them as "plan item N") and [docs/sources.md](docs/sources.md) (the projects we draw from,
how far each was read, what was taken and what not).

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
GAME=manosaba`, `apk` and `test` (NDK r30; the APK's checks in `tools/tests/t_apk.py`), and with the test phone connected `make -C android
run-tests`, a headless run of manosaba under adb (`work/scripts/android_smoke.sh <dir> <seconds>`: the RomFS and system font pushed once to
`/data/local/tmp/dm`) and a run of the app (`work/scripts/android_app_smoke.sh <dir> <seconds>`: installed through the phone's root, a test save of its
own, shown over the lock screen: HyperOS allows it with its "show on lock screen" setting for the app, on on the test phone; a locked phone
without it is reported as skipped). `android/run.sh` builds, installs and runs the app by hand; `t_vk_be` tests the Vulkan backend there.
The APK carries the Vulkan shader bundle (`DM_GAME=manosaba gfx/tools/dmsh_vk.py work/shextract work/build/android/manosaba.dmvk`, DM_ROOT the
workspace; again after a change of the game's shaders or of dmsh_vk.py; `tools/tests/t_dmsh_vk.py` checks the workspace's). To play on the
phone without touching it, `work/scripts/android_live.sh start [<save dir>]` (DM_LIVE_ENV: more NAME=value words for env.txt), `key
<name>...`, `cmd <line>` (e.g. `passdump 2`), `shot <png>` (the game's frame, vk_shot.c), `screen <png>` (the screen), `stop [<boot.txt>]` drive the app
through its command channel (DM_CMD, host/front/cmdchan.c) with a test save of its own (a copy of the given save; HyperOS's pocket mode takes
touches while the phone's top is covered, the channel's keys are not touches).

**How a change is made.** One plan item at a time, each with a unit test and a broken version of the change that must fail it. A batch of
items ends with the gfx tests (Metal 4 and Metal 3), the texture tests, the host tests and their sanitizer run, the public audit and the tool
tests; `verify/regress.sh --fuzz` when the rewriter, a game's rules or `verify/equiv.py` changed; both IPAs (`ios/run.sh ipa`, `DM_IOS_M3=1
ios/run.sh ipa`); the Android host and its tests, on the phone when it is connected, with a 40 s headless run; a short Mac run of each game
against the previous batch's screenshots and red lines; then one commit whose subject starts with
"Plan batch N". A change to `tools/patch_nso.py` or a game's `rules.py` changes the rewriter stamp in the products, so they are built again.
The repository is public: `tools/audit_public.py` checks the git index before every push.

**How the plan is kept.** A batch's record is its commit message: what changed, the measurements, the test results. The documents keep only
the outcome. [docs/plan.md](docs/plan.md) holds what is left, at most three lines an item, and an item leaves it when nothing is left;
[docs/plan-done.md](docs/plan-done.md) gets one record an item, one to three lines: what was done, the number that shows it or decided against
it, where it lives, whose work it follows. No batch entries, no test counts, no history of how it was found; a number is written in one place.
The status below carries its date and is rewritten when the devices or the state change.

- **Memory over repeated loads on the Mac**: `games/manosaba/reload_loop.sh [--stay] <run dir> <save dir> <cycles> [host binary] [VAR=value …]`
  loads a save's newest autosave every 40 s by tapping through the game's own menus; `tools/memstat_cycles.py <run dir>/mem.tsv` prints one
  row per load. With `DM_GUEST_ENV="GC_PRINT_STATS=1" DM_DBG=1` the IL2CPP collector logs every collection; `tools/gc_stats.py <run dir>/log.txt`
  summarizes it. The managed heap census (`DM_HEAP_CENSUS`, `DM_HEAP_WATCH`, `DM_HEAP_REFS`, …) is in [host/README.md](host/README.md).
- **A memory limit kill on iOS** leaves no crash report as a rule (JetsamEvent reports are rate-limited, the crash is "non-actionable"). With the
  device connected: `sudo /usr/bin/log collect --device-udid <UDID> --start '<yyyy-mm-dd hh:mm:ss>' --output kill.logarchive`, then
  `/usr/bin/log show kill.logarchive | grep -E 'ReportSystemMemory|killed by jetsam'`. `DM_MEMSTAT` (relative paths land in `Documents`) and the
  low-memory watch in `host/diag/memstat.m` show what grew.
- **Performance on the iPhone**: compare builds in pairs run one after the other, each after a rest, and judge the sustained rate after the
  first minute (the phone allows a burst of about 60 s, see below). Capped frames lower the clocks, so GPU milliseconds at a held rate
  understate a saving (it shows as a lower clock); the sky over Tears of the Kingdom's island changes with the game's clock, so pin the frame step
  (`DM_FRAME_STEP_MS`) or use a check mode before blaming a picture difference on a render change.
- **Pitfalls**: zsh has a `log` builtin (use `/usr/bin/log`); zsh does not split words (`set -- $var` does not do what bash does); make compares
  modification times, so two edits within one second can skip a rebuild; copy a host binary elsewhere before a long test if you will rebuild
  the host meanwhile; Apple's Metal HUD crashes with `CAMetalDisplayLink` on iOS 18.6 (keep it off there).

## Where the work stands (2026-11-11)

What plays is in the README's status; what is left is [docs/plan.md](docs/plan.md). Supported systems: macOS 26 and iOS 26 / 27 (Metal 4); iOS
18 and macOS 15 through the Metal 3 builds (`DM_IOS_M3=1`, `DM_MAC_M3=1`); iOS 17 is not supported. Devices: a Mac with an M5; the test phone,
an iPhone Air (iOS 27.0.1, a free developer account) since 2026-10-27 and an iPhone 17 Pro (iOS 26.4) before it; the iPhone Duo for its two
displays (302); a Redmi K100 Pro Max (Android 17) for the Android app; manosaba also on an iPad mini 7 (iOS 18.6.2) and an iPad Pro M4 (iOS 18.5,
the unsigned IPA) up to 2026-10-16.

**manosaba 2.0.4b** plays on the Mac and the iPhone, and as an Android app through its title, prologue and first trial (301). Open:
1. **The game's own memory leak.** Every save load builds one more set of Naninovel's `ScriptManager` and `ResourceProviderManager`
   (`DM_HEAP_WATCH`) and the old sets stay referenced: live data grows about 20 MB a load, the heap about 28 MB. The root reference is not found.
   Next: trace every old instance of the two managers, bound each object by its class's `instance_size`, scan the guest stacks too; then clear
   the root from a `games/manosaba` hook after a load.
2. **A trial's loading peak.** Its depth and stencil targets have no memory since plan item 280's batch (the Mac's sampled peak 2,458-2,656 →
   2,120-2,199 MB of RSS); the phone's peak (3,240 MB of RSS on the Air before that) is not measured again. Without the increased memory limit an
   iPad mini 7 allows about 3.4 GB and an A16 iPad 3071 MB (killed at a trial's start); a free account signs the limit (65, 211).
3. Smaller: 13 NVN functions looked up but unknown (NULL, never called); compute dispatch never seen; keyboard, mouse and controller untested
   with this game.

**Tears of the Kingdom 1.4.3** plays on the Mac and the iPhone (the prologue, its movies, the dive, the Great Sky Island and after). The iPhone
build embeds `product60` and presents at 120 Hz. What limits it there (230): the phone allows a burst of about a minute, then a sustained power
budget that goes to a busy GPU first while the CPU's clock is cut, and the frame is one chain (the game waits every frame for the GPU), so only
less energy a frame turns into frames. On the iPhone 17 Pro that held about 59.5 fps on the island at a 1280x720 scene (239). On the iPhone Air
(five GPU cores, no vapor chamber) it is about 59 fps until the thermal state turns serious (44-85 s after a cool start in the last runs), then the app's
hold at 30 fps (285, 324, 325); the iOS default stays 1280x720 (the user's choice; the settings offer 540p with MetalFX).

## Tears of the Kingdom

`games/totk` (1.4.3, program 0100F2C0115B6000, main build 277178b7dba1b6d4949a84778d4abc58), data from your NSPs with `tools/game_extract.sh`
into `work/data/switch/totk-1.4.3`. SDK 20.5.22; `sdk` and `subsdk0` have the same build IDs as nxnative's Breath of the Wild 1.9.0 (the same
system layer). The parts below are the reference; numbers in brackets are plan items ([docs/plan-done.md](docs/plan-done.md)).

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
in-process audio renderer (`host/hle/hle_audren.c`, `host/hle/hle_guestthread.c`, `dm_pcmout_*`), the hardware Opus C API, indexed cache storage
(`$DM_CACHE`), another application's save not existing (TotK looks for BotW's), svc GetInfo 20, play reports, amiibo (`host/hle/hle_nfp.c`), clock
snapshots and steady / network clocks (from Ryujinx), NVN functions (13 names in `gfx/nvn/names.txt`).
- Cores (`host/hle/hle_thread.c`): the job system keys per-worker output by `nn::os::GetTlsValue` = core + 1. Core mask calls follow the Horizon kernel
  (Atmosphère mesosphere: -1 don't care, -2 the ideal core, -3 keep it), ideal core and mask from main.npdm. Virtual cores (game.json
  `virtual_cores`, from MacNativeGames' runtime/threads.c): a guest thread holds one of three while it runs guest code; host-work threads
  (`dm_guest_thread_spawn`, the audio renderer) hold none.
- Command memory: a disabled or short callback continues in host segments. TotK calls cached 512-byte bind windows every frame, so `cmd_t`
  leaves +0x10 to the game (`guest_flags`: its NVN 55 inline wrappers store a dirty byte there), a host segment lives as long as the JUMP that
  links it, and the JUMP counts as used (`t_nvn` 5a).
- Movies: the 28 in `Event/Movie` are WebM, one VP9 track, no sound. `SetDataSource(path)` (`dm_fs_host_file`), a WebM / Matroska demuxer
  (`host/media/movie_webm.c`, RFC 9559 / 8794), a VP8 / VP9 decoder on libvpx with MacNativeGames' behaviour and pacing (NV12, at most one frame per
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
  use: `DMSH_CHAIN` in `gfx/nvn/dmsh_chain.h`.
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

**Diagnostics**: `DM_WATCHPOINT` (`host/diag/diag.c`), `DM_CRASH_MEM` (`host/runtime/crash.c`, `*xN`), `DM_NVN_PASSDUMP_ALL`, `DM_NVN_GPUWATCH=<first>-<last>`,
`DM_NVN_LIVEROWS=<file>` (single wrong frames in window play), `DM_NVN_LIVESHOT=<first>,<count>:<path with %d>`, `DM_NVN_NOCULL`,
`DM_NVN_RTCLEAR=<serial>`, `DM_NVN_TEXSNAP`, `DM_NVN_TEXNZ`, `DM_NVN_TEXCLASS`, `DM_NVN_REUPLOG`; the command channel `DM_CMD=<file>` (47); per-pass
GPU time `DM_NVN_PASSGPU` (49); the frame timeline `DM_NVN_TRACE` with `tools/trace_report.py`; on iOS, switches in `Documents/env.txt` (22);
`DM_FP16_PROBE=1` logs what the GPU stores for a float above float16's range (1e5: +inf on an A19 Pro, 65504 on the M5; 310). The
cliff repro: a manual save at the cliff edge, `DM_KEYS="A@26.5,A@31,A@35,LSU@f1380+240,X@f1400"`. Lesson: MacNativeGames' `nvn_mtl.m` described
the dive's flicker and its fix; search the references for a symptom before building probes, then measure to confirm the cause here.

Known gaps: depth-stencil (stencil) views are not modelled; an integer render target's partial clear is not drawn (red line); other iPhones and
iPads are not tried (GPUs without BC turn BC1 into ASTC and decompress the other BC formats, 62c / 62c2).
