# Plan archive

Plan items 1-259 (batches 1-84, 2026-10-19 to 2026-10-23) as they stood on 2026-10-23, moved here unchanged from DEVELOPMENT.md so that
the plan item numbers cited in code comments ("plan item N") and in commit subjects still lead somewhere. Items that were still open then
continue, shortened, in [DEVELOPMENT.md § Open work](../DEVELOPMENT.md#open-work); their status here is not updated, and the list "What is
left (2026-10-21)" below is that day's. Each finished item's full notes are also in the commit whose subject starts with "Plan batch N"; the
reviews' own findings with their evidence: `git show 66fa170:DEVELOPMENT.md`. The projects named here: [sources.md](sources.md).

## Plan: all of the wider review, simple to complex (2026-10-19)

Every finding of the reviews above, done in this order. `[ ]` open, `[x] batch N` done in the commit whose subject starts with "Plan batch N", `[-]`
measured and not needed (with the number). Each item gets a unit test and a broken version that must fail it; each batch ends with the gfx tests
(Metal 4 and Metal 3), the host tests, a manosaba and a Tears of the Kingdom smoke run, then one commit. The full notes of each finished item
(sources, measurements, test details) are in `git show 66fa170:DEVELOPMENT.md` and in each batch's commit message.

What is left (2026-10-21): every item the Mac can do alone is done. The open items and the device checks of done ones, by what they need:

- The user at the Mac (a window, sudo, a display): 97 (`powermetrics`), 71b (the covered window), 71d (a display above 120 Hz), the window
  checks of 123 (the 30 fps setting) and 127.
- Play or saves: 69 (game over), 71c (product60's 30 fps systems), 132 (a save where the wave program draws), and the checks of 161 (grass), 163
  (hair and cloth), 165 (`DM_NVN_FBX=split` over paved ground or decals), 167 (an overworld save for towns).
- The console: 94a (Link close to the camera, the Great Sky Island grass), 124 (stick diagonals).
- An iPhone or iPad: 71b's Metal 4 screen; the checks of 148, 153, 171 (done on 2026-10-21: 65, 68, 106,
  134, 135, 140, 173, 175, and manosaba's part of 142).
- A controller: 66, 67 (67b, 67c, 67d).
- Tears of the Kingdom on the phone (it runs since 2026-10-21, 62): 62c2, 62g, 62h, 62j, 63b
  (`DM_APP_POOL_ADD_MB`), 170 (the game's text), 172, 181 (the shrine that fell to 10 fps), how 62l's 60 fps build plays.
- The next game: 64's open sub-items.
- The user's choices: 192 (the touch controls after Touchable).
- Decisions: `DM_FRAME_STEP=vsync` as iOS's default (133: 35 % less CPU on the Mac); `DM_PRESENT_LATENCY=auto` as the default (after 153's
  check).

Tier 1, small and local. Correctness:
- [x] batch 1 · 1 svc 0x34 type 3 (64-bit wait if equal), unknown types refused by 0x34 / 0x35, an arbiter race test (`host/tests/t_arbiter.c`)
- [x] batch 1 · 2 controller face buttons by the printed letter on Nintendo controllers (`host/gamepad_map.h`, `t_padmap`; the device check is 66)
- [x] batch 1 · 3 the BindProgram stage mask at the three scans (`defer_try` Q scan, `fuse_try` steps four and five; `t_defer_chain` mode 6)
- [x] batch 1 · 4 sampler LOD bias and min / max reduction (`gfx/tests/t_samp.m`; macOS / iOS 26, reported once before that)
- [x] batch 1 · 5 GPU time per frame as the union of command-buffer spans (`t_gputime`); `@gpu_ms` is the union from batch 1 on, `@gpu_sum_ms` the
  old sum, so GPU numbers above this plan are sums
- [x] batch 1 · 6 a fault guard around uniform snapshot copies (`t_usnap` section 5; without it SIGBUS)
- [x] batch 1 · 7 overlay directory listings without the 2048-name cap, duplicates found by hash (`t_fs`)
- [x] batch 1 · 8 the silent NVN no-ops red on first call; SetDepthBounds reported when enabled (`t_nvn` 7b)
- [x] batch 1 · 9 precise SleepThread (`host/tests/t_sleep.c`)
- [x] batch 1 · 10 rodata read-only after relocation (`host/rodata_seal.h`, `DM_RODATA_RW=1` leaves it), svc 0x02 with Horizon's checks; read / none
  reported, not applied: Tears of the Kingdom only asks for read-write
- [x] batch 1 · 11 the 8 / 16-bit UI2F / I2F vertex formats (`gfx/tests/t_vfmt.m`); Metal refuses 32-bit integers for a float input (measured), so
  the 32-bit and 10:10:10:2 kinds stay unmapped
- [x] batch 1 · 12 thread dumps list the handles of a WaitSynchronization with each one's state (`t_waitsync`)

Tier 1, performance:
- [x] batch 2 · 13 busy-yield back-off releasing the virtual core, a power line every 300 frames. Off by default (`DM_YIELD_BACKOFF=<µs>`): in
  Tears of the Kingdom's prologue it cost energy and frames; the power line shows manosaba at about 25,000 wakeups a second (see 76)
- [x] batch 2 · 14 targeted wakes (`t_waitsync`); upload waits on a condition variable (`t_upwait`); the audio renderer blocks on buffer releases
- [x] batch 2 · 15 replay lookups: per-thread pool cache, dirty range on the texture and `mprotect` over page runs, residency no-op skip
- [x] batch 2 · 16 a spare texture for re-uploaded CPU textures, dropped after 120 submissions unused (`t_texcheck` 4b)
- [x] batch 2 · 17 reflection summary per program, attachments walked once per pass (`t_zskip`)
- [x] batch 2 · 18 libvpx row multithreading (`DM_VPX_ROW_MT=0` off): the Ganon movie 1.02-1.03 → 0.95 ms a frame, frames identical

Tier 1, iOS and Mac:
- [x] batch 3 · 19 a Metal 4 launch check with a screen (Metal 3 for the iOS 18 build; `DM_TEST_NO_METAL4=1` shows it anywhere)
- [x] batch 3 · 20 game data excluded from backups (RomFS and sysfont outside the bundle; the boot log reads the value back)
- [x] batch 3 · 21 `GCSupportsGameMode` (iOS 18.0-18.5 read only this one; `LSSupportsGameMode` is from 18.6, Apple's reference)
- [x] batch 3 · 22 `env.txt` switches on the device: the bundle's (`DM_ENV_FILE` at build) then Documents' (`host/envfile.h`, `t_envfile`)
- [x] batch 3 · 23 audio notifier thread at USER_INTERACTIVE (`t_audio`)
- [x] batch 3 · 24 compressed files stored, not deflated, in the IPA (`.zs`: 1.66 s deflated into a larger archive, 0.26 s stored)
- [x] batch 3 · 25 iOS 17 code guards (`@available` around the residency set and `hasPerformanceProfile`; iOS 17 is not supported since, see 59)
- [x] batch 3 · 26 quit waits for a save in progress, 15 s on the Mac, 3 s on iOS (`dm_fs_save_wait`, `t_fs`)
- [x] batch 3 · 27 a covered Mac window discards presents (`DM_OCCL=0` off, `DM_TEST_COVER=<s>,<len>` to check). The window check with the display
  on is still to run (71b)

Tier 1, tooling and diagnostics:
- [x] batch 4 · 28 one game at a time on the Mac (`host/game_slot.c`: a flock(2) and a holder file; a run for the player, the .app or
  `DM_SLOT_OWNER=user`, stops an agent's run with SIGTERM, any other run waits; `DM_SLOT=0` off, `DM_SLOT_DIR`; `t_gameslot`)
- [x] batch 4 · 29 a stamp of the rewriter's sources in the product (`# REWRITER` in each fixups header, `tools/rewriter_stamp.py`; the loader says
  when a product differs). The Tears of the Kingdom default product is the 30 fps one kept on purpose since dba469f (its "frame step: main has no
  host page" red line is expected; `product60` carries the stamp)
- [x] batch 4 · 30 the rewriter stops on a system register outside NZCV, FPCR, FPSR and DCZID_EL0 (`tools/sysreg.py`); neither game has one
- [x] batch 4 · 31 the SDK's abort text in the log: the last 8 OutputDebugString lines and AbortImpl's return address printed at a Break
- [x] batch 4 · 32 the last 256 lock / condition variable calls in thread dumps (`DM_KTRACE=0` off); `DM_DEBUG_CV_LATE_WAIT=<µs>` (`t_ktrace`)
- [x] batch 4 · 33 upstream's `t_loader_weak` (`host/tests/t_loader_weak.c`: absent weak imports stay 0, our loader already did)
- [x] batch 4 · 34 `DM_MEMSNAP=frame[,frame...]:prefix` (`host/memsnap.c`) and `tools/memsnap.py` summary / diff / refs
- [x] batch 4 · 35 GitHub Actions (`.github/workflows/ci.yml`, macos-26): the hosts of every game (Metal 4 and Metal 3), the host tests and the tool
  tests
- Found while checking batch 4: `gfx/tests/t_ussort` failed about one run in ten (a block whose four probes find no place is collected again and
  the merge drops the repeats); the test now allows that. The host build has 34 compiler warnings, the same before batch 1

Tier 1, nxnative after 2b31dbd (reviewed through 19d7a70 on 2026-10-19 afternoon; MIT; batch 5, before Tier 2):
- [x] batch 5 · 73 the host races of nxnative 8530042: QueryMemory under the registry lock; a SystemEvent's binding ends with its write handle,
  the signal under one hold of G; audio output backends change under one lock, a second consumer reported red; CloseAudioRenderer joins the
  render thread (`dm_pcmout_close` frees the AudioOut slot)
- [x] batch 5 · 74 the yield back-off start, `DM_YIELD_BACKOFF=<cap µs>[:<start µs>]`, default 2 ms. Still off by default: no energy or frame
  difference in Tears of the Kingdom's prologue; manosaba's guest threads never yield
- [x] batch 5 · 75 the c1 guard by interval arithmetic over the whole index, every stage (`gfx/tools/c1_guard.py`); over chain b-1e41705de87b it
  lists the same 49 shaders as the old one, so nothing changes at run time
- [x] batch 5 · 76 two more power lines: render-target traffic a frame and guest CPU by Switch priority (`t_threadcpu`). manosaba's 25,000
  wakeups a second and full performance-core share come from host threads
- [x] batch 5 · 77 checked: the opening cave's stone blocks and steps beside the torch are lit warm here; not this port's bug

Tier 2, small code, one shader chain rebuild for all four:
- [x] Tier 2 · 36 the BAHS shaders: `engines/bnsh/shader_extract.py` reads every binary entry of a `.sharcb` (after nxnative's `bahs_shaders`):
  1,551 shaders new to the Tears of the Kingdom capture, 178 still capture-only. Not read yet: `Lib/NintendoSDK/TextureShader.bnsh`'s two 0x7d1-byte
  control sections and the Title layout's `bgsh`; compute code areas with the 0x12345679 header are translated from +0 (to skip to +0x100 when a
  game's header carries data)
- [x] Tier 2 · 37 FFMA / FMUL `.FMZ` (after tomodachi-native a0aa6c2). Neither game uses it
- [x] Tier 2 · 38 SHF wraps or clamps to the width, a shift of 0 no longer ORs both words; I2I `.SAT` in the source's signedness
  (`gfx/tests/t_xlate_ops.py`, LASRecomp d4c2ff6's vectors)
- [x] Tier 2 · 39 the alpha test, as a game profile option (`alpha_test` in game.json; after lordran-native a63dd76): fragment shaders end with
  the comparison under function constants (`gfx/tests/t_alpha.m`). Off for both games, which never call ColorStateSetAlphaTest
- Chain 1e41705de87b → bab8d4746968 for 36-39 (lock: the patched translator). Both games build every shader; the c1 list is the same 49

Tier 3, medium:
- [x] batch 6 · 40 host calls and svcs on a per-thread host stack (`host/hstack.S`, `hstack.c`; after MacNativeGames' hle_stack, MIT): imports,
  takeovers, NVN functions and svc gates run on the guest thread's own 1 MiB pthread stack; a frame record links back to the guest caller and gives
  GetThreadContext3 the guest's registers. Thread dumps show each thread's least free guest stack (a `[stack]` line below 1 KiB). `DM_HSTACK=0` off
- [x] batch 7 · 41 occlusion counts across submissions (after lordran-native 3ef6915, MIT): the open count's slots folded into a CPU subtotal before
  a visibility ring buffer is reused (`gfx/tests/t_visibility.m`); the overflow buffer of more than 8,192 counting passes is retired now
- [x] batch 8 · 42 draws that sample what their own pass draws (`DM_NVN_COUNTS=1` counts them): the replay copies only where NVN defines the read
  (a draw after a texture barrier that follows draws of its pass: `fbx_prepare` / `fbx_sub`); copying for every such draw put prologue shots outside
  the console footage's brightness ranges. `DM_NVN_FBX=all` copies for every such draw, `=0` never (`t_fbx`)
- [-] batch 8 · 43 checked: no two timestamp reports of one submission are subtracted (the game reads only the type 0 report, via
  `nvnDeviceGetTimestampInNanoseconds`, main+0x76920; a load watchpoint, `DM_NVN_COUNTS_WATCH=<n>` `DM_WATCHPOINT_LOAD=1`, saw no other read),
  so the command buffer is not split at reports
- [-] batch 8 · 44 counted: no ClearTexture in either game, 4 render passes a frame end without a draw in the prologue: keeping clears would save at
  most those
- [-] batch 8 · 45 counted: about 31,000-34,000 system counter reads a second in Tears of the Kingdom, about 1.5 ms of CPU a second in all: not
  worth an inline read
- [-] batch 9 · 46 counted: no pass of either game draws only a top-left part (also with timestamps made late, `DM_NVN_TS_LATE_US=<µs>`, new), so the
  render area stays the attachments' size (`gfx/tests/t_extent.m`)
- [x] batch 10 · 47 a live command channel, `DM_CMD=<file>` (`host/cmdchan.c`, after MacNativeGames' `TOTK_INPUT_FILE`): `key` / `down` / `up`,
  `wait`, `sleep`, `shot`, `passdump`, `threads`, `memsnap`, `hlecount`, `quit`; a line runs once its newline is written (`t_cmdchan`)
- [x] batch 11 · 48 sead heaps and list watch (`host/sead.c`, after MacNativeGames' TOTK_CRASH_HEAPFIND / HEAPWALK / LISTCHECK):
  `DM_CRASH_HEAP=<reg>,...`, the channel's `heap <addr>` and `heaps`, `DM_LIST_WATCH=[heap+]<addr>,...`; a use list counts as broken only in a heap class
  that has whole ones
- [x] batch 12 · 49 per-pass GPU time, `DM_NVN_PASSGPU=<tsv>` (after upstream 44a6770, MIT): Metal 4 counter heaps around each encoder and dispatch,
  Metal 3 stage-boundary samples, a ring of 8 invalidated before reuse; every 300 frames the five costliest kinds of pass (`gfx/tests/t_passgpu.m`)
- [x] batch 13 · 50 soak and fixed-scene measurement, `tools/soak.py` (after MacNativeGames' `mac_v143_soak.py` / `mac_v143_smoke.py`): `run` (memory
  slope after the warm-up, frame and GPU time early against late, late red lines) and `window` (N runs to a frame, `--clean`). Run-to-run GPU noise
  in the prologue is about 3 %
- [x] batch 13 · 51 lifecycle and swipe-away tests (upstream `ios/tools/lifecycle.sh`, `logred.sh`): `DM_TEST_BG=<s>,<len>` on the Mac too,
  `tools/lifecycle_check.py`, `ios/run.sh lifecycle`; the watchdog skips while the clock is frozen for the background; `host/tests/t_fs_kill.c`
  (saves killed at random stay whole). The home button and the app switcher need a phone (item 70)
- [x] batch 14 · 52 scene render size on 1.4.3, `DM_RENDER_RES=<w>x<h>` with the rules product (after MacNativeGames PR #19, MIT): four words at
  0x585e48-0x585e60 read the size from host page +16 (x17, not the PR's x3); `games/totk/frame_step.c` writes the game's own parameter there unless
  `DM_RENDER_RES` is set. `games/totk/check_render_size.py`; product60 rebuilt (`product60.prev` kept)
- [x] batch 15 · 53 frame pacing between 30 and 60 fps (after MacNativeGames' TOTK_UNCAPPED, MIT): a game whose step follows the measured frame time
  (`dm_game_realtime_step`) is paced by our clock at up to 120 fps (`gfx/nvn/nvn_pace.h` `nvn_uncapped_hz`; `DM_UNCAPPED=0` off,
  `DM_UNCAPPED_HZ=30..120` or `display`). `DM_RENDER_RES` above 1920x1080 is refused (crash at main+0x1c2f20; see 95). The 30 fps constants need
  play: 71c; a window run is part of 71b
- [x] batch 16 · 54 user mod folders, `DM_MODS=<dir>` (`host/mods.c`, after SkyRecomp's `runtime/mods.c`, MIT): each entry's RomFS is a layer in
  front of the game's (`<entry>/romfs`, `contents/<program id>/romfs`, `atmosphere/contents/<program id>/romfs`, loose files), in name order, `.` or
  `-` off; on iOS `Documents/mods`
- [-] batch 17 · 55 measured: the AudioUnit open takes 22-26 ms in the games here (tomodachi-native d7f52e1 moved it after a 40 s stall), so it stays
  where it is; the time and thread are logged (red above 1 s) for a device run (70). `DM_AUDIO_MUTE=1` feeds the sound card silence
- [-] batch 18 · 56 measured (justr4mzi's mush8 fork aafef22, ideas only): cold uploaded textures (`DM_NVN_COUNTS=1` census) are about 3 % of Tears of
  the Kingdom's Metal memory: not now; with the game on a device (62) the memory-low lines say whether to
- [x] batch 19 · 57 code patches chosen at launch, one product for 30 and 60 fps (after tomodachi-native 0428c80, MIT): rules.py `OPTIONS`, listed in
  `<module>.options.tsv`, applied by the Mac loader to the `DM_OPTIONS` names or the product's `options.default` (`host/options.c`, `dm_option_on`).
  Tears of the Kingdom: `fps60`, `realtime`, `scene`; `DM_OPTIONS=none` runs the game's code as shipped. The default 30 fps product
  (`work/build/totk/product`) is left as it is. iOS needs the changed pages embedded (tomodachi-native afd88e9), item 62
- [x] batch 20 · 58 on-screen controls with a drag-to-look camera (BotWRecomp `runtime/src/host/touch.cpp` at 2b84b82, MIT): `host/touchpad.c` for
  game.json `touch` "pad" (`DM_TOUCH=pad` / `screen`), iOS `DmPadView`, the eye's choice in `dm.touch_hidden`; `DM_TEST_TOUCHPAD=check:<s>` runs
  BotWRecomp's checks (17 of 17). The feel needs Tears of the Kingdom on iOS (62), or manosaba with `DM_TOUCH=pad` on a device (70)
- [-] 59 not done: iOS 17 is not supported (the user's decision, 2026-10-20); the apps stay at iOS 26 (Metal 4) and iOS 18 (Metal 3)
- [-] batch 21 · 60 measured: no game calls an indirect draw or dispatch yet (396 logs); implemented when a game calls them (item 64). `t_nvn` 7b
  holds them all to the stub

Tier 3b, from the wider review of 2026-10-20 (before Tier 4). Correctness, small:
- [x] batch 22 · 78 the pipeline warm-up from the player's own list without a seed list (`gfx/nvn/nvn_warm_list.h`); rows of an earlier
  translation skipped and counted. No title list: the warm-up is done long before the first frame that needs it
- [x] batch 22 · 79 the prediction table (after MacNativeGames PR #29, MIT): an untaken guess freed two submissions after its kick, each scan capped
  (8,192 commands, 32 keys, 2 ms), guesses only from loaded programs (`prog_peek`); a `pipeline prediction` power line. The cold case is a device's
- [x] batch 22 · 80 the present's command buffer committed with the residency set committed (MacNativeGames PR #30, open; `present_now`,
  `t_present_res`)
- [x] batch 22 · 81 a sync's fence stored before its flag, release / acquire (`nvn_obj.h` `sync_fence_store` / `sync_fence_load`; GirlNet f2faa5f)
- [x] batch 23 · 82 NVN texture layout per the SDK (`nvn_tex.c`, after nxnative PR #11, digitalhivehub bacf2cb, MIT; census `DM_NVN_TEXCENSUS=1`):
  cube array depth counts faces, 0x40 alone linear, MINIMAL_LAYOUT, the packaged block depth (`pkg_layout`); `t_tex_auto` with PR #11's vectors and
  totk-mobile's sizes (Apache-2.0, numbers only)
- [x] batch 23 · 83 the format's own swizzle in the texture views (`tex_by_id`; totk-mobile d3dd76c, Apache-2.0, reimplemented); red in
  `SetRenderTargets` if an R / B-swapped format is ever a render target (`t_fmt_swz`)
- [x] batch 23 · 84 socket errno from nn::socket's own values (the SDK's Errno: ENetDown 100, EAfNoSupport 97; `t_net`)
- [x] batch 23 · 85 sticks inside the circle of radius AnalogStickMax for every source (`hle_hid.c` `stick_circle`)
- [x] batch 23 · 86 the Mac app's first iCloud listing driven by a local run loop on the main thread (`cloud.m` `listing_query`, upstream 1ceaa6b, MIT)
- [x] batch 23 · 87 quit waits while the game is inside its exit-request section (`hle_oe.c` Enter / Leave, `hle_lifecycle.c` `quit_wait`;
  GirlNet 291a9cc)
- [-] batch 23 · 88 measured: SleepThread under 20 µs is rare (677 a minute in Tears of the Kingdom, none in manosaba). manosaba sleeps 20 µs-1 ms
  about 24,000 times a second (98 / later)
- [-] batch 23 · 89 measured: neither game reads the six-axis state (Tears of the Kingdom asks for its handles, never the state); a resting state
  belongs with the gyro (67)
- [x] batch 23 · 90 red lines: a second texture or sampler pool, packaged layout byte 1 not 0, a dual-source blend factor, an unimplemented import
  (`UNIMPL`, with its caller: `DM_UNIMPL_GUEST_RA`) and an unknown svc. None fires in either game
Tier 3b, Tears of the Kingdom:
- [x] batch 24 · 91 shrines at 60 fps: nxnative PR #11's five words (digitalhivehub 97172c1 / a6b7191, MIT) joined `fps60` (now 12 words): a shrine's
  forced fixed interval not taken, the lowering re-armed from 1 (`check_frame_rate.py`; `product60.prev91` kept). Not run in a shrine here: no save
  inside one
- [x] batch 24 · 92 the BNSH files inside the game's modules (`shader_extract.py --modules <product>`; MacNativeGames 2f9e707, MIT): main has 40
  shaders, capture-only shaders 178 → 138
- [-] batch 24 · 93 measured: the half-step selector (sys+0x1da bit 0) stayed 0 through 191 interval changes and the cliff save, so the step is never
  halved (T5eng's note does not hold here)
- [x] batch 24 · 94 GPU-rendered textures in CPU-cached pools counted (`DM_NVN_TEXCENSUS`); the experiment `DM_NVN_WB_IDLE=1` (after totk-mobile's
  write-back, Apache-2.0, reimplemented) writes back a texture idle for 30 submissions and hands it back to the CPU when the CPU writes it. It also
  makes Link close to the camera solid (by default a translucent ghost); which is the console's picture is open: off by default, 94a
- [ ] 94a the write-back's other half per region: what the RG8 256x256 texture the CPU rewrites every few submissions is (zeros each time; its
  hand-back changes Link's near-camera fade), console footage of Link close to the camera and of the Great Sky Island grass, then
  `DM_NVN_WB_IDLE` on by default or a narrower rule (hand back only what was written back, or only whole-texture copies)
- [x] batch 24 · 95 `DM_RENDER_RES` above the output size: `games/totk/heapdef.py <RomFS> <overlay> <w>x<h>` writes a HeapDef overlay with the
  graphics heaps grown by the scene's pixels (MacNativeGames' make_output_res_overlay.py, MIT; 1920x1080 matches totk-mobile's numbers) and a note of
  the size, up to which `frame_step.c` takes `DM_RENDER_RES` (3840x2160 at most)
- [x] batch 24 · 96 anisotropic filtering as a switch, off by default (`DM_NVN_ANISO=<2..16>`, `nvn_aniso.h`; tvc97's TOTK_ANISO): raised only on
  mipmapped samplers that minify linearly, without a depth compare or a reduction

Tier 3b, performance and power (measure first):
- [ ] 97 a present with a deadline against the GPU clock drop (`powermetrics`, the 30 fps product). Needs the user: the presents only happen with a
  window (headless runs have no drawable) and `powermetrics` asks for the password; mode 0 (`DM_PRESENT_LINK=0`, the rescue path) is the one
  without a deadline, the display link's default presents with one
- [x] batch 25 · 98 the audio renderer paced by its own clock (`host/audren_pace.h`, after SkyRecomp 0620de9, MIT): three silent frames queued
  first, a stall restarting the deadline; catch-up frames 3,731 → 10 in 75 s; a gap histogram every 2,000 frames
- [-] batch 25 · 99 measured: depth textures with or without lossless compression look the same (`DM_NVN_DEPTH_PLAIN=1`, LASRecomp PR #204, an A/B
  switch, off); the default stays compressed

Tier 3b, one shader chain rebuild for all three:
- [x] batch 26 · 100 the Ultrahand mask shader's integer output: `games/<id>/fout.tsv` read by `dmsh_build.py` (nxnative PR #11's shader_fout.tsv,
  MIT); its MSL declares `uint4` at colour 0
- [x] batch 26 · 101 the NoContraction helpers inlined (`gfx/tools/msl_helpers.py`): `spvFAdd` always-inline with FP contraction off in its body
  (totk-mobile's idea only), bit for bit the same (`t_fpcontract`); no measurable GPU gain on the M5. Chain bab8d4746968 → 4236b355e989 (with 100)
- [-] batch 26 · 102 not needed: 90's red line for dual-source factors fired in neither game

Tier 3b, iOS and tooling:
- [x] batch 26 · 103 metallibs copied into memory on iOS (`lib_from_file`; `DM_NVN_LIB_COPY=1` elsewhere; MacNativeGames e099353, MIT): no vnode
  held per library
- [x] batch 26 · 104 a pipeline retried after a compiler-service interruption (`nvn_retry.h`, after MacNativeGames 39048ca, MIT: up to 4 attempts
  for "XPC_ERROR_CONNECTION_INTERRUPTED"); not recorded as failed
- [x] batch 26 · 105 the command channel follows a replaced or truncated file (`cmdchan.c` `cmd_reopen`; T5eng's TOTK_INPUT_REOPEN)
- [x] batch 27 · 106 run diagnostics (lordran-native f5834c5 / 9567c94, MIT; the code is ours): UTC millisecond log stamps (`host/logstamp.h`,
  `DM_LOG_TIME=1` on the Mac), `Documents/boot.running` → red line and `boot.interrupted.txt` (`host/rundiag.h`), Export Diagnostics
  (`ios/Sources/diag_export.m`), the fps / GPU line (`gfx/nvn/nvn_perfline.h`; Settings: Show frame rate, `DM_PERF_LINE=1`).
  Checked on the iPhone 17 Pro (2026-10-21): a foreground kill's red line and `boot.interrupted.txt`, Export Diagnostics, Show frame rate
- [x] batch 27 · 107 the frames-stopped dump (justr4mzi's mush8 fork 979317e, GPL: the idea only): `DM_FREEZE_DUMP` seconds (6) without a present give
  one thread dump per freeze; thread dumps print after resuming (`hle_thread.c` `thr_bt`)
- [x] batch 27 · 108 the Metal 4 iOS build compiles for `apple-a14` (was a18; lordran 78dd33d): one IPA for every Metal 4 device (`t_ios_cpu.py`)
- [x] batch 27 · 109 touch camera sensitivity (0.25-3×, before the turn speed saturates) and X / Y inversion (lordran 42ad4cf, MIT: the idea;
  `touchpad.c` `tp_look_tuning`): Settings → Touch Camera, or `DM_TOUCH_LOOK=<s>[,x][,y]`
- [-] batch 27 · 110 not needed: `devicectl device copy to` already skips unchanged files and resumes (devicectl 642.16); `ios/run.sh push-data`
  says so when a copy fails
- [x] batch 27 · 111 `make -C host/tests run-ubsan` (the parsers of outside input, UBSan fatal; `t_ubsan_on`) and `tools/audit_public.py`
  (bayonetta3-mobile's idea, MIT: private paths, game data, keys and tokens, binary content; `--history`), both in CI; LDC.64 with the address
  register among its destinations (LASRecomp a179452's cases, MIT) in `t_xlate_ops.py`

Tier 3c, from the deep review of 2026-10-20 (after 3b, before Tier 4). Correctness, small:
- [x] batch 28 · 115 pipeline keys carry zeros for the blend state of targets whose blending is off (`nvn_pso_skey.h` `pso_key_blend_canon`, in
  `pso_key_fill` and when the warm-up list is read, folding rows that become one pipeline: Tears of the Kingdom's 729 keys → 397)
- [x] batch 28 · 116 SyncWait on a sync never fenced returns FAILED, as the SDK's NVNsyncWaitResult says (was TIMEOUT_EXPIRED), counted and logged;
  QueueWaitSync left as it is (the SDK gives it no failure)
- [x] batch 28 · 117 the Metal 3 buffer table grows and is indexed (`nvn_m3buf.h`, on `nvn_pool_find.h`; acnh-mobile c2af194's case, MIT): Tears of
  the Kingdom passes 513 buffers in the prologue, and the old 512 cap left s1800 black on `dmhost_m3` (the iOS 18 app)
- [x] batch 28 · 118 a write released through one range stamps every range on that host page (`nvn_dirty.c` `stamp_shared`, also in the fault handler:
  atomics only): two pools on one page each missed the other's writes; acnh-mobile's `t_dirty_shared.c` (MIT) as is
- [-] batch 28 · 119 not needed: with `DM_NVN_COUNTS=1` no draw sampled a texture that shares an attachment's storage under another serial (0 in the
  prologue, the cliff save and manosaba); the count stays in the counts mode (`nvn_obj.h` `tex_same_storage`)
- [x] batch 28 · 120 cull FRONT_AND_BACK drops polygons only (`nvn_obj.h` `nvn_prim_has_face`; points and lines were dropped too); neither game draws
  a point or a line under it (logged once if one does)
- [x] batch 28 · 121 a pass without attachments takes the viewport's extent when no scissor is set (`nvn_attless_size`; it was 1x1,
  bayonetta3-mobile's case), with a scissor its extent as before; samples stay 1 (none above 1 in either game, logged if one comes)
- [x] batch 28 · 122 the silent audio sink starts again from now when more than 100 ms behind (`audren_pace.h` `sink_step`; acnh-mobile, nxnative
  3bd1e9e): after a 2 s freeze 9 pulls, not 409
- [x] batch 28 · 123 the display-link step from the rate the link runs at (`nvn_pace.h` `nvn_link_step`): on the power-saving 30 Hz link interval 2
  takes one tick (30 fps; was 15). The window run on the device or the Mac with the 30 fps setting is for the user
- [x] batch 28 · 124 the sticks set NpadButton StickL / StickR Left, Up, Right, Down (bits 16-23, the SDK's names; `dm_input.h` `dm_stick_dirs`) at
  half the axis range or more (blade2-mobile 34755e9, MIT); whether the console also sets one on a diagonal outside the dead zone circle with both
  axes under half is not documented and not checked on a console
- [x] batch 29 · 125 crash and diagnostics fixes: the backtrace reads each frame with vm_read_overwrite (`crash.c` `dm_backtrace`; totk-mobile does
  so); the svc gate hands `svc_dispatch` the svc's address (`hle.c` `gate_dispatch`: red SVC lines and Break said "?"), Break names the module and the
  caller apart; each `RED_ONCE` tag one message (`tools/tests/t_red_once.py`); the shader capture writes `.data` then `.ctl`, each through a rename
  (`hle_nvn.c` `shdump_to`)
- [-] batch 29 · 126 not needed, and wrong for Tears of the Kingdom: it keeps its own fields right after a view (7250b79: a 12-byte `view_t` overwrote
  them), so its view is not Bayonetta 3's 16 bytes; every view both games hand over was made by our SetDefaults, none sets a depth / stencil mode
- [x] batch 29 · 127 the Mac window: a press on the title bar goes to AppKit with its drag (moving the window by its title bar did nothing), the black
  bars stay no touch (`host/mouse_route.h`); App Nap held off while a window runs (`beginActivityWithOptions`; blade2-mobile). The window checks are
  for the user
- [x] batch 30 · 128 the deep review's tests (their vectors as data, the code ours): `t_writetexels` (totk-mobile's regions), `t_vfetch` (dr3-mobile's
  UNORM8 and NaN payloads; `vfmt_for_float_input`), the svc vectors in `t_guest_mem` section 11 and `t_coremask` (with 143; the core table stays
  Atmosphère's where totk-mobile expects otherwise), `t_time` (acnh-mobile's zones), `t_relr` (144), the pool lookup (`nvn_pool_find.h`: a range
  wrapping past the end of the address space now in no pool), a NaN alpha reference in `t_alpha` (bayonetta3-mobile's)
- [x] batch 31 · 129 rewriter: a jump table whose add has the loaded entry first (`ldrsw x18, [xB, xI, lsl #2]; add x18, x18, xB; br x18`) found with
  the operands swapped; decoding goes past a word Capstone cannot decode (`decode_words`); equiv takes an exclusive store's success path and leaves
  out only 8-byte writes to x18's and the thread pointer's slot; `tools/tests/t_rewriter_synth.py` runs the rewriter, structck and equiv on a
  synthetic module in CI (smo's selftest words, bayonetta3-mobile's sequences) and on damaged products; CI installs lz4 and unicorn

Tier 3c, Tears of the Kingdom:
- [x] batch 32 · 130 pre-rendered movies held at 30 fps: the game extends the user inactivity time (`nn::oe::SetUserInactivityDetectionTimeExtended`)
  for every cutscene, so the oe HLE hands the call and the guest's return address (`DM_GUEST_RA`) to a game hook `dm_game_inactivity_fps`, and
  `games/totk/movie_fps.c` holds only the call returning to main+0x2b36104 (a pre-rendered movie, FPSLocker's flag) through `nvn_hold_interval`.
  `DM_INACTIVITY_FPS` replaces the held rate (0 = no hold, for 72); `DM_TEST_FPS_HOLD=<from>:<to>:<fps>` holds without a movie
- [x] batch 33 · 131 the game's GPU time is measured again: Tears of the Kingdom times its frame as the difference of two TIMESTAMP reports of one
  submission (main 0xa1a50), and every report took its completion time (it read 0-0.06 ms); now each report takes its place inside the submission by
  the share of the work before it (`gfx/nvn/nvn_ts.h`): the game reads p50 6.33 ms, the controller's view unchanged. `DM_NVN_GPU_TS=completion`
  restores the old timing, `DM_NVN_TS_SCALE=<x>` spreads the reports; `DM_PROBE_FP=1` adds s0..s7 to probes. The dynamic resolution itself never runs
  here (131a)
- [-] batch 61 · 131a what sets the dynamic resolution's enable bit (+0x1c bit 0 of one object per game, set up at 0x8c4eb0 as the constant 0x804;
  T5eng saw TotK's scene size change on an iPhone Air): nothing does; with the GPU reported 30 ms late the view size routine 0xd5c24 ran 789 times in
  60 s without the object, its paths never reached in 190 s, and no store in main sets the bit. T5eng's line is an inference. To look at again if a
  device log shows the scene's render size changing
- [ ] 132 the wave program's wide lines (2026-10-20: not reached: no `SetLineWidth` other than 1 in the prologue, on the cliff save or in the five
  other saves of `save_play` in 75 s each, all early game; the wave program's shaders 0cd9edc0a623c237 / e04d541253557805 are not in our capture;
  totk-mobile expands them by editing those shaders' source, so it needs both: a save where the game draws them, from the user)
- [x] batch 35 · 133 a frame step in whole vsyncs decided by the host, as a switch (`DM_FRAME_STEP=vsync`; `gfx/nvn/nvn_fstep.h`, nxnative's design
  and numbers written anew, MIT): each frame stays k vsyncs and the game steps k (paying back lost time, at most 15); 130's hold is its kmin. The 60
  fps product's prologue: frame p50 16.7 ms, 35 % less CPU than the mean step with uncapped pacing (the default, unchanged). Not needed: FPSLocker's
  three swapchain textures. Not measured: the day clock (main 0x68c15c), which needs a later save (71c)

Tier 3c, iOS:
- [x] batch 36 · 134 iOS audio: the session asks for 48 kHz and a 5 ms I/O buffer and logs what it got (nxnative 18c6143); underruns and the zeros
  filled counted, logged by a watchdog (after nxnative b301431, MIT) that moves a unit silent for 200 ms, or one whose Start the device refuses, to
  the silent sink and retries it (1, 2, 4, then every 8 s); the media services reset makes every unit again (`dm_audio_reset`). On the iPhone 17
  Pro (2026-10-21): 48000 Hz and 5.00 ms as asked, output latency 12.4 ms
- [x] batch 37 · 135 iOS: the frame rate setting held at 30 in Low Power Mode or from thermal state Serious up (`nvn_fps_policy` in
  `gfx/nvn/nvn_pace.h`, after super-mario-odyssey-ios's frame pacer, MIT), the reason logged; `ENABLE_DEBUG_DYLIB: NO` in `ios/project.yml`. On the
  iPhone 17 Pro (2026-10-21): Low Power Mode on and off logged "frame rate 30 fps (Low Power Mode)", then "frame rate 60 fps (the setting)"
  - [x] batch 67 · 135a the thermal hold a switch, the player's choice (the user, 2026-10-22): "Hold 30 fps when hot" (`dm.hot_hold`, unset reads
    as on, the behaviour before) in the Settings page and the in-game panel; off, the setting stays at any thermal state, Low Power Mode still
    holds (`nvn_fps_policy(…, hold_hot)`; `t_pace`, -DDM_TEST_POLICY_HOT_ALWAYS fails 2); the boot line names it
  - [x] batch 67 · 135b the target paces the game too: the thermal hold (or DM_FPS=30) only set the display link's rate, so Tears of the Kingdom's
    60 fps build in its map screen kept presenting 60 frames a second at a 30 Hz link, half drawn and dropped (the overlay: "60 fps, on-screen
    30"; 149-151 of every 300 presents discarded). `nvn_pace_hold` takes the lower of the game's hold and a target below 60 into
    `nvn_hold_interval` (`win_interval`, the vsync step); `t_pace`, -DDM_TEST_TARGET_NOT_HELD fails 2. On the Mac product60 with DM_FPS=30: frame
    time p50 33.34 ms. manosaba presents FIFO, which the 30 Hz link already held to 30
- [x] batch 39 · 136 no option pages on iOS (iOS maps the signed executable's code): `ios/tools/gen_embed.py` builds the code options (DM_OPTIONS at
  build time, else the product's options.default) into the embedded image, each checked whole, and `options.c` `dm_options_embedded` reads them back,
  so `dm_option_on` answers as on the Mac; run-time choices stay data (the frame rate by the host's pacing, realtime and scene through the host page).
  manosaba has no options

Tier 3c, larger:
- [x] batch 40 · 137 pairs noted at bind time: `pso_get` writes each vertex / pixel pair to `<DM_SHDUMP>/pairs.tsv` (`nvn_mtl.m` `pair_note`), so
  BindProgram's pairs reach the next shader build's padding. No "not written by vertex shader" failure in 208 run logs since chain bab8d4746968, so
  the union-interface library moved to 62g
- [x] batch 41 · 138 render targets of virtual pools in a heap, a switch (`DM_NVN_RT_HEAP=1`; the placement rule is totk-mobile's
  `nvn_texture_alias.inc`, Apache-2.0, written anew in `gfx/nvn/nvn_rtheap.h`): a virtual, CPU-inaccessible pool gets a placement heap of 1.25 × its
  size, a target goes at its guest offset × 1.25 when it fits; Metal 4 only, with the TBDR rewrites off. The cliff save: GPU allocated about 2,120 →
  1,960 MB, timing unchanged. Off at first (Metal's layout is not Maxwell's); 166 made it the default on the Mac, 62h is iOS
- [x] batch 38 · 139 code caves in the rewriter (`CAVES` in a game's rules.py; totk-mobile's routines appended to the patch area, written anew): one
  instruction becomes `b <routine>`, the routine is emitted into the patch area from words and forms (`('orig',)`, `('adrp_host', d)`, `('b' | 'bl',
  target)`) and branches back; words that cannot run elsewhere (PC-relative, x18, svc, system registers, register branches) are refused;
  `<module>.caves.tsv` lists them, structck ⑬ checks them, fnfuzz leaves cave functions out
- [x] batch 42 · 140 FIFO frame consumption for a game that steps once per frame (game.json `present_consume` `"fifo"`: manosaba;
  `DM_PRESENT_CONSUME=newest|fifo` overrides; after acnh-mobile c2af194, MIT, written anew): the display link shows the oldest queued frame
  (`nvn_mtl.m` `link_take`), each at least its present period from the tick that showed it (`nvn_pace.h` `nvn_fifo_due`); uncapped pacing keeps the
  newest. On the iPhone 17 Pro (2026-10-21): the line is logged and steady play drops 0 stale frames; each return from the background drops 1-3
  (5 in 4 returns)
- [x] batch 43 · 141 IL2CPP class names in crash reports and memory snapshots (`host/il2cpp_obj.h`; armyja's nxnative-aa-port reads the same fields,
  MIT, written anew): a Unity game's crash report (`game.c` `dm_engine_il2cpp`) names the object each of x0-x28 points to; `tools/memsnap.py` `class`,
  `refs` and `diff` (manosaba from frame 600 to 2400 grew 29,776 System.String, 12,625 Naninovel.PlaybackSpot, 8,930 Naninovel.RawValuePart, 7,924
  Naninovel.UniTaskCompletionSource)

Tier 3c, smaller items listed at the user's request (2026-10-20: every finding of the three reviews as a to-do):
- [x] batch 44 · 142 the swapchain acquire against the GPU work of the frame last drawn into its image (after blade2-mobile 43e760e, MIT): the present
  records the newest submission's fence (`win_t` `rfence`) and the timing summary counts acquires before that work finished; `DM_NVN_ACQ_GPU=wait`
  waits. 0 on the Mac in both games, so the wait stays off. On the iPhone 17 Pro (2026-10-21): 0 in manosaba and in Tears of the Kingdom
- [x] batch 30 · 143 svc details as Horizon (switchbrew's results; totk-mobile's vectors): SetHeapSize (2 MiB multiples up to 4 GiB, OutOfMemory
  0xD001, no shrink over a MapMemory source, fresh pages), MapPhysicalMemory's InvalidAddress / InvalidSize / InvalidMemoryRegion, GetInfo
  PriorityMask, RandomEntropy, SystemResourceSize (`game.json` "npdm") and Total / Used; `tools/new_game.py` reads the NPDM fields
- [x] batch 30 · 144 the DT_RELR table checked whole before any slot is touched (`loader.c` `relr_each`): a bad bitmap, slot or table refuses the
  module with a red line; both games' tables pass unchanged
- [x] batch 45 · 145 small diagnostics (bayonetta3-mobile's copy of blade2-mobile, MIT, written anew): the frame log (`DM_NVN_TSV`) stops at
  `DM_NVN_TSV_MAX_MB` (32 by default); `DM_PRESENT_LUMA=<file>` compares each present's guest image with its drawable, red once under 35 % (needs a
  window; `nvn_mtl.m` `pl_*`); `DM_HID_RECEIPT_LOG` deduplicates each (controller, style) on its own
- [x] batch 46 · 146 NCZ input in the game extraction (`tools/game_extract/Ncz.cs`; the layout as totk-mobile's `extract_romfs.py` reads it,
  Apache-2.0, written anew): an .nsz's NCZ is restored to its NCA and must hash to its file name; NCZBLOCK refused. ZstdSharp.Port 0.8.8 (MIT), pinned
  by content hash. Not run on a real .nsz here
- [x] batch 47 · 147 tests that run the game's own SDK code against the host (`host/sdktest.c`, `DM_SDK_TEST=time,font` with any run; after
  totk-mobile's `tests/test_time_sdk.c` and `test_font_sdk.c`, Apache-2.0, written anew; they need the game, not CI): at the 60th present a thread
  calls the SDK by name (`loader.c` `dm_sym_bound`), logs "[sdk-test]" lines and ends the run (exit 0 / 1); 8 of 8 in both games. Vibration waits for
  67
- [x] batch 48 · 148 an in-game settings panel and a layout editor for the on-screen controls (BotWRecomp's settings button and lordran-native's
  control editor, MIT, the ideas): `touchpad.c` moves and sizes controls (`dm.touch_layout`), opacity (`dm.touch_opacity`), the editor `tp_edit`; on
  iOS (`front_ios.m`) the panel opens from a menu button or a three-finger tap. On a device (the user): the panel from the button and from three
  fingers, a layout edited, saved and kept across a launch

Tier 3d, from the evening sync of 2026-10-20 (after 3c, before Tier 4). Correctness, small:
- [x] batch 49 · 155 the present viewport rounded before it is centred (`nvn_mtl.m` `present_vp`, now also the Metal 3 path's; Touchable 5cab3ac,
  MIT): it put the picture at -1 on 2868x1320 and 3456x2234 (the Mac's full screen), also 2796x1290 and 2556x1179
- [x] batch 49 · 156 dropped and slow frames judged against each frame's hold (`nvn_pace.h` `nvn_hold_ms` / `nvn_expect_ms` / `nvn_dropped` /
  `nvn_slow`, after nxnative e7a84c7, MIT); the frame interval line counts frames over 1.1 × their hold (it was > 16.7 ms: 299 of 300 at 30 fps);
  `[slowframe]` the same
- [x] batch 49 · 157 the oe notification event set and cleared under the queue lock (`hle_oe.c`, after nxnative ee51071, MIT)
- [x] batch 49 · 158 the user clock context in Horizon's layout (`hle_time.c`, after nxnative a35e964, MIT): the offset in seconds and the steady time
  point it was made at, made again when the clocks part by more than 2 s, once under `DM_FIXED_TIME`
- [x] batch 49 · 159 the Metal 4 feedback queue drains its autorelease pool per work item (`nvn_mtl.m`, lordran-native 6145d1d, MIT)
- [x] batch 49 · 160 GetInfo of an unknown type answers InvalidEnumValue 0xF001 (Atmosphère's GetInfo default; Touchable the same; ours succeeded with
  0), with a red line once per type and its caller

Tier 3d, Tears of the Kingdom:
- [x] batch 50 · 161 sampler border colours Metal lacks (after MacNativeGames b6231e6 / 874a32d, #49, MIT, written anew): the cliff save's grass
  interaction map uses (0.5, 0.5, 0.5, 0.5) and was drawn as transparent black; such a sampler clamps to its edge and a stage sampling through it gets
  a variant (`border_variant`, `dm_border_sample`) compiled from the `.metal` beside the library; an iOS app has none, so there it reads the edge
  colour (a red line once; for 62 those sources could ship). To check in a game (the user): grass that stands up away from Link and can be cut, in a
  grassy area of Tears of the Kingdom
- [x] batch 51 · 162 one pixel shader bound to float and to integer targets: every key in `fout.tsv` keeps its float library and gets an
  integer-output variant `<key>_i_b` (`dmsh_build.py`); `pso_desc` takes it for an integer colour format in a slot the shader writes (`fout_fn`).
  `games/totk/fout.tsv` lists the 23 keys of ours in MacNativeGames' shared list (MIT, keys as data). Chain ca5eb9623812
- [x] batch 52 · 163 hair and cloth at 60 / 120 fps (MacNativeGames PR #79, merged, MIT; its sites, routine words and Unicorn table as data, the code
  ours): the actor animation update builds its cloth and hair clock every frame as {seconds = factor / 30, ratio = factor}; `games/totk/rules.py`
  CAVES at the inlined copies 0x246eac, 0x345904 and the calls 0x24944c, 0x28bc98 to 0x2a0e40 scale it by host page +0 and +8 (`frame_step.c` writes
  +8), scratch x8 and s4. Caves can sit behind an option (`patch_nso.py`: a fifth item; the branch is an options.tsv row; caves.tsv has an option
  column), here realtime. `games/totk/check_cloth_clock.py <product> <main NSO>`. To check in a game (the user): hair and cloth in wind at 60 and 120
  fps against 30
- [x] batch 52 · 164 a mod that carries one of our RomFS overlay files (MacNativeGames #73, open): the mod's copy won without a word; now (`mods.c`
  `dm_mods_pin_overlay`, pinned through `hle_fs.c` `dm_rom_pin`) the SHA-256 of the mod's copy decides: the game's own file → the overlay's;
  `<mods>/-merged/<that SHA-256>/<path>` → that merge; otherwise the mod's copy with a red line naming the merge command (`romfs_overlay.py <RomFS>
  <mods> --merge <the mod's file>`). On iOS the merge is made on a Mac and copied to `Documents/mods/-merged`
- [x] batch 53 · 165 decals that read their own G-buffer pass, a switch (`DM_NVN_FBX=split`; MacNativeGames PR #67, open, MIT, written anew in
  `fbx_prepare`): no copies; the pass is stored and reopened before a draw that samples an attachment holding pixels newer than memory, a run of such
  draws sharing one split (the cliff save: 3 a frame, GPU time unchanged). Default off: the ghosting it fixes shows in motion where there are ground
  decals. To check (the user): a turning camera over paved ground or decals (Hyrule Castle courtyard in the PR) with and without the switch
- [x] batch 54 · 166 a three-hour soak of the 60 fps product before 151 (MacNativeGames #57, #72, #74; upstream #90; `tools/soak.py run --input`,
  `games/totk/soak_walk.cmd`): the cliff save at 120 fps stopped after 47 min (`FATAL GPU allocated 6555 MB, above the budget of 6553 MB`: 3,185
  render targets of virtual pools, each a Metal texture of its own); with DM_NVN_RT_HEAP=1 180.2 min, exit 0, footprint +77 MB/h. So the heap is the
  default where it can be (`rt_heap_decide`: Metal 4 with the TBDR rewrites off, Tears of the Kingdom on the Mac; DM_NVN_RT_HEAP=0 turns it off); iOS
  stays without it (62h). The memory pool table held 3,198 (peak 3,470) of its 4,096 entries
- [x] batch 53 · 167 a teleport command (`games/totk/survey.c`, `DM_SURVEY=1`; MacNativeGames d164868 `world_survey.c`, MIT: its 1.4.3 addresses,
  actor layout and bounds as data, written anew): `survey pos <tag>` / `survey move <tag> X Y Z <frames>` on the command channel (hooks
  `dm_game_command`, `dm_game_frame_sync` at main 0x2db958) through the game's forceSetMatrix (main 0x45ce70). An overworld save is still needed for
  towns (the user)
- [-] batch 53 · 168 measured: no second BC4 encoder in Tears of the Kingdom's runs (census `ttm_note`: 2D against 2D array, handled by `tex_fit`, and
  three pixel shaders reading a 4x4 RGBA8 texture as texture3d, no visible effect); a 2D-to-3D copy waits for a scene that shows it

Tier 3d, iOS, Mac and tooling:
- [x] batch 53 · 169 shader functional keys as a fallback (Touchable's internal/tools/shader_keys.py, MIT, written anew): sha256 of the stage, the
  code's layout and code (`shader_fkey`, kept by program serial in `mtl_prog_fkeys` so prog_t does not grow); `shmap_from_capture.py` writes it as the
  shader map's third column; a key not in the map takes the first with its functional key (`shmap_resolve`)
- [x] batch 53 · 170 the game language from the app's own language setting (Touchable's switch_language / CFBundleLocalizations, MIT, written anew):
  game.json `languages` → `CFBundleLocalizations` (`ios/tools/project_lang.py`); at launch the first preferred language maps through `host/lang.h` to
  DM_LANG when the game has it (Tears of the Kingdom without Portuguese: USpt's code is not in this SDK's list). On the iPhone 17 Pro (2026-10-21) the
  app's language follows the Language row; manosaba keeps the language chosen in the game, so the game's text is to check with Tears of the Kingdom (62)
- [x] batch 53 · 171 the touch controls below the iPadOS window controls and controllers (Touchable, MIT, written anew): `tp_layout` puts the left
  column below the window controls (iOS 26 `UIViewLayoutRegion safeAreaLayoutRegionWithCornerAdaptation:`); a key pressed on a controller hides them
  (`host/gamepad.m` `dm_front_controller_key`), a touch or the last controller gone shows them (`tp_controller`). To check (the user): an iPad window,
  a controller
- [x] batch 53 · 172 ProMotion 120 Hz for a game that steps by real time (Touchable's SS_DISPLAY_HZ=120, MIT): `nvn_promotion_hz` (Tears of the
  Kingdom with realtime: 120 while a 120 Hz screen runs at 120; DM_PROMOTION=0 or DM_PRESENT_HZ leave it); manosaba steps per frame and stays at 60.
  To check (the user): a ProMotion iPhone's frame rate line in Tears of the Kingdom
- [x] batch 53 · 173 install checks (Touchable's launcher install, MIT, written anew; `ios/tools/install_check.py`): `run.sh install` refuses an
  expired signature or a profile without this device, warns within two days of expiry and backs the app's Documents up with a SHA-256 manifest first.
  Checked: installs to the iPhone 17 Pro (2026-10-21) with the expiry warning and the backup
- [x] batch 53 · 174 the iOS shader directory checked complete before packing (acnh-mobile 67afe52 check_invariance, MIT;
  `ios/tools/shader_check.py`): it found Tears of the Kingdom's profile naming manosaba's iOS directory; run.sh stops on it
- [x] batch 53 · 175 the game data verified once, then a marker (super-mario-odyssey-ios ea19678, MIT; `dm_romfs_validate_marked`): the key files
  every launch, the tree walked once while the marker matches; DM_ROMFS_RECHECK=1 walks again. On the iPhone 17 Pro (2026-10-21): 667 files walked in
  49 ms after an install, the marker in 1 ms after
- [x] batch 53 · 176 residency sets only on an Apple6-family GPU (GirlNet d2r-mobile 3e051f1, MIT): on a virtual GPU (GitHub's runners) Metal 3 relies
  on its useResource declarations and Metal 4 stops with a clear line; CI job `gfx-m3` (by hand: Actions → CI → Run workflow) runs the Metal 3 gfx
  tests there. Not run on GitHub yet
- [-] batch 53 · 177 measured first (LASRecomp dc1d28f: wait for the fence before the newest submission): an nvnSyncWait census: Tears of the Kingdom
  0.17-0.20 waits a frame on the newest still running, manosaba 1.00 (1.6-1.8 ms a frame), the pattern LASRecomp fixed. Not changed: whether manosaba
  misses frames on a phone decides it (97 / 142)
- [x] batch 53 · 178 the diagnostics export redacted (lordran 40a9a72's rules, MIT, written anew in `host/rundiag.h`): app container and bundle paths,
  a home folder's user, UUIDs, MAC, IPv4 and e-mail addresses; memory read every 60 frames (lordran 24740d4); the memory line gives the Metal 4
  command allocators
- [x] batch 54 · 179 tests to take: overlapping `UpdateUniformBuffer` writes (Touchable; `t_nvn` 5a2), a 512 re-upload soak (lordran; `t_upsoak`), the
  pass-continuation cases (GirlNet `t_p5r_rt_continue_gpu.m`: ten written anew in `gfx/tests/t_rt_continue.m` with an eleventh)
- [x] batch 34 · 180 screens judged by TypeSafe's Jev model drive the test runs (the d2r-mobile author's method; the key stays outside the repository
  in `~/.config/typesafe/api_key`): `tools/jevdrive.py` drives a running host's command channel (`DM_CMD`, `DM_NVN_SHOT_OUT=960x540`) until a goal's
  done condition holds on two frames in a row; goals in `games/<id>/jev.json` (questions in English). Tears of the Kingdom `load_save` about 25 s
  against about 50 s for the fixed keys; manosaba `first_dialogue`. Without a key it exits 2 and the fixed keys stay the fallback

Tier 4, large:
- [x] batch 55 · 61 NVN translation on a thread of its own (after 5, 47 and 49; `DM_NVN_XLAT`, hle_nvn.c, nvn_mtl.m `mtl_sub_capture`; D2R Mobile
  b94670c and MacNativeGames' TOTK_ASYNC_REPLAY, MIT, written anew): submissions, presents and the frame number go in order to one thread; the
  submit captures what the console reads there (deferred handles, bound uniform bytes, FenceSync records) and returns; a fence the game takes is a
  token (SYNC_TOKEN); the thread is USER_INTERACTIVE with a time constraint of half the present period (DM_NVN_XLAT_RT=0 drops it: 61d).
  `gfx/tests/t_xlat.m`. Tears of the Kingdom's prologue at 30 fps frame p95 46.3 → 33.9 ms, the 60 fps product at 120 Hz 15.6 → 11.0 ms, an hour's
  soak clean; manosaba, which waits for its newest submission every frame (177), worse (p95 17.8 → 20.5 ms; one freeze in 16 runs, inside the
  driver's submitCommandBuffers). So the default for Tears of the Kingdom (games/totk/defaults.c, DM_NVN_XLAT=0 turns it off), off for manosaba
  - [x] batch 54 · 61a LASRecomp's render-queue cases as a test first (`gfx/tests/t_rqueue.c`: nested lists, a fence in a nested list, two waiters,
    the four wait results, a reused ring slot); `nvnCommandBufferFenceSync` is now a record (OP_FENCE_SYNC) whose replay gives the sync its
    submission's fence. Completion may only be held after QueueFinish (the submit waits for one of the four occlusion-count buffers, `g_vis_ring`)
  - [x] batch 54 · 61b deferred bindings frozen at submit (read once a submission, stamped with its generation at `mtl_sub_begin`); binds of more
    than 32 handles split into records of 32, refused only past the replay's tables (textures and samplers 128, images 32).
    `gfx/tests/t_bindfreeze.m`
  - [x] batch 54 · 61c a per-frame critical-path trace (`gfx/nvn/nvn_trace.{h,c}`; totk-mobile's host/timeline.c, Apache-2.0, written anew):
    `DM_NVN_TRACE=<first>+<frames>:<path>` writes a TSV, `tools/trace_report.py` splits each frame into translation / sync waits / acquire / present
    / game with the GPU's busy, latency and starved time. Prologue: at 30 fps 297 of 299 frames wait for the display (61 does not raise 30 fps); the
    60 fps product at 120 Hz 16 % wait for translation (more on an iPhone's CPU: 61d). `gfx/tests/t_trace.c`, `tools/tests/t_trace_report.py`
  - [x] batch 69 · 61d the translation thread's time-constraint policy (re-declared at most every 200 ms) against none, on the iPhone (with 61e's
    DM_NVN_PASSGPU count there: the single-draw passes' cost on a tile GPU). iPhone 17 Pro, 2026-10-22, the shrine save standing 90 s, runs
    alternating, the phone warm: with the policy against `DM_NVN_XLAT_RT=0`, performance cores 66.0 / 65.9 % against 64.1 / 64.5 %, frame time
    p50 19.1 / 19.4 against 19.4 / 22.1 ms, p95 about 33 ms and CPU 1.7-2.0 W either way: no consistent difference beyond the warm-up
    drift (consecutive default runs went 3.09 → 2.61 GHz); kept. DM_NVN_PASSGPU there: 159 passes, 12.4 ms of GPU a frame; the 100
    single-draw render passes 3.6 ms of it (29 %; the M5's prologue: 84, 2.76 ms), mostly 1280x720 passes loading their attachments (a lead
    for 62j and 187)
  - [-] 61e full-screen single-draw passes as compute: not done on the Mac. DM_NVN_PASSGPU (item 49) on the prologue (M5): 84 single-draw passes a
    frame, 2.76 ms together, nearly all small downsample targets; idle gaps between encoders 0.19 ms a frame plus 0.84 ms waiting for the CPU to
    commit (61's matter). Compute versions need the translator to make kernels, to save at most the passes' own cost (under 0.9 ms a frame) on a GPU
    busy 6.7 of 33 ms. On an iPhone's tile GPU a dependent pass costs more (totk-mobile: 38-57 µs on an M4), so the DM_NVN_PASSGPU count joins 61d's
    device run
- [ ] 62 Tears of the Kingdom on iOS: guest memory layout, VP9 on iOS, BC → ASTC, a device run (after 19, 25, 59); the cold-texture census of 56 under its memory limit; code options (57) through embedded changed pages (tomodachi-native afd88e9)
  2026-10-21, the first device run (iPhone 17 Pro, iOS 26.4, free account, the 30 fps product, handheld): into the prologue's cave and play; 30 fps,
  GPU p50 8-20 ms, RSS 2.3 GB with 3.8 GB to the limit, the Mac prologue's three red lines and no other. Left: a movie (62b, 62b2 in batch 66), 62j, 62k (batch 65), the census
  - [x] batch 54 · 62a guest memory reserved at the top of `main()`, before UIKit and Metal (`svc_reserve_early`; `host/guest_as.h`: the lowest free
    gap from 4 GiB, 2 MiB aligned, ending at or below 2^39, from 6 GiB down by 256 MiB to what the game needs; `vm_map` fixed, mmap's choice if that
    fails, the free gaps logged when short). Every platform: a block past 2^39 is moved below it (macOS 27 reserves about 24 GiB in
    0x70_0000_0000..2^39, MacNativeGames). Tears of the Kingdom: `guest_heap_mb` 2176, `ios_pool_add_mb` 0 (the console's pool; manosaba keeps 512).
    `host/tests/t_guest_as.c`. The iPhone 17 Pro gave the whole 6144 MB without extended virtual addressing (2026-10-21)
  - [x] batch 54 · 62b VP9 on iOS with libvpx (GirlNet dropped VideoToolbox's VP9 probe, which faulted at a movie's start):
    `ios/tools/build_libvpx.sh` builds 1.17.0 (SHA-256 pinned, the value totk-mobile pins) static for arm64 iOS 18.0+ into
    `$DM_ROOT/build/ios/libvpx`; `ios/run.sh` links it after `ios/tools/vpx_check.py`, hle_movie.c finds the headers through `DM_VPX_INC`; without
    it a line says movies will not play. `tools/tests/t_vpx_check.py`. A cutscene on the phone is a device check once Tears of the Kingdom runs
    there
  - [x] batch 66 · 62b2 the iOS libvpx built right: the opening cutscene (1080p VP9) ran at 3 fps on the iPhone 17 Pro, frames growing from 49 to
    331 ms with the GPU at 1.3 ms and the audio starving. libvpx's configure fixes the iOS minimum of a static build at 7.0 and adds
    `-miphoneos-version-min=7.0`; against our `-target arm64-apple-ios18.0` its `-Werror` feature checks failed ("overriding
    '-miphoneos-version-min=7.0' option"), so the library had no NEON, no `-O3` (the code is `-O0`'s) and assertions in. `build_libvpx.sh`
    sets the minimum in the unpacked configure.sh to 18.0 and stops unless `HAVE_NEON 1` and `-DNDEBUG -O3` come out; `vpx_check.py` refuses a
    library without `_vpx_convolve8_neon` or referencing `___assert_rtn` (`t_vpx_check.py`, `T_VPX_CHECK_NO_NEON=1` fails 2). The rebuilt one
    (140 NEON functions) plays the cutscene smoothly on the phone (the user, 2026-10-21). Hardware VP9 through VideoToolbox is open to apps from
    iOS 26.2 (`VTRegisterSupplementalVideoDecoderIfAvailable`, SDK header) and not needed for now
  - [x] batch 54 · 62c BC1 → ASTC 4x4 on the GPU on GPUs without BC (MacNativeGames `nvn_bc1astc.c`, `nvn_bctc.metal`): `gfx/tex/nvn_bc1astc.c` and
    the kernel b1a (nvn_mtl.m), the three-colour midpoint and alpha exact; WriteTexels, texture → buffer copies and ReadTexels of BC textures
    without BC refused in red. `DM_NVN_BC1_ASTC=0`: decompressed as before. `gfx/tests/t_bc1astc.m`. Prologue at frame 3840 (DM_TEST_NO_BC=1): 2,314
    MB against 3,545 MB decompressed and 1,984 MB with BC
  - [ ] 62c2 the other BC formats on GPUs without BC: BC2 / BC3 / BC7 → ASTC 4x4 and BC4 / BC5 → EAC R11 / RG11 (MacNativeGames
    `nvn_bctc.metal`: decoded and encoded again, lossy); now decompressed (4x and 2x their size). After a run of Tears of the Kingdom on a GPU
    without BC shows what its memory comes to (62's census under the limit): lossy, and only those devices gain. With BC1 as ASTC the prologue
    still takes 330 MB more than with BC (2,314 against 1,984 MB at frame 3840): what the other formats cost
  - [x] batch 55 · 62c3 BC textures the GPU writes, on GPUs without BC (Apple's Metal Feature Set Tables of 2026-05-21: every iPhone with an A14,
    A15 or A16; `supportsBCTextureCompression` tells): the encoders' block draws go into the auxiliary block texture as with BC, and after the pass
    `blk_decode` decodes them into the texture (bcd; b1a for BC1 kept as ASTC; bcd3 for a 3D depth plane); BC6H / BC7 still left out, red once.
    `gfx/tests/t_blkunp.m`, t_mtlguard +1
  - [x] batch 54 · 62d handheld mode by default on iOS (1280x720): game.json `ios_mode` "handheld" (Tears of the Kingdom; manosaba stays docked);
    `DM_OPMODE` still decides when set. `host/tests/t_oe.c`
  - [x] batch 54 · 62e RomFS as 1 GiB parts with an index; a ready marker instead of the tree walk at every launch (the marker: item 175):
    `tools/romfs_parts.py pack <tree> <out>` writes the parts and `romfs.dmri` (sizes, files, SHA-256), `check` holds parts to it; copied into
    Documents, the next launch (`host/romfs_parts.c`, `romfs_locate`) checks them and unpacks part by part into Application Support/romfs.partial (a
    launch cut short carries on; absolute, "." and ".." paths refused). `host/tests/t_romfs_parts.c`. Tears of the Kingdom's 16
    parts went to the iPhone 17 Pro in 513 s and were unpacked at the first launch in 4.5 minutes (62i)
  - [x] batch 54 · 62f `maxBufferLength` and the recommended working set on the "Metal device" line (mush8 PR #3); `DM_COOL_START=nominal` (or
    `fair`) waits before the game starts until the thermal state is at most that (looking every 5 s, at most 10 minutes; T5eng: the same scene at 60
    fps on a cool phone, 30 on a hot one): `host/coolstart.h`, `host/tests/t_coolstart.c`
  - [ ] 62g one varying interface as a fallback library (bayonetta3: every vertex shader exports the union of the pixel inputs) if a device's log
    shows a "not written by vertex shader" failure (item 137: none on the Mac since chain bab8d4746968; the red line names both keys)
  - [x] batch 76 · 62h render targets of virtual pools in a heap on for iOS (`DM_NVN_RT_HEAP=1`, item 138: 140-190 MB less GPU memory on the cliff save)
    after screenshot comparisons over more scenes; with the TBDR rewrites on it needs them alias-aware (pass merges and content facts by heap
    range) or stays off. Since batch 64 Tears of the Kingdom runs without the TBDR rewrites on iOS, so the heap is on there by default (the iPhone's
    log of 2026-10-21 says so). Compared on the iPhone 17 Pro on 2026-10-22 (product60): the prologue from an empty save (12 shots, frames 1300-3500)
    and Great Sky Island standing (5 shots): with `DM_NVN_RT_HEAP=0` the same pictures (looked at; on the island the difference to a heap run is
    below that between two heap runs). GPU memory on the island 2,076-2,088 MB with the heap against 2,186 MB without (RSS 3.6 GB there), the
    prologue 1,933 / 1,920 MB. Kept on
  - [x] batch 64 · 62i what the first device run needed: the game data located off the main thread with a screen saying so (the 16 RomFS parts took
    4.5 minutes to unpack and the 304,243 files 14 s to walk; on the main thread the launch watchdog ends the app); `DM_CACHE` in Library/Caches on
    iOS (unset, the Mac's default path does not exist there and CreateCacheStorage failed 933,995 times in 10 s into the log, now at most 3 lines);
    `games/totk/survey.c` through vm_read_overwrite (the iOS SDK's mach_vm.h is an #error). Install before copying the parts: `run.sh install` backs
    the app's Documents up, parts included (16 GB back to the Mac)
  - [x] batch 76 · 62j the TBDR rewrite that breaks on the phone: with them on the iPhone drew a trail behind Zelda's torch and a glow over half the picture
    (the prologue's frame 1300 from an empty save); off, the picture is the Mac's, so Tears of the Kingdom runs without them on iOS too since batch
    64 (`games/totk/defaults.c`). Each rewrite off in turn (store lookahead, load elision, same-target continuation, clear-only hoist, on-demand
    depth-stencil) against that shot names it.
    Found 2026-10-22: the Mac shows it too with product60 at 60 Hz (`DM_NVN_TBDR=1`, frames 2300 and 3500 of the prologue: brightness 83.8 / 119.8
    against 45.1 / 75.9). `DM_NVN_TBDR_OFF=<part>[,...]` (new, diagnostic) turns single rewrites off: only `store` cured it (44.8 / 75.8); the
    other eleven parts and the store lookahead alone did not. The store decision's unwritten-DontCare (a pass that loaded an attachment and did
    not write it stores DontCare) never saw clears drawn as quads (`clear_quad`, `clear_ds_quad`: scissored or masked clears), so a pass that
    loaded a target and only cleared part of it lost the clear (186's lead from DavidZande/MacNativeGames 907138d). Such a clear now marks its
    attachments written (`sd_note_quad`). With it `DM_NVN_TBDR=1` gives the TBDR-off picture on the Mac (43.8 / 74.9, looked at) and on the
    iPhone 17 Pro (the prologue, no glow and no trail). `gfx/tests/t_clearquad.m` (`-DDM_TEST_CLEARQUAD_UNNOTED` fails q1).
    The default stays off on iOS too: on the iPhone 17 Pro (Great Sky Island, standing, product60, two runs each, alternating) `DM_NVN_TBDR=1`
    loaded 194 MB and stored 205 MB a frame against 229-244 / 263-279 MB, its passes took 22.8 against 24.3 ms of GPU, but frames took 66.6
    against 33.4 ms: the translation thread's CPU rose from 7.4-7.8 to 12.5-12.7 ms a frame and, without the mid-submission commits the
    rewrites rule out, the GPU starts only once the submission is encoded (12.7 + 25 ms misses the 30 fps slot)
  - [x] batch 65 · 62k a cave wall missing from some camera angles (the player on the iPhone, not on the console; on the Mac too: the iPhone's
    cave save, Link climbing the rock at the cave's start). The wall was two draws (6636 and 1647 indices) that the game left out whenever its
    CPU frustum test put their bounds outside the view, which was right: they were drawn where another mesh is. The cave meshes decode their
    vertices with constants updated inline before each draw; a draw bound after `upd_flush` had written the submission's updates (a copy or a
    dispatch in between) got the uniform snapshot of the submit call without them, because the private copies were gated on updates still
    unwritten (`g_nupd`). Now they follow every update of the submission (`upd_any`, `gfx/nvn/nvn_mtl.m`), for draws, replay-time reads and
    compute uniform pointers; `gfx/tests/t_ubren` 2c, 5b, 6 (`-DDM_TEST_UB_FLUSHED_SNAPSHOT` fails them). Frames 1400-1415 and the climb
    (1450-1650) keep the wall, the same picture as `DM_NVN_UB_RENAME=0` (0.8-1.6 % of pixels differ, particles). Found by elimination: not
    the occlusion queries (forced either way), conditional rendering, FPCR, the TBDR rewrites or the x18 rewrites of the culling code
  - [x] batch 67 · 62l a 60 fps iPhone build. The app's frame rate setting is a ceiling, and the phone's build embedded the 30 fps product (the
    game set interval 2 itself; the user's "60 fps on, still 30"). `ios/run.sh` takes `DM_PRODUCT=<dir>` (relative to DM_ROOT) and the options
    with it: `DM_PRODUCT=build/totk/product60 DM_OPTIONS=fps60,realtime,scene` (no `output`, which makes the handheld swapchain 1080p, no
    `aspect`); the game's controller picks 60 / 30 / 20. The app's render resolution setting now reaches Tears of the Kingdom: without
    `DM_RENDER_RES`, `DM_RENDER_HEIGHT` asks for that many lines at 16:9 (`frame_step.c` scene_env; `t_frame_step`, -DDM_TEST_SCENE_NO_HEIGHT
    fails 3); on the Mac `[scene size] 960x540 (DM_RENDER_HEIGHT)`. Budget seen on the phone at 30 fps outdoors: GPU p50 10-12 ms (24 in one
    heavy place), the translation thread 12-16.5 ms of CPU a frame for 3,300-4,000 draws at 1.3-1.6 GHz once the thermal state is serious, when
    the app holds 30 fps anyway; dips below 25 fps came from pipelines made on a first visit (up to 360 ms a frame) and from that CPU time
- [x] batch 56 · 63 the 2K canvas and MetalFX (veneers in the rewriter, HeapDef overlay, larger application pool; after 52 and 95)
  - [-] 63a MetalFX's docked guard against the scene's content size (MacNativeGames ff092e3): their fix is in their replacement of the game's
    upscale pass; our present-time MetalFX (`fx_apply`) already compares the content size with the size on screen (63c: the valid region from the
    pass's constants)
  - [x] 63b the output size ("native 2K", after MacNativeGames' game_patches_v143.c patch_output_res, MIT, reviewed at 41e1e0c): code option
    `output` (rules.py): the swapchain size built at main 0xbdb578 / 0xbdb584 is loaded from the host page +24, and a gated code cave at 0x3dcb28
    sets the canvas (x3 and display 0's +0x10) from +32 when that is not 0 (MacNativeGames stores unconditionally). `games/totk/frame_step.c` writes
    them from `DM_OUTPUT_RES` (1920x1080 / the game's own), up to the heap overlay's `.dm_output` note (`games/totk/heapdef.py ... --output
    <w>x<h>`, MacNativeGames' formula); on iOS the growth needs `DM_APP_POOL_ADD_MB` (device check). `games/totk/check_output_res.py`,
    `t_frame_step`, `tools/tests/t_heapdef.py`. 2560x1440: the UI sharper, GPU p50 6.2 against 4.7 ms; the post-processing size follows the canvas
    (canvas / 1.2, a multiple of 16). product60 rebuilt (previous: `product60.prev63`)
  - [x] 63c MetalFX in place of the game's own upscale pass (after MacNativeGames' mfxup_dock, MIT; `gfx/nvn/nvn_mtl.m` `fxp_dock`): with
    `DM_UPSCALE=metalfx` (the iOS setting) the draw of the profile's `upscale_pass` (game.json {fs, color}, `tools/gen_game.py`; Tears of the
    Kingdom's ac58fc18, FSR 1 EASU-style, HDR) is left out and MetalFX's spatial scaler writes the canvas from the valid region its constants give,
    only when that draw alone would write the whole canvas. Mean RGB within 0.2 of the game's own; GPU p50 6.30 against 6.20 ms at 2560x1440
  - [x] 63d MetalFX before the game's compose pass (after MacNativeGames' mfxup_draw, the "iOS handheld" case; `fxp_scene` / `fxp_sub`): in handheld
    the profile's `compose_pass` (0ac9a549) samples a MetalFX upscale of the scene instead of the scene (substituted by texture ID). 960x540
    handheld: the same brightness, smoother edges, GPU p50 4.25 against 3.36 ms bilinear. `gfx/tests/t_fxp.m`, `tools/tests/t_upscale_pass.py`
- [ ] 64 for the next game: add-on content, FenceSync, ClearBuffer and the other NVN functions, LDN (the list of 2026-10-20 too)
  - [x] batch 54 · 64a the 47 NVN functions bayonetta3-mobile has and we lack: 27 now (getters of colour, vertex, sampler and texture view state, a
    texture's address, the window crop; BindImage, BindTextures, the deferred image / separate texture / separate sampler binds read at the
    submission's start as 61b's; the queue's acquire; the checkpoint mask 1; colour reduction and ZCull compression accepted).
    `gfx/tests/t_nvn64a.m`
  - [ ] 64a2 the rest of 64a: ClearBuffer, DrawArrays / DrawElementsIndirect, the IndirectCount multi-draws, DispatchComputeIndirect and
    Downsample (Metal's indirect draws through `draw()`, whose rewrites read vertex counts on the CPU: deferral, chunks, coverage), the deferred
    uniform / storage buffer binds (the uniform snapshot walks only plain binds), SetPatchSize with 64f; left to the red-reporting stub: the
    subtile size calls, GetCommandMemorySize, GetSparseTileLayout (its layout is not in our header) and the integer border colour getters (no
    setters here)
  - [x] batch 54 · 64b CopyCommands as a copy made at the call (the SDK copies, as dr3-mobile read it; ours recorded a call): nested calls inlined,
    refused past 16 call levels, 1,000,000 records or 64 MiB, a cycle or a bad record; the replay follows calls 64 deep at most. No handle registry
    (Tears of the Kingdom's 1.28 million calls a run would grow it). `gfx/tests/t_copycmd.m`
  - [x] batch 54 · 64c depth bounds through `setDepthTestMinBound:maxBound:` (a record OP_DEPTH_BOUNDS; bounds clamped to 0..1, empty bounds skip
    the draw; the Metal 3 build behind @available), checked with bayonetta3's vectors (`gfx/tests/t_dbounds.m`). Neither game enables depth bounds
  - [x] batch 54 · 64d rasterizer discard (draws left out while on, reported once when the vertex stage may write memory), `SetScissors` /
    `SetDepthRanges` batches (entry 0 applied, others reported once), a second queue ordered on the one Metal queue; `QueueWaitSync` false on a sync
    never fenced. `gfx/tests/t_nvn64d.m`
  - [x] batch 54 · 64e several texture and sampler pools (bayonetta3's descriptors, lordran's checks): each pool an ID range from its descriptor
    memory's base (`hle_nvn.c pool_ns`; one pool unchanged), handles resolved at the base of the pool set last (`h_tid` / `h_sid`). Neither game
    sets a second pool. `gfx/tests/t_pools.m`
  - [ ] 64f tessellation through a Metal 3 command buffer between Metal 4 submissions. With the next game that tessellates: neither has a
    tessellation stage (control +0x714 of every capture is 0, 1 or 5: TotK 28,652 + 24,371, manosaba 2,740), ProgramSetShaders already reports one
    red, and it needs the translator's hull / domain stages as well
  - [ ] 64g HSETP2 `.H_AND` as LASRecomp and yuzu (5 vectors); translator call arguments kept local. With the next change of the translation
    chain: both are translator changes, so the chain version moves and every shader of both games is translated again (Tears of the Kingdom's
    25,320 and manosaba's 1,299, and the iOS libraries), while neither game has the instruction (the review counted 0 uses)
  - [x] batch 54 · 64h symbol resolution as Horizon's rtld (the SystemEvent: 64h2), by the ISC-licensed oss-rtld's rules: hidden / protected
    references bind in their own module, default ones to the first module in the order main, subsdk0-9, sdk that defines the name non-locally
    (`bind_sym`; it differs from ours only in the sdk's typeinfo and vtables: 801 manosaba, 782 TotK). Both games ran the same, so it is the
    default; DM_RTLD=0 binds by ours. `host/tests/t_rtld.c`
  - [ ] 64h2 a SystemEvent kept while its detached read end waits (blade2-mobile, UE4's audio renderer): neither game's main or subsdk0 imports
    `DetachReadableHandleOfSystemEvent` (only the sdk defines it); with a UE4 game
  - [x] batch 54 · 64i `getentropy` as HLE_FORCE, and the crypto / os random bytes from the host's entropy (deterministic with DM_DETCLOCK);
    `nn::pl::GetSharedFont` every shared font at once in type order (24-byte SharedFontData, read off manosaba's sdk); IL2CPP's `SystemNative_Stat`
    / `LStat` are not dynamic symbols in manosaba's main, so not taken (armyja patches them by offsets: with a Unity game that needs it).
    `host/tests/t_rng_font.c`
  - [x] batch 54 · 64j AudioOut volume and counters (Set / GetAudioOutVolume, GetAudioOutPlayedSampleCount, GetAudioOutBufferCount,
    ContainsAudioOutBuffer, FlushAudioOutBuffers; the SDK's own read through a null pointer for a host output) with the SDK reference's and yuzu's
    semantics (facts only), `host/tests/t_audio_extras.c`; FlushTexels as a CPU write unless WriteTexels wrote the texture since its last flush
    (Tears of the Kingdom pairs each of its 42 WriteTexels with one), `gfx/tests/t_flushtexels.m` (AVC / AAC from the bytes the game sends: 64j2)
  - [ ] 64j2 AVC / AAC decoded from the bytes the game sends (lordran acfb424: VideoToolbox from SPS / PPS, the reorder window from the SPS,
    AudioConverter for AAC; about 1,000 lines). Ours decodes the same track again with AVFoundation, and its Extractor hands the game that track's
    own samples, so for manosaba the bytes and the re-read agree. With the next game that demuxes itself or gives a memory source, or a desync seen
  - [x] batch 54 · 64k named time-zone rules (LoadTimeZoneRule, ToCalendarTime / ToPosixTime by the named zone through CFTimeZone;
    `host/tests/t_tzrule.c`); `IsSaveDataExisting` from $DM_SAVE, EnsureSaveData making it; OpenPreselectedUser; the NPDM's system resource size
    (done by 160); movie decoders reading the game's DecoderEvents when they signal (RegisterClientEvents keeping its pointer);
    ShowControllerSupport's result (`host/tests/t_hid_applet.c`); a separator before the mount name ("/rom:/…") and `\` between path components. [-]
    Unity shader layouts: our shaders come from the NVN calls (`shmap_from_capture.py`), not Unity's files
  - [x] batch 54 · 64l the control section length by GPU code version (0x838-0x880, at +0x20; bayonetta3-mobile's nvn_control.h as data): a shorter
    section is read to its own length and zeros to 0x880, so keys and .ctl files stay as dmsh_build.py checks; another version reported once. Both
    games' captures are all 1.16 / 0x880, so no key moves. t_nvn 5b2
  - [x] batch 54 · 64m guards against Metal aborts (super-mario-odyssey-ios 693bc6b / 356ab26, MIT, written anew): a non-depth format on the depth
    attachment left out, a cube view whose slices are not a multiple of 6 made 2D / 2D array, and (found with DM_TEST_NO_BC=1) a view of another
    format of a BC texture kept decompressed or as ASTC left out, red once (decoded since 62c3). `gfx/tests/t_mtlguard.m` (each case in a process of
    its own)
  - [x] batch 54 · 64n provisional profiles from the game files (LDN: 64n2): `tools/new_game.py --check` works the profile keys out again from each
    workspace and compares them with game.json, printing the engine markers (nxnative's profile_guess.py marker list, MIT, as data); both games
    agree. `tools/tests/t_new_game.py`
  - [ ] 64n2 LDN as dr3 (Bonjour, an HMAC-signed control protocol, a two-process test): neither game imports any `nn::ldn` function; with the next
    game that has local play
- [-] batch 55 · 112 conditional rendering on the GPU: not worth it on the Mac. The prologue makes 11,000-19,000 conditionals per 300 frames, 0-2 %
  on a samples-passed result still pending (at most about 1.3 draws a frame the console might have skipped); the GPU way needs GPU-written results,
  indirect draws and predicated clears (totk-mobile's nvn_query_gpu / nvn_conditional_draw). On the iPhone 17 Pro (2026-10-21): 0 of 11,298
  conditionals pending, so it stays as it is
- [-] batch 56 · 113 framebuffer fetch for in-pass reads of the current pixel (after 42): not done. In the prologue 16 fragment shaders sample what
  their own pass draws, all but one before any texture barrier, where the replay must read memory as the console does (42); the one after a barrier
  (fcd1a8d9) samples at a coordinate from its varyings, not its own pixel. What its attachment copies (three 1600x896 twice a frame docked) cost on
  an iPhone is a device check. That reason was wrong (2026-10-22): its vertex shader 6286c2c1 writes that varying as ((w + x) / 2, (w - y) / 2, -, w)
  of its position, so xy / w is its own pixel; done as 181
- [x] batch 57 · 114 an iOS Simulator build for the touch controls, safe areas and lifecycle: `ios/run.sh sim-build` / `sim-run [s]` / `sim-stop`
  (ios/README.md). The Metal 3 build with MetalFX compiled out (`DM_METALFX` in `gfx/nvn/nvn_mtl.m`; the Simulator's SDK has no Metal 4 headers or
  MetalFX), unsigned, without libvpx; `ios/tools/sim_metal.py` compiles the map's MSL for the Simulator (`sim-run` sets `DM_NVN_TBDR=0`); the game
  data cloned into the container. Its pictures are not a device's. manosaba on an iPhone 17 Pro Simulator runs, `DM_TOUCH=pad` lays out in its safe
  area, `DM_SIM_BG=20,10` gives LIFECYCLE OK, `DM_SIM_ENV="DM_TOUCH=pad DM_TEST_TOUCHPAD=check:10"` 17 passed (`simctl` has no touch input).
  `tools/tests/t_sim_metal.py`

Tier 5, needs the user's devices, controller or account:
- [x] batch 63 · 65 increased-memory-limit with a free account (one signing attempt): a personal team signs it (Xcode made the profile with it on
  2026-10-21; with manosaba the iPhone 17 Pro's limit went from about 3.4 to 6.1 GB), so `ios/tools/project_free.py` keeps it (`t_ios_cpu`; dropping
  it as before fails 1). Extended virtual addressing is refused to personal teams ("do not support the Extended Virtual Addressing capability"),
  which bears on 62's address space with a free account
- [ ] 66 a Switch Pro controller check of 2
- [x] batch 59 · 67 gyro and rumble (implemented here; the checks with the controller and the phone are on the device list)
  - [x] batch 58 · 67a vibration handles per style, only the connected style forwarded, bool arguments masked: `host/hle_motion.c`. Handles as
    1.4.3's sdk makes them ({type, Npad ID's low byte, device index, 0}; type 3 full key, 4 handheld, 5 pair, 6 left, 7 right); only the connected
    controller's values reach the front end (`dm_vibration_take`, dm_input.h), each band at its peak since the last take (totk-mobile); off while
    not permitted or out of focus (LASRecomp's rule). The prologue now gets 2 vibration handles and 1 six-axis handle. `host/tests/t_motion.c`
    (totk-mobile's landing pulse as data)
  - [x] batch 59 · 67b the Pro Controller's IMU and HD rumble over IOHID on the Mac (atone): `host/procon_mac.m` opens the controller (0x057e /
    0x2009) beside the system driver, switches its IMU on and turns input report 0x30 into six-axis samples (`host/procon.h`, atone's axes); the
    game's vibration goes out as HD rumble (output report 0x10, dekuNukem's encoding) instead of CoreHaptics. `DM_PROCON=0` leaves it to
    GameController. `host/tests/t_procon.c`. Device checks: aiming and Ultrahand rotation by motion, the landing pulse felt
  - [x] batch 58 · 67c CoreHaptics with plain players (atone f6dba3d); phone haptics rate-limited and off at a serious thermal state:
    `host/gamepad.m` takes the values every 15 ms, an engine per handle, a new endless event per level change; `host/haptics_map.h` maps the bands
    to intensity and sharpness (dekuNukem's table). iOS without a controller: the phone's haptics as pulses with acnh-mobile's limits, none at a
    serious thermal state or with `DM_PHONE_HAPTICS=0`. `host/tests/t_haptics.c`. The feel is a device check with the user's controller and phone
  - [x] batch 58 · 67d gyro fusion with the rest bias learned (fed by a Pro Controller since 67b): the six-axis state from `dm_motion_set`'s
    samples, the orientation by atone/nxnative 185cfda's method (Mahony's filter, written anew), the zero point learned from still samples
    (BotWRecomp's idea; GyroscopeZeroDriftMode sets the threshold); `GetSixAxisSensorState` writes a not-connected state without a source.
    GameController's axes are to be measured first: `DM_PAD_MOTION_LOG=1` (device check: flat, tilted forward, turned left); then `gamepad.m` feeds
    `dm_motion_set`
  - [x] batch 62 · 67e the phone's haptics after a trip to the background: iOS stops the engine there and refused the new one made at once
    (AVAudioSession 'what', a red line on the iPhone 17 Pro), after which the phone stayed still for the session. Now made at the first pulse due
    in focus, tried again 2 s after a failed start, doubling up to 30 s (`haptics_map.h` `dm_phone_engine_due`, `gamepad.m`; `t_haptics`)
    Checked on the iPhone 17 Pro on 2026-10-22 (Tears of the Kingdom): after switching to another app and back, the vibration test pulses again
- [x] batch 62 · 68 MixWithOthers on an iPhone: another app's music plays on under the game (iPhone 17 Pro, 2026-10-21; "mixes with other apps: yes")
- [x] batch 75 · 69 the game over flow with the overlay, in play. iPhone 17 Pro, 2026-10-22, the 60 fps build with the RomFS overlay served (194),
  driven through the command channel from a copy of slot_00 of the 05:09 install backup (the only slot in the copy, so "continue" loads it):
  walking off Great Sky Island's edge, GAME OVER with 重試 selected, A, the loading screen with its tips, and back on the island at the save
  point with the area's name. GPU time there (DM_NVN_PASSGPU): the island in daylight ~25 ms a frame (the 60 fps build runs it at 30: four
  720x720 depth passes ~3 ms, 1280x720 passes loading 4-5 attachments the most), the black game over screen ~20 ms (six passes a frame
  loading five 1280x720 attachments, 6.6 ms: the world is still drawn behind it); both for 62j / 186's tile loads
- [x] 70 manosaba's iOS app with chain bab8d4746968 on the devices (was 1e41705de87b), with the host stack of 40 (both IPAs built with it); `ios/run.sh lifecycle` there, and the home button and the app switcher by hand (51); the log's "CoreAudio output opened in" time there (55); the on-screen controls of 58 with `DM_TOUCH=pad` (`DM_TEST_TOUCHPAD=check:30`, then by hand: the stick, the camera drag, the eye, a controller connected)
  2026-10-21 on the iPhone 17 Pro (chain ca5eb9623812): `ios/run.sh lifecycle` LIFECYCLE OK, the home button and the app switcher fine by
  hand, "CoreAudio output opened in 4.1 ms". The on-screen controls of 58 with `DM_TOUCH=pad` not wanted: manosaba plays by touch natively and
  needs no stick (the user, 2026-10-22); they are Tears of the Kingdom's, used in the user's play on the iPhone (2026-10-22, seen through mirroring)
- [-] 71 not done: iOS 17 is not supported (59)
- [ ] 71b the covered-window check of 27 in a window (`DM_WINDOW=1 DM_TEST_COVER=30,10`, display on); the Metal 4 screen of 19 on a device
  (`DM_TEST_NO_METAL4=1` in Documents/env.txt); the backup exclusion of 20 in Settings → iCloud → Backups; uncapped pacing of 53 in a window (product60, `DM_WINDOW=1`)
- [ ] 71d displays above 120 Hz: macOS rounds a 60 Hz request to 72 on a 144 Hz panel (tvc97); needs such a display
- [ ] 71c in play with product60: the systems BotW had on 30 fps constants (stamina regeneration and drain, arrow speed, shield surfing, rune
  aiming, flurry rush) at 30 and at 60 fps, and cutscenes there (none listed for Tears of the Kingdom by MacNativeGames; BotWRecomp's BotW list
  as the guide)

Tier 6, optional (seen, not planned before; listed at the user's request 2026-10-20):
- [x] batch 60 · 149 face buttons by position for PlayStation and Xbox controllers, as a switch (MacNativeGames 31052d6, MIT, written anew):
  `DM_FACE_BUTTONS=position`; Nintendo controllers keep their printed letters. Unset, the system's letters as before (MacNativeGames' default is
  position; the user plays with an Xbox controller by its letters). `t_padmap`
- [x] batch 60 · 150 pchtxt patches at load and a cheat VM, refusing rewritten sites (MacNativeGames 1b33531, PR #22)
  - [x] batch 60 · 150a pchtxt at load (MacNativeGames 1b33531's TOTK_PCHTXT, MIT, written anew): `DM_PCHTXT=<file>[:<file>...]` (`host/pchtxt.c`,
    Mac): the `@enabled` sections go into the module its `@nsobid` names (against `<module>.cuts.tsv`'s `# BUILD_ID`) before the code is sealed; a
    patch touching a word the rewriter changed, or past the image, refuses the whole file in red. iOS: a red line. `host/tests/t_pchtxt.c`
  - [x] batch 60 · 150b a cheat VM (Atmosphère's cheat file format; MacNativeGames PR #22's split, MIT, open, written anew): `host/cheat_vm.c` runs
    Atmosphère's documented encodings (docs/features/cheats.md; facts only, its code is GPL); `host/cheats.c` (Mac): `DM_CHEATS=<file>` (main's
    build ID), `DM_CHEATS_ON=all` or names; store-only cheats into main's code at load (refused over the rewriter's words), the rest about 30 times
    a second on a host thread (mach_vm_read_overwrite / mach_vm_write). No cheat codes in the repository. iOS: a red line. `t_cheat_vm`, `t_cheats`
- [x] batch 79 · 151 the save-loading crash at 1.4.3 main+0x1bd01c4 (MacNativeGames dd0e893, MIT; written anew; batch 60 left it until it showed up):
  on the iPhone on 2026-10-23, 7 minutes into play (thread BaseProcExecutor0), the optional reference at x19+0x6b0 held 0xffffffffffffffff
  (MacNativeGames' Mac: 0xffff0000ffffffff while a save loaded) and `ldr x21, [x8]` read through it. `games/totk/optref.c`, a fault hook of crash.c:
  at that pc with the product's two loads only, a value above the 39-bit address space is taken as absent, as the game's cbz takes 0: the field
  cleared (compare-and-swap) and the load run again; one log line each time. `games/totk/tests/t_optref.c` runs the game's three instructions
  under a signal handler (10 checks); MacNativeGames' value alone (`DM_TEST_OPTREF_ONE_VALUE`) fails 1, the iPhone's crash
- [-] batch 61 · 152 frame interpolation by replaying the frame (LASRecomp `frame-interp-rt`; peiikai/LASRecomp `runtime/nvn/interp.c`,
  dc1842b..777fe71, MIT, work in progress): not taken. product60 already presents at 120 Hz on the Mac (GPU p50 4.5-4.8 ms); on a phone a replay
  costs a frame's GPU work again, and what limits Tears of the Kingdom there is not measured yet. To look at again once it runs there, if the GPU
  has room and the CPU side is the limit
- [x] batch 61 · 153 frame latency 2 at 30 Hz (lfkdsk/switch-appletv-port-skill references/05-perf.md §5.3, MIT: present-to-present standard
  deviation 0.603 → 0.301 ms, for 33 ms more latency): `DM_PRESENT_LATENCY=auto` (`gfx/nvn/nvn_pace.h` `nvn_latency_setting` / `nvn_frame_latency`,
  `link_latency_sync`) takes 2 while the display link ticks at 30 Hz, 1 otherwise; a number fixes it; unset stays 1. The default waits for a device
  check (the present interval spread at the 30 fps setting with and without `auto`). `t_pace`
- [x] batch 61 · 154 the ultrawide aspect patch on the Mac (MacNativeGames `aspect_patch.h` at 1.4.3, MIT, written anew): option `aspect`
  (`games/totk/rules.py`, gated code caves at 0x628c74, 0xb43460, 0x1ff04f4, 0x253fbb0) multiplies the cameras' ratio by the host page +40, by 1
  while it holds 0 (a factor, not MacNativeGames' replacement), which `games/totk/frame_step.c` writes at every present. `host/aspect.c`:
  `DM_ASPECT=<w>:<h>`, `<w>x<h>`, a ratio from 1 to 4, or `window`; View → Wide Picture (⌘4) switches 21:9; the present pass stretches the picture
  (`mtl_present_stretch`). The HUD stretches with it, as in MacNativeGames; iOS refuses `DM_ASPECT` in red. `games/totk/check_aspect.py`,
  `t_aspect`, `t_present_vp`. product60 rebuilt (previous: `product60.prev154`), `options.default` = `fps60,realtime,scene,output,aspect`

Tier 7, performance on the iPhone, from the sixth review of 2026-10-22 (its notes, with each source's commits and numbers, in the review's merge;
the device the user's iPhone 17 Pro):
- [x] batch 68 · 181 the iPhone's 10 fps shrine (2026-10-22: 60 fps facing one way, 10 the other; the user's save `work/run/totk/save_iphone_1022`
  slot_02, on the Mac with `DM_RENDER_HEIGHT=720`): 480 G-buffer decal draws a frame (7d1b1cac with c1 / c3 / c7, 5733c5bf with c1 / c3) each read
  their own pixel of the pass they draw into right after a texture barrier, and each got whole-attachment copies and a split: 1,347 copies, 6.5 GB
  loaded a frame, GPU p50 30.3 ms, translation 10.6 ms (`DM_NVN_FBX=0`: 4.8 / 4.5 ms; on the iPhone GPU 86 ms, translation 80 ms). Keeping a copy
  until its attachment is written never helped (every decal writes what the next one reads; DavidZande/MacNativeGames found the same). Now they
  read the tile (`fbx_fetch`, after OJZen/totk-mobile's framebuffer fetch and DavidZande/MacNativeGames 4b18416): `dmsh_fetch.py` rewrites a sample
  at `in.m.xy * (1.0 / in.m.w)` ("proj") and proves the vertex side (`screen_locn`: ((w + x) / 2, (w - y) / 2, -, w) of the final position; 2,388
  vertex shaders, 3,232 fragment shaders with such samples, 468 of them with no readable-tile version before), the host checks the viewport, the
  view (the attachment itself; a single-channel format's own swizzle expansion is accepted) and binds the readable-tile version. Shrine: 598 draws
  a frame read the tile, 0 copies, 315 MB loaded, GPU p50 5.5 ms, translation 5.9 ms; pictures within run-to-run variation (30 fps, two copy runs and
  one tile-read run, looked at and measured). 118 of the 598 follow another decal with no barrier between (undefined in NVN; they read the tile,
  where the copies read the previous split's store). `DM_NVN_FBX_FETCH=0` copies as before. Chain `ba19de4f1d15`. `tools/tests/t_dmsh_fetch.py`
  (`DM_TEST_FETCH_NO_PROJ=1` fails 2), `t_refl` (`-DDM_TEST_REFL_NO_SCREEN` fails 2), `t_fbx` fa-fd, fr, fs (`-DDM_TEST_FBX_NO_FETCH` fails 4,
  `-DDM_TEST_FBX_SWIZZLE_ANY` 2). Device check: the shrine on the iPhone
- [x] batch 68 · 182 the time-constraint declarations (61d's code part; nvn_pace.h `nvn_rt_*`): both threads declare the cap's period (the display rate
  and the app's own hold, not the interval the game raised when late), a new period keeps the computation declared (it went back to 2 ms), and the
  translation thread declares 1.25 x the p90 of its own CPU a frame (it declared half the period). nxnative 5efe1d8 / 04ea8f8 and MacNativeGames
  c5d8671 (MIT): with the late interval declared, TotK on an iPhone fell to 1.5 GHz and 5 % on performance cores at thermal state 0; ours showed
  0-15 % at 0.9-1.3 GHz. The comment that a thread using more than its computation is demoted was wrong (XNU: it ends its deadline for the period).
  `DM_RT_DECL=old` declares as before. `t_pace` (`-DDM_TEST_RT_COMP_RESET` fails 2). Device check: 61d's A/B on the iPhone (performance-core share,
  clocks, frame time, with and without `DM_RT_DECL=old`, after a cooldown). Done 2026-10-22 (iPhone 17 Pro, shrine save, alternating, warm, no
  cooldown between runs): `DM_RT_DECL=old` 1.75 W, 2.62 GHz, performance cores 66.1 %, frame p50 19.9 ms against 1.73-1.74 W, 2.63-2.64 GHz,
  65.8-65.9 %, 19.2-19.5 ms: no difference once warm (the first, cooler run of either is higher)
- [-] batch 74 · 183 guest yields: `host/hle_thread.c` turns them into `sched_yield`, and the iPhone's log showed the game's main thread yielding
  14,000-20,000 times (15-25 ms) a frame; Apple's Tech Talk 110147 says a yield drops the thread's priority for up to 10 ms and spinning keeps a
  thread from moving to a performance core. Measured, not taken: those numbers were the 10 fps shrine before 181. The iPhone 17 Pro's logs of
  2026-10-22 with the 60 fps build (b72walk, b73stand3; the 300-frame "translation detail" lines) show 182-6,697 yields a frame taking 0.2-2.8 ms,
  0.4-0.6 µs each: the yields come straight back. In XNU (osfmk/kern/syscall_subr.c) `sched_yield` is `swtch_pri(0)`, which returns at once when
  no other thread waits for the processor and lowers the priority (for at most one 10 ms quantum, undone when the thread runs again) only when one
  does; so a spin or a block could win back at most those 0.2-2.8 ms. `thread_switch(MACH_PORT_NULL, SWITCH_OPTION_NONE, 0)` (libpthread's
  `sched_yield` with `PTHREAD_YIELD_TO_ZERO=0`) yields without lowering the priority, if it is ever needed
- [-] batch 77 · 184 pipelines compiled at first sight (up to 360 ms a frame on the iPhone): MacNativeGames' one binary archive per pipeline (iPhone 14: warm-up
  95-122 s → 5.9 s, pipeline time in long frames 1,452 → 102 ms; its pitfalls: many pipelines in one file, 142 files searched, `gpuarchiver-*`
  folders), Apple's offline `metal-tt` route from our metallibs or Metal 4's `MTL4Archive`; a seed `pso_manifest.tsv` from Mac runs (the slot of
  78 no tool fills); MacNativeGames PR #166's prediction rescans. Measured, not taken (iPhone 17 Pro, 2026-10-22): the seed (209) and the
  phone's own `pso_user.tsv` warm 3,621 render and 63 compute pipelines in 3.8 s of background time (the system's Metal cache keeps them
  between launches), and standing on Great Sky Island no long frame had a new pipeline; a walk, a game over and its reload had 10 such frames,
  730 ms (the worst 260 ms, the reload), pipelines met there for the first time, which an archive made on the device would not hold either.
  The rescans of 205 cover the rest
- [x] batch 75 · 185 the replay's CPU per draw (22,000-34,000 instructions): `prog_get` returns an `__unsafe_unretained` pointer ARC ignores (a
  retain and autorelease per draw; DavidZande/MacNativeGames b484570), a vertex-binding cache (nxnative 697df75), a view cache by serial (nxnative
  9b7cb76), `g_rmu` as an `os_unfair_lock` (MacNativeGames 5f5de5e), MacNativeGames' replay caches (dfa7ec4, shrine submit 7.1 → 6.0 ms on a
  Mac), `-O2` against the iOS default `-Os`; first a per-stage CPU split of the draw path (GirlNet's `DM_NVN_SUBPROF`).
  The split by sampling instead (`sample(1)` of the translation thread for 10 s, the iPhone's shrine save on the Mac, 6,960 draws a frame, 30 fps):
  `bf_fn`, which picks a decal's readable-tile function, formatted three NSStrings and up to 16 `appendFormat:`s as its cache key at every draw
  that reads the tile (since 181): ~16 % of the thread's busy time. Now a binary key (`bf_key`: the shader key, colour mask, constants and
  attachment map) in an `nvn_hmap`, the strings only the first time a key is seen. `arena_alloc` handed its buffer back through an
  autoreleasing out parameter (a retain, an autorelease and a release per call, several per draw): now unretained (the arena holds it until the
  submission completes), with the buffer's length and contents kept in the arena. Per frame there: 19,400 → 12,700 instructions a draw,
  25.5 → 17.8 M cycles (the thread busy 25.6 → 21.2 % of the time); the CPU milliseconds move less because the Mac lowers the clock with the
  load. Not taken, as measured: `-O2` (and `-Os`) against the Mac's `-O1`: 17.7-18.6 M cycles a frame in all three; the vertex-buffer lookups
  (`mtl_gpu_addr` from `draw`, 1.7 % of the thread's time), `prog_get` and `g_rmu` (under 0.2 % each) are too small to chase. `t_fbx` bk (3;
  `-DDM_TEST_BF_KEY_NO_LMAP` fails 1), ar (`-DDM_TEST_ARENA_AUTORELEASING`: +100 on the buffer's retain count). On the iPhone 17 Pro (batch 76's
  install, Great Sky Island standing, ~4,300 draws a frame, against batch 74's on the same save): 28,500-28,900 → 22,700-23,500 instructions a
  draw, 31-33 → 28-30 M cycles and 7.8-9.7 → 7.1-7.8 ms of translation CPU a frame
- [ ] 186 62j's leads: shared storage tracked by serial (`stor_key_of`, the store lookahead and hoist), clear quads that leave `g_sd[].wrote` unset
  while `udc_dead` stores DontCare (done in 62j: it was the glow), `attr_read` without `pend_hits` (`fz_load_covers`) (DavidZande/MacNativeGames 907138d, 14142f8); the load probe's
  readback not ordered after the probed pass on Metal 4 (`lp_finish`); `DM_NVN_XV=0` in the one-at-a-time list; sentinel clears; the iPhone run with
  `DM_NVN_TBDR=1` under Metal API validation
- [-] batch 75 · 187 passes: lazy clears without the TBDR set (nxnative f5646ef), narrower encoder-start barriers with a vertex-stage check (tvc97
  12ca537). Measured, not taken (Mac M5, Tears of the Kingdom with the TBDR rewrites off, its default). The render-target line now counts the
  passes opened only to land clears and what they store: the prologue 3.2-5.8 a frame storing 19-34 MB, the iPhone's shrine save 9-11.7 storing
  22-25 MB. Lazy clears written after nxnative (a waiting clear taken as the load action of the next pass on the same texture, level, layer and
  view; `t_lazyclear`, 14 checks with a level-blind negative) took about 2 clears a frame in the prologue (clear-only passes 3.6 → 3.5, stored
  19.8 → 19.5 MB) and 0-1 in the shrine. A logging run gave why the others landed: the next pass was on other targets (10,300 of ~14,000),
  then render enables, sampling and compute dispatches; letting them wait across passes that do not touch them made it worse (sampling inside a
  later pass split it: 22 MB more loaded and stored a frame). Removed again, the counter kept. Encoder-start barriers: in the shrine, GPU p50
  with the full barrier 7.85-9.34 ms, with none at all 7.22-8.69 ms and with tvc97's narrow pair 7.35-8.82 ms over two rounds each, inside the
  run-to-run spread; Apple's documentation does not say which stage an attachment's load belongs to, so a narrower barrier risks races a
  screenshot would not show, for no measured gain
- [-] batch 75 · 188 attachment copies: copy textures without `PixelFormatView` (lossless compression), only the drawn level and layer. Measured,
  not taken: since 181 the decals read the tile, and the copies left are 0 a frame in the prologue and the shrine on the Mac and 5 a frame on
  the iPhone's game over screen, the same 5 in its 20 ms and its 11.6 ms stretches (69), so they are not what costs there
- [-] batch 77 · 189 back up to 60 fps after a step down (nxnative 9409472 / b53bb89, tvc97 `nvn_cap_auto.h`: under 30 % on performance cores in two windows).
  Measured, not taken (iPhone 17 Pro, product60, 2026-10-22): our only step down of our own is the 30 fps hold in Low Power Mode or from thermal
  state serious, re-applied when either changes (`main.m`); the frame rate is otherwise the game's own controller, which went back to 60 by
  itself (the walk test's game over menu: 33 → 16.7 ms frames, GPU 22-26 → 11.6 ms). The host's frame step (`DM_FRAME_STEP=vsync`, 133) on Great
  Sky Island probed k = 1 ten times, two of them failed outright, and stayed at k = 2 with GPU p50 17.7-19.7 ms: that scene does not fit 60 there.
  Wrong (216): those were the GPU's times at the clock it keeps for 30 fps; the same scene takes about 12 ms at 60, once the clock comes up
- [ ] 190 a scene size driven by dropped frames (upstream `hle_oe.c`: an iPhone 17 Pro Max's dropped frames 23 % → 10 %), when TotK's scene size
  can change while it runs
- [x] batch 74 · 191 correctness from the review:
  - `QueryHostSupportsTextureGatherOffsets() => false` (totk-mobile eaec4e4): in place another way since 686b81e (`third_party/ryujinx`'s dm
    patch: a four-offset gather becomes four one-offset gathers, after nxnative 771f62f); the `agl_technique_pfx` compute shader with register
    offsets has its four gathers in its MSL, and every captured shader has a Mac and an iOS library (the Shaders note above was stale)
  - `host/lang.h` reads the script before the region (Touchable 69ddbd5): "zh-Hans-HK", "zh-Hans-TW" and "zh-Hans-MO" gave Traditional Chinese.
    `AppleLanguages` first not taken: an Apple engineer calls it "an implementation detail, not something that's considered API" (Apple
    developer forums, thread 769769), and a developer there reports `Locale.preferredLanguages.first` follows the per-app language; the boot
    line logs the tag. `t_lang` (+1; `-DDM_TEST_LANG_REGION_FIRST` fails 1, `-DDM_TEST_LANG_REGION_IGNORED` now 6)
  - the `EFAULT` retry: 196; region-only Read / WriteTexels: 206
  - 64-bit driver handles (aerolithink e218be4): not needed. Tears of the Kingdom's shaders read no c2 at run time (`dmsh_build.log`: c2 0),
    manosaba's five read only the handles' low words (their words sit at multiples of 8 from the tables' bases 0x168 and 0x568)
  - the BC kernel's casts (aerolithink 4e98460): the bytes are widened to `uint` before they are shifted (C++14's shift rules, which MSL
    follows, already define the old `b[7] << 24`; aerolithink shows no miscompile). New `gfx/tests/t_bcd.m`: the kernel against the CPU decoder,
    1,024 random blocks of each of RGB_DXT1, RGBA_DXT1, DXT3, DXT5, BC4 and BC5 (unsigned, signed), every channel within 1
    (`-DDM_TEST_BCD_HIGH_BYTE_LOST` fails the four BC1-3 checks)
  - write-back batched (DavidZande/MacNativeGames 8c46687, MIT, written anew: a 9-level texture took a command buffer per level, ~1.25 ms each,
    ~110 ms stalls after spawning there): every level, layer and texture of an invalidate, and of the idle pass, in one command buffer with one
    wait (`wb_textures`); `DM_NVN_WB_BATCH=0` as before; an "invalidate N ms" line when one takes over 20 ms. `t_wb` (+4: one command buffer for
    a 4-level texture and another, the levels intact; `DM_NVN_WB_BATCH=0` fails the count, 5)
  - SDK 27's Metal4FX barrier properties (bayonetta3): nothing to do, the builds already use Xcode 27.0 (27A266a) with the iOS and macOS 27.0
    SDKs and none of those properties
- [ ] 192 touch controls after Touchable (476fb39): its fixed 8-way D-pad, Liquid Glass, face-button slides and layout; Touchable has no analog stick
  or camera drag, so the left side, the stick's origin, how presses look, the extra controls and opacity wait for the user's choices
- [x] batch 74 · 193 the iOS logs' `thermal -1`: `dm_thermal_now` had no iOS definition (`gfx/nvn/hle_nvn.c` said `front_ios.m` had one), so the
  `[perf] thermal` lines and perf.txt read -1 while `main.m`'s frame rate policy read the real state. `host/thermal.m` (in the host's sources, so
  the Mac and the iOS app): NSProcessInfo's thermal state. `host/tests/t_thermal.m` (with only the weak default, `-DDM_TEST_THERMAL_WEAK_ONLY`: -1,
  fails). Device check: the iPhone's `[perf] thermal` line 0-3
- [x] batch 69 · 194 the ability wheel (hold L) over black on the iPhone (2026-10-22, the user's screenshot after 62l's 60 fps build): the RomFS
  overlay's quick-menu fix (MacNativeGames' menu capture; the RomFS overlay note above) was never served on iOS: `games/totk/defaults.c`
  looks under the workspace, which the app does not have. The app now serves `romfs_overlay` from the RomFS's places (`main.m`
  `overlay_locate`) when `dm_romfs_overlay_check` passes (each manifest file whole, each original the RomFS's own: a half-copied overlay or
  another game version is refused with a red line); `ios/run.sh push-data [overlay]` and the data IPA carry it, as MacNativeGames ships its
  overlays with the data. On the iPhone 17 Pro, shots 60 / 180 / 300 / 420 frames into a hold: black in all 5 runs without it, the world
  behind the wheel in both runs with it (named by env.txt, then found by the app itself); the earlier runs that had looked fine ran at 30 fps. `t_romfs_validate` (+6; `DM_TEST_OVERLAY_ANY_ORIGINAL` fails 1). Also: the command channel's
  `texdump <path>` (the next frame's colour targets, as `DM_NVN_TEXDUMP`; `t_cmdchan`), and `t_ktrace` signals until the waiter woke (the
  2026-10-22 CI hang)

Tier 8, correctness and performance from the upstream refresh of 2026-10-22 afternoon (the user: do them and test on the iPhone through mirroring).
Correctness, small:
- [x] batch 70 · 195 a replaced sampler released once the GPU is done with it (acnh-mobile 029dd30): `mtl_reg_samp` released it at once, while a
  texture slot's old view already waits (`defer_obj_unref`). `t_samp` +2 (the old sampler waits in the deferred releases and leaves after two
  submissions; a weak reference cannot tell, the device keeps sampler states of its own); `DM_TEST_SAMP_RELEASE_AT_ONCE` fails 1
- [x] batch 70 · 196 `ReadFile` prepares its destination again on `EFAULT` (nxnative 53d7aad; 191's part from yokeyhh v18): the dirty-page
  tracker write-protects pool pages again between the prepare and `pread` (Monster Train 2 at 60 fps on a Mac: a read lost in 2 of 3 runs, none in
  4 after); up to 8 times, a line the first time. `t_fs` +1 (pages protected again after the first prepare); `DM_TEST_FS_NO_REPREP` fails 1
- [x] batch 70 · 197 pchtxt `@flag offset_shift` applied to the lines after it and `#` information lines skipped (MacNativeGames e55ad1c;
  IPSwitch's format, 3096/ipswitch example/README.md: an offset counts from the uncompressed NSO file, its 0x100-byte header included): the image
  offset is offset + shift − 0x100; ours skipped every `@flag` (a shift other than 0x100 patched 0x100 bytes off) and refused a file with a `#`
  line; `print_values` taken, any other flag or an offset before the image refuses the file. `t_pchtxt` +5; `DM_TEST_PCHTXT_OLD_FLAGS` fails 5
- [x] batch 70 · 198 a cube array whose depth is not a multiple of 6 named in red (blade2-mobile 4ebabb2; ours already takes depth / 6;
  red line 115). `gfx/tests/t_cubearr.m`; `DM_TEST_CUBE_DEPTH_SILENT` fails 1
- [x] batch 70 · 199 `tools/sdis.py --xref` / `--callers` to the end of the PLT, and an image without sized exports (nxnative 108d483).
  `tools/tests/t_sdis.py` (a synthetic image: code after the last sized export, then every export unsized); `DM_TEST_SDIS_OLD_END=1` fails 4
- [-] batch 72 · 200 `XLATE_DEBUG=0` refused or made safe: with it our translator gives Tears of the Kingdom 306 failures (call arguments) and 4,175 modules
  whose else-if bodies are unreachable (the AST optimizer's ElseIf blocks, which the SPIR-V generator never branches to; nsCHaojIMaLiAo,
  nxport's 0001 / 0002), manosaba 70 / 14; with the default (debug: no expression propagation) 0 / 0. `Program.cs` says the semantics are unchanged.
  Any change there gives a new chain version (dmsh_chain.py hashes dmsh_build.py, Program.cs and the translator), and such a chain is not loaded:
  the host loads only the default options' version (dmsh_chain.py: "other options give another V, which the host does not load"), so the
  trap takes a change in two places. The comment's correction and a refusal of XLATE_DEBUG=0 (or nxport's 0001 / 0002) wait for the next chain
  rebuild made for another reason
- [x] batch 71 · 201 the rewriter's red zone: for a post-index pop larger than 16, `site_plain`'s save at −112 lies past Apple's 128 bytes below
  the new sp until the restore (`ldp x18, x19, [sp], #0x40`: −176; upstream 373b402 is the Android form, no save below sp at all). None of our
  modules has one (the games' sdk pops by 16: −128). `verify/equiv.py` now refuses a patched side that touches memory more than 128 bytes below
  the CURRENT sp where the original did not ("redzone"; it watched the start sp's 128 bytes; upstream 4f0603f counts the Android form), so a game
  that has one fails `regress.sh` before it runs; the rewriter itself is unchanged (a change re-stamps every product). `regress.sh --fuzz`: TotK
  (both products) and manosaba EQUIV OK. `t_rewriter_synth` +2 (a pop by 0x40 flagged, by 0x10 not); `DM_TEST_EQUIV_RZ_START=1` fails 1
- [x] batch 71 · 202 a guest thread stopped inside a rewrite block: there the guest values of the block's two scratch registers sit below the
  reported sp, outside what manosaba's IL2CPP collector scans (`hle_thread.c` set_activity / get_context3); now resumed and stopped again until pc
  is outside the patch area (`in_patch_area`), up to 2,000 tries (a copied exclusive section may spin on a lock the pausing thread holds: then
  stopped there, a line once). From reading the code; not observed. `t_hstack` 9 (a thread half its time in a fake patch area: 0 of 50 stops
  inside, 20 runs); `t_hstack_inpatch` (`DM_TEST_STOP_IN_PATCH`) 26 of 50 inside, fails 1
Correctness, Tears of the Kingdom:
- [x] batch 70 · 203 GPU write-back decided by the texture's own bytes (MacNativeGames 8a29e0a, PR #188): ours skipped a write-back when any page
  of the texture was written since (16 KiB pages hold neighbours); the iPhone's log of 2026-10-22 skipped 1 of the first 4 terrain normal maps
  (260x260, format 0xd), the case MacNativeGames saw as tiles turning dark (12 of 240 skipped → 0 of 244). `wb_note` keeps a copy of a texture up
  to 256 KiB (64 MiB of copies in all: the table holds up to 8192); pages written but its own bytes as they were: written back, a line the first
  4 times. `DM_NVN_WB_EXACT=0` decides by pages. `t_wb` +2; `DM_TEST_WB_PAGE_ONLY` fails 2
- [x] batch 70 · 204 border colours per axis (MacNativeGames PR #148 / be82504): `dm_border_sample` returned the border when any coordinate was
  outside; now only on the axes whose wrap mode is a border (the variant's key names them). `t_border` +2 (border on s, repeat on t);
  `DM_TEST_BORDER_ANY_AXIS` fails 1. Not taken: the filtered edge of PR #205 (open, not checked in a game; two more samplers per border sampler)
Performance (measure first where marked):
- [x] batch 70 · 205 pipeline prediction scans again after a scan that stopped at its budget, keys already pending skipped before they count
  (MacNativeGames PR #166: pipeline time in long frames 35.8 / 7.1 / 2.3 s → 5.5 / 2.9 / 1.05 s, M4 Max, world entry); the pending table 768, 64
  keys; `pp_scan_due` / `pp_pending`, `DM_NVN_PSO_RESCANS` (32; 0 as before). `t_ppred` +5; `DM_TEST_PP_ONE_SCAN` fails 1
- [x] batch 72 · 206 CPU Read / WriteTexels of a region copy only the region's block-linear bytes (MacNativeGames PR #185: frames over 40 ms
  25.9 → 1.0 a minute, M5 Max, 1.1.2): `nvn_region_move` (gfx/tex), used on storage by `mtl_read_texels` / `mtl_write_texels`, the whole level
  when the region cannot move alone; counted (a line at the 1st, 2nd, 4th, ... call). Tears of the Kingdom 1.4.3's cliff save, 70 s on the Mac:
  no such call (its 42 WriteTexels are on GPU-written textures), so nothing measurable there. `t_tex` G (40 random regions each of six textures
  against the whole level; `DM_TEST_REGION_NO_SPLIT` fails 6), `t_writetexels` +1 (`DM_TEST_TEXELS_WHOLE_LEVEL` fails 1)
- [-] batch 72 · 207 occlusion results' freshness (nxport `report_guard.rs`): when the game reads a report its GPU work has not written, it reads
  an older value, as it would on a console reading before its GPU finished. Seeing those reads takes a read guard on the report pages, which is
  nxport's change itself; its numbers (72.6 against 65.9 fps) compare with an occlusion wait we never had, and no run here showed the objects
  missing for frames at detail switches that it describes. Not taken
- [-] batch 72 · 208 the dirty tracker's fault-around and page-exact GPU pages (dodola 2e58d47 / 58d3aa2): the iPhone's logs of 2026-10-21 give
  57 write-protect faults a second over 288 s and 168 over 1,536 s (peak 2,670 a second, about 45 a frame at 60 fps), against dodola's 420 a
  frame before its change: at most about 1 ms a frame at the peak, under 0.1 ms on average. Not taken
- [x] batch 72 · 209 184's warm-up seed from the players' `pso_user.tsv` lists merged (upstream 08f51a8): `tools/pso_merge.py` (each row once, in
  the order met; rows the warm-up does not read dropped) writes `pso_seed.tsv` beside the shader map, and `ios/run.sh build` packs it as the
  app's `pso_manifest.tsv`, which the warm-up reads before the player's list: a new install warms what earlier play met. Tears of the Kingdom's
  from the iPhone's list (05:09 backup) and the Mac's: 6,365 rows (2,895 shaders, 3,411 render pipelines, 59 compute). Not for the Mac host: its
  own list already covers the prologue (1 pipeline, 3 ms in long frames), and warming the seed there (16.8 s in the background) gave 55 long frames
  against 29. On the user's iPhone it repeats the player's own list; a fresh install's first play with it is not measured. `tools/tests/t_pso_merge.py` (`DM_TEST_PSO_MERGE_NO_DEDUPE=1` fails 2). For 184's
  archives, noted: an archive serves a specialised function only when it holds that specialisation, and a fail-on-miss request counts its hits
  (nxport 17d527f); no idle rule by draw counts (MacNativeGames 30a1b93)
- [-] batch 72 · 210 single-draw passes as compute kernels (nxport `tiny_kernels.rs`; 61e): on the iPhone (batch 69) 100 single-draw passes took
  3.6 of 12.4 ms GPU, mostly 1280x720 passes loading their attachments, while nxport moves only rectangles up to 640x360 (its full-size post
  passes as compute "nearly doubled the frame's GPU time"). The cost there is the loads: 62j and 187. Not taken
iOS:
- [x] batch 71 · 211 the unsigned IPA signed ad hoc with the increased memory limit alone (`ios/tools/ipa_sign.py`; LNPANDY c3bccb5: AltStore
  keeps the ~3,375 MB default without it; a free account signs no other of ours), read back from the signature; the packaging line gives the size
  and above 4 GiB (ZIP64) names the 16.6 GB IPA iOS refused (13d9191) and the RomFS parts. `tools/tests/t_ipa_sign.py`; `DM_TEST_IPA_UNSIGNED=1`
  fails 1
- [x] batch 71 · 212 `-Werror=unguarded-availability-new` for the iOS 18 build (acnh-mobile a98d127; `ios/tools/project_m3.py`). `t_ios_cpu` +1;
  `DM_TEST_M3_NO_WERROR=1` fails 1
- [x] batch 71 · 213 the iPhone's fps line showed "60 fps ¬∑ on screen 98 ¬∑ GPU 11.4 ms" (seen through mirroring, 2026-10-22): NSString's `%s`
  reads the default C string encoding (Mac Roman), so UTF-8's "·" became "¬∑"; the Mac window's title, "<game title> — <fps line>", the same
  with a Chinese or Japanese title. `host/ns_text.h` `dm_ns_text` (UTF-8, other bytes one character each) in `log_overlay.m` and `front_mac.m`;
  the other `%s` in NSString formats carry ASCII (game id, chain, shader keys). `host/tests/t_ns_text.m`; `DM_TEST_NS_TEXT_DEFAULT_ENCODING` fails 2
- [x] batch 73 · 214 sampler border colours Metal lacks on iOS (161 there): the walk on the iPhone (2026-10-22, the user's save on the Great Sky
  Island) named `ff9def5b3c418851_b` in red, "its MSL source is not here": the grass interaction map's (0.5, 0.5, 0.5, 0.5), which drawn as
  transparent black laid the Mac's grass flat (161); without the variant the edge colour is drawn. `games/totk/border_msl.txt` lists the shaders
  met (ff9def5b3c418851_b, 8126ed56ae359349_b, 86b5745642f11b32_b, 11687c00bab7d73b_b: each walk met another grass vertex shader), and
  `ios/tools/border_msl.py` copies their .metal from the Mac's MSL (the iOS libraries are compiled from the same MSL) with every vertex shader
  whose texture bindings are a listed vertex shader's (309 more, 7.3 MB in all; TotK's 7,751 vertex shaders that sample are 370 MB of MSL);
  `border_variant` compiles the variant on the device (MacNativeGames 852f862 builds its variants into the app ahead of time). On the iPhone 17
  Pro: 7 variants compiled in the first world load (ff9def5b3c418851_b and six vertex shaders, four of them found by their bindings), no border
  red line, one 40 ms frame at the first; 60 fps, GPU 11.8 ms. `tools/tests/t_border_msl.py` (`DM_TEST_BORDER_MSL_NONE=1` fails 2,
  `DM_TEST_BORDER_MSL_LISTED_ONLY=1` fails 1)
- [x] batch 73 · 215 the fps line kept its last "on screen" rate when no frame came with a screen time: while an iPhone is mirrored (locked) every frame's
  presentedTime is 0 (the walk: "[perf] ... intervals 0/0/0, dropped 600" in every 10 s), so a session mirrored after play showed "on screen 98"
  for good. Now "on screen –" after 1.5 s of presents without one. `t_perfline` +3 (`DM_TEST_PERFLINE_STALE_SCREEN` fails 1)

Tier 9, a bottom-up look at the iPhone's frame (2026-10-22 evening and night; the user: big wins thought through from the bottom, the best value for
the effort first, and the 120 Hz mode's fall from 100 fps to 40-60 solved before that). Great Sky Island standing (the `go_save0` copy of the user's
save), the iPhone 17 Pro with product60 and the Mac M5 in handheld mode; per-pass times from `DM_NVN_PASSGPU`, the draws from `DM_NVN_PASSDUMP_ALL`,
the frame's timeline from `DM_NVN_TRACE`, Time Profiler on the Mac, three Metal benches of the M5 written for this:
- Where a frame's GPU time goes (the phone's pass time): G-buffer and decals 34.5 %, full-screen passes of a few draws 20 %, about 104 single-draw
  passes of up to 256x256 16 %, other passes of many draws (a 64x64 probe, a 192x192 depth pass) 13 %, the four 720x720 shadow cascades 13 % (2.74 M
  indices a frame), compute 2.4 %. The Mac's shares are the same.
- Not where it goes: the resolution (the Mac at 960x540 and 640x360: the work, normalized by the passes whose size does not change, 12 % and 22 %
  less; the GPU milliseconds rose 6.2 → 7.8 because the GPU clocks down at the 60 fps cap); a draw's own cost (bench: 0.05 µs; an argument buffer per
  draw costs nothing measurable); pipeline changes (418 a frame); vertex outputs no fragment shader reads (none); caching the shadow cascades (their
  constants change every frame, the first 64 bytes too). Geometry: the game's shadow pass takes 0.29 ns an index on the M5, as a bench of depth-only
  grids does (0.28-0.65). A single-draw render pass costs 16-23 µs on the M5 against 1.4-2.0 µs for the same step as a compute dispatch.
- The clocks follow the frame rate: one phone run held 30 fps at 20.65 ms of pass time a frame, then picked 60 by itself and took 11.09 ms for the
  same standing scene, every kind of pass about 1.9 times faster; four other runs stayed at 30 fps with GPU 25-28 ms. With every small pass dropped
  (`DM_NVN_SKIP_SMALL=256`) the 30 fps GPU time stayed 24.7 ms: at a held rate, saved work becomes a lower clock, not a shorter frame. 189's "does not
  fit 60" was this.
- The game's controller (probes on the Mac, main 0x2db174 / 0x2b3d07c / 0x2db1ac; main 0x2dab98 and 0x2b3cff8 write its numbers): CPU time = the
  tick after the frame's update / draw / submit calls less the frame's start tick (taken after the previous present), GPU time = the frame's last
  report less that start tick; biases 0.05 (CPU) and 0.02 (GPU) of a 60 Hz frame, so 60 needs CPU under 15.83 ms and GPU under 16.33 ms; two
  frames over raise the interval, cooldown 6.
- iPhone Mirroring runs the phone's screen at 60 Hz with every presentedTime 0; on its own screen the phone runs 120 Hz, where the game's intervals
  1 / 2 / 3 are up to 120 / 60 / 40 fps. A 60 Hz present rate holds Great Sky Island at 30 fps without mirroring too (`DM_PROMOTION=0`, 10 minutes).
- The 120 Hz fall (six runs, ~5-8 minutes each): 64-81 fps on the island until 59-79 s after the app starts (loading included), then 44-54 fps
  for good, the CPU's power 2.2-2.9 → 1.2-1.5 W, the translation thread's clock 2.3-3.7 → 1.9-2.2 GHz; iOS's thermal state stayed nominal for 7
  minutes and more (with the user's clip-on cooler). The translation thread runs on the performance cores 100 % of the time there
  (`thread_selfcounts` per perf level, 225), at that clock. The frame is one chain: the game's main thread waits every frame for the GPU to finish the
  frame it just handed over (main 0x2db99c, the sync fenced right after the frame's one submission; the acquire's sync at 0x2db958 returns at once)
  before the render thread submits the next; the submit call (the capture) took 1.14 ms of it, the translation 9-13 ms with the GPU following its
  parts, starved 3.2-3.8 ms a frame (median) and 1.9-2.1 ms before its first part. Interval workgroups, the API for telling iOS about a deadline,
  are audio-only (the SDK's os/workgroup_interval.h); Game Mode's keys were set already (21)
- What sets the sustained level (night of 2026-10-23, the island at 120 Hz, sustained fps against a run of the same build without the switch: 54.3
  for the first, 56.0 for the others): the GPU kept busy (`DM_NVN_KEEPALIVE=1`) 40.2, its frame 14.2 → 8.3 ms but the translation thread moved to
  the efficiency cores at 1.41 GHz and the CPU to 0.33 W; the GPU one frame behind (227) 54.2, the clock lowered; the TBDR rewrites without the
  merging ones (228) 55.1 with 12 % less traffic; yields backing off (`DM_YIELD_BACKOFF=250:2000`, about one a frame) 54.3; the scene at 960x540
  (`DM_RENDER_RES`) 59.6, 98 % of the frames shown at 16.7 ms, 41 % less traffic, the CPU 1.29 → 1.44 W and the translation thread 1.81 → 2.09
  GHz. So after its first minute the phone holds the CPU,
  the GPU and memory to one power budget: work moved between them or kept busy is taken back as lower clocks, and only less energy a frame turns
  into frames (225: 50.6 → 56.0). The scene size is set once, at boot (main+0x585e64 hit once in a run), so a dynamic resolution of our own
  through the host page would rebuild the render targets, and the game's own is off (131a). The user keeps the game's 720p as the default
  (2026-10-23); Settings → the app → Graphics → Render resolution 540p (MetalFX upscaling on) holds about 60 on the island
- Why the CPU's clock falls, measured without the game (230, 2026-10-23 morning, `DM_PWRTEST`, two CPU threads of plain arithmetic at
  user-interactive, 240 s each, thermal state nominal throughout): alone, 4.10 GHz at 6.4-6.8 W, easing to 3.64 GHz at 5.0 W, then at 65-66 s a step
  to 3.31-3.33 GHz at 3.6 W for good; beside a GPU kept fully busy (compute dispatches back to back), 4.10 → 2.85 GHz over the first minute, then at
  61-62 s 1.31 GHz at 0.65 W for good, a third of the time on the efficiency cores (which ran 1.70 GHz), while the GPU lost 22 % of its
  dispatches (54 → 42 a second); beside a GPU about 60 % busy, 3.57 GHz, then 3.23 GHz at 3.4 W after the step (the GPU's clock fell instead:
  its dispatches 29 → 6-7 a second at the same share). So the phone grants a burst for about a minute and then a sustained budget, and a busy GPU
  keeps its share of it while the CPU is cut to 1.3 GHz. Tears of the Kingdom's GPU is busy 75-80 % of the time there (12.5-17 ms a frame at 47-64
  fps), its CPU at 1.4-1.7 GHz, and the frame waits on the CPU's chain. No API lets an app move that split (Game Mode's thread-group flag for
  the performance controller needs Apple's entitlement, XNU kern_resource.c); less GPU work a frame gives the CPU its clock back (960x540: 1.81 →
  2.09 GHz, 56.0 → 59.6 fps). Low Power Mode for a while and off again brings the burst back: the minute at 30 fps refilled it (the user's
  session: 88 fps after 90 s of it, not after 4 s)
- [x] batch 77 · 216 frame-rate trials (`gfx/nvn/nvn_fptrial.h`, `DM_FPS_TRIAL=1`): a game whose step the host sets, held at interval 2 or 3 for
  5 s with the target at 60, reads its report times drawn toward their submission's GPU start (× 0.35) until it picks interval 1 and 1 s after,
  so the clock can come up; a trial that steps back within 4 s on the real times has failed and the wait doubles (up to 32 s). Only at a 60 Hz
  present rate in game interval mode. On the Mac with a stand-in for the phone's clock (`DM_NVN_TS_DVFS`) held at 60; on the phone at 60 Hz none of
  22 trials on the island held (its sustained rate is under 60), each a few seconds of uneven frames: off by default. `gfx/tests/t_fptrial.c` (a
  clock that heads for 3/4 busy, 12 checks; `DM_TEST_FPT_NO_GRACE` fails 5, `DM_TEST_FPT_NO_BACKOFF` fails 1). Report times scaled per submission
  (`mtl_ts_factor`)
- [x] batch 84 · 217 single-draw passes up to 256x256 as compute kernels (210 reopened; after nxport's `tiny_kernels.rs`, MIT, written anew),
  opt-in (`DM_NVN_KPASS=1`): on the iPhone they did not save GPU time. Measured first: dropping every pass of up to 256 wide
  (`DM_NVN_SKIP_SMALL=256`, the picture wrong) on the phone's island, two pairs: GPU 11.1 / 11.3 against 12.3 / 12.9 ms a frame, the game's
  sustained fps 67.4 / 66.1 against 62.6 / 60.2, frame p95 15.9 / 16.3 against 20.4 / 21.1 ms. `gfx/tools/dmsh_kernel.py` makes each listed stage
  (`games/totk/kernel_shaders.txt`, 48 stages from the island's pass dumps) into kernels beside its library (`<key>_bk.metallib`, Mac and iOS):
  the stage's MSL as an inline function, the vertex kernel writing the draw's 3-6 vertices to records, the fragment kernel over the target's
  pixels in 2x2 quads (implicit-LOD samples get the quad's gradients; uncovered pixels of a covered quad run as helpers), coverage on vertices
  snapped to 1/256 pixel with the top-left rule (homogeneous for a vertex at w <= 0), perspective / no-perspective / flat inputs, the viewport,
  scissor and depth clipping, blending and write masks (read-write targets: MTLReadWriteTextureTier 2 formats), and the samples-passed count.
  `nvn_mtl.m` kp_draw takes a pass when it holds one draw (a scan to the pass's end), its targets have shader-write usage (up to 256x256, or
  with levels from 512x512, cubes; `tex_desc`), no clear is pending, depth and stencil are unused, the draw is triangles or a strip of 3-6
  vertices, one instance and sample, and the stages do not sample the level they draw; both stages' arguments are made before the shared compute
  encoder flushes uniform updates, and while the game counts samples passed the kernel adds to a visibility slot of its own. On the island
  38.9 passes a frame. `gfx/tests/t_kpass.m` (the kernels against Metal's render pipeline, 21 cases × pixels and samples passed, 42 checks;
  `-DDM_TEST_KPASS_STRIP_AS_LISTED` fails 2, kernels made with `DM_TEST_KERNEL_NO_GRAD=1` fail 1, with `DM_TEST_KERNEL_TIE=1` fail 14),
  `gfx/tests/t_kpnvn.m` (through the replay: one draw taken with the pass's pixels, two draws / a pending clear / no kernels not taken, a counted
  pass reports 32; `-DDM_TEST_KPASS_NO_SCAN` fails 1), `tools/tests/t_dmsh_kernel.py` (22; `DM_TEST_KERNEL_NO_GRAD=1` fails 6).
  `DM_NVN_KPASS_CHECK=<frame>` draws that frame's passes both ways and compares them: on the M5 all 96 within 0.002, on the iPhone the same but
  the sky's cube map faces (RG11B10Float, 16x16 and 8x8): one unit of the format apart in up to every pixel. The phone's island, two pairs: GPU 12.6 / 13.0
  against 12.3 / 12.4 ms, sustained 61.1 / 59.5 against 63.2 / 61.2 fps, CPU 1.20 / 1.20 against 1.27 / 1.24 W; the targets' shader-write usage
  alone (`DM_NVN_KPASS=2`) 12.6 / 12.4 against 12.5 / 12.8 ms, no difference. So the kernels do not save what the passes cost here. A sky cloud
  the first Mac shots showed only with the kernels comes and goes from run to run with a pinned frame step too (the clouds move with the
  game's clock, which a different frame time moves differently)
- [ ] 218 the G-buffer split by compute: two depth-probe dispatches (3c8e9ba061067c07_b, bc58d5dec860c367_b) sample the G-buffer's depth in the middle
  of it into a 23,232-byte buffer later passes bind, and 11 dispatches come before the decals; each split stores and reloads four colour targets and
  the depth (about 24 MB at 1024x576). Deferring the probes changes what they sample
- [ ] 219 the TBDR rewrites with mid-submission commits: on the phone GPU 24.3 → 22.8 ms and 15 % less traffic, but 66.6 ms frames without the
  commits (62j's runs)
- [ ] 220 MetalFX frame interpolation (iOS 26): it takes motion and depth textures, and the G-buffer has no motion target (three RGBA8 and an
  RG11B10F beside the depth), so the camera's motion would come from the depth and two frames' matrices; moving things would ghost
- [ ] 221 fewer triangles through the game's own settings (LOD, draw distance; the community's Tears of the Kingdom mods have such options): the
  user's call on picture against frame rate
- [x] batch 77 · 222 the host frame step's trials (`nvn_fstep.h`, with `DM_FRAME_STEP=vsync`): after 5 s at k above its minimum a trial of k - 1
  whatever the cost, its first second not counted late while the clock comes up; a failed one doubles the wait (up to 32 s). `DM_FSTEP_OPT=0`
  turns them off. `t_fstep` +5 (a light scene held at 30 with the clock low reaches 60; a heavy one stays at 30 with under a tenth of its time in
  trials); `DM_TEST_FSTEP_NO_GRACE` fails 2
- [-] 223 the frame step in ticks of 120 Hz (k = 1 / 2 / 3 / 4 for 120 / 60 / 40 / 30 fps on a ProMotion screen): tried and dropped. On the phone
  it settled at ~24 fps: its lateness reads the GPU's busy time, which at a lowered clock fills about 3/4 of any interval, so each step up lowered
  the clock and the next looked late too; and the game ran 7.8 % fast (game time / real time 1.078). Pacing at 120 Hz would have to judge frames
  by when they reach the screen alone
- [-] 224 the GPU one frame behind the game (LASRecomp dc1d28f, MIT: a wait for the newest fence waits for the one before it; their M3 Max 27.6 →
  60 fps): measured, not taken. On the phone at 120 Hz it ran 44 fps against 47.5: with the previous frame still on the GPU, the submission's
  uniform snapshot cannot take the ranges that frame's inline updates write (2.8 a frame), their updates go to memory before the draws (231 a
  frame), and the passes split (183 → 357 a frame; loads 0.32 → 1.81 GB, stores 0.36 → 1.85 GB a frame). And it is not safe for Tears of the
  Kingdom: the previous frame's updates would reach memory after the CPU wrote the next frame's. `DM_NVN_GPUWATCH` now compares the vertex and
  index ranges draws read in place as well (none changed in flight on the Mac at 60 fps)
- [x] batch 77 · 225 the translation thread, the frame's critical path, made cheaper (Time Profiler on the M5, island standing; its cycles a frame
  23.75 → 18.1 M, CPU 6.02 → 4.84 ms, instructions a draw 23.3k → 15.3k):
  - private copies of updated uniform ranges: ~360 a frame applied ~90 updates each, sorted from the log's walk order (newest first for one start:
    ~4,000 moves a copy), 18 % of the thread. The walk is reversed before the sort, and a range updated again in the submission gets its copy from
    its previous one with only the updates after it (2.6 applied a copy). `t_ubren` +7 (7-9); `DM_TEST_UL_NO_REVERSE` fails 1 (19,900 moves),
    `DM_TEST_UBSH_ANY_SUB` (an earlier submission's copy taken as the base) fails 2
  - the newest-update answers cached per range until the log changes (`DM_TEST_UL_SEQ_STALE` fails 6 of t_ubren); the snapshot lookup tries the
    copy it found last (`t_usnap` +5; `DM_TEST_US_LAST_UNCHECKED` fails 9)
  - the pool lookup: the hit counted per thread without an atomic, the slow path in a function of its own, the last hit tried first (9 → 5 % of the
    thread with the next); `t_poolcache` counts per thread
  - vertex streams bound once per encoder: every draw looked up and set every stream again, half the address lookups (`gfx/tests/t_vbelide.m`,
    7 checks: four draws reading their colour from stream 0 bound to X, Y, X, X and a second pass; `DM_TEST_VB_ELIDE_ANY` fails 3)
  - the uniform copies (the submit call's capture on the game's thread, the snapshot on the translation thread) under one fault guard a run: a
    `sigsetjmp` saving the signal mask is two system calls on Darwin, and it was 38 % of the game thread's submit call (`t_usnap` +6, a fault
    leaves the copies after it made; `DM_TEST_US_GUARD_EACH` fails 1)
  - `DM_NVN_GPUWATCH`'s check off the per-binding path when off; the translation thread's time and clock per CPU kind in the translation detail
    line (`thread_selfcounts` THSC_TIME_CPI_PER_PERF_LEVEL)
  On the phone (120 Hz, island standing, four runs): sustained 50.6 → 52.9 → 54.3 → 56.0 fps (the last with the one fault guard), the
  CPU's power 1.45 → 1.29 W, on-screen 16.7 ms intervals 71 → 85 %, the GPU starved 3.75 → 1.26 ms a frame (median, `DM_NVN_TRACE`); iOS took
  about half the saving as a lower clock
- [ ] 226 next on the chain, measured: the submit call's capture walks every command of the frame on the game's thread (52 % of the call; the
  records it wants could be noted when they are recorded) and copies 1.5 MB of uniform data that the snapshot then copies again; the snapshot
  lookup and the newest-update cache still miss between draws binding several blocks (4 % and 2.6 % of the thread); texture views are checked or
  made again in 6 % of it (textures initialized over live storage ~2,000 a second bump the generations); the render pass begins are 18 %
  (Metal's own), which fewer passes (217, 218) cut
- [-] 227 the GPU one frame behind made safe (after 224): a snapshot range that an inline update flush of the previous frame still writes was laid
  over with that flush's bytes, which are known, instead of being left to memory (the CPU wrote 1 of 63,392 such ranges before the next submit
  call on the Mac, while loading), so the passes stayed at 183, and no vertex or index range changed in flight (`DM_NVN_GPUWATCH`, three frames
  on the phone). On the phone 54.2 fps against 56.0 on the same build, the GPU starved about as long (1.50 against 1.26 ms a frame, median) and the
  translation thread's clock went 1.81 → 1.47 GHz. Measured, not taken; the code is not kept
- [x] batch 78 · 228 mid-submission commits with the TBDR rewrites that neither merge nor defer passes: they stopped while any rewrite was on, but
  only defer, fuse and hoist build passes across the submission; the others decide within a pass or at its end, and a commit comes only where no
  encoder is open. `t_chunk --tbdr-nomerge` (on with defer, fuse and hoist off, two targets: committed in parts) and `--tbdr-all` (one command
  buffer), 4 checks each; `DM_TEST_CHUNK_ANY_TBDR` fails 1. Store decisions and same-target continuation alone (`DM_NVN_TBDR=1
  DM_NVN_TBDR_OFF=defer,fuse,hoist,cover,sdx,xv,zskip,ds,range,rtheap`): the Mac's island 671 → 588 MB of loads and stores a frame at the same CPU,
  the prologue 640 → 545 MB with its pictures as before; on the phone 679 → 598 MB, translation cycles +6 %, 55.1 fps against 56.0. Not a default
- [ ] 229 the guest main thread's QoS class: it runs on the host thread that called `svc_thread_main_init` (the iOS app's boot thread, made without
  one) while every other guest thread runs user-interactive, and it is Tears of the Kingdom's busiest thread (143.6 s of CPU in the iPhone's
  7-minute session of 2026-10-23, the busiest worker 119.7 s); it runs real-time at priority 97 while it presents. Batch 79: `DM_MAIN_QOS=ui` gives
  it user-interactive (`host/tests/t_mainqos.c`, run without and with the switch; `DM_TEST_MAIN_QOS_IGNORED` fails 1), and the translation detail
  line shows the presenting thread's time and clock per CPU kind too. The A/B on the phone is not run: after 230 the limit is the power split
- [x] batch 79 · 230 `DM_PWRTEST=<threads>[:gpu[<busy %>]][:<seconds>]` (`ios/Sources/pwrtest.m`): the app runs a plain load instead of the game and
  logs each second each thread's share and clock per CPU kind, the work done, the CPU energy and the thermal state (results above, "Why the CPU's
  clock falls"). The spec in `host/pwrspec.h`, `host/tests/t_pwrspec.c` (7 checks; `DM_TEST_PWRSPEC_NO_DUTY` fails 1)

Tier 10, from the seventh review and the deep review of the new Tears of the Kingdom projects (2026-10-23; the user: A to E, in order, tested on the
iPhone without asking; F, the features, later). All sources MIT unless said. Correctness:
- [x] batch 80 · 231 command memory (nxnative 0b3c7b1, its issue #61: Tears of the Kingdom 1.4.3 crashed in `mtl_exec` after 8-30 minutes on an M3
  Pro, a JUMP's target overwritten): 16 bytes kept after every record but the END (`OP_KEEP`), the END takes a segment's last bytes, and a
  recording is linked only once it has its BEGIN. Ours kept 24 after every record, the END included, so a list of 496 bytes in a 512-byte window
  put its JUMP at 488..504, past the measured size; the rule holds for our slot + used reading and nxnative's fixed slots alike. t_nvn 5a: (2) a
  496-byte list has no JUMP and reports 496, (5) eight lists in slots of 496 bytes with 512-byte windows replay whole, (6) a recording whose BEGIN
  does not fit leaves the next slot's list alone (the record walker checks JUMP targets and stops at 4,096 records); `DM_TEST_CMD_KEEP24` fails 4,
  `DM_TEST_CMD_LINK_EMPTY` fails 1
- [x] batch 83 · 232 load-exclusives as acquire loads (nxport ce8f1c3, MIT: Apple cores reorder the reads after a plain `ldxr` that a spinlock
  protects; Breath of the Wild's double-run jobs and crashes gone, Tears of the Kingdom's walking speed the same): `tools/patch_nso.py` sets bit 15
  of every LDXR / LDXP / LDXRB / LDXRH the disassembler confirms (o2 = 0, L = 1, o0 = 0; a caspa has those bits too and is left), in place and
  before the sites are found, so the exclusive sections copied to the patch area copy the acquire form; the words of cuts and options stay as the
  rules give them. `<module>.acq.tsv` lists them. structck ⑭: the list is exactly the plain load-exclusives llvm-objdump reads outside cuts and
  options, the image is compared against an original with the same words (② and ⑪), and none is left in the patch area (④); equiv's deexcl and
  fnfuzz's single-threaded runs read both forms alike. Tears of the Kingdom main 17,079, sdk 234; manosaba main 2,310, sdk 233; the sites the
  same. t_rewriter_synth +5 (0x900: a spinlock's ldxr / stxr, ldxrb, ldxrh, ldxp and a caspa; 0xa00: an x18 section that loads with ldxr: the
  list, the words, the window's copy, structck refusing 0x900 put back); `DM_TEST_ACQ_OFF=1` fails 7
- [x] batch 80 · 233 a storage binding that runs past its pool binds from the pool it starts in (nxport 79b736d; `sbuf_gpu_addr`), and one in no
  pool still gets the dummy buffer; both are counted and given a red line the first time (before, silent: the GPU's writes lost).
  `gfx/tests/t_sbufpast.m` (a kernel writes two words through a binding 64 bytes before its pool's end with 4096 bytes; 6 checks);
  `DM_TEST_SBUF_WHOLE_RANGE` fails 3
- [-] 234 uniform reads at a computed index bounded by the bound size (nxport `msl_specialize.rs`: skinning indices going negative, a GPU hang in
  Wonder); ours guards c1 only (`c1_guard.py`), 7,023 shader files read c3-c19 at a computed index: no GPU fault, hang or command buffer error
  in Tears of the Kingdom's logs, and the checks would add instructions to those 7,023 shaders (a chain rebuild). Until a game shows it
- [-] 235 a Stencil8 view of a depth-stencil texture as `MTLPixelFormatX32_Stencil8` with every channel from red, and integer textures sampled as
  such (blade2 a7dff99; the known gaps above list stencil views): not seen in Tears of the Kingdom (no red line 72, "not a valid Metal view", or
  11 in 66 Mac runs and the phone's sessions), and a stencil view samples right only from a shader typed for integers (a chain rebuild). Until a
  game needs it
- [x] batch 80 · 236 the system fonts' `.bfttf` in both word orders, told apart by the magic word (LASRecomp PR #330, b159129): ours read the order
  of a firmware's font archives only; the byte-swapped console files of LASRecomp issue #48 were refused. `host/tests/t_bfttf.c` (a stand-in
  font in both orders and a wrong magic, 3 checks); `DM_TEST_BFTTF_ONE_ORDER` fails 1
- [-] 237 socket Poll / Select wait out their timeout instead of failing at once (nxnative 65d4a4d: a network thread spun at 100 %): Tears of the
  Kingdom makes no socket, nifm or ssl call in the prologue (`DM_HLECOUNT`, 80 s: all 0). Until a game spins on them
- [-] 238 a cube view whose layer count is not a multiple of 6 counted as whole cubes, and CPU-written linear textures off the 256-byte row
  alignment through the re-upload path (AnySwitch 6058d2e, 5e9fcc1): neither line ("a cube view of", red line 82) in 66 Tears of the Kingdom logs.
  Until a game needs it
- [x] batch 80 · 259 the crash report's wait for a stderr lock another thread holds is measured, not counted in sleeps: on GitHub's macOS runner
  its 100 sleeps of 10 ms took 5.4 s and failed CI (batch 79). `t_crash_stderr` gets a case whose sleeps take six times as long (13 checks);
  `DM_TEST_CRASH_COUNTED_WAIT` fails 1 (6.1 s)

GPU work a frame (230: less of it gives the CPU its clock back):
- [x] batch 81 · 239 early fragment tests for the fragment shaders whose control section sets them (+0x718 bit 0, the driver's EarlyZForce; after
  MacNativeGames PR #222 and nxport, MIT): `dmsh_build.py` passes the bit as the job's `early_z`, `dmsh_xlate/Program.cs` as `earlyZForce`, and the
  translator's SPIR-V gets EarlyFragmentTests, which spirv-cross writes as `[[ early_fragment_tests ]]`: 1,677 of the capture's fragment shaders.
  Metal otherwise tests depth after any fragment function that writes memory, so the occluded fragments of every material shader (each stores a
  tile flag) were shaded and hidden surface removal was off for them. Chain ba19de4f1d15 → f9e7d3ecf49e. On the iPhone (Great Sky Island at
  120 Hz, sustained after the first minute, the same code on both chains): 55.4 / 56.4 → 59.5 / 59.6 fps, GPU 13.7-13.9 → 12.9 ms a frame, the
  translation thread 1.81-1.93 → 1.94-1.99 GHz, frames shown at 16.7 ms 86-87 → 97-98 %: the GPU's smaller share of the power budget went to the
  CPU (230). The pictures the same (the prologue's cave and torch, the island; nxport's unlit tile classification on 1.1.2 not seen). The Mac's
  island: GPU 6.93 → 6.80 ms at 60 Hz. `gfx/tests/t_early_z.py` (the translator with and without the field; the library: every pixel shader has
  the attribute exactly when its control section asks); `DM_TEST_EARLY_Z_OFF=1` fails 1
- [x] batch 83 · 240 the scene size changed while the game runs (NativeTOTK 1723fed, BennettStaley/NativeTOTK-Apple-Silicon, MIT, written anew):
  the game reads the page's size only when it picks one (0x585da0, on a mode change), so `games/totk/frame_step.c` (`scene_latch_step`) sends a
  size asked while it runs down the game's own mode-change path once. Two code caves of the option scene (rules.py): 0x585e48 also keeps the
  renderer (x19) at the host page +48, and 0x580920 ORs the page +56 into the display state's flags, which the per-frame update tests for 0x280
  before its forced pick (0x580b28 → 0x585bd0; x10 dead there: every path calls first). At a present the size goes to +16 / +20, the renderer's
  cached mode index (+0xcd0, only compared) to -1 and +56 to 0x80, cleared once the index shows the pick (or after 600 presents). The command
  channel's `scene <w>x<h>` (`totk_scene_request`), up to the larger of 1600x900, the boot size and the heap overlay's note. The Mac's island,
  handheld: the scene's depth targets 1280x720 → 960x536 → 1600x896 → 1280x720 in pass dumps, each the next frame, and four switches moved the
  footprint by under 64 MB, so 9a92046 (targets of a size the game left released) is not needed here. The iPhone 17 Pro's island: each size
  picked the next frame, the footprint 3,574 MB before and after; the dumps' depth targets 1024x576 (before any request, 1280x720 on the page),
  960x536, 1280x720 (1600x900 asked) and 1280x720: two of the four at 0.8 of the page's size, the sign 131a set for looking at the game's own
  dynamic resolution again (on the Mac every dump had the page's size). `check_render_size.py` +5 (the renderer
  at +48, the flags with and without 0x80; the original ignores both); t_frame_step +14 (the latch, a live size's bounds);
  `DM_TEST_SCENE_LATCH_NO_KICK` fails 2, `DM_TEST_SCENE_LATCH_NO_CLEAR` fails 3. What it allows: 190 (960x540 holds 59.6 fps on the island
  against 56.0)
- [-] 241 multiplies and adds fused in vertex stages, relaxed instead of safe math (nxport: ~12 % less GPU in Hateno; fusing fragment stages gave white
  specks): nothing to fuse here. nxport fuses the FMUL / FADD pairs Ryujinx marks NoContraction, written through spirv-cross's helpers; in chain
  f9e7d3ecf49e none of the 16,099 vertex shaders has one (all 3,103 files with a helper are fragment shaders, 5,568 calls), and the offline compile
  with `-fmetal-math-mode=safe` already fuses a plain a * b + c (a scratch kernel: its bits equal fma's for 65,536 triples whose fused and rounded
  results all differ; with `#pragma clang fp contract(off)` they differ). t_fpcontract's note that safe math fuses nothing holds for a compile
  at run time (MTLCompileOptions), not for the game's offline libraries
- 217 (batch 84, opt-in) after nxport's small-pass kernels (`tiny_kernels.rs`): a render pass ~40 µs against ~1 µs for a dispatch there; on the
  iPhone the kernels did not take GPU time off

CPU energy a frame:
- [-] 242 a uniform range's private copy only as long as the shader reads (DavidZande b9f8b16 after tvc97 PR #159 / nxnative f5646ef; MacNativeGames
  PR #257: Breath of the Wild on a Mac, render thread 88 → 65 % busy; on 1.4.3 most bindings read under 2.4 KB): our bindings are about that
  short already (the snapshot line: 802 bindings, 1,414.8 KB a submission, 1.8 KB each), and the private copies take 4.4 % of the translation
  thread on the Mac's island (Time Profiler, 2026-10-23); the extents would need a new reflection field from the chain. Not now
- [-] 243 guest loops that spin on memory without a system call allowed to sleep (nxnative 99c3f91: Paper Mario on a Mac, CPU energy a frame about
  halved; 843 such loops in Tears of the Kingdom's main). Measured first: on the phone's island (batch 82's runs) the two ModuleSystemWorker
  threads take 330-345 ms/s each and yield 150,000-200,000 times a second each (the power line's `most yields`), the main thread 383-394 ms/s.
  Time Profiler on the Mac's island (10 s, 2026-10-23): no guest loop gathers samples (the busiest 256 bytes of guest code, main+0x148600, hold
  1.1 % of the guest-code samples of all threads), and the workers wait with yields (svc 0xB), not in loops without a system call. nxnative's own
  count: 701 of the 843 are one inlined atomic decrement retried after another thread changed the count. Nothing here for the rule to take
- [-] 244 the border-colour pipeline variant's key cached (MacNativeGames PR #286: `pso_get` 11.7 → 3.4 % of replay on an M4 Air): on the Mac's
  island `border_variant` is 0.4 % of the translation thread (`pso_get` 4.8 %). Not worth a cache here
- [x] batch 82 · 245 the Metal calls on a worker thread of their own (nxnative PR #62, `nvn_rec.h`: Breath of the Wild on an iPhone 17 Pro Max, submit
  thread 8 → 3.3-4.5 ms): our encode stage (`nvn_render_stream.h`: render encoders on a second thread; it lost on the M5) is on by default on iOS
  now. On the phone's island at 120 Hz, two pairs: sustained 60.0 / 60.6 fps against 59.6 / 53.4 (the second pair warm, thermal state 1), frames
  at 16.7 ms 97 / 98 % against 98 / 74 %, the first 25 s 84.0 / 87.9 against 76.9 / 78.1, the process's CPU energy 1.22 / 1.20 against 1.39 /
  1.31 W: under the power split two threads at lower clocks do the frame for less. Off elsewhere; `DM_NVN_RENDER_STREAM=0 / 1` chooses. t_rstream
  +3 (`rs_default_on`); `DM_TEST_RS_DEFAULT_OFF` fails 1. PR #62's wider worker (copies, dispatches and commits too) not done
- [x] batch 81 · 246 the player's pipeline list written on a serial queue of its own (AnySwitch c483c04): the first new line read the seed and the
  list (6,365 rows for Tears of the Kingdom) into a set, and every one flushed the file, on the submit / translation thread. `gfx/tests/t_psouser.m`
  (with the queue held a recorded line is not in the file yet, then once; repeats left out; 200 lines from two threads; 4 checks);
  `DM_TEST_PSO_USER_INLINE` fails 1
- [x] batch 81 · 247 `passdump` returns before reading the frame number when off (shh632058099 e460049): the same answers, an atomic load less a few
  times a draw; no test of its own (nothing to break that a test could see)

Pacing and hitches:
- [-] batch 81 · 248 the real-time computation declared covers 1.1 × the 120-frame peak as well as 1.25 × the p90 (nxnative 65d4a4d: p99 63-127 →
  21 ms in Dark Souls Remastered): `DM_RT_PEAK=1` (`nvn_rt_sample`; t_pace +2, `DM_TEST_RT_NO_PEAK` fails 1). On the phone's island (239's chain)
  58.4 fps, 94 % of the frames at 16.7 ms, against 59.6 and 98 % without it (the main thread's declaration went to 1.84 ms): not a default
- [-] 249 `DM_PRESENT_INTERVAL=display` measured with product60 at 120 Hz (MacNativeGames issue #136, an iPhone 17 Pro Max: the game switched its
  interval 920 times in 18 minutes, 43-61 presents a second; one frame a tick held about 65): on the iPhone 17 Pro's island (239's chain) 55.7 fps,
  85 % of the frames at 16.7 ms, against 59.6 and 98 % in the game's own interval mode (it presented at 60 Hz). Not taken
- [x] batch 81 · 250 the frame step averages the intervals before its floor (MacNativeGames' Breath of the Wild path: clamping each made it run 6 %
  fast at about 95 fps under 120 Hz): `frame_step.c` takes the mean of the last five present intervals, then keeps it between 1/120 and 1/15 s
  (presents 6 and 11 ms apart in turn: 9.0 ms, 9.9 before). `games/totk/tests/t_frame_step.c` +3 (53 checks); `DM_TEST_FSTEP_CLAMP_EACH` fails 3
- [-] 251 first-sight pipelines (totkrecomp bbf0fd4, an 8 ms budget a frame on the submitting thread, the rest in the background with their draws
  skipped: 8,024 → 84 skipped draws in the prologue): a budget only limits skipping, which we declined; our first-sight stalls are 184's
  (10 frames, 730 ms in a walk, a game over and its reload)
- [-] 252 the frame step's trials leave out one-off hitches and want the late frames spread over the window (LASRecomp f99186b): the host's frame
  step (`DM_FRAME_STEP=vsync`, 133) is off by default in both games. With 133, if it becomes a default
- [x] batch 81 · 253 pipeline keys with the write mask and blend bit of the colour targets a normal pass lacks cleared (MacNativeGames f3e2f66;
  `pso_key_target_canon`, also on the warm-up's rows): the player's list from the phone 3,645 → 3,617 keys, the seed 3,411 → 3,383. t_pso_canon
  +2 (7 checks); `DM_TEST_PSO_NO_TARGET_CANON` fails 1. MacNativeGames f134a7b's scan budget is not taken: its gain was in shader scans, which we
  do not have

Tools:
- [-] 254 one frame's submissions replayed K times, two settings alternating on the same frame (nxport `--replay=F:K[:M]`, `--replay-ab`): a frame
  replayed repeats its GPU writes into the game's memory (storage buffers, counters, query results, fences), which the next frames read. Our
  phone A/B alternates runs with a rest between (230, 239, 245 told their differences apart)
- [x] batch 82 · 255 `DM_SCHED_STATS=1`: waits per kind with the wake-to-run latency and the time taking a virtual core back (totkrecomp 65a9411),
  and system calls a second by number (AnySwitch 477f749), a `[sched]` line every 5 s (`host/schedstats.h`, fed by `hle_thread.c`).
  `host/tests/t_schedstats.c` (6 checks); `DM_TEST_SCHED_WAKE_FROM_START` fails 1. Tears of the Kingdom's prologue on the Mac: wake-to-run about
  0.01 ms for every kind (max 0.56), but svc 0xB (sleep, yields among them) 300,000-480,000 a second against 3,500-4,300 of 0x34 / 0x35: guest
  threads spin on yields, a lead for 243
- [x] batch 82 · 256 `DM_NVN_PASSGPU_EVERY=<n>`, `DM_NVN_PASSGPU_FROM=<frame>`, `DM_NVN_PASSGPU_FRAMES=<n>` (shh632058099 9ecf169, GirlNet 9dc64f9):
  a submission left out takes no heap and writes no row. `t_passgpu --every` (six submissions, three rows; the first frame ahead, none);
  `DM_TEST_PG_SAMPLE_ALL` fails 2
- [x] batch 81 · 257 `DM_CLEAR_SHADER_CACHE=1`: the iOS app starts with an empty Metal shader cache, as after a fresh install (AnySwitch ec152a8):
  Library/Caches/<bundle id>/com.apple.metal, com.apple.gpuarchiver and com.apple.metalfe removed at boot (`host/shadercache.h`).
  `host/tests/t_shadercache.c` (4 checks); `DM_TEST_SHADERCACHE_METAL_ONLY` fails 2
- [x] batch 81 · 258 the address space a free-signed install gets (LASRecomp PR #238 8dd3116: 59 GB on an iPhone 15 Pro with the memory-limit
  entitlement; previousdolphin: 6 GB above 0x70_0000_0000 without entitlements): the user's free-account install (the increased memory limit,
  no extended virtual addressing) reserves the whole 6 GB on the iPhone 17 Pro, iOS 26.4 ("iOS reserved 6144 MB of 6144", 2026-10-23). The note in
  `hle_svc.c` that 6 GB failed there dates from before the memory limit was signed in (65)

Open:
- [-] 72 the blood moon crash: after 1, the stack logging of 40 and 47, dump the threads right at the skip; with 130's movie hold, the skip with the
  hold, without it (`DM_INACTIVITY_FPS=0`) and with the 30 fps product (needs a blood moon in play: the user). 2026-10-22: skipped with the
  hold on the iPhone (the 60 fps product, 5ee1644), no crash (the user); left: a skip with the 30 fps product. Closed 2026-10-24: the user
  judges it an earlier crash that happened to land at the skip

## Evidence the open items relied on (2026-10-23)

- 94a: totk-mobile (`nvn_texture_rendered.inc`, `nvn_texture_writeback.inc`) writes back a GPU write after 30 frames without another, at the end of
  that submission, with per-page hashes; ours does only on `nvnMemoryPoolInvalidateMappedRange` unless `DM_NVN_WB_IDLE=1`. The Great Sky Island
  grass had a second, proven cause, the border colours of 161 (done): judge the write-back against the console footage with 161 in.
- 97: LASRecomp PR #203 (364b383): on an M3 Max a plain present every 33 ms let macOS drop the GPU to 338 MHz (frame 20 ms instead of 7, locked at
  30 fps); `presentDrawable:atTime:` kept the clock, `afterMinimumDuration` did not. Our keep-alive kernels are Mac only.
- 132: totk-mobile `nvn_wide_line.inc` expands the wave program's lines on the GPU.
- 61d: MacNativeGames' job queue sets the time-constraint policy on its translation thread: without it, replay 6.6 → 13 ms.
- 62 (a device run): T5eng: about 6 GiB of address space below 2^39 on iOS 27, arm64-iphoneos metallibs (MSL compiled on the device gave argument
  encoders of length 0), a translation thread (frame p50 22.9 → 16.67 ms on an iPhone Air), handheld 1280x720 (2-3 ms GPU and 100 MiB less), A/B
  runs need a cooldown (60 fps cool, 30 fps hot). totk-mobile: 12.5 GB reserved with extended-virtual-addressing on an iPhone 15 Pro and M1 iPads
  (exit if above 2^39), the stock application pool, and "do not push the large resource files with devicectl directly" (it can exhaust the Mac's
  memory and disk). Bayonetta 3 on an iPhone Air: 1568x720 → MetalFX 2736x1256, median 17.1 ms, GPU 9.4 ms, footprint 2.4 GB, thermal 1, no jetsam;
  a present buffer lost to GPU recovery at launch. MacNativeGames e099353: ~8,800 kept metallib mappings filled an A16 iPad's 11,400 vnodes (103).
- 62g: bayonetta3 exports the union of all fragment inputs from every vertex shader (2,045 libraries against a 14-location interface, no pipeline
  failure in 181 new pairs); TotK's fragment shaders use 24 locations.
- 62h: totk-mobile `nvn_texture_alias.inc`: Great Sky Island has 354 targets, 373 MiB of layouts in a 111 MiB pool; a heap of 1.25× the pool
  places all but 3 MiB.
- 64 (the next game; the open sub-items carry their own sources): from 10-20, add-on content mounts (upstream PR #69), UE4 / CRIWARE notes
  (d2r-port-radar), `Downsample` as a resolve, VideoToolbox VP9; the 47 NVN functions bayonetta3 has and we lack were split into 64a / 64a2.
- 65: BotWRecomp `ios/PORTING.md` (Xcode made a Personal Team profile with increased-memory-limit) and mush8 `make-ios.sh` (FREE=1 keeps it) say a
  free account can sign it; `ios/tools/project_free.py` strips it and `ios/README.md` says it cannot. MacNativeGames played 1.4.3 on an iPhone 14
  and an A16 iPad "signed with a personal development team in Xcode", with the entitlement (3.6 of 4 GB used). One signing attempt decides
  (`embedded.mobileprovision`, `os_proc_available_memory`). Decided on 2026-10-21: it can (65).
- 66: upstream 46cb318 (issue #57: a Switch Pro on iPadOS 27.0.1 recognised as Nintendo and still swapped); Kamisorara 55c6fba is the same fix.
- 68: MixWithOthers from upstream 9393b00, ours 46b63f4, never run on a device. 69: the overlay's game over flow is checked by bytes only
  (`games/totk/tests/t_romfs_overlay.py`). 70: its metallibs are built in `work/build/ios/dmsh_b/b-bab8d4746968`; the last device builds recorded
  are from 2026-10-16.
- 71c: BotWRecomp `fps.cpp` (ideas only) lists Breath of the Wild's 30 fps constants and the cutscenes that crash above 30 fps; FPSLocker says
  gameplay speed, physics, UI and logic follow its frame step alone.
- 72: none of the projects had anything on the blood moon crash (10-20). Leads: `WaitLightEvent` trusts a 0 result (svc 0x34 type 3, fixed in 1);
  Tears of the Kingdom makes threads with stacks as small as 4 KiB and 16 KiB host pages make 4 KiB guard pages writable (40); FPSLocker marks its
  cutscenes "must be blocked to certain framerate to not cause issues" and our crash follows a skipped pre-rendered movie (130's hold); the guarded
  backtrace (125) and the frames-stopped dump (107).
