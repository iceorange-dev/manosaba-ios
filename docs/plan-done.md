# Plan: finished items

Every plan item with work done on it, one record each, by number (items 1-326, as of 2026-11-11). Code comments and commit subjects cite them
as "plan item N". What is still open: [plan.md](plan.md).

`- <item> · <state> · <what was done>`. The states: `b85`, done in the commit whose subject starts with "Plan batch 85" (its message has the
measurements and the test results in full); `not taken`, measured or looked at and left as it is, with the reason; `part`, something is left
(it ends with `Left:` and is in plan.md); `open`, nothing done yet. Sources are credited as in the code: project or owner/repo, commit,
licence. The long text these records were written from: `git show e683f23:DEVELOPMENT.md` and `git show e683f23:docs/plan-archive.md`.

## Findings that belong to no item

- **Where a frame's GPU time goes.** The phone's island: G-buffer and decals 34.5 %, full-screen passes 20 %, single-draw passes of up to
  256x256 16 %, the four shadow cascades 13 %.
- **Where GPU time does not go.** The resolution, a draw's own cost (0.05 µs), pipeline changes (418 a frame), caching the shadow cascades.
- **The clocks follow the frame rate.** A phone run held 30 fps at 20.65 ms of pass time a frame, then picked 60 and took 11.09 ms: at a held
  rate, saved work becomes a lower clock.
- **The game's frame-rate controller.** Main 0x2db174 / 0x2b3d07c / 0x2db1ac: 60 needs CPU under 15.83 ms and GPU under 16.33 ms.
- **Mirroring presents at 60 Hz.** IPhone Mirroring runs the phone's screen at 60 Hz; a 60 Hz present rate (`DM_PROMOTION=0`) holds the
  island at 30 fps.
- **The 120 Hz fall.** 64-81 fps on the island until 59-79 s after the app starts, then 44-54 fps for good, the CPU's power 2.2-2.9 →
  1.2-1.5 W, thermal state nominal.
- **What sets the sustained level.** After its first minute the phone holds the CPU, the GPU and memory to one power budget: only less energy
  a frame turns into frames (960x540, `DM_RENDER_RES`: 59.6 fps against 56.0). The user keeps the game's 720p as the default (2026-10-23);
  Render resolution 540p (MetalFX upscaling on) holds about 60.
- **Why the CPU's clock falls.** `DM_PWRTEST` (230), two CPU threads: a step at 65-66 s to 3.31-3.33 GHz alone, at 61-62 s to 1.31 GHz beside
  a fully busy GPU: a burst for about a minute, then a sustained budget of which a busy GPU keeps its share.
- **No API moves the split.** Game Mode's thread-group flag needs Apple's entitlement (XNU kern_resource.c); Low Power Mode for a while and
  off again brings the burst back (the user's session: 88 fps after 90 s of it, not after 4 s).
- **Manosaba's controls and choices.** R fast-forward on / off (Options → 文本快进模式 全部 for unread text), X the phone menu (save, load,
  options, help), Y the encyclopedia, B hides the text box; the investigation map: UP to a location, LEFT / RIGHT between floors, A to go there.
  Chapter 1's choices that end the game: the first option of the first two, the second of the ones at Noah's room and in the shower room.
- **62l's output note corrected.** B121: the option output, on or off, presented 1920x1080 in handheld (GPU p50 6.11 against 6.22 ms), so
  62l's note that it makes the handheld swapchain 1080p did not hold.
- **ThreadedStreamBuffer handshake not in manosaba.** Unity's ThreadedStreamBuffer handshake (nxnative d51417b) is not in manosaba's
  main: no store-release followed by a plain load in the 93 functions that wait on or post a semaphore, event, condition variable or
  address, or call those that do (b147).
- **NaN min / max on the K100.** GLSL.std.450 FMax / FMin / FClamp with a NaN operand (a compute probe, b148): max(0, NaN) 0,
  min(NaN, 1) 1, clamp(NaN, 0, 1) 0, as Maxwell, so no NMin / NMax rewrite (doom-port measured their cost at +24 %).
- **Chain changes not taken (b149).** FSETP / FSET / FCMP `.FTZ` (nxnative 84dea69; no Switch 1 game has shown the symptom, and it adds
  work to nearly every compare on the GPU-bound iPhone); a thread exiting before a barrier (nxport dc36147; none of the 325 compute
  kernels does).

## Items

- 1 · b1 · svc 0x34 type 3 (64-bit wait if equal); unknown types refused by 0x34 / 0x35.
- 2 · b1 · controller face buttons by the printed letter on Nintendo controllers (`host/gamepad_map.h`; the device check is 66).
- 3 · b1 · the BindProgram stage mask at the three scans (`defer_try`, `fuse_try`).
- 4 · b1 · sampler LOD bias and min / max reduction.
- 5 · b1 · GPU time per frame as the union of command-buffer spans (`@gpu_ms`; `@gpu_sum_ms` the old sum).
- 6 · b1 · a fault guard around uniform snapshot copies (without it SIGBUS).
- 7 · b1 · overlay directory listings without the 2048-name cap.
- 8 · b1 · the silent NVN no-ops red on first call.
- 9 · b1 · precise SleepThread.
- 10 · b1 · rodata read-only after relocation (`DM_RODATA_RW=1` leaves it); svc 0x02 with Horizon's checks, read / none reported, not applied.
- 11 · b1 · the 8 / 16-bit UI2F / I2F vertex formats; 32-bit and 10:10:10:2 stay unmapped (Metal refuses 32-bit integers for a float input).
- 12 · b1 · thread dumps list the handles of a WaitSynchronization with each one's state.
- 13 · b2 · busy-yield back-off (`DM_YIELD_BACKOFF`), off by default: it cost energy and frames in Tears of the Kingdom's prologue.
- 14 · b2 · targeted wakes; upload waits on a condition variable; the audio renderer blocks on buffer releases.
- 15 · b2 · replay lookups: per-thread pool cache, dirty range on the texture and `mprotect` over page runs, residency no-op skip.
- 16 · b2 · a spare texture for re-uploaded CPU textures, dropped after 120 submissions unused.
- 17 · b2 · reflection summary per program, attachments walked once per pass.
- 18 · b2 · libvpx row multithreading (`DM_VPX_ROW_MT=0` off): the Ganon movie 1.02-1.03 → 0.95 ms a frame.
- 19 · b3 · a Metal 4 launch check with a screen (Metal 3 for the iOS 18 build).
- 20 · b3 · game data excluded from backups.
- 21 · b3 · `GCSupportsGameMode` (iOS 18.0-18.5 read only this one).
- 22 · b3 · `env.txt` switches on the device: the bundle's, then Documents' (`host/envfile.h`).
- 23 · b3 · audio notifier thread at USER_INTERACTIVE.
- 24 · b3 · compressed files (`.zs`) stored, not deflated, in the IPA (1.66 s deflated, 0.26 s stored).
- 25 · b3 · iOS 17 code guards; iOS 17 is not supported since, see 59.
- 26 · b3 · quit waits for a save in progress, 15 s on the Mac, 3 s on iOS.
- 27 · b3, b190 · a covered Mac window discards presents (`DM_OCCL=0` off). Checked in a window in b190 (71b).
- 28 · b4 · one game at a time on the Mac (`host/game/game_slot.c`, `DM_SLOT=0` off): a run for the player stops an agent's run with SIGTERM.
- 29 · b4 · a stamp of the rewriter's sources in the product; the Tears of the Kingdom default product is the 30 fps one, on purpose since dba469f.
- 30 · b4 · the rewriter stops on a system register outside NZCV, FPCR, FPSR and DCZID_EL0; neither game has one.
- 31 · b4 · the SDK's abort text in the log at a Break.
- 32 · b4 · the last 256 lock / condition variable calls in thread dumps (`DM_KTRACE=0` off).
- 33 · b4 · upstream's `t_loader_weak` (absent weak imports stay 0, our loader already did).
- 34 · b4 · `DM_MEMSNAP=frame[,frame...]:prefix` and `tools/memsnap.py`.
- 35 · b4 · GitHub Actions (macos-26): the hosts of every game, the host tests and the tool tests.
- 36 · done (Tier 2) · the BAHS shaders: `engines/bnsh/shader_extract.py` reads every binary entry of a `.sharcb` (after nxnative's
  `bahs_shaders`). Not covered: `Lib/NintendoSDK/TextureShader.bnsh`'s two control sections and the Title layout's `bgsh`.
- 37 · done (Tier 2) · FFMA / FMUL `.FMZ` (after tomodachi-native a0aa6c2). Neither game uses it.
- 38 · done (Tier 2) · SHF wraps or clamps to the width; I2I `.SAT` in the source's signedness (LASRecomp d4c2ff6's vectors).
- 39 · done (Tier 2) · the alpha test as a game profile option (`alpha_test` in game.json; after lordran-native a63dd76). Off for both games.
- 40 · b6 · host calls and svcs on a per-thread host stack (after MacNativeGames' hle_stack, MIT). `DM_HSTACK=0` off.
- 41 · b7 · occlusion counts across submissions (after lordran-native 3ef6915, MIT).
- 42 · b8 · draws that sample what their own pass draws: the replay copies only where NVN defines the read; copying for every such draw put
  prologue shots outside the console footage's brightness ranges (`DM_NVN_FBX=all` / `=0`).
- 43 · not taken (b8) · splitting the command buffer at timestamp reports: no two reports of one submission seen subtracted (but see 131).
- 44 · not taken (b8) · keeping clears: no ClearTexture in either game, 4 render passes a frame end without a draw in the prologue.
- 45 · not taken (b8) · an inline system counter read: about 1.5 ms of CPU a second in all in Tears of the Kingdom.
- 46 · not taken (b9) · a smaller render area: no pass of either game draws only a top-left part.
- 47 · b10 · a live command channel, `DM_CMD=<file>` (after MacNativeGames' `TOTK_INPUT_FILE`).
- 48 · b11 · sead heaps and list watch (`host/diag/sead.c`, after MacNativeGames' TOTK_CRASH_HEAPFIND / HEAPWALK / LISTCHECK).
- 49 · b12 · per-pass GPU time, `DM_NVN_PASSGPU=<tsv>` (after upstream 44a6770, MIT).
- 50 · b13 · soak and fixed-scene measurement, `tools/soak.py` (after MacNativeGames' `mac_v143_soak.py` / `mac_v143_smoke.py`).
- 51 · b13 · lifecycle and swipe-away tests (upstream `ios/tools/lifecycle.sh`, `logred.sh`). The home button and the app
  switcher were for the phone (70).
- 52 · b14 · scene render size on 1.4.3, `DM_RENDER_RES=<w>x<h>` (after MacNativeGames PR #19, MIT).
- 53 · b15, b190 · frame pacing between 30 and 60 fps (after MacNativeGames' TOTK_UNCAPPED, MIT; `DM_UNCAPPED=0` off). In a window in b190 (71b);
  the 30 fps constants in play are 71c.
- 54 · b16 · user mod folders, `DM_MODS=<dir>` (after SkyRecomp's `runtime/mods.c`, MIT); on iOS `Documents/mods`.
- 55 · not taken (b17) · moving the AudioUnit open (tomodachi-native d7f52e1 moved it after a 40 s stall): 22-26 ms in the games here.
- 56 · not taken (b18) · cold uploaded textures (justr4mzi's mush8 fork aafef22, ideas only): about 3 % of Tears of the Kingdom's Metal memory.
- 57 · b19 · code patches chosen at launch (`DM_OPTIONS`), one product for 30 and 60 fps (after tomodachi-native 0428c80, MIT). iOS (the
  changed pages embedded, tomodachi-native afd88e9; item 62): see 136.
- 58 · b20 · on-screen controls with a drag-to-look camera (BotWRecomp `runtime/src/host/touch.cpp` at 2b84b82, MIT; `DM_TOUCH=pad`).
  Its device check went with 70.
- 59 · not taken · iOS 17 is not supported (the user's decision, 2026-10-20); the apps stay at iOS 26 (Metal 4) and iOS 18 (Metal 3).
- 60 · not taken (b21) · indirect draws and dispatches: no game calls one yet (396 logs); implemented when a game calls them (item 64).
- 61 · b55 · NVN translation on a thread of its own (`DM_NVN_XLAT`; D2R Mobile b94670c and MacNativeGames' TOTK_ASYNC_REPLAY, MIT,
  written anew): the default for Tears of the Kingdom (prologue at 30 fps frame p95 46.3 → 33.9 ms), off for manosaba (p95 17.8 → 20.5 ms).
- 61a · b54 · LASRecomp's render-queue cases as a test first; `nvnCommandBufferFenceSync` a record.
- 61b · b54 · deferred bindings frozen at submit.
- 61c · b54 · a per-frame critical-path trace (`DM_NVN_TRACE=<first>+<frames>:<path>`, `tools/trace_report.py`;
  totk-mobile's host/timeline.c, Apache-2.0, written anew).
- 61d · b69 · the translation thread's time-constraint policy (MacNativeGames' job queue sets it on its translation thread: without it,
  replay 6.6 → 13 ms) against none (`DM_NVN_XLAT_RT=0`): no consistent difference on the iPhone 17 Pro (frame time p50 19.1 / 19.4 against
  19.4 / 22.1 ms); kept.
- 61e · not taken · full-screen single-draw passes as compute: they need the translator to make kernels, to save under 0.9 ms a frame at
  most (the prologue, M5).
- 62 · part (b54, b55, b64, b65, b66, b67, b76) · Tears of the Kingdom on iOS; code options (57) through embedded changed pages
  (tomodachi-native afd88e9). First device run 2026-10-21 (iPhone 17 Pro): 30 fps. T5eng: about 6 GiB of address space below 2^39 on iOS 27,
  arm64-iphoneos metallibs. totk-mobile: "do not push the large resource files with devicectl directly". Bayonetta 3 on an iPhone Air:
  footprint 2.4 GB. MacNativeGames e099353: ~8,800 kept metallib mappings filled an A16 iPad's 11,400 vnodes (103).
  Left: other devices, the cold-texture census of 56 under a memory limit.
- 62a · b54 · guest memory reserved at the top of `main()`, before UIKit and Metal; the iPhone 17 Pro gave the whole 6144 MB.
- 62b · b54 · VP9 on iOS with libvpx 1.17.0 (GirlNet dropped VideoToolbox's VP9 probe, which faulted at a movie's start;
  `ios/tools/build_libvpx.sh`, SHA-256 pinned, the value totk-mobile pins).
- 62b2 · b66 · the iOS libvpx built right: `build_libvpx.sh` sets the iOS minimum to 18.0 (libvpx's configure fixed it at 7.0: no NEON, no
  `-O3`). The cutscene plays smoothly (the user, 2026-10-21); hardware VP9 not needed for now.
- 62c · b54 · BC1 → ASTC 4x4 on the GPU on GPUs without BC (MacNativeGames `nvn_bc1astc.c`, `nvn_bctc.metal`; `DM_NVN_BC1_ASTC=0`): the
  prologue 2,314 MB against 3,545 MB decompressed and 1,984 MB with BC.
- 62c3 · b55 · BC textures the GPU writes, decoded after the pass on GPUs without BC (Apple's Metal Feature Set Tables of 2026-05-21: every
  iPhone with an A14, A15 or A16); BC6H / BC7 still left out.
- 62d · b54 · handheld mode by default on iOS: game.json `ios_mode` "handheld" (Tears of the Kingdom; manosaba stays docked).
- 62e · b54 · RomFS as 1 GiB parts with an index (`tools/romfs_parts.py`, `romfs.dmri`), unpacked at the next launch.
- 62f · b54 · `maxBufferLength` and the recommended working set on the "Metal device" line (mush8 PR #3); `DM_COOL_START=nominal` (or `fair`)
  waits for that thermal state (T5eng: the same scene at 60 fps on a cool phone, 30 on a hot one).
- 62h · b76 · the heap of item 138 on for iOS (`DM_NVN_RT_HEAP=1`): the same pictures on the iPhone 17 Pro, GPU memory on Great Sky Island
  2,076-2,088 MB against 2,186 MB without. Kept on. totk-mobile `nvn_texture_alias.inc`: 373 MiB of layouts in a 111 MiB pool on Great Sky
  Island; a heap of 1.25× the pool places all but 3 MiB.
- 62i · b64 · after the first device run: the game data located off the main thread; `DM_CACHE` in Library/Caches on iOS.
- 62j · b64, b76, b187 · the TBDR rewrites on the phone: clears drawn as quads now mark their attachments written (`sd_note_quad`;
  186's lead from DavidZande/MacNativeGames 907138d). They stay off on iOS: `DM_NVN_TBDR=1` frames 66.6 against 33.4 ms on the iPhone 17 Pro.
  Its leads (186) worked under 219 (b187): see 219.
- 62k · b65 · a cave wall missing from some camera angles: draws bound after `upd_flush` got the uniform snapshot without the submission's
  updates; private copies now follow every update.
- 62l · b67 · a 60 fps iPhone build: `ios/run.sh` with `DM_PRODUCT=build/totk/product60 DM_OPTIONS=fps60,realtime,scene`;
  `DM_RENDER_HEIGHT` reaches Tears of the Kingdom.
- 63 · b56 · the 2K canvas and MetalFX (after 52 and 95).
- 63a · not taken · MetalFX's docked guard against the scene's content size (MacNativeGames ff092e3): our present-time MetalFX already
  compares the content size with the size on screen.
- 63b · b56 · the output size ("native 2K", after MacNativeGames' game_patches_v143.c patch_output_res, MIT, reviewed at 41e1e0c): code option
  `output`, `DM_OUTPUT_RES`. On iOS the growth needs `DM_APP_POOL_ADD_MB` (281).
- 63c · b56 · MetalFX in place of the game's own upscale pass (after MacNativeGames' mfxup_dock, MIT; `DM_UPSCALE=metalfx`).
- 63d · b56 · MetalFX before the game's compose pass in handheld (after MacNativeGames' mfxup_draw, the "iOS handheld" case).
- 64 · part (b54, b150) · for the next game: add-on content mounts (upstream PR #69), FenceSync, ClearBuffer and the other NVN functions
  (the 47 bayonetta3 has and we lack were split into 64a / 64a2), LDN, UE4 / CRIWARE notes (d2r-port-radar), `Downsample` as a resolve,
  VideoToolbox VP9. Left: 64a2, 64f, 64h2, 64j2, 64n2.
- 64a · b54 · the 47 NVN functions bayonetta3-mobile has and we lack: 27 now.
- 64b · b54 · CopyCommands as a copy made at the call (the SDK copies, as dr3-mobile read it).
- 64c · b54 · depth bounds through `setDepthTestMinBound:maxBound:` (bayonetta3's vectors).
- 64d · b54 · rasterizer discard, `SetScissors` / `SetDepthRanges` batches, a second queue on the one Metal queue.
- 64e · b54 · several texture and sampler pools (bayonetta3's descriptors, lordran's checks).
- 64g · b150 · HSETP2 `.H_AND` combines each half with the predicate first, then ANDs the two, the second result its complement
  (`InstEmitFloatComparison.cs`; LASRecomp d4c2ff6, MIT; yuzu's rule); no shader of either game changed. `XLATE_DEBUG=0` refused (item 200).
- 64h · b54 · symbol resolution as Horizon's rtld, by the ISC-licensed oss-rtld's rules; the default, DM_RTLD=0 binds by ours.
- 64h2 · open · a SystemEvent kept while its detached read end waits (blade2-mobile, UE4's audio renderer), with a UE4 game.
- 64i · b54 · `getentropy` as HLE_FORCE; `nn::pl::GetSharedFont` every shared font at once. Not taken: IL2CPP's `SystemNative_Stat` / `LStat`
  (armyja patches them by offsets: with a Unity game that needs it).
- 64j · b54 · AudioOut volume and counters with the SDK reference's and yuzu's semantics (facts only); FlushTexels as a CPU write unless
  WriteTexels wrote the texture since its last flush.
- 64k · b54 · named time-zone rules, `IsSaveDataExisting`, OpenPreselectedUser, ShowControllerSupport's result. Not taken: Unity shader
  layouts (our shaders come from the NVN calls).
- 64l · b54 · the control section length by GPU code version (0x838-0x880, at +0x20; bayonetta3-mobile's nvn_control.h as data).
- 64m · b54 · guards against Metal aborts (super-mario-odyssey-ios 693bc6b / 356ab26, MIT, written anew).
- 64n · b54 · provisional profiles from the game files: `tools/new_game.py --check` (nxnative's profile_guess.py marker list, MIT, as data).
- 65 · b63 · a personal team signs the increased memory limit (BotWRecomp `PORTING.md` and mush8 `make-ios.sh` say a free account can;
  MacNativeGames played 1.4.3 with it on a personal team; decided on 2026-10-21), so `ios/tools/project_free.py` keeps it (manosaba,
  iPhone 17 Pro: about 3.4 → 6.1 GB); extended virtual addressing is refused to personal teams.
- 67 · b59 · gyro and rumble (67a-67e). Left: the checks with the controller and the phone.
- 67a · b58 · vibration handles per style, only the connected style forwarded (`host/hle/hle_motion.c`), each band at its peak since the last
  take (totk-mobile); off while not permitted or out of focus (LASRecomp's rule).
- 67b · b59 · the Pro Controller's IMU and HD rumble over IOHID on the Mac (atone; atone's axes, dekuNukem's rumble encoding):
  `host/front/procon_mac.m`; `DM_PROCON=0` leaves it to GameController.
- 67c · b58 · CoreHaptics with plain players (atone f6dba3d): `host/haptics_map.h` (dekuNukem's table); without a controller the phone
  pulses with acnh-mobile's limits (`DM_PHONE_HAPTICS=0`: none).
- 67d · b58 · gyro fusion with the rest bias learned: atone/nxnative 185cfda's method (Mahony's filter, written anew), zero point from
  still samples (BotWRecomp's idea). GameController's axes measured (`DM_PAD_MOTION_LOG=1`): with 67.
- 67e · b62 · the phone's haptic engine, stopped by iOS in the background, is made at the first pulse due in focus and retried after 2 s,
  doubling up to 30 s (`dm_phone_engine_due`).
- 68 · b62 · MixWithOthers (from upstream 9393b00, ours 46b63f4) on an iPhone: another app's music plays on under the game (iPhone 17 Pro,
  2026-10-21).
- 69 · b75 · the game over flow with the overlay (194; its bytes checked by `games/totk/tests/t_romfs_overlay.py`), played on the
  iPhone 17 Pro, 2026-10-22. GPU (`DM_NVN_PASSGPU`): the island in daylight ~25 ms a frame, the black game over screen ~20 ms.
- 70 · done · manosaba's iOS app on the iPhone 17 Pro, 2026-10-21 (chain ca5eb9623812): `ios/run.sh lifecycle` LIFECYCLE OK; its metallibs
  are built in `work/build/ios/dmsh_b/b-bab8d4746968`. The on-screen controls of 58 not wanted: manosaba plays by touch natively (the user,
  2026-10-22).
- 71 · not taken · iOS 17 is not supported (59).
- 71b · part (b190) · the Mac's window checks: manosaba covered by an opaque window (27, `DM_WINDOW=1 DM_TEST_COVER=30,10`) goes on at 60 fps
  with its presents dropped; Tears of the Kingdom's product60 (53) is paced at the window's 60 Hz. Fixed: the on-screen statistics ask for a
  present's time by its own ticket (`nvn_pace.h` nvn_pres_time, `hle/pacing.inc`). Left: the device checks (19, 20), 127's by hand, 153's default.
- 72 · not taken · the blood moon crash: skipped with 130's movie hold on the iPhone (the 60 fps product, 5ee1644), no crash (the user,
  2026-10-22); closed 2026-10-24: the user judges it an earlier crash that happened to land at the skip. None of the projects had anything on
  it (10-20; FPSLocker marks its cutscenes "must be blocked to certain framerate to not cause issues").
- 73 · b5 · the host races of nxnative 8530042 (QueryMemory, a SystemEvent's binding, audio output backends, CloseAudioRenderer).
- 74 · b5 · the yield back-off start, `DM_YIELD_BACKOFF=<cap µs>[:<start µs>]`, default 2 ms; still off: no energy or frame difference.
- 75 · b5 · the c1 guard by interval arithmetic over the whole index (`gfx/tools/c1_guard.py`): the same 49 shaders as the old one.
- 76 · b5 · two more power lines; manosaba's 25,000 wakeups a second come from host threads.
- 77 · not taken (b5) · the opening cave's stone blocks and steps beside the torch are lit warm here; not this port's bug.
- 78 · b22 · the pipeline warm-up from the player's own list without a seed list. No title list.
- 79 · b22 · the prediction table (after MacNativeGames PR #29, MIT): untaken guesses freed, each scan capped. Not measured: the cold case
  on a device.
- 80 · b22 · the present's command buffer committed with the residency set committed (MacNativeGames PR #30, open).
- 81 · b22 · a sync's fence stored before its flag, release / acquire (GirlNet f2faa5f).
- 82 · b23 · NVN texture layout per the SDK (after nxnative PR #11, digitalhivehub bacf2cb, MIT; totk-mobile's sizes, Apache-2.0, numbers only).
- 83 · b23 · the format's own swizzle in the texture views (totk-mobile d3dd76c, Apache-2.0, reimplemented).
- 84 · b23 · socket errno from nn::socket's own values.
- 85 · b23 · sticks inside the circle of radius AnalogStickMax for every source.
- 86 · b23 · the Mac app's first iCloud listing driven by a local run loop on the main thread (upstream 1ceaa6b, MIT).
- 87 · b23 · quit waits while the game is inside its exit-request section (GirlNet 291a9cc).
- 88 · not taken (b23) · SleepThread under 20 µs is rare: 677 a minute in Tears of the Kingdom, none in manosaba.
- 89 · not taken (b23) · neither game reads the six-axis state; a resting state belongs with the gyro (67).
- 90 · b23 · red lines, among them a dual-source blend factor and an unimplemented import (`UNIMPL`). None fires in either game.
- 91 · part (b24) · shrines at 60 fps: nxnative PR #11's five words (digitalhivehub 97172c1 / a6b7191, MIT) joined `fps60`. Left: a shrine
  entered at 60 fps (not run here).
- 92 · b24 · the BNSH files inside the game's modules (MacNativeGames 2f9e707, MIT): capture-only shaders 178 → 138.
- 93 · not taken (b24) · the half-step selector (sys+0x1da bit 0) stayed 0, so the step is never halved (T5eng's note does not hold here).
- 94 · b24 · the experiment `DM_NVN_WB_IDLE=1` (after totk-mobile's write-back, Apache-2.0, reimplemented), off by default: it makes
  Link close to the camera solid. Which is the console's picture: 94a.
- 94a · part · GPU write-back of idle textures (`DM_NVN_WB_IDLE=1`; totk-mobile's rule, `nvn_texture_rendered.inc`, `nvn_texture_writeback.inc`:
  a GPU write written back after 30 frames without another; ours otherwise only on `nvnMemoryPoolInvalidateMappedRange`) makes Link close
  to the camera solid instead of a translucent ghost. Left: console footage, then on by default or a narrower rule.
- 95 · b24 · `DM_RENDER_RES` above the output size, 3840x2160 at most: a HeapDef overlay (MacNativeGames' make_output_res_overlay.py, MIT;
  1920x1080 matches totk-mobile's numbers).
- 96 · b24 · anisotropic filtering as a switch, off by default (`DM_NVN_ANISO=<2..16>`; tvc97's TOTK_ANISO).
- 98 · b25 · the audio renderer paced by its own clock (after SkyRecomp 0620de9, MIT): catch-up frames 3,731 → 10 in 75 s.
- 99 · not taken (b25) · depth textures without lossless compression look the same (`DM_NVN_DEPTH_PLAIN=1`, LASRecomp PR #204).
- 100 · b26 · the Ultrahand mask shader's integer output: `games/<id>/fout.tsv` (nxnative PR #11's shader_fout.tsv, MIT).
- 101 · b26 · the NoContraction helpers inlined (totk-mobile's idea only): no measurable GPU gain on the M5. Chain bab8d4746968 → 4236b355e989.
- 102 · not taken (b26) · not needed: 90's red line for dual-source factors fired in neither game.
- 103 · b26 · metallibs copied into memory on iOS (MacNativeGames e099353, MIT): no vnode held per library.
- 104 · b26 · a pipeline retried after a compiler-service interruption (after MacNativeGames 39048ca, MIT).
- 105 · b26 · the command channel follows a replaced or truncated file (T5eng's TOTK_INPUT_REOPEN).
- 106 · b27 · run diagnostics (lordran-native f5834c5 / 9567c94, MIT; the code is ours): log stamps, `boot.interrupted.txt`, Export
  Diagnostics, the fps / GPU line.
- 107 · b27 · the frames-stopped dump, `DM_FREEZE_DUMP` (justr4mzi's mush8 fork 979317e, GPL: the idea only).
- 108 · b27 · the Metal 4 iOS build compiles for `apple-a14` (was a18; lordran 78dd33d).
- 109 · b27 · touch camera sensitivity and X / Y inversion (lordran 42ad4cf, MIT: the idea): `DM_TOUCH_LOOK=<s>[,x][,y]`.
- 110 · not taken (b27) · not needed: `devicectl device copy to` already skips unchanged files and resumes.
- 111 · b27 · `make -C host/tests run-ubsan` and `tools/audit_public.py` (bayonetta3-mobile's idea, MIT) in CI; LDC.64 with the address
  register among its destinations (LASRecomp a179452's cases, MIT).
- 112 · not taken (b55) · conditional rendering on the GPU (totk-mobile's nvn_query_gpu / nvn_conditional_draw): 0-2 % of the prologue's
  conditionals wait on a pending result, 0 of 11,298 on the iPhone 17 Pro.
- 113 · not taken (b56) · framebuffer fetch for in-pass reads of the current pixel: before a texture barrier the replay must read memory as
  the console does (42); the one after a barrier (fcd1a8d9): done as 181.
- 114 · b57 · an iOS Simulator build for the touch controls, safe areas and lifecycle: `ios/run.sh sim-build` / `sim-run [s]` / `sim-stop`.
- 115 · b28 · pipeline keys carry zeros for the blend state of targets whose blending is off: Tears of the Kingdom's 729 keys → 397.
- 116 · b28 · SyncWait on a sync never fenced returns FAILED (was TIMEOUT_EXPIRED).
- 117 · b28 · the Metal 3 buffer table grows (acnh-mobile c2af194's case, MIT): the old 512 cap left s1800 black.
- 118 · b28 · a write released through one range stamps every range on that host page; acnh-mobile's `t_dirty_shared.c` (MIT) as is.
- 119 · not taken (b28) · not needed: no draw sampled a texture that shares an attachment's storage under another serial.
- 120 · b28 · cull FRONT_AND_BACK drops polygons only.
- 121 · b28 · a pass without attachments takes the viewport's extent when no scissor is set (it was 1x1, bayonetta3-mobile's case).
- 122 · b28 · the silent audio sink starts again from now when more than 100 ms behind (acnh-mobile, nxnative 3bd1e9e).
- 123 · b28, b190 · the display-link step from the rate the link runs at: on the 30 Hz link interval 2 takes one tick (30 fps; was 15).
  In a window with the 30 fps setting (b190): 33.34 ms a frame, 30 on screen.
- 124 · part (b28) · the sticks set NpadButton StickL / StickR Left, Up, Right, Down at half the axis range or more (blade2-mobile 34755e9,
  MIT). Left: a diagonal with both axes under half is not checked on a console.
- 125 · b29 · crash and diagnostics fixes, among them: the backtrace reads each frame with vm_read_overwrite (totk-mobile does so); the svc
  gate hands `svc_dispatch` the svc's address.
- 126 · not taken (b29) · not needed, and wrong for Tears of the Kingdom: its view is not Bayonetta 3's 16 bytes (7250b79).
- 127 · part (b29) · the Mac window: a press on the title bar goes to AppKit with its drag; App Nap held off while a window runs
  (blade2-mobile). Left: the window checks are for the user.
- 128 · b30 · the deep review's tests (their vectors as data, the code ours): totk-mobile's regions, dr3-mobile's UNORM8 and NaN payloads,
  acnh-mobile's zones, bayonetta3-mobile's NaN alpha reference; the core table stays Atmosphère's where totk-mobile expects otherwise.
- 129 · b31 · rewriter: a jump table whose add has the loaded entry first is found; decoding goes past a word Capstone cannot decode; equiv
  takes an exclusive store's success path; a synthetic module in CI (smo's selftest words, bayonetta3-mobile's sequences).
- 130 · b32 · pre-rendered movies held at 30 fps (`games/totk/movie_fps.c`; FPSLocker's flag). `DM_INACTIVITY_FPS` replaces the held rate.
- 131 · b33 · the game's GPU time: each TIMESTAMP report takes its place inside the submission (`gfx/nvn/nvn_ts.h`); the game reads p50
  6.33 ms. `DM_NVN_GPU_TS=completion` restores the old timing.
- 131a · b61, b117 · what sets the game's dynamic resolution: looked for in b61 (no enable bit found), answered by 276 (the controller main+0x584300).
- 133 · part (b35) · a frame step in whole vsyncs, as a switch (`DM_FRAME_STEP=vsync`; nxnative's design and numbers written anew, MIT):
  35 % less CPU than the mean step with uncapped pacing (the default, unchanged) in the 60 fps product's prologue on the Mac. Not needed:
  FPSLocker's three swapchain textures. Left: iOS's default, the user's choice.
- 134 · b36 · iOS audio: the session asks for 48 kHz and a 5 ms I/O buffer (nxnative 18c6143); a watchdog (after nxnative b301431, MIT)
  retries silent units.
- 135 · b37 · iOS: the frame rate setting held at 30 in Low Power Mode or from thermal state Serious up (after super-mario-odyssey-ios's
  frame pacer, MIT).
- 135a · b67 · the thermal hold a switch, the player's choice (the user, 2026-10-22): "Hold 30 fps when hot" (`dm.hot_hold`).
- 135b · b67 · the target paces the game too (`nvn_pace_hold`): at a 30 Hz link Tears of the Kingdom's 60 fps build had 149-151 of every
  300 presents discarded.
- 136 · b39 · no option pages on iOS: `ios/tools/gen_embed.py` builds the code options into the embedded image. manosaba has no options.
- 137 · b40 · vertex / pixel pairs noted at bind time in `<DM_SHDUMP>/pairs.tsv`; the union-interface library moved to 62g.
- 138 · b41 · render targets of virtual pools in a heap (`DM_NVN_RT_HEAP=1`; the placement rule is totk-mobile's `nvn_texture_alias.inc`,
  Apache-2.0, written anew in `gfx/nvn/nvn_rtheap.h`).
- 139 · b38 · code caves in the rewriter (`CAVES` in a game's rules.py; totk-mobile's routines appended to the patch area, written anew).
- 140 · b42 · FIFO frame consumption for manosaba (`DM_PRESENT_CONSUME=newest|fifo`; after acnh-mobile c2af194, MIT, written anew).
- 141 · b43 · IL2CPP class names in crash reports and memory snapshots (armyja's nxnative-aa-port reads the same fields, MIT, written anew).
- 142 · b44 · swapchain acquires before the image's last GPU work finished, counted (after blade2-mobile 43e760e, MIT): 0 in both games;
  `DM_NVN_ACQ_GPU=wait` stays off.
- 143 · b30 · svc details as Horizon (switchbrew's results; totk-mobile's vectors).
- 144 · b30 · the DT_RELR table checked whole before any slot is touched.
- 145 · b45 · small diagnostics (bayonetta3-mobile's copy of blade2-mobile, MIT, written anew): `DM_NVN_TSV_MAX_MB`, `DM_PRESENT_LUMA=<file>`.
- 146 · b46 · NCZ input (`tools/game_extract/Ncz.cs`; the layout as totk-mobile's `extract_romfs.py` reads it, Apache-2.0, written anew;
  ZstdSharp.Port 0.8.8 (MIT)). Not run on a real .nsz.
- 147 · b47 · the game's own SDK code run against the host (`DM_SDK_TEST=time,font`; after totk-mobile's `tests/test_time_sdk.c` and
  `test_font_sdk.c`, Apache-2.0, written anew). Vibration waits for 67.
- 148 · b48 · an in-game settings panel and a layout editor for the on-screen controls (BotWRecomp's settings button and lordran-native's
  control editor, MIT, the ideas).
- 149 · b60 · face buttons by position for PlayStation and Xbox controllers (MacNativeGames 31052d6, MIT, written anew):
  `DM_FACE_BUTTONS=position`; unset as before (the user plays with an Xbox controller by its letters).
- 150 · b60 · pchtxt patches at load and a cheat VM, refusing rewritten sites (MacNativeGames 1b33531, PR #22).
- 150a · b60 · pchtxt at load (MacNativeGames 1b33531's TOTK_PCHTXT, MIT, written anew): `DM_PCHTXT=<file>[:<file>...]`
  (`host/game/pchtxt.c`); a patch touching a word the rewriter changed refuses the whole file in red.
- 150b · b60 · a cheat VM (Atmosphère's cheat file format; MacNativeGames PR #22's split, MIT, open, written anew): `host/game/cheat_vm.c`
  runs Atmosphère's documented encodings (docs/features/cheats.md; facts only, its code is GPL); `DM_CHEATS=<file>`.
- 151 · b79 · the save-loading crash at 1.4.3 main+0x1bd01c4 (MacNativeGames dd0e893, MIT; written anew): `games/totk/optref.c` takes
  a value above the 39-bit address space there as absent, clears the field and runs the load again.
- 152 · not taken (b61) · frame interpolation by replaying the frame (LASRecomp `frame-interp-rt`; peiikai/LASRecomp
  `runtime/nvn/interp.c`, dc1842b..777fe71, MIT, work in progress): product60 already presents at 120 Hz; a replay on a phone
  costs a frame's GPU work again; to look at again once it runs there.
- 153 · b61 · frame latency 2 at 30 Hz (lfkdsk/switch-appletv-port-skill references/05-perf.md §5.3, MIT: present-to-present standard
  deviation 0.603 → 0.301 ms, for 33 ms more latency): `DM_PRESENT_LATENCY=auto`. Left: the default waits for a device check.
- 154 · b61 · the ultrawide aspect patch on the Mac (MacNativeGames `aspect_patch.h` at 1.4.3, MIT, written anew): option `aspect`
  (`games/totk/rules.py`) multiplies the cameras' ratio by a factor; `host/game/aspect.c`: `DM_ASPECT=<w>:<h>` or `window`, View → Wide
  Picture (⌘4) switches 21:9.
- 155 · b49 · the present viewport rounded before it is centred (Touchable 5cab3ac, MIT).
- 156 · b49 · dropped and slow frames judged against each frame's hold (after nxnative e7a84c7, MIT).
- 157 · b49 · the oe notification event set and cleared under the queue lock (after nxnative ee51071, MIT).
- 158 · b49 · the user clock context in Horizon's layout (after nxnative a35e964, MIT).
- 159 · b49 · the Metal 4 feedback queue drains its autorelease pool per work item (lordran-native 6145d1d, MIT).
- 160 · b49 · GetInfo of an unknown type answers InvalidEnumValue 0xF001 (Atmosphère's GetInfo default; Touchable the same).
- 161 · part (b50) · finished: sampler border colours Metal lacks (after MacNativeGames b6231e6 / 874a32d, #49, MIT, written anew):
  a variant of the stage; iOS reads the edge colour. Left: the user's checks (grass that stands up away from Link and can be cut).
- 162 · b51 · one pixel shader bound to float and to integer targets: an integer-output variant per key of `games/totk/fout.tsv`, the 23 keys
  of ours in MacNativeGames' shared list (MIT, keys as data).
- 163 · b52 · hair and cloth at 60 / 120 fps (MacNativeGames PR #79, merged, MIT; its sites, routine words and Unicorn table as data, the code
  ours): CAVES behind option realtime. Left (the user): wind at 60 and 120 fps.
- 164 · b52 · a mod that carries one of our RomFS overlay files (MacNativeGames #73, open): the SHA-256 of its copy decides between the
  overlay's file, a merge in `<mods>/-merged/<that SHA-256>/<path>` and the mod's copy (red line).
- 165 · b53 · decals that read their own G-buffer pass (`DM_NVN_FBX=split`; MacNativeGames PR #67, open, MIT, written anew in `fbx_prepare`),
  default off. Left (the user): a turning camera over paved ground or decals.
- 166 · b54 · a three-hour soak of the 60 fps product (MacNativeGames #57, #72, #74; upstream #90): the cliff save stopped after 47 min above
  the GPU budget, with DM_NVN_RT_HEAP=1 180.2 min: the heap is the default where it can be.
- 167 · b53 · a teleport command (`DM_SURVEY=1`; MacNativeGames d164868 `world_survey.c`, MIT: its 1.4.3 addresses, actor layout and bounds
  as data, written anew). Left: an overworld save for towns (the user).
- 168 · not taken (b53) · no second BC4 encoder in Tears of the Kingdom's runs; a 2D-to-3D copy waits for a scene that shows it.
- 169 · b53 · shader functional keys as a fallback (Touchable's internal/tools/shader_keys.py, MIT, written anew).
- 170 · b53 · the game language from the app's own language setting (Touchable's switch_language / CFBundleLocalizations, MIT, written anew).
  Left: the game's text in Tears of the Kingdom (62).
- 171 · b53 · the touch controls below the iPadOS window controls and controllers (Touchable, MIT, written anew). Left (the user): an iPad
  window, a controller.
- 172 · b53 · ProMotion 120 Hz for a game that steps by real time (Touchable's SS_DISPLAY_HZ=120, MIT). Left (the user): a ProMotion iPhone's
  frame rate line in Tears of the Kingdom.
- 173 · b53 · install checks and a Documents backup in `run.sh install` (Touchable's launcher install, MIT, written anew).
- 174 · b53 · the iOS shader directory checked complete before packing (acnh-mobile 67afe52 check_invariance, MIT).
- 175 · b53 · the game data verified once, then a marker (super-mario-odyssey-ios ea19678, MIT; DM_ROMFS_RECHECK=1 walks again).
- 176 · b53 · residency sets only on an Apple6-family GPU (GirlNet d2r-mobile 3e051f1, MIT); CI job `gfx-m3`. Not run on GitHub yet.
- 177 · not taken (b53) · the wait for the fence before the newest submission (LASRecomp dc1d28f): manosaba waits 1.00 times a frame on the
  newest still running (1.6-1.8 ms); whether it misses frames on a phone decides it (97 / 142).
- 178 · b53 · the diagnostics export redacted (lordran 40a9a72's rules, MIT, written anew in `host/rundiag.h`); memory read every 60 frames
  (lordran 24740d4).
- 179 · b54 · tests to take: overlapping `UpdateUniformBuffer` writes (Touchable), a 512 re-upload soak (lordran), the pass-continuation cases
  (GirlNet `t_p5r_rt_continue_gpu.m`, written anew in `gfx/tests/t_rt_continue.m`).
- 180 · b34 · screens judged by TypeSafe's Jev model drive the test runs (the d2r-mobile author's method): `tools/jevdrive.py`.
- 181 · b68 · the iPhone's 10 fps shrine: 480 G-buffer decal draws a frame, each reading its own pixel of the pass it draws into, got
  whole-attachment copies; now they read the tile (`fbx_fetch`, `dmsh_fetch.py`; after OJZen/totk-mobile's framebuffer fetch and
  DavidZande/MacNativeGames 4b18416): shrine on the Mac GPU p50 30.3 → 5.5 ms. `DM_NVN_FBX_FETCH=0`; chain `ba19de4f1d15`.
- 182 · b68 · the time-constraint declarations (nvn_pace.h `nvn_rt_*`): both threads declare the cap's period, the translation thread
  1.25 x the p90 of its own CPU a frame (nxnative 5efe1d8 / 04ea8f8 and MacNativeGames c5d8671 (MIT)). `DM_RT_DECL=old` on the iPhone
  17 Pro: no difference once warm (frame p50 19.9 ms against 19.2-19.5 ms).
- 183 · not taken (b74) · guest yields as `sched_yield` (`host/hle/hle_thread.c`): the iPhone 17 Pro's 60 fps logs show 182-6,697 yields a
  frame taking 0.2-2.8 ms, the most a spin or a block could win back. `thread_switch(MACH_PORT_NULL, SWITCH_OPTION_NONE, 0)` noted.
- 184 · not taken (b77) · pipeline archives (MacNativeGames' one binary archive per pipeline): on the iPhone 17 Pro the seed (209) and
  `pso_user.tsv` warm 3,621 render and 63 compute pipelines in 3.8 s. The rescans of 205 (MacNativeGames PR #166) cover the rest.
- 185 · b75 · the replay's CPU per draw: `bf_fn`'s cache key is binary (`bf_key` in an `nvn_hmap`), `arena_alloc` hands its buffer back
  unretained. iPhone 17 Pro: 28,500-28,900 → 22,700-23,500 instructions a draw. Not taken, as measured: `-O2`, `prog_get`, `g_rmu`.
  Sources: DavidZande/MacNativeGames b484570, nxnative 697df75, nxnative 9b7cb76, MacNativeGames 5f5de5e, dfa7ec4; GirlNet's
  `DM_NVN_SUBPROF`.
- 186 · b187 · 62j's leads, taken up under 219: shared storage tracked by serial (`stor_key_of`), `attr_read` without `pend_hits` (`fz_load_covers`)
  (DavidZande/MacNativeGames 907138d, 14142f8); the load probe's readback not ordered after the probed pass on Metal 4 (`lp_finish`);
  `DM_NVN_XV=0` in the one-at-a-time list; sentinel clears; the iPhone run with `DM_NVN_TBDR=1` under Metal API validation.
- 187 · not taken (b75) · lazy clears without the TBDR set (nxnative f5646ef) took about 2 clears a frame in the prologue, 0-1 in the
  shrine, removed; narrower encoder-start barriers (tvc97 12ca537): GPU p50 7.35-8.82 ms against 7.85-9.34 ms, inside the spread.
- 188 · not taken (b75) · attachment copies without `PixelFormatView`: since 181 the copies left are 0 a frame on the Mac and 5 a frame
  on the iPhone's game over screen (69).
- 189 · not taken (b77) · back up to 60 fps after a step down (nxnative 9409472 / b53bb89, tvc97 `nvn_cap_auto.h`): the only step down
  is the 30 fps hold in Low Power Mode or from thermal state serious; the game's own controller went back to 60 by itself.
- 190 · part (b183) · `DM_SCENE_AUTO=1`, off by default: a scene size driven by dropped frames (`games/totk/frame_step.c`, after upstream's
  3D scale controller, `hle_oe.c`, MIT, written anew; its GPU-time rule does not fit). On the iPhone Air it changes nothing: thermal state serious
  comes 44-58 s into Great Sky Island, the app then holds 30 fps and frames are judged against that. Left: a phone with room, then a setting.
- 191 · b74 · from the review: gather offsets (totk-mobile eaec4e4) in place since 686b81e (after nxnative 771f62f); `host/lang.h`
  reads the script before the region (Touchable 69ddbd5); 64-bit driver handles (aerolithink e218be4) not needed; the BC kernel's bytes
  widened before the shift (aerolithink 4e98460); write-back batched into one command buffer (`wb_textures`, `DM_NVN_WB_BATCH=0`;
  DavidZande/MacNativeGames 8c46687, MIT, written anew); SDK 27's Metal4FX barrier properties (bayonetta3): nothing to do.
- 193 · b74 · the iOS logs' `thermal -1`: `dm_thermal_now` had no iOS definition; `host/diag/thermal.m` gives NSProcessInfo's thermal state.
- 194 · b69 · the ability wheel (hold L) over black on the iPhone: the RomFS overlay's quick-menu fix (MacNativeGames' menu capture) was
  never served on iOS; the app serves `romfs_overlay` when `dm_romfs_overlay_check` passes.
- 195 · b70 · a replaced sampler released once the GPU is done with it (acnh-mobile 029dd30; `mtl_reg_samp`).
- 196 · b70 · `ReadFile` prepares its destination again on `EFAULT`, up to 8 times (nxnative 53d7aad; 191's part from yokeyhh v18): the
  dirty-page tracker write-protects pool pages again before `pread`.
- 197 · b70 · pchtxt `@flag offset_shift` applied and `#` lines skipped (MacNativeGames e55ad1c; IPSwitch's format, 3096/ipswitch
  example/README.md): the image offset is offset + shift − 0x100.
- 198 · b70 · a cube array whose depth is not a multiple of 6 named in red (blade2-mobile 4ebabb2).
- 199 · b70 · `tools/sdis.py --xref` / `--callers` to the end of the PLT, and an image without sized exports (nxnative 108d483).
- 200 · b72, b150 · `XLATE_DEBUG=0` refused by `dmsh_build.py` and `dmsh_chain.py` (batch 150; left in batch 72 for the next chain rebuild):
  it gives Tears of the Kingdom 306 failures and 4,175 modules with unreachable else-if bodies (nsCHaojIMaLiAo, nxport's 0001 / 0002).
  `Program.cs`'s comment corrected.
- 201 · b71 · the rewriter's red zone: `verify/equiv.py` refuses a patched side touching memory more than 128 bytes below the CURRENT
  sp ("redzone"; upstream 373b402, 4f0603f: the Android form); none of our modules has such a pop.
- 202 · b71 · a guest thread stopped inside a rewrite block is resumed and stopped again until pc is outside the patch area
  (`hle_thread.c`, `in_patch_area`), up to 2,000 tries. Not observed.
- 203 · b70 · GPU write-back decided by the texture's own bytes (MacNativeGames 8a29e0a, PR #188): `wb_note` keeps a copy of a texture
  up to 256 KiB; `DM_NVN_WB_EXACT=0` decides by pages.
- 204 · b70 · border colours per axis (MacNativeGames PR #148 / be82504): `dm_border_sample` returns the border only on the axes whose
  wrap mode is a border. Not taken: the filtered edge of PR #205.
- 205 · b70 · pipeline prediction scans again after a scan that stopped at its budget (MacNativeGames PR #166: pipeline time in long
  frames 35.8 / 7.1 / 2.3 s → 5.5 / 2.9 / 1.05 s, M4 Max, world entry): `DM_NVN_PSO_RESCANS` (32; 0 as before).
- 206 · b72 · CPU Read / WriteTexels of a region copy only the region's block-linear bytes (MacNativeGames PR #185: frames over 40 ms
  25.9 → 1.0 a minute, M5 Max, 1.1.2): `nvn_region_move`; Tears of the Kingdom's cliff save makes no such call.
- 207 · not taken (b72) · occlusion results' freshness (nxport `report_guard.rs`): its numbers (72.6 against 65.9 fps) compare with an
  occlusion wait we never had.
- 208 · not taken (b72) · the dirty tracker's fault-around and page-exact GPU pages (dodola 2e58d47 / 58d3aa2): the iPhone's peak is
  about 45 write-protect faults a frame against dodola's 420: at most about 1 ms a frame.
- 209 · b72 · 184's warm-up seed from the players' `pso_user.tsv` lists merged (upstream 08f51a8): `tools/pso_merge.py` writes
  `pso_seed.tsv`, `ios/run.sh build` packs it as the app's `pso_manifest.tsv`. Not for the Mac host: 55 long frames against 29.
  Noted for 184's archives: nxport 17d527f, MacNativeGames 30a1b93.
- 210 · not taken (b72) · single-draw passes as compute kernels (nxport `tiny_kernels.rs`; 61e): on the iPhone they are mostly 1280x720
  passes loading their attachments (100 took 3.6 of 12.4 ms GPU); nxport moves only rectangles up to 640x360.
- 211 · b71 · the unsigned IPA signed ad hoc with the increased memory limit alone (`ios/tools/ipa_sign.py`; LNPANDY c3bccb5: AltStore
  keeps the ~3,375 MB default without it).
- 212 · b71 · `-Werror=unguarded-availability-new` for the iOS 18 build (acnh-mobile a98d127; `ios/tools/project_m3.py`).
- 213 · b71 · the fps line's "·" shown as "¬∑" (NSString's `%s` reads Mac Roman): `host/ns_text.h` `dm_ns_text` in `log_overlay.m`
  and `front_mac.m`.
- 214 · b73 · sampler border colours Metal lacks on iOS (161 there): `ios/tools/border_msl.py` copies the .metal of the shaders in
  `games/totk/border_msl.txt`, `border_variant` compiles the variant on the device (MacNativeGames 852f862 builds its variants into
  the app ahead of time).
- 215 · b73 · the fps line kept its last "on screen" rate when no frame came with a screen time; now "on screen –" after 1.5 s of
  presents without one.
- 216 · b77 · frame-rate trials (`gfx/nvn/nvn_fptrial.h`, `DM_FPS_TRIAL=1`): a game held at interval 2 or 3 reads its report times drawn toward
  the GPU start; on the phone at 60 Hz none of 22 trials held: off by default.
- 217 · b84 · single-draw passes up to 256x256 as compute kernels (210 reopened; after nxport's `tiny_kernels.rs`, MIT, written anew), opt-in
  (`DM_NVN_KPASS=1`): no GPU time saved on the phone's island, 12.6 / 13.0 against 12.3 / 12.4 ms.
- 218 · not taken (b185) · the G-buffer split by two depth-probe dispatches (3c8e9ba061067c07_b, bc58d5dec860c367_b) that fill a grid of
  depths: each needs the depth of exactly its place, so neither can wait; drawing depth first would change which of two coplanar fragments
  wins. Worth two splits, about 18 MB loaded and 18 stored each at 1280x720, of 315 MB loaded and 304 stored a frame.
- 219 · part (b186, b187, b194) · the TBDR rewrites that merge or defer passes. Great Sky Island: 228's set 230 MB loaded and 231 stored a frame
  against 251 and 241 on the Mac (b186), 30.0 fps against 29.8 on the Air under the hold. b187 (62j's leads): shared storage left alone
  (`core/rt.h` tex_shared). manosaba: `DM_NVN_TBDR=0` changes no store, GPU time, picture or memory (b194): left on. Left: 228's set on the Air.
- 222 · b77 · the host frame step's trials (`nvn_fstep.h`, with `DM_FRAME_STEP=vsync`): after 5 s a trial of k - 1; `DM_FSTEP_OPT=0` turns them off.
- 223 · not taken · the frame step in ticks of 120 Hz: on the phone it settled at ~24 fps and the game ran 7.8 % fast.
- 224 · not taken · the GPU one frame behind the game (LASRecomp dc1d28f, MIT): on the phone 44 fps against 47.5, and not safe for Tears of the
  Kingdom. `DM_NVN_GPUWATCH` now also compares the vertex and index ranges draws read in place.
- 225 · b77 · the translation thread made cheaper: uniform range copies made from their previous one, vertex streams bound once per encoder, the
  uniform copies under one fault guard a run. The phone's island at 120 Hz: sustained 50.6 → 56.0 fps.
- 226 · b110, b111 · the submit call's capture walks its lists breadth first (`gfx/nvn/core/capture.c`): calls go on a queue and the list
  48 places on is fetched as one is taken. Tears of the Kingdom on the M5 Mac standing on Great Sky Island: the lists 215 → 161 us, the capture
  322 → 265 us. Tried and left: recording-time notes (359 against 347 us), no de-duplication before the sort (364 us). Closed b111, rest under 271.
- 227 · not taken · the GPU one frame behind made safe (after 224): on the phone 54.2 fps against 56.0. The code is not kept. As part of it,
  pending updates' bytes were laid over memory the same way (b134), declined with it; there the CPU had written 1 of 63,392 such ranges
  before the next submit call on the Mac, while loading.
- 228 · b78 · mid-submission commits with the TBDR rewrites that neither merge nor defer passes; store decisions and same-target continuation
  alone: 55.1 fps against 56.0 on the phone. Not a default.
- 229 · not taken (b183) · the guest main thread's QoS class (`DM_MAIN_QOS=ui` gives it user-interactive, batch 79): its A/B on the
  iPhone Air shows no difference: 27.44 fps under the hold against 27.40-27.65 (after 230 the limit is the power split).
- 230 · b79 · `DM_PWRTEST=<threads>[:gpu[<busy %>]][:<seconds>]` (`ios/Sources/pwrtest.m`, `host/pwrspec.h`): the app runs a plain load instead of
  the game.
- 231 · b80 · command memory (nxnative 0b3c7b1, its issue #61): 16 bytes kept after every record but the END (`OP_KEEP`); ours kept 24, the END
  included.
- 232 · b83 · load-exclusives as acquire loads (nxport ce8f1c3, MIT): `tools/patch_nso.py` sets bit 15 of every LDXR / LDXP / LDXRB / LDXRH.
- 233 · b80 · a storage binding that runs past its pool binds from the pool it starts in (nxport 79b736d; `sbuf_gpu_addr`) and gets a red line.
- 234 · not taken · uniform reads at a computed index bounded by the bound size (nxport `msl_specialize.rs`): no GPU fault or hang in Tears of
  the Kingdom's logs. Until a game shows it.
- 235 · not taken · a Stencil8 view of a depth-stencil texture as `MTLPixelFormatX32_Stencil8`, and integer textures sampled as such (blade2
  a7dff99): not seen in Tears of the Kingdom. Until a game needs it.
- 236 · b80 · the system fonts' `.bfttf` in both word orders, told apart by the magic word (LASRecomp PR #330, b159129; LASRecomp issue #48).
- 237 · not taken · socket Poll / Select wait out their timeout instead of failing at once (nxnative 65d4a4d): Tears of the Kingdom makes no
  socket, nifm or ssl call in the prologue. Until a game spins on them.
- 238 · not taken · a cube view whose layer count is not a multiple of 6, and CPU-written linear textures off the 256-byte row alignment
  (AnySwitch 6058d2e, 5e9fcc1): neither in 66 Tears of the Kingdom logs. Until a game needs it.
- 239 · b81 · early fragment tests for the fragment shaders whose control section sets them (+0x718 bit 0, the driver's EarlyZForce; after
  MacNativeGames PR #222 and nxport, MIT): chain ba19de4f1d15 → f9e7d3ecf49e. The iPhone's island at 120 Hz: 55.4 / 56.4 → 59.5 / 59.6 fps.
- 240 · b83 · the scene size changed while the game runs (NativeTOTK 1723fed, BennettStaley/NativeTOTK-Apple-Silicon, MIT, written anew):
  `games/totk/frame_step.c` (`scene_latch_step`) sends a size asked (`scene <w>x<h>`) down the game's own mode-change path; 9a92046 (targets of
  a size the game left released) is not needed. On the iPhone 17 Pro each size picked the next frame, two of four depth dumps at 0.8 of the
  page's size (the sign 131a set). What it allows: 190.
- 241 · not taken · multiplies and adds fused in vertex stages (nxport): none of the 16,099 vertex shaders has the pairs nxport fuses, and the
  offline compile with `-fmetal-math-mode=safe` already fuses a plain a * b + c.
- 242 · not taken · a uniform range's private copy only as long as the shader reads (DavidZande b9f8b16 after tvc97 PR #159 / nxnative f5646ef;
  MacNativeGames PR #257): our bindings are about that short already (1.8 KB each). Not now.
- 243 · not taken · guest loops that spin on memory without a system call allowed to sleep (nxnative 99c3f91): on the Mac's island no guest loop
  gathers samples; the workers wait with yields (svc 0xB).
- 244 · not taken · the border-colour pipeline variant's key cached (MacNativeGames PR #286): `border_variant` is 0.4 % of the translation thread.
- 245 · b82 · the Metal calls on a worker thread of their own (nxnative PR #62, `nvn_rec.h`): our encode stage (`nvn_render_stream.h`) is on by
  default on iOS (`DM_NVN_RENDER_STREAM=0 / 1` chooses): the phone's island, sustained 60.0 / 60.6 fps against 59.6 / 53.4. PR #62's wider worker
  not done.
- 246 · b81 · the player's pipeline list written on a serial queue of its own (AnySwitch c483c04), off the submit / translation thread.
- 247 · b81 · `passdump` returns before reading the frame number when off (shh632058099 e460049).
- 248 · not taken (b81) · the real-time computation declared covers 1.1 × the 120-frame peak too (nxnative 65d4a4d; `DM_RT_PEAK=1`): on the
  phone's island 58.4 fps against 59.6 without it. Not a default.
- 249 · not taken · `DM_PRESENT_INTERVAL=display` (MacNativeGames issue #136): on the iPhone 17 Pro's island 55.7 fps against 59.6 in the game's
  own interval mode.
- 250 · b81 · the frame step averages the intervals before its floor (MacNativeGames' Breath of the Wild path): `frame_step.c` takes the mean of
  the last five present intervals.
- 251 · not taken · first-sight pipelines on an 8 ms budget a frame (totkrecomp bbf0fd4): a budget only limits skipping, which we declined.
- 252 · not taken · the frame step's trials leave out one-off hitches and want the late frames spread over the window (LASRecomp f99186b): with
  133 (`DM_FRAME_STEP=vsync`, off by default in both games), if it becomes a default.
- 253 · b81 · pipeline keys with the write mask and blend bit of the colour targets a normal pass lacks cleared (MacNativeGames f3e2f66;
  `pso_key_target_canon`): the player's list from the phone 3,645 → 3,617 keys. MacNativeGames f134a7b's scan budget is not taken.
- 254 · not taken · one frame's submissions replayed K times (nxport `--replay=F:K[:M]`, `--replay-ab`): a replayed frame repeats its GPU writes
  into the game's memory.
- 255 · b82 · `DM_SCHED_STATS=1` (`host/schedstats.h`): waits per kind with the wake-to-run latency (totkrecomp 65a9411), system calls a second
  by number (AnySwitch 477f749).
- 256 · b82 · `DM_NVN_PASSGPU_EVERY=<n>`, `DM_NVN_PASSGPU_FROM=<frame>`, `DM_NVN_PASSGPU_FRAMES=<n>` (shh632058099 9ecf169, GirlNet 9dc64f9): a
  submission left out takes no heap and writes no row.
- 257 · b81 · `DM_CLEAR_SHADER_CACHE=1`: the iOS app starts with an empty Metal shader cache (AnySwitch ec152a8; `host/shadercache.h`).
- 258 · b81 · the address space a free-signed install gets (LASRecomp PR #238 8dd3116; previousdolphin): the user's free-account install (the
  increased memory limit, no extended virtual addressing) reserves the whole 6 GB on the iPhone 17 Pro, iOS 26.4.
- 259 · b80 · the crash report's wait for a stderr lock another thread holds is measured, not counted in sleeps (5.4 s on GitHub's macOS runner
  failed CI).
- 260 · b85 · min / max / clamp give Maxwell's result for a NaN operand on every GPU: `precise::` instead of spirv-cross's `fast::`
  (`gfx/tools/msl_helpers.py` `precise_minmax`, applied by `dmsh_build.py`; seedds/nxnative 56704a0, MIT, written anew). The M5 already
  agreed, an A20 Pro does not. Chain f9e7d3ecf49e → ae6a8bd1acdd. The iPhone 17 Pro's (A19 Pro) own `fast::` results were not measured.
- 261 · b86 · the Metal backend split by subject: `gfx/nvn/nvn_mtl.m` (10,113 lines) keeps the device, queues, residency and deferred
  releases (295 lines) and includes 38 parts from `gfx/nvn/mtl/` in the original order; one translation unit as before, the preprocessed
  text the same but for blank lines and the 19 `__LINE__` values of 217's `KP_NO`.
- 262 · b87 · the single-draw kernel passes of 217 removed: off by default, and on the iPhone they saved no GPU time. Out: `mtl/kpass.inc`
  and its hooks, `nvn_kpass.h`, `gfx/tools/dmsh_kernel.py`, `games/totk/kernel_shaders.txt`; the backend is the one before 217 again. Git
  history keeps all of it (b921a1e).
- 263 · b95 · the two switches off by default traced and removed: `DM_NVN_RT_RANGE` (a pass continued across a SetRenderTargets that only
  narrows a colour view's levels; from the initial release 46578a2, no measurement on record, spread over ten parts) and `DM_NVN_DEPTH_PLAIN`
  (plan item 99's A/B: the same picture either way). The default path is unchanged; in `DM_NVN_TBDR_OFF` "range" now names nothing.
- 264 · b88-b94, b96-b107, b189, b192, b195 · the replay core and the Metal backend apart (the user, 2026-10-24: the 10,000-line Metal file
  first, before Android; headers with a Vulkan backend in mind): what the replay decides in plain C in `gfx/nvn/core/`, each step with its header
  and a test that needs no GPU; `gfx/nvn/nvn_backend.h` with the replay loop in `core/exec.c` (`nvn_exec`); the backend's private headers
  `mtl/mtl.h`, `mtl/enc.h`, `mtl/reg.h` and its own units `mtl/keepalive.m`, `frame_timing.m`, `passgpu.m`, `present_pipe.m`, `texdump.m`,
  `lod_bias.m`, `present.m` (the presentation layer, b195); `gfx/nvn/hle_nvn.c` in 17 `gfx/nvn/hle/` parts, `host/`'s 62 sources in directories.
  Not taken: the other parts as units (alone each lacks 9 to 59 of the main unit's file-scope names, or the draw reads it at every call).
- 265 · b108 · GPU write-back records that outlived their pool (MacNativeGames 0930478 / 873edff, MIT, written anew: after a warp, Tears of
  the Kingdom's heap blocks, an allocator node and index buffers were overwritten with an old rendering): a finalized pool takes its
  textures' records with it (`wb_drop_range` from `mtl_pool_del`); a dirty-tracking range keeps the clock it was registered at (`range_t.born`).
- 266 · b108 · the dirty-page stamps around an upload (seedds/nxnative 5b0c11a, MIT: Tears of the Kingdom's glyph letters missing): the stamp
  is taken before the pages are protected (`nvn_dirty_protect_stamp`); a packaged texture's `seen` is its upload's stamp; a page another
  thread is releasing when the protection comes is waited for and protected.
- 267 · b108 · upload batches bounded (nxnative 7dc250e, MIT: Tears of the Kingdom peaked at 10.7 / 12.8 GiB on its Mac, an A20 Pro iPhone was
  killed at 8.09 GiB with 1.1 GB re-uploaded in one frame): a batch is committed once its staging would pass `DM_NVN_UP_BATCH_MB` (128; 0
  unbounded), and a committed batch's one-off blocks and CPU-path buffers are released when that batch completes (`up_retire`).
- 268 · not taken · nxnative 82ab25b's present allocator reused only after its own present completed: not reachable here. The drawable is
  taken before the allocator is waited for and reset, and each of the layer's three drawables returns only after the present that showed it.
- 269 · b108 · the memory limit in the iOS boot log (xinerqu/ns2ios-shell 8160029's line, MIT, written anew): `boot: memory limit <N> MB
  (footprint + available), RAM, and whether the increased memory limit is in the app's signature`, read back from the executable's own code
  signature (`ios/Sources/codesig.h`), so a low limit says whether the signer left the entitlement out.
- 270 · not taken (b120) · a 720-row drawable (`DM_IOS_DRAWABLE_H=720`, added in b109; the compositor scales it) against the native one on the
  iPhone Air, standing on Great Sky Island: at 30 fps GPU p50 27.62 / 28.25 against 27.60 / 27.07 ms, at 60 fps (hold60) 15.57 / 15.13 native
  against 15.64 / 15.57 ms. No gain the runs tell apart from their spread: the switch stays, off.
- 271 · b116, b119, b120, b187, b191 · CPU per draw: b116 took off the texture views made again on the same base texture (`mtl/descriptors.inc`);
  b119 the memcmp of 8-byte hash keys (`gfx/nvn/nvn_hmap.h` nvn_hm_keq; MacNativeGames PR #750, MIT, written anew); b187 a uniform update's private
  copy holds only what its reader reads (`nvn_refl.h` crn, `core/upd.c` upd_shadow_at's need): 5.5 → 1.6 MB copied a frame; b191 a uniform binding's
  pool looked up only when no private copy or snapshot stands for it (`mtl/encode_args.inc`): 67.83-68.94 → 67.16-68.00 M instructions a frame.
  Not taken from that PR (all its switches: +11 % fps on an M4 MacBook Air): `TOTK_CBUF_CACHE`, `TOTK_AB_SAME`, `TOTK_POOL_HASH`, `TOTK_DIRTY_FAST`,
  dispatch arguments before the encode stage's wait, the per-shader border key (border_variant 0.0 %); 4ppp44 cb21bec / d0d3cb1: no verdict.
- 272 · not taken (b110) · each list's walk kept while it lives (nxport 9c5463d): the lists that cost are recorded anew every frame.
- 273 · not taken (b117) · the translation thread on the iPhone Air (sustained, standing on Great Sky Island): 30 fps with it and without
  (frame p50 33.3 / 33.5 ms, GPU 28.2 / 28.0 ms); the thread takes 2 ms of the game's submit and present (7.5 against 9.6 ms), and the GPU
  sets the rate, so the default stays on.
- 274 · not taken (b116) · of the 4 render passes a frame a dispatch ends on Great Sky Island, 1.2 are taken up again with the same targets,
  and the one that costs (17.6 MB loaded again a frame) is 218's depth probe bc58d5dec860c367_b, which reads the depth the pass wrote, so it
  cannot run before the pass.
- 275 · not taken (b116) · the completion handler runs 0.19 ms after the GPU's end on the M5, and Tears of the Kingdom waits on a submission
  still on the GPU 0.00 times a frame on the Mac, 0.16-0.19 on the Air: the event's earlier wake would give about 0.03 ms a frame.
- 276 · b117 · the game's own dynamic resolution: the controller main+0x584300 (MacNativeGames 2279d4b, MIT, written anew) steps the
  scene through 100 / 90 / 80 % by predicted frame time; the option fixedres (`games/totk/rules.py`, off by default; on iOS built in with
  DM_OPTIONS) makes it 1.0. The iPhone Air at 30 fps sits at 90 % or 80 % much of the time (one window 519 of 600 presents at 0.9).
- 277 · not taken (b111) · heap members keep lossless compression (GPU p50 with it off 7.21 / 7.37 ms against 6.66 / 9.83 on, within the runs'
  spread); no `MTL4VisibilityOptionResourceAlias`: nxnative's case is two Metal buffers over the same bytes, which ours never makes (of Tears
  of the Kingdom's 2,686 pools none overlaps another's guest range).
- 278 · b112 · GPU results written back after the game's fence waits (MacNativeGames 1bffa28, MIT, written anew; `mtl/writeback.inc`
  `mtl_wb_fence_done`): a target up to 64 KiB in a CPU-visible pool whose rendering has completed is written back with no invalidate, as
  on the console (Tears of the Kingdom's grass cut map, 128x128 RGBA8). DM_NVN_WB_FENCE=0 turns it off. Not yet seen in a grass field here.
- 279 · b111 · the GPU memory budget waits before it stops (MacNativeGames 4de7b4e, MIT, written anew; `mtl/commit.inc` `gpu_budget_settle`):
  over the budget, deferred releases and completed batches' upload staging are released while running work completes, at most 2 s; only
  memory still allocated stops it. A zero-copy pool buffer Metal refuses is retried 8 times, 2 ms apart doubling (MacNativeGames 11ea252).
- 280 · part (b113, b114, b188, b193) · manosaba's trial peak. b114: a finalized pool's shared textures go after 16 submissions (`mtl/textures.inc`).
  b188: depth / stencil targets memoryless (`DM_NVN_DS_MEMORYLESS=1`, `gfx/nvn/mtl/rtheap.inc`): RSS 2,458-2,656 → 2,120-2,199 MB on the Mac. b193:
  the validity ledger off (`DM_NVN_XV=0`). Left: the peak on the phone; an asset change for the user to decide (PR #2's `patch_character_ppu.py`).
- 281 · part (b109, b121) · native resolution, iOS setting "Native" (`dm.render_height` -1). manosaba (b109): Unity's two constant 1920x1080
  answers cut to host call 0x81 (`rules.py`, `graphics.c`). Tears of the Kingdom (b121) docked at 1920x1080 (`games/totk/frame_step.c`): Air frame
  p50 46.4 against 33.3 ms. Left: above 1080; other games by the backend route (LASRecomp's "Match display", AnySwitch's `DM_INTERNAL_RES`).
- 282 · b111 · res_add / res_remove in their own autorelease pool (nxnative 7d368c4): the weak table's containsObject: autoreleased the object
  on guest threads, whose pool never drains. Tears of the Kingdom on the Mac (DM_RES_CENSUS=1, standing): retired but not destroyed 25
  textures → 0, RSS 12 MB less, GPU allocated the same.
- 283 · b116 · the pipeline guesses compile on at most two threads, the oldest kick first, and a draw needing one no thread has started yet
  compiles it on the submit thread (4ppp44 cb21bec, MIT, written anew; `mtl/pipeline.inc`; `DM_NVN_PP_WORKERS=<n>`, 0 as before). iPhone
  Air, manosaba's first trial, cold shader cache: long frames 37 for 2,688 ms in all against 60 for 3,338 ms. The tile-read pipelines
  MacNativeGames 59e240a added to its seed are in ours already.
- 285 · part (b118) · Tears of the Kingdom on the iPhone Air: on Great Sky Island the game's controller picks interval 2 within the first minute
  and from thermal state serious (1-2.5 minutes in, on USB power) our hold keeps 30 (135). hold60 (`games/totk/rules.py`, off by default) keeps the
  controller at 1: about 60 fps until thermal state serious, then 30. The phone has a cooler on it (the user, 2026-10-28). Left: a run off USB power.
- 286 · b115 · 278's write-back without a GPU round trip on the game's thread: the submission that renders such a target copies it to a buffer
  of its own at its end (`mtl/writeback.inc` wb_stage_encode), and the write-back after a fence wait puts those bytes into the storage when
  they are of the newest rendering. Standing on Great Sky Island (M5): 0.6-1 → 0.043 ms a write-back; frame p95 17.2 → 16.8 ms.
- 287 · b113 · a depth / stencil texture bound as a colour target is left out (`mtl/renderpass.inc` rt_color_fmt, red line 116), and the
  parallel precompile predicts a depth attachment only for a depth-renderable format (`mtl/lookahead.inc`): manosaba at its first trial's
  start bound a Depth32Float_Stencil8 target as colour 0 and an RGBA8 one as depth, and Metal's descriptor validation ended the process.
- 288 · b122 · tenth review (2026-10-30, [sources.md](sources.md)): SDK functions with the wrong return type (seedds/nxnative 971d336,
  MIT, written anew); Tears of the Kingdom's main imports both, manosaba's neither: `nn::time::CalculateSpanBetween` returns bool, true when
  computed (ours returned a Result, 0 on success), and `nn::audio::AcquireAudioDeviceNotificationForOutput` returns a Result (ours was void).
- 289 · b122 · the SDK's address-region node area cut from 0x3fe00000 (1022 MB) to 0x3e00000 (62 MB) for both games' sdk builds (nxnative
  7deea8e / d1b7444, nx2android 8056043, MIT; the words and the constructor checked here): `games/totk/rules.py` sdk 574284b40c06 +0x145d0c
  / +0x145d78, `games/manosaba/rules.py` sdk ab3835b2a37b +0x14fbf0 / +0x14fc5c. Highest offset used on the Mac: Tears of the Kingdom 1715
  → 749 MB, manosaba 2460 → 1518 MB. `hle_svc.c` logs the highest region end at each 512 MB and a refusal red.
- 290 · b141 · texture reads of guest memory the game took back, guarded (MacNativeGames f3bb714, MIT: SIGBUS in the deswizzle, the Depths;
  written anew): `gfx/nvn/core/guard.c`'s guard_run catches a fault inside the read's range on that thread (one sigsetjmp, 213 ns on the M5);
  the Metal backend's CPU upload and content hash keep the texture it had with a red line, the Vulkan upload tries again at the next use.
- 291 · b141 · quad strips (MacNativeGames bff585f, MIT, written anew: the Hero's Path's route): `gfx/nvn/nvn_quad_strip.h` makes each pair
  after the first a quad a b d c (triangles a b d / c a d). The Metal backend skipped them (a red line), now draws them from shared indices
  or indices rewritten into the arena; the Vulkan backend drew them as triangle strips, now the same as Metal.
- 292 · b182 · fade steps that truncate to 0 above about 63.5 fps (MacNativeGames 3e9e7f67 / PR #1112, #1134's rule: a positive step at
  least 1, zero stays zero): four caves under `realtime` (`games/totk/rules.py`) at main 0x0a7ac4, 0x95dd24, 0x6155e4, 0x615660; checked by
  `games/totk/check_fade_step.py` (Unicorn), not shown in play on the Mac: it rests on the check and MacNativeGames' reports (#466, #538, #556).
- 293 · not taken (b182) · cutscenes at 60 fps (71c; mamcjr/TOTKRecompAndroid da06d5a, facts only: four functions run once a frame only
  in cutscenes, main 0x45c700, 0x2b23c7c, 0x42c3bc, 0xa5a188). Not needed here: product60's opening cutscene follows real time.
- 294 · b182 · `fixedres` also makes the controller's three stores of its tier nops (0x584a70, 0x584b70, 0x584b94; TOTKRecompAndroid 1e56bfb,
  facts only), so the tier stays what it was made with. Both products rebuilt (`product.prev292`, `product60.prev292` kept; 16 caves from 12,
  47 option words from 40).
- 295 · b182 · the translator's BRX in the sequential form from here on, not the chained (nx2android 02e336f, MIT), each logged into the
  reflection's notes (`InstEmitFlowControl.cs`): 91 shaders, 237 BRX, each sequential; translated both ways ours lost no case. Chain
  ba6c7622e044, one rebuild for 292, 294 and 295. The other two nx2android translator fixes not needed with our `XLATE_DEBUG=1` (plan item 200).
- 296 · b142 · smaller correctness after seedds/nxnative (MIT, written anew), the Metal backend unless said: a draw whose argument binding
  ended its pass is begun again in a new one (81ccacf, tex_upgrade_pfv); reused argument blocks follow the serial table's steps (971d336 after
  MacNativeGames e49aa55); a compatible view (tex_fit) of a view is made from the texture under it (d9c2d5e); a texture of another type in a
  3D slot binds the zero 3D texture (2bccdcc); a writable storage binding of size 0 drops the uniform snapshots over the rest of its pool
  (f293a70); a vertex shader reading past a stream's bound size is reported (1c54ea0; the zero-padded copy to do when a game shows the line);
  the host (ce99604): the audio renderer's command list bounded, audio outputs and new guest threads registered once made, thread priority under G.
- 297 · b142 · the virtual core given up while waiting (game.json virtual_cores, Tears of the Kingdom; a no-op without): every nn::fs call the
  host takes over (valentderah/totk-pc-recomp 101f2c8, MIT), nvnMemoryPoolInitialize (up to 20 ms there), and the GPU wait of a write-back on
  invalidate (MacNativeGames 0fd9358e, MIT: the wait only). NVN_BLOCKING now lives in hle_nvn.c's preamble.
- 298 · b141 · the crash report with a time limit (jhaizhou-ops/d2r-mobile 3ebb29bd, MIT, written anew; n2ray's f697868f in its review
  branch): the first part (CRASH line, registers, frame-pointer backtrace through dm_mem_read) is built on the stack and written with
  write(2), no allocation or lock; the second part (symbols, thread dump) runs under a 3 s alarm that ends the process with a line saying so.
- 300 · not taken (b182) · texture filtering at 1/256 of a texel for vertex-shader lookups and gathers
  (TOTKRecompAndroid 8adbc5c, c02a43f, facts only: dotted dark lines on the terrain on an Adreno): Great Sky Island walked on the Mac at
  60 and 120 Hz shows no dotted line, no row of dots. Not needed on Apple's GPUs; on an Adreno it is in 313.
- 301 · part · manosaba on Android (the user: under way from 2026-10-30; 301 next after the tenth review) on a Redmi K100 Pro Max (Adreno
  840, Android 17): the replay core shared with iOS, a Vulkan backend behind `nvn_backend.h`, in steps.
- 301a · b125 · the pool memory, a probe of ours on the K100 (2026-10-30), in the app domain: all four ways work at a fixed 4 KB-aligned guest
  address: AHardwareBuffer mapped over the pool (nx2android; HyperOS's gralloc refused it on a POCO F7 Ultra, BOTWRecompAndroid 4fc0d28), an
  exported fd mapped at the pool's address (PKShield-Android `nvn_vk.c`), `VK_EXT_map_memory_placed`, copied pools with write-back.
- 301b · b123 · the rewriter's android target (`DM_TARGET=android`, `tools/patch_nso.py`; after shirai91/nx2android's DM_TARGET and
  jhaizhou-ops/d2r-mobile's NOREDZONE / sp_frame, MIT, written anew): the slot block at bionic's TLS_SLOT_APP; no red zone on Linux, so each
  block moves sp down and an x18 instruction on sp is re-addressed by 16 (`sp_moved`); a site that cannot be encoded keeps the red-zone form
  (`<name>.belowsp.tsv`): manosaba 0 of its 55,245 sites, Tears of the Kingdom 7. The verification knows the target (`TARGET_ANDROID`).
- 301c · b124 · the host under adb on the phone (`android/`: `make -C android GAME=manosaba`, NDK r30 at API 35): manosaba headless at 60 fps
  from its 600th frame (frame time p50 16.6 ms). OS differences go through `plat.h` (`runtime/plat_apple.c`, `android/plat_linux.c`: xattrs in
  `user.` (jhaizhou-ops `hle_fs.c`), the thread stack without bionic's guard page (RogelioLB/nxnative-android)), `slots.h`, `uc.h`, `table.h`,
  `runtime/asm.h`, `android/compat/`; the null backend `gfx/nvn/null/null.c` draws nothing; the loader refuses a product of the other target.
- 301d · b125 · the app (`make -C android GAME=manosaba apk`, `android/run.sh`): manosaba on the Redmi K100 Pro Max at 60 fps from its 600th
  frame with sound, touch, controllers and saves, nothing drawn yet. APK by `android/tools/apk.py` (after shirai91/nx2android's mkapk.py, MIT,
  written anew; Python and the openssl command, no Android SDK), APK Signature Scheme v2 by a self-signed key in `~/.config/dm-android`;
  host `libdmhost.so`, `android/app_android.c`, `android/front_android.c` with `input_map.h`, AAudio in `hle_audio.c`, `android/movie_android.c`.
- 301e · part · the Vulkan backend, `gfx/nvn/vk/`. No other Android port (2026-10-30 survey, docs/sources.md) keeps pools at their
  addresses on Qualcomm's driver; the 840's has `VK_EXT_map_memory_placed`, so ours does.
- 301e1 · b126 · the Vulkan backend's base (`gfx/nvn/vk/`, the app's; adb shell's host keeps the null backend): the app presents through Vulkan
  on the K100 at 60 fps, draws and dispatches dropped until 301e2. `vk_device.c` (Vulkan 1.3 on the 840's 1.4.295), `vk_pools.c` (memory pools
  placed at the guest's addresses, VK_EXT_map_memory_placed), `vk_submit.c` (a ring of 8 command buffers, a timeline semaphore whose value is
  the NVN fence), `vk_tex.c`, `vk_fmt.c`, `vk_ops.c` (copies, clears), `vk_present.c` (FIFO swapchain, the crop letterboxed by a linear blit).
- 301e2 · b127 · shaders and draws on Vulkan: the app draws manosaba's title on the K100 at 60 fps, no red line. `gfx/tools/dmsh_vk.py` builds
  the Vulkan bundle (`<game>.dmvk`, in the APK as `shaders.dmvk`, `DM_VK_SHADERS`; pixel bindings +64 so a program's stages share one set;
  manosaba's 2740 shaders, 50.8 MB; not part of the chain version). `vk_shader.c`; `vk_draw.c`: dynamic rendering, a pipeline per program,
  pushed descriptors, quads through shared indices, 8-bit indices (VK_KHR_index_type_uint8); `vk_tex.c`: uploads take the dirty-page stamp first.
- 301e2b · b128 · dispatches, images and the rest of the bindings on Vulkan: manosaba played on the K100 through the prologue into the first
  trial at 60 fps, no red line. `vk_bind.c` (layouts pushed when they fit, else a set of their own; `DM_VK_PUSH=0`; storage images; texel
  buffers null with a red line), `vk_draw.c` (dispatches, indexed quads), `vk_tex.c` (storage views; in the trial 116 images alive, 444 MB);
  `work/scripts/android_live.sh` / `android_play.sh` drive the app through its command channel (keys, not touches).
- 301e3 · part (b129, b130, b131, b132, b133, b134, b148) · Vulkan passes kept open and measured: the same-target continuation (`nvn_be_reset`)
  and renamed uniform updates (`core/upd.h`, `vk_upd_flush`; b129), each submission's GPU span for the fps line (`mtl_set_frame`; b130), barriers
  kept in the pass (`vk_pass_barrier`; b131), each pass's GPU time by its targets (`vk_ptime.c`, `DM_VK_PASS_GPU=1`; b132), the arena waits only
  for a block entered anew (`nvn_be_arena`; batch 133), ranges only known writes reach get a snapshot (`us_gw_note`, `pend_lay`, `pend_begin`):
  debate 58-60 fps on the phone, flushes of renamed updates none since b134; the 16-bit formats, their order settled by 303e (b148). `vk_shot.c`
  (`DM_NVN_SHOT`). Pre-rotation costs no GPU pass (2026-10-30). Left: the rest of the set (the NVN fence: LASRecompAndroid 54686f6).
- 301f · part (b124, b135, b136, b137, b140) · frame rate and heat on the phone (nx2android #8 / #11: pinning to the prime core, the present wait
  counted as work). The debate's dips were the render thread making and dropping texture pools (b135), mapping and unmapping their memory (b136),
  copying out a pool's content the game gave back unread (b140): pages /proc/self/pagemap shows never written are not copied in; a pool gone stays
  placed, not copied out, until the host gives its memory back (`dm_guest_unmap_register`): debate copying 0.14 s against 2.0. `alias_phys`
  (`host/hle/hle_svc.c`): one `mprotect` to map, one `mmap` a run of host pages to unmap. `dm_thermal_now` (`android/diag_android.c`) follows
  AThermal (b137), `nvn_fps_policy` holds 30 fps from serious up unless `DM_HOT_HOLD=0`. Left: the rest (scheduling for an app, b124).
- 301g · b138 · the settings panel on Android (the user, 2026-10-30: an interface like the iPhone's for anyone): Back or three fingers open it
  (`android/front_android.c`); Java widgets (`android/java/io/github/iceorange_dev/dm/Panel.java`, `apk.py --dex`) write `files/settings.txt`.
  `android/settings.c`: env.txt wins, `DM_SETTINGS=0` leaves the file out; native by `host/native_size.h`. Three-finger tap not tried on the phone.
- 301h · part (b139) · the game's data for a player without adb (the user, 2026-10-30): `make apk-data` writes manosaba's APK with its data
  (2769 MB, 19 s); on its first start the app copies the data into the external files folder (`android/data_apk.c`, `Progress.java`'s dialog):
  674 files, 2714 MB, in 2.3 s on the phone; the everyday `make apk` carries no data. Left: Tears of the Kingdom's data imported by the app.
- 302 · b143, b144, b145 · manosaba on the iPhone Duo (the user's main phone; the user, 2026-11-01). The refresh rate comes from the screen
  the window is on (`mtl_display_hz` through `front_ios_screen_hz`; Apple's app-resizability skill, Xcode 27.1). "Native" follows the
  display (the user, 2026-11-01): `games/manosaba/graphics.c` `step_gate` calls Screen.SetResolution for `dm_game_native_screen`: opened
  2852x1604, closed 2034x1144, two frames after the window moved; StopAudioOut releases the queued buffers and signals the event (Ryujinx's
  AudioDeviceSession.Stop, MIT, written anew). The picture clear of the outer front camera (the user, 2026-11-01): `host/fit_rect.h`
  `dm_fit_rect`, `dm_known_cutout`. Upstream d2r-mobile's iPhone Duo support (v2.0 b811b90, MIT); d-date/iphone-duo-skill (MIT, 31bdab9).
- 303a · b146 · (the user, 2026-11-08: fix the bugs) the store-decision records hold 10 (`SD_MAX`: 8 colours, the depth and the stencil;
  nxnative 9508b93, MacNativeGames 6f4cbe3): they were 9, overrun by an 8-target pass with a depth-stencil target.
- 303b · b146 · pages protected under the slot mutex from the lookup to the last mprotect (nxnative af616f5, MacNativeGames 9505582): a pool
  destroyed in between left its returned memory read-only with no range.
- 303c · b146 · an ID's cached view dropped when the ID has no texture (`view_still_good`; nxnative 0e4480d: nil == nil kept it).
- 303d · b146 · the AudioOut notifier disarms with an acquire-release exchange (`notify_arm` / `notify_disarm`; nxnative c76a424): none lost
  in four thread pairs of 2,000,000 rounds each; the release-only store lost 115-230 in every run.
- 303e · b146 · RGBA4 read fully reversed from Metal's ABGR4 (`SV`; SkyRecomp 81f6507, nxport 00e04bc, nxnative 230fe53, deko3d's table as
  facts); the 16-bit family's order written down in `nvn_fmt_mtl.c`.
- 303f · b146 · no blending on integer colour outputs, the write mask kept (MacNativeGames PR #1797: Metal aborts).
- 303g · b146 · an empty memory pool registers nothing (MacNativeGames d3407e2): Metal refuses the buffer.
- 303h · b146 · the Mac app out of Game Mode (`LSSupportsGameMode` false in `tools/mac_app.sh`; nxnative cb59a67).
- 304 · b147 · Link keeps moving ground's velocity above 100 fps (nxport 1bf994a, MIT, written anew; MacNativeGames and SkyboundTOTK for
  1.0.0): a cave under `realtime` (0x562a64) compares +0x28 × +0x38, 30 / time scale, where the check at 0x562a58 kept a tenth above 100
  (`games/totk/check_ground_carry.py`).
- 305 · part (b194) · Tears of the Kingdom's scene targets across a resolution step (BennettStaley 38a1348, MIT: temporal history flashes in
  shrines): in a shrine on the Mac with the game's steps forced and logged (`DM_SCENE_DR_LOG=1`, `games/totk/frame_step.c`) no flash; their
  answer not taken while nothing shows. MacNativeGames #1348 checked at 90 and 120 with 292. Left: the camera-rate getter (nxport b588cf2).
- 306 · b147 · a virtual core's owner moves to a free core it may use when a waiter may use only that core (MacNativeGames 3a48acf, written
  anew; Horizon's suggested-thread migration). On the Mac (product60, `DM_SCHED_STATS=1`) moves happen, but the core takes of about 10 ms
  stay and frame times are as before.
- 307 · b147 · the lookahead guesses no attach mode without the depth a draw tests or writes, nor without the stencil it tests
  (`pp_variant_ok`; MacNativeGames 3578d66).
- 308a · b148 · Vulkan on the K100 (the user, 2026-11-08: the phone connected; rules in `gfx/nvn/vk/vk_rules.h`): an attachment the pixel
  shader writes no output to gets write mask 0 and no blending, an integer target no blending (upstream PR #229; the Metal backend's rule).
- 308b · b148 · a view's swizzle replaces the texture's (nx2android f13ecfa).
- 308c · b148 · one texture at two colour slots attached once, a pending clear moved to the slot kept (`rt_dup_slot`, as on Metal;
  nx2android 8e06fd2).
- 308d · b148 · the swapchain through a lost surface or a refused window (made again at the next present) and up to 16 images (LASRecomp
  ee37681 / 2745a5e / 202916d, from mamcjr's LASRecompAndroid).
- 308e · b148 · the pipeline cache kept in the app's cache folder (`DM_VK_PCACHE`; LASRecomp 202916d, SkyRecomp ec367c6): on the K100
  120,594 bytes saved, read back at the next launch, the first four pipelines then made in 0.8 ms against 21.7.
- 308f · b148 · the manifest's `configChanges` with mcc, mnc, locale, touchscreen, layoutDirection, colorMode, grammaticalGender,
  fontWeightAdjustment and fontScale (0x5000ffff with the rest; the values from Android's ActivityInfo; nx2android d47cb00).
- 308g · b148 · a barrier to host reads at the end of each submission (PKShield b4002cc).
- 308h · b148 · nearest filters where the view's format is not filtered linearly (upstream 0733eb4): on the K100 (Adreno 840, driver
  0x8034a02c) every integer format and D32_SFLOAT_S8_UINT.
- 308i · b148 · a low-latency AAudio stream that stalled opened again without low latency, once (doom-port 728dd65).
- 308j · not taken (b148) · staging buffers stay per upload: 25 in 40 s of the title and prologue, 2.6 ms in all.
- 309 · b148 · the game's frame rate voted for the window once it has held for 60 presents (`ANativeWindow_setFrameRate`, fixed source;
  nx2android 70c8cb8; `DM_VK_VOTE=0` off): 60 voted and taken a second in.
- 310 · part (b152) · a pool's zero-copy buffer no longer made over read-only pages (`mtl_pool_add`; nxnative 2d544e0 / issue #1591, MIT,
  written anew); `gfx/nvn/mtl/fp16_probe.h` (nxnative PR #1128); commits every 75 draws kept (nxnative PR #1123); the half-float rounding went
  to 315d, the pad's redraw to 315e. Left: sound with other apps' (acnh-mobile 8dcd00c), memory touched before loading (nxnative-mkw b8f81e4).
- 311a · b149 · chain ae6a8bd1acdd → 5285ca7bad0f (the user, 2026-11-08: port what is worth porting). The Vulkan bundle's jobs give the
  translator the host's subgroup size, 128, and 8 bits of gather precision (`Program.cs` `QueryHostSubgroupSize`,
  `QueryHostGatherBiasPrecision`; nx2android 44eca7e, MIT). TMML returns the LOD as unsigned 24.8, a negative LOD as 0 (`InstEmitTexture.cs`;
  nx2android 44eca7e; yuzu #10234's rule). The Mac libraries compiled for macOS 26.0 (`dmsh_build.py` `MAC_MIN`; MacNativeGames #1845).
  Fixed: `gfx/tools/msl_retarget.sh` compiled the older-API package's libraries without `-fpreserve-invariance -fmetal-math-mode=safe`.
- 311b · b150 · chain 5285ca7bad0f → ce89a37d8939. Split storage words read whole (`gfx/tools/msl_helpers.py` `wide_loads`; nxnative
  1b9fbea, MIT, written anew): 614 of Tears of the Kingdom's shaders, 17 of manosaba's. One translation for shaders that differ only in
  control bytes the translator does not read (`dmsh_build.py` `xlate_identity` / `xlate_share`, `gfx/tools/dmsh_vk.py`; MacNativeGames
  fb9b734, facts): the Vulkan bundle 50.8 → 32.5 MB. An app carries only its game's libraries (`gfx/tools/shader_pack.py`, `twins.tsv`):
  manosaba's IPA carried Tears of the Kingdom's 28,652 libraries (447 MB), now 4,842 files, 24.1 MB, 59 MB as an IPA.
- 312 · b147 · WaitSynchronization ends with the index of the handle whose signal woke it (nxnative db206a4): an event signalled and
  cleared before the waiter ran sent it back to sleep.
- 313 · part (b149, b151) · kept: early fragment tests (nxnative), the 1 GiB pool cap (MacNativeGames' 4K failure is not ours), the pass-end barrier
  (nxnative PR #1572), no pre-rotation on the K100 (nx2android f7a3371). Left: Tears of the Kingdom on Android (301e2c / e3): the eleventh review's
  ports (PKShield f4f9f1c, mleo2003 77f6586 and 3f5467d, triasbrata 41beb51 and 8bb3212, AurelioB/totk-android `VULKAN.md`, nx2android e154eec,
  diablo3-port 5bb14e8), 300's 1/256-texel lookups, the twelfth's Vulkan items (S2Arm, written anew; Ryujinx's `hasVectorIndexingBug`).
- 314a · b153 · the dirty tracking checks `mprotect`'s result: a page that did not become writable stays PROT and its fault is not claimed,
  one that did not become read-only counts as written until a protection succeeds; failures counted and logged outside the fault path
  (S2Arm 4c0bec5, Apache-2.0, written anew).
- 314b · b153 · the argument buffer's length from the reflection, 8 bytes an id up to the highest (`shaders.inc` `shader_arg_len`); Metal
  is asked only with `DM_NVN_ARGCHECK=1`: a function Metal gives no argument buffer makes `newArgumentEncoderWithBufferIndex` assert,
  which stopped the game at the shader's load (S2Arm 4c0bec5, Apache-2.0, written anew).
- 314d · b153 · the app's `CFBundleDisplayName` is the game's id in ASCII, the home screen's title in `<loc>.lproj/InfoPlist.strings` for
  each of the game's localizations and en (`ios/tools/app_names.py`, `Gen/Names`; S2Arm 4c0bec5, Apache-2.0, written anew).
- 314e · b153 · the phone's CoreHaptics engine plays haptics only, as the controllers' do (S2Arm 4c0bec5, Apache-2.0, written anew);
  S2Arm's 2,049 ms sound stall at an engine's start was not seen here. A running app is now asked about before any launch.
- 315 · b154, b155, b156, b157 · Tears of the Kingdom on the iPhone (bandwidth, heat, memory), each part measured before it is kept.
- 315a · b154 · the store of an attachment a pass loaded and did not write is dropped with the TBDR rewrites off too (S2Arm 4c0bec5,
  Apache-2.0, written anew): `sd_decide` applies the rule (udc), `DM_NVN_UNWRITTEN_DC=0` off. Tears of the Kingdom's prologue on the M5: 379 →
  304 MB stored a frame, GPU p50 7.0 → 6.7 ms; on the iPhone Air (Great Sky Island) 37-46 MB a frame saved, energy inside the scene's spread.
- 315b · b155 · a mid-submission commit waits while the game's work is on the GPU (`g_inflight`) and is made at the first encoder boundary
  after the GPU has run out; `DM_NVN_CHUNK_BUSY=0` as before (S2Arm 4c0bec5, Apache-2.0, written anew; S2Arm: "the GPU waited 5-40 ms with the
  next one already committed"). Commits a frame 9.9 → 3.0 on the M5, 13.2 → 2.1-2.2 on the iPhone Air; kept on for the command buffers it saves.
- 315c · b155 · pools only textures come from kept out of the residency set (583 pools, about 919 MB resident on the Air): a texture upload
  from a pool not yet resident reads the storage through a zero-copy buffer over the texture's own pages, retired with the batch's staging;
  `DM_NVN_UP_TEXBUF=0` as before (S2Arm 4c0bec5, Apache-2.0, written anew). Pools resident 896 → 335 MB on the M5, 969 → 406-408 MB on the Air.
- 315d · not taken (b156) · S2Arm's rounding of unblended half-float colour outputs toward zero in the shader by a per-pipeline mask (nx-port's
  clamp was not taken because it hit R32F targets): the M5 rounds toward zero (0x3c00), the iPhone Air's A19 Pro to nearest (0x3c01); Tears of
  the Kingdom's targets hold nothing at ±65504 or ±inf, no difference seen (port when a game needs it). In: `texdump -` counts them and NaN.
- 315e · b156 · the on-screen pad: the stick's base and knob and a camera finger's ring are layers of their own, the rest drawn again only
  when `tp_visual_sig` changes (310; upstream 7b62d9c, MIT; S2Arm's one layer per control, Apache-2.0, written anew). On the Air:
  450-451 updates and 0 redraws in 30 s; shown and idle the pad costs what none does. Command channel: `padtest <name>`, `padshot <path>`.
- 315f · not taken (b157) · virtual cores only for threads that ask for their core or are bound to one (S2Arm's on-demand mode: 0 shared core
  numbers on the Mac where none gave 671) would free nothing: 51 of Tears of the Kingdom's 52 threads would keep their core (the masks are the
  game's own), the free one waits 0.00-0.02 ms/s for a core (the M5's prologue). `DM_SCHED_STATS=1` gets a line a thread every 5 s.
- 315g · not taken (b157) · the NaNs 315d's counts found in Tears of the Kingdom's low-resolution two-channel half-float targets (11,296-80,192
  values, 400x224 and below) are not written by the game's passes: they are the bits of other targets the game places later at the same
  offsets of its virtual pool (rtheap.inc), read as half floats; none with `DM_NVN_RT_HEAP=0`, on the M5 and the Air; nothing reads them so.
- 316 · b158 · manosaba's short sleeps (88) backed off (S2Arm 4c0bec5, Apache-2.0, written anew; S2Arm on a Unity game: 5.13 W for the
  device, 4.03 W at 100 µs): in a run of sleeps under 1 ms from one return address each sleep after 2 ms of the run is max(asked, run / 8)
  up to 100 µs (`DM_SLEEP_BACKOFF=<µs>`, `host/sleepstats.h`). M5: 24,000 → 9,200 a second; iPhone Air 17,700 → 12,300, CPU 0.13 → 0.10 W.
- 316b · b159 · from 316's counts: Tears of the Kingdom's 64,000-122,000 SleepThread(20 µs) calls a second returned at once (`sleep_precise`
  kept a 20 µs margin; ModuleSystem's waits, `main+0x794b0`); now one asked within 20 µs of its deadline sleeps once (`DM_SLEEP_SHORT_REAL=0` as
  before). The M5: 130,000 → 7,200 calls a second, CPU 0.97 → 0.80 W; the iPhone Air: CPU 137-187 → 124-146 %; frame time and GPU p50 the same.
- 317a · part (b160) · iOS diagnostics, MetricKit (`ios/Sources/metrickit.m`, `ios/Sources/mxseen.h`; S2Arm 4c0bec5, Apache-2.0, written anew):
  the reports the system kept are taken at launch, exits, crashes, hangs and CPU and disk-write exceptions logged, each report saved once as JSON
  in Documents/metrickit (payloads kept by their own time span, named at the next launch). Left: whether a free-account install receives payloads.
- 317b · b160 · the signature's expiry read from `embedded.mobileprovision` (`ios/Sources/provexp.h`), given in the boot log, and with less
  than 48 hours left shown over the game for 12 s in the device's language (S2Arm 4c0bec5, Apache-2.0, written anew).
- 317c · b179 · the translator's own warnings kept in the reflection's notes as `translator: <message>` (`dmsh_xlate`, chain 47b5de2aa276).
  Across the 31,415 shaders: 7,776 "invalid instruction" notes in 324 of Tears of the Kingdom's compute shaders, 5 global stores without a
  storage buffer in one vertex shader, PIXLD left out in two of manosaba's pixel shaders (item 322); no dropped reduction (S2Arm found 398).
- 318 · part (b161, b181, b184) · sampler LOD bias and min / max reduction on GPUs before Apple10 (S2Arm's probes, Apache-2.0, written anew):
  b161 a probe and a red line (`gfx/nvn/mtl/samp_probe.h`); b181 the bias by a shader variant where it is ignored (`mtl/lod_bias.inc`); b184
  sample_compare too, and `games/totk/lodbias_msl.txt`'s MSL in the iOS app. Left: a device before Apple10 (the user, 2026-11-10: none for now).
- 319 · b162 · the install with the player's own free account without `local.env`, the weekly renewal without a build (S2Arm 4c0bec5,
  Apache-2.0, written anew; the user asked for all of it): `ios/tools/acct.py` finds the team, the one paired phone and the bundle ID;
  `ios/run.sh resign` (`ios/tools/resign.py`, `ios/tools/prov`) signs again in 4-9 s on the M5; `push-data` by content (`ios/tools/push_hash.py`).
- 320 · b163-b177 · Tears of the Kingdom filling the iPhone's and the iPhone Duo's screen with the HUD at its own size, split into its parts.
  The route chosen by the user on 2026-11-09: S2Arm's, whole (graphics/widen and graphics/nvn/widen_glue.inc at 4c0bec5, Apache-2.0,
  written anew): 16:9 render targets wider by k, viewports × k, the projection's x row ÷ k, the UI layer at its size (2868x1320 → 2346x1080).
- 320a · b163 · `gfx/nvn/core/widen.h` gives k and the widened sizes from the screen's size (S2Arm's rules written anew): k snapped so that
  1920 gains an even number of columns (2868x1320: 426, k 1.221875; 1280 → 1564), Ky in rows for a screen narrower than 16:9 (the Duo's inner
  2670x1878, 1.42: 270 rows over 1080, 1350; 720 → 900); 4:3 and square textures stay. A screen within 1 % of 16:9 widens nothing.
- 320b · b164 · with `DM_WIDEN_SCREEN=<w>x<h>` (it stands in for a phone's screen on the Mac; off without it) a 16:9-family colour or depth
  target the GPU draws into gets its Metal storage at the widened size while the game's view of its size stays, and the render-target heaps
  are made and placed at their scale times k (or ky) (`gfx/nvn/mtl/textures.inc` tex_widen): with 2868x1320 a 1280x720 target 1564x720.
- 320c · b165 · a pass whose targets are widened knows its exact widening (`g_pass.kx`, `ky`) and maps a draw's viewport and scissor and a
  clear's scissor by it: a 3D draw's × k, the HUD in the centred 16:9 band, a full-canvas 2D page with black side bands. With 2868x1320 on the
  M5 the game's viewport over the whole 1280x720 target reaches column 1560 of 1564.
- 320d · b166, b167, b168 · `gfx/nvn/core/widen_rw.h` finds by shape perspective projections, view-projections and their inverses, screen
  size and aspect pair in the blocks a draw binds and rewrites them in a copy for the draw, the x row divided by k (S2Arm's core rules,
  Apache-2.0, written anew); `DM_WIDEN_RW=0` off. `wrw_maybe` first, then reuse by hash: the M5's submit thread 7.15 → 3.20 ms of CPU a frame.
- 320e · part (b169) · sampling and copies: a `[nvn] widening's census` line every 300 frames (Tears of the Kingdom's prologue on the M5 has
  no dispatch, clear or copy meeting widened storage); a whole-level clear on widened storage clears the whole level, a copy between textures
  widened by the same ratio is mapped. Left: compute and a copy's destination widened after its source (S2Arm's rules) when a frame needs them.
- 320f · b170, b171, b172 · the translator scales x and y apart (S2Arm's change, written anew in `third_party/ryujinx`): the support buffer
  gains `render_scale_y` (`mtl/args.inc`, `vk/vk_bind.c`); b171 the scales per draw; b172 every vertex shader ends with S2Arm's edge push,
  written anew (`EmitterContext.EmitWidenPush`, `widen_push`; chain 28614768c859): the title's side bands 187 / 105 of 255 → 0 / 0.
- 320g · part (b173, b174, b175, b176, b177) · the present of the widened image over the whole screen: b173 iOS's switch, `DM_WIDEN=1`
  widens to the screen the game starts on (`ios/Sources/main.m`, `host/native_size.h` `dm_widen_screen`, `present_vp`); b174 the present's
  crop mapped by the storage's ratio (`core/widen.h` `widen_map_rect`); b175 the rewrite scans only the slots a shader reads (`cb_slots`,
  `widen_rw.h` `wrw_block_r`; chain 36a510fd7866): iPhone Air, Great Sky Island standing, widened 27.3 fps against 28.3 unwidened; b176 the
  setting "Wide screen (next launch)" (`ios/Settings.bundle/Root.plist` `dm.widen`, and the in-game panel; the user, 2026-11-09: a setting,
  off unless chosen); b177 the edge push per axis (`EmitterContext.EmitWidenPushAxis`, `widen_push_y`; chain 1f985598396c). Left: the Duo.
- 321 · part (b178, b195) · the frame step's rules S2Arm measured on an iPhone (written anew in `gfx/nvn/nvn_fstep.h`), for when
  `DM_FRAME_STEP=vsync` becomes the iOS default: the window (step up only when more than 24 of the last 120 frames were shown late), the probes
  back to 60, the cost as the critical path from the frame's first submit to its GPU end (`nvn_fstep_cost`; Great Sky Island on the Mac: 0.61-0.70
  vsyncs against the GPU's 0.46-0.55); `DM_FSTEP_RULES=0` off. iPhone Air, the first two on and off: 30 fps throughout. Left: the phone with the path.
- 322 · part (b180) · the translator's warnings (317c, b179): 324 of Tears of the Kingdom's compute shaders decoded through 24 zero words;
  vertex shader 588e242a52f5f7aa's stores with no storage buffer; manosaba's pixel shaders a1fe71290083482a and 45eb69b3b99c6600 reading a
  multisample texture at PIXLD.MY_INDEX, left out. `DM_NVN_KEY_WATCH` found none of the three drawn. Left: each changed when a run draws with it.
- 323 · not taken (b182) · Link drawn flat red-orange in some Great Sky Island frames on the Mac: the game's low-health alarm, not ours
  (he is red only in the frames where the HUD's hearts glow red too).
- 324 · b184 · the iPhone Air started hot ran 15.0 fps: with the target 30 the game's interval 3 (20 fps) took 2 ticks of the 30 Hz link.
  Now the 30 Hz link only while a frame is a whole number of its ticks (`gfx/nvn/nvn_pace.h` nvn_link30_step, `present_thread.inc` link_sync),
  a ProMotion screen presents at 120 while the target is below 60 too (`nvn_promotion_hz`). The Air (2026-11-10, `DM_FPS=30`): 29.0-30.0 fps.
- 325 · b185 · the decals' tile reads lost behind the edge push (from chain 28614768c859, b172): `gfx/tools/dmsh_fetch.py` screen_locn now
  takes x and y from before the push and is made in the build's second stage (`dmsh_build.py`), `mtl/draw.inc` wd_push_ok never pushes
  a draw that reads the tile; chain fd1036a8b871. Mac: 508 → 315 MB loaded a frame; the Air: 27.4 fps with the loss, 29.8 again (with 326).
- 326 · b186 · the three draws a frame on Great Sky Island that still got copies and a split: `gfx/tools/dmsh_fetch.py` variant made no
  readable-tile version of a fragment shader with more than four colour outputs or a depth output (356 in all); now up to eight colours and a
  depth output kept (`gfx/nvn/nvn_refl.h` fo_color 8). Chain 5a7de6a8fef9. The Mac: no copy, 315 → 251 MB loaded and 304 → 241 stored a frame.
