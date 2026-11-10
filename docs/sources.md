# Sources

The projects this port draws from, how far each was read, and what was taken or left. Fetch them into a scratch repository, read only,
never run their code; port by hand with a test per item, check our code first (several fixes are already here in another form), and credit
the source in the commit and the README. Licences decide what may be taken: MIT, ISC and Unlicense code may be rewritten here with credit;
Apache-2.0 (totk-mobile) is reimplemented or kept with its licence and NOTICE; GPL and MPL projects (mush8, yuzu, Atmosphère, Cemu's parts of
BotWRecomp, strato, IPSwitch) and projects without a licence give ideas and facts only, never code. Ryujinx, whose shader translator we build
on, is MIT.

## How far each was read

The last commit read of each (October 2026); the next sync starts there. Older markers: the history of DEVELOPMENT.md. Look for new projects in
d2r-port-radar first. The review notes with each source's commits and numbers: the merge notes of each review (not in git) and
`git show 66fa170:DEVELOPMENT.md` for those of 10-19 and 10-20.

d2r-mobile and its derivatives (MIT, keeping jhaizhou's LICENSE; the derivatives use the old `dm/` layout):

| Project | Last read | Notes |
|---|---|---|
| [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) (upstream, fetch only) | main 6a3f1ee2 (11-04, v2.3.0; PRs to #248, issues to #252); `android-crash-handler-safe` f755c9ab, `camera-genshin-controls` 311344d0 (10-30), `experiment/over-the-shoulder-camera` f23047f7; `android-pipe-guard` d4275cd, `android-warm-seed-pack` 9625ef3 | v2.0 b811b90 (Android and HarmonyOS on a Vulkan engine, Korean, Xbox layout, iPhone Duo); since: Adreno 740-799 rewrite policy, subgroup helper inlining (#156 open), BC4 / BC5 8-bit fallback, a persistent pipeline crash guard (2428e5b3), a crash handler with a signal-safe first half and a 3 s limit (3ebb29bd), the rewriter without a red zone for Android (`patch_nso.py` `NOREDZONE` / `sp_frame`); PRs to #248: #144 (MetalFX) closed unmerged on 10-28 and continued as PowerBeef/d2r-metal (MIT), #156 closed on 10-30. d2r-mobile/d2r-mobile is a rewritten copy of its history up to 21cf97b |
| [seedds/nxnative](https://github.com/seedds/nxnative) | main 1794a35 (11-08) | PRs and issues to #1783; `chain-refresh` gone. Its history was rewritten on 10-22 (b932024's tree is 5dae4db). No Android port (PR #447, NVN replay on Vulkan, closed 10-28 as outside its Apple scope: RogelioLB/nxnative-android below); its "Vulkan layer" runs Switch games that use Vulkan on Metal. seedds/smbw-mobile is the same repository renamed |
| [ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames) | main 9fe3fdc (11-06) | Tears of the Kingdom 1.0.0 / 1.1.2 / 1.4.3 on the Mac, the iPhone and Vision Pro; PRs to #1917; no Android (#151, #357) |
| MacNativeGames forks | 10-29 | tvc97 main 67cf3da (11-03; its own work: cheats only), `poc/nxnative-perf` e8bd37d; DavidZande/MacNativeGames `performance` a60cdb3 (unchanged); BennettStaley/NativeTOTK-Apple-Silicon 0f34663 (11-01; render targets carried across a resolution step, 38a1348); dnlflores/MacNativeGames ff8b194 (Breath of the Wild on the iPad; a GPU clock probe, ff8b1947); NicholasDominici/totk-ios b3de471 (its aspect data is GPL-3.0); colonnellotong/MacNativePorts d2e1e6d (unchanged); Sacrappy/NativeTOTK gone (404) |
| [T5eng/nxnative_totk](https://github.com/T5eng/nxnative_totk) (MIT with a note) | 057575d | Tears of the Kingdom 1.4.3 on an iPhone Air (iOS 27.0.1) as patches on MacNativeGames |
| [atone/nxnative](https://github.com/atone/nxnative) | e3144b7 (an upstream merge only) | Pro Controller motion and HD rumble |
| [chayan-bit/AnySwitch](https://github.com/chayan-bit/AnySwitch) | 29d88a4 (10-27) | base nxnative 23a11da; render-target heaps in any pool, heap members without lossless compression (8ba3c71), `DM_INTERNAL_RES` |
| LNPANDY/NX2Apple (was nxnative-romfs-archive) | 3dbb450 (11-08) | MIT with its RomFS archive's copyright; nxnative pinned (`.upstream-ref`) plus 64 patches, its own work front ends; a one-click Mac app; issue #23: an A20 Pro limit of 8191 MB |
| [Nishikori-Yui/lordran-native](https://github.com/Nishikori-Yui/lordran-native) | 85220ca (10-27); PR #40 419f0dc | Dark Souls Remastered, from nxnative; pool base snapshots at record time (c400b58) |
| [GirlNet/d2r-mobile](https://github.com/GirlNet/d2r-mobile) | `codex/p5r-runtime` 4062bf4 (10-27) | Persona 5 Royal |
| [jhaizhou-ops/S2Arm](https://github.com/jhaizhou-ops/S2Arm) (Apache-2.0: written anew) | main 4c0bec5 (11-08, v0.1.0, 7 commits: read as a tree) | the upstream author's one app for a library (211 games surveyed): NVN / Vulkan / OpenGL straight to Metal from nxnative's line, Ryujinx's GPU emulation as the fallback; Tears of the Kingdom 1.4.3 in bring-up there (wide screen on iPhone and Duo); Android in progress |
| [huajiaoshu520/acnh-mobile](https://github.com/huajiaoshu520/acnh-mobile) | 2206ea7 (11-08; `ios18-compat` gone) | Animal Crossing; its base is now nxnative e72a065 |
| ha7rock/blade2-mobile | d0ce642 (11-07) | Blade II, UE4 4.17 |
| enginefuture/bayonetta3-mobile, enginefuture/dr3-mobile | e5ce116, a31c36c | dr3: Diablo III on nxnative 9579030, with LDN |
| MZhao-ouo/Touchable | f0bf368 (10-30) | Rhythm Heaven and Silksong (Unity: its render height, capped by the 1920-wide guest window textures); touch controls; MIT |
| kuosuko/tomodachi-native | 5587d1b | Tomodachi Life, SDK 20 |
| armyja/nxnative-aa-port, armyja/nxnative-cuphead | df997fa, a393de4 | Ace Attorney (Unity, RE Engine); Cuphead |
| shh632058099/d2r-mobile | e460049 (10-23) | iOS |
| xinerqu/ns2ios-shell, Xc1Ym/LumaNX, IMTHECOOLEST/NXNATIVE-1.43.1 | d3eceaa (11-08), 9fbca65 (11-06), 53c3875 (10-31) | an IPA packed on Windows (its table of signers that keep the increased memory limit, ios/README.md); a Mac GUI; a 1.43:1 view |
| kxn/d2r-mobile `codex/*` (`codex/android-native-backup-20261019` 7091475b, 10-28), kxn/nvn-kgsl ed090cb, n3crosis, TIMYW2K `lang-voice` 2d9ac3a, ghostyshell/nx2apk d0b5267 (gone, 404 on 11-08) | 10-29 | Android measurements on an Adreno 735 (the translation on the calling thread 43 against 32 fps); NVN on KGSL; a browser IPA packager; portrait D2R; nxnative for Android with Darwin shim headers |
| littckerQiu/witchspringr-ios | 2a0e59d (11-03) | a fork of this repository with an nxnative overlay for WitchSpring R |
| dodola, n2ray, tib-kpl (d2r-mobile forks) | 69b1b09f; main e92f417 (11-02, merges only), `generic/android` 60c720f, `review/upstream-183` f697868f (10-29); ef9c13c (11-04) | dodola: the newest write per 64-page block, an ordered NVN worker |
| hyc1743/super-mario-odyssey-ios | 3ef14d3, `codex/nxnative-verify-once` 103329c (10-22) | |
| q520asdf0123/nsCHaojIMaLiAo, DanielZhangyc/smbw-mobile | 10-22, 56737aa | Super Mario 3D World on upstream; Super Mario Bros. Wonder on the Mac |
| realskyrin/ns-game-apple-arm-port | 4d2986e | MIT since that commit |
| yokeyhh/switch-port-to-apple, aerolithink, bluewater0121 | 8e1d2af, 43cde9a, 30ca74e (10-22) | a porting playbook; two A12Z iPad Pro D2R ports |
| Johnoooob/nx-port, Kamisorara/d2r-mobile, 4ppp44/d2r-mobile-ios17 | 3e6366b, fae8c68 (11-02: reset to upstream v2.2.4), cb21bec | 4ppp44 (an iOS 17 build): argument blocks reused across passes, the driver segment filled per vec4 offset, capped speculative pipeline workers |
| TommyLau/botw-mobile | gone (404 since 10-17) | its commits named in this file can no longer be fetched |

Independent projects:

| Project | Last read | Notes |
|---|---|---|
| [c22dev/mush8](https://github.com/c22dev/mush8) | 35fed4d (11-08) | **GPL-3.0: ideas and behaviour only.** Mario Kart 8 Deluxe in Rust; its IPA is ad-hoc signed with the increased memory limit for signers that read the app's own entitlements ([ios/README.md](../ios/README.md)) |
| peiikai/LASRecomp (MIT for its own code, with a note) | main 579fdad, `beta_6` 4756d2a (11-08) | Link's Awakening static recompilation; the GPU overlap merged (#333), Vulkan executor, iOS (PR #238 dac664c: "Match display" render-target scaling); hjqcan's fork is a copy |
| Apolinario-coder/SkyRecomp (MIT with a note) | ad649b4 (10-31) | Skyward Sword HD; much of it is LASRecomp's |
| IshaanKalra2103/BotWRecomp | 2b84b82 | MIT; its Cemu-derived parts (MPL) and `fps.cpp`: ideas only |
| [OJZen/totk-mobile](https://github.com/OJZen/totk-mobile) | 6d4c1d3 (10-22) | Apache-2.0 (its `core/` is upstream's MIT code); Tears of the Kingdom 1.4.3 on a Mac and M1+ iPads |
| nxporting/nxport | main fd4a248 (11-07) | MIT, Rust, Tears of the Kingdom 1.1.2 only; a Vulkan backend, frame capture with pass-by-pass replay against both backends |
| itzzslimechisel/totkrecomp, fedebuonco/nxrt, MayBAMemory/nvn-metal | b1b3d2d, c41d913 (10-29), 10-23 | MIT (Windows through dynarmic and Vulkan); Unlicense (a runtime for static recompilations, macOS only since 20360ec); MIT (Kirby) |
| [catoncat/d2r-port-radar](https://github.com/catoncat/d2r-port-radar) | 489adac (11-08, 458 entries) | an index of derivative ports |
| masagrator's FPSLocker-Warehouse, alanyangcn/smbwonder-ios | | no licence: facts only |
| vecStardo/VA-11_ns2ios | | GameMaker through GLES, no NVN: little to take |

Android ports (the tenth review, 2026-10-30; none of their code was run). Their NVN → Vulkan backends surveyed the same day for 301e (facts
taken, nothing copied): nx2android (AHardwareBuffer pools in 256 MB chunks, Ryujinx SPIR-V with one set per stage, VkRenderPass, a timeline
semaphore, command buffers cut every 128 draws: Adreno refused one of about 700 draws, LRZ and binning workarounds), d2r-mobile 2.1 (pools
copied per draw, one set by binding × 6 + stage, load-time SPIR-V rewrites for the Adreno 740-799 policy and helper inlining for the 750-799
compiler, a pipeline crash guard), PKShield (exported-fd / mremap / mirrored pools, push descriptors, dynamic rendering, a shader translation
on the phone), RogelioLB/nxnative-android (copies into an arena, Adreno 619 fixes: a vec4 component by dynamic index, unwritten varyings),
mamcjr's Recomps (a placed heap only on Mesa's driver, the stock driver copies), kxn/nvn-kgsl (NVN on KGSL, no frame yet):

| Project | Last read | Notes |
|---|---|---|
| [shirai91/nx2android](https://github.com/shirai91/nx2android) | main 4689378 (11-08) | Its README: "MIT, as nxnative" (no LICENSE of its own; `nxnative/LICENSE` is seedds's). nxnative 6034fca as a subtree plus an Android runtime (Vulkan, NativeActivity, AAudio, MediaCodec, an APK writer of its own); 18 games with Android profiles on a Legion Y700 (Adreno 830). Its forks with work of their own: iamfaraz `s26-ultra` (Adreno 840), GabZoFar `feat/links-awakening` (the Android Emulator on a Mac, deebab4), parsifal295 `vulkan11`, freapsy PR #7 (a KGSL power hook, Turnip only); since 10-30 mleo2003 `perf/*` (pools backed on first GPU use, a spin before host waits, 11-07) and game forks (kianborg, RibhuNoodleBoodle, DeathKhan, sid350, zouyoujia, kalasyan) |
| mamcjr/TOTKRecompAndroid, BOTWRecompAndroid, LASRecompAndroid | 58d39a5, 7bb5bc9, 82da18b (11-08) | MIT (Peiikai) for LASRecomp's own code; mamcjr's additions state no licence: facts only. Static recompilations on Vulkan; BOTWRecompAndroid 4fc0d28: HyperOS's gralloc refused AHardwareBuffer pools (-129, -22) |
| simipoB/PKShield-Android | be5b6d3 (11-06) | MIT. Pokémon Shield on the d2r-mobile engine; pools by host import, else an exported fd mapped at the pool's address (`nvn_vk.c`), then `mremap`, then a mirror |
| akeem000-ship-it/nxnative-android, RogelioLB/nxnative-android | b490eed (11-01), d23d7a3 (10-29) | MIT. A host with no graphics backend (thread slots through `tpidr_el0`, 086bd8a); NVN replay on Vulkan (Unity's thread stack reported without bionic's guard page) |
| Desm0nt/doom-port | 51e4246 (11-07) | MIT. DOOM 2016 on the d2r-mobile engine (Adreno 750, Turnip) |
| AurelioB/totk-android | 670b71d (11-07) | MIT ("MacNativeGames contributors"). Tears of the Kingdom 1.4.3 on Android from MacNativeGames, kitcatkode's `android-port` and d2r-mobile, with Mesa's Turnip bundled (no stock Qualcomm driver passed its start-up check: no `VK_EXT_external_memory_host` or dma-buf); Adreno 740 30 fps, 830 42-60; `games/totk/android/VULKAN.md` |
| triasbrata/nxnative-android, Desm0nt/diablo3-port | 14868f5 (11-08), 0545886 (11-06) | MIT. nxnative on Android (FF XII, Tomodachi Life; Adreno 825); Diablo III from the DOOM port (write tracking against `vm.max_map_count`) |
| igawa6/Raitchi, VinniGamgee/TOTK-recomp, valentderah/totk-pc-recomp, NotRexed/SkyboundTotk | | Android, with GPL parts: facts only; C generation, not running yet; Windows (MIT); Windows on WebGPU (MIT) |

Android and HarmonyOS (10-23 night): upstream v2.0 runs D2R on both through its NVN → Vulkan engine (`dm/gfx/vulkan`, from biglawine's
HarmonyOS port); nxnative has no Android port. The radar (c7002ad) lists about 30 Android or HarmonyOS ports, nearly all of D2R
(wdjwxh/D2R-Mobile-Android, Timqt/d2r-android, akari-sora/d2r-native AGPL-3.0, …); of other games wdjwxh/Taikou5-mobile, todwang96/rhythm-heaven-android,
akari-sora/taiko-android (MIT) and rootrd/dontstarve-harmony; none of Tears of the Kingdom. For ours: the host calls 89 backend functions, 41 of
which upstream's Vulkan engine lacks, and 26 of the 60 host sources use Apple-only APIs.

New on 2026-11-08 (eleventh review, read only where a finding came from): Thijs2004/nxnative-mkw b1c5e4e (MIT; memory touched before loading), PowerBeef/d2r-metal (MIT, upstream's MetalFX PR), leeashine/ot2-mobile (MIT, Octopath Traveler II on an iPhone 18 Pro), kencalx/Nintanium (MIT, os_signpost lanes), yuchanns/nxnative-tvos (MIT), Poshandka/AnimalDuo (MIT), Timqt/pokelegza-and (no licence), Switch 2 ports of Mario Kart World and Pokopia, about 30 IPA builders.

Seen, not reviewed: CODPHISH/ns2app (Silksong on iOS), WangTingYeYe/rtg-mobile, rootrd/dontstarve-harmony (NVN → Vulkan), hitsmaxft/nsp-dawrin
(GPL-3.0: a synthetic NSP that CI could use).

## Reviews and the plan items they produced

- 2026-10-17, upstream and the derivatives: ported that day (below).
- 2026-10-19, all references fetched again: ported the same day (below) but for three features, later 63 (the 2K canvas and MetalFX), 67 (gyro
  and rumble) and 62c (BC → ASTC).
- 2026-10-19, wider review (eight reviewers): items 1-72; nxnative 2b31dbd..19d7a70 the same afternoon: 73-77.
- 2026-10-20, wider review (seven reviewers): 78-114. Deep review (each project's own work against its base): 115-141, then every remaining
  finding at the user's request: 142-154. Evening sync: 155-180.
- 2026-10-22, sixth review (performance on the iPhone; four reviewers of the references, one of Touchable, one of Apple's documentation):
  181-193. Afternoon refresh: 194-215.
- 2026-10-23, seventh review (five reviewers) and a deep review of the new Tears of the Kingdom projects (five reviewers, layer by layer
  against ours): 231-259. Below the shaders none of the new projects does more than ours.
- 2026-10-23 evening, upstream v2.0: nothing to port then (see Not taken); its NaN rule became 260 that night, after nxnative 56704a0.
- 2026-10-27, ninth review (six reviewers; the A20 Pro "MetalFX to 1080p at a steady 60" claim is in none of them: nxnative #183 has
  Tears of the Kingdom at about 30 fps on an iPhone 18 Pro): 265-284.
- 2026-10-30, tenth review (four reviewers, after a read of nx2android; the Android version of manosaba is starting): 288-301.
- 2026-11-08, eleventh review (six reviewers, after Xcode 27.2): 303-313.
- 2026-11-09, twelfth review (six reviewers, S2Arm): 314-321 and 313's Tears of the Kingdom on Android part.

The items: [plan-done.md](plan-done.md); the open ones: [plan.md](plan.md).

## Taken from each project

What was ported from each project, by hand and credited where it lives in the code (the README names the projects; this is the list behind it).

- **[d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)** ([jhaizhou](https://github.com/jhaizhou-ops), MIT): the architecture and most of
  the host, graphics layer and verification tools; later the controller mapping, silent no-ops reported, the per-program reflection summary, the
  Metal 4 launch check, the audio notifier's priority and dropped presents for a covered window; for Android, the rewrite blocks that move sp before
  they save a register (no red zone on Linux) and the save's extended attributes in the `user.` namespace.
- **d2r-mobile contributors** [SepheTT](https://github.com/SepheTT) (buffer ↔ texture copy strides, the weak import test) and
  [BossaMelon](https://github.com/BossaMelon) (the declared launch screen).
- **[nxnative](https://github.com/seedds/nxnative)** ([seedds](https://github.com/seedds), MIT) and **botw-mobile**
  ([Tommy Lau](https://github.com/TommyLau), MIT; the repository is no longer on GitHub), community ports built on d2r-mobile: the crash-report,
  dirty-page, RomFS-index and thread-exit fixes, and from nxnative the memory pool lookup index, compatible texture views, unwritten colour
  outputs and frame-timed scripted input, credited in their commits; `tools/game_extract` follows nxnative's `nx_extract`. For Tears of the
  Kingdom, whose system modules are the ones nxnative brought up for Breath of the Wild 1.9.0: the explicit jump-table rewrite (`site_br_tab`),
  the in-process audio renderer, host guest threads, amiibo, play report, oe / hid / fs additions, NVN 55 functions, deferred texture binds,
  growable command-memory segments, the NVN API version setting, shader keys over control section and code, linear textures on the pool buffer,
  the inline-uniform ring and per-stage vertex shader padding; later the autorelease pool for retired objects, the driver segment for vertex and
  compute shaders, clears on a left-out attachment slot, the growable dirty-range table, QueueFinish without present waits, two translator fixes,
  vertex output defaults, invariance and the Metal compile cache; then precise SleepThread, env.txt switches, compressed files stored in the IPA,
  yield back-off (opt-in), targeted wakes, waits on upload completion and buffer releases, a per-thread pool cache, the dirty range kept per
  texture, residency no-op skips, page runs, a spare texture for re-uploads and VP9 row multithreading; then the host race fixes, the c1 guard
  by interval arithmetic, the yield back-off start, the guest CPU power line and the BAHS shader entries; later Maxwell's min / max / clamp results for a NaN operand on
  every GPU (`precise::`), and the SDK's address-region node area cut and two SDK return types; then the store-decision records for ten
  attachments, page protection under the pool-slot lock, an ID's cached view dropped with its texture, the AudioOut notifier's disarm and Game
  Mode off on the Mac; WaitSynchronization ended by the signal that woke it, credited in the code; the translator's split storage words read
  whole; a pool's buffer made only over writable pages.
- **[tomodachi-native](https://github.com/kuosuko/tomodachi-native)** ([kuosuko](https://github.com/kuosuko), MIT), a fork of nxnative: the
  variant of the duplicated colour slot handling that keeps the slot the pass writes, the memory snapshots (`DM_MEMSNAP`), the FMZ multiply in
  the shader translator, the attachment copies for draws that sample what their pass draws and code options applied at load.
- **[MacNativeGames](https://github.com/ly05010419/MacNativeGames)** ([ly05010419](https://github.com/ly05010419), MIT), a Tears of the Kingdom
  port on the same lineage: which RomFS files hold the shaders, virtual cores, clears drawn as quads, BC render targets through an auxiliary
  texture, storage buffers addressed from uniform buffers, stale texture views, the behaviour and pacing of the VP9 movie decoder, the 60 fps
  controller words, the real-time frame step, its uncapped pacing and the scene render size, uniform snapshots, and from its graphics layer the pending-write table, keep-alive, chunked
  commits, render-state filter, upload staging pool, storage-followed and storage-shared textures, GPU write-back, uniform renaming,
  small-texture hashing and once-per-submission checks, layered passes, the encode stage, driver segment fills, block height and D16 clear fixes,
  the GPU memory budget and pool range checks, and the RomFS overlay findings for 60 fps; then the fault guard around snapshot copies, read-only
  rodata, waiting for a save at quit and one game at a time on the Mac, host calls on a per-thread host stack with the frame record back to
  the guest, the live command channel, the sead heap walk and list watch and the soak measurement; then no blending on integer colour outputs,
  empty memory pools, virtual cores handed over by moving their owner and fewer pipeline guesses, ported by hand and credited in the code; the
  Mac shader libraries' minimum system version; and shaders that differ only in control bytes the translator does not read translated and
  packed once.
- **[LASRecomp](https://github.com/peiikai/LASRecomp)** ([Peiikai](https://github.com/peiikai), MIT), a static recompilation of Link's Awakening:
  its rule that a texture is uploaded from guest memory until the GPU renders into it, behind the CPU-filled textures of Tears of the Kingdom,
  and the order of WaitSynchronization's checks; GPU time as the union of command buffers; the ring of lock / condition variable calls, the late
  condition variable wait and the layout of its CI checks; its SHF / I2I instruction vectors; HSETP2 `.H_AND`'s rule and its vectors.
- **Single fixes from other ports**, credited in the code: [Kamisorara/d2r-mobile](https://github.com/Kamisorara/d2r-mobile) (controller face
  buttons by the printed letter, also upstream's), [jiangjifeng/nxnative](https://github.com/jiangjifeng/nxnative) (scaled vertex formats),
  [GirlNet/d2r-mobile](https://github.com/GirlNet/d2r-mobile) and [lordran-native](https://github.com/Nishikori-Yui/lordran-native) (sampler LOD
  bias and reduction; lordran-native also the alpha test and occlusion counts across submissions), [SkyRecomp](https://github.com/Apolinario-coder/SkyRecomp) (the handles of a WaitSynchronization in thread dumps, user mod folders, RGBA4's
  channel order, as [nxport](https://github.com/nxporting/nxport) found it; nxport also Link's grip on moving ground above 100 fps),
  [BotWRecomp](https://github.com/IshaanKalra2103/BotWRecomp) (game data out of backups, GCSupportsGameMode),
  [4ppp44/d2r-mobile-ios17](https://github.com/4ppp44/d2r-mobile-ios17) (what an iOS 17 build needs), [GirlNet/nxnative](https://github.com/GirlNet/nxnative)
  (the host built for every game on a CI runner), [RogelioLB/nxnative-android](https://github.com/RogelioLB/nxnative-android) (a thread's stack
  reported without bionic's guard page), upstream d2r-mobile's later fixes (no writes for outputs a pixel shader leaves, nearest filters for formats
  not filtered linearly, the on-screen controls drawn again only when they look different), [LASRecomp](https://github.com/peiikai/LASRecomp) and [SkyRecomp](https://github.com/Apolinario-coder/SkyRecomp) (a
  swapchain that survives a lost surface, a pipeline cache kept between launches), [PKShield-Android](https://github.com/simipoB/PKShield-Android)
  (the barrier to host reads at a submission's end), [doom-port](https://github.com/Desm0nt/doom-port) (AAudio's legacy path after a stall).
- **[nx2android](https://github.com/shirai91/nx2android)** ([shirai91](https://github.com/shirai91), MIT as its README states), nxnative's Android port:
  the address-region refusals logged and the node area cut for a Unity 6 sdk build, the rewriter's Android target (thread slots in bionic's
  TLS_SLOT_APP), an APK written and signed without the Android SDK (binary manifest, aligned ZIP, APK Signature Scheme v2), credited in the
  code; its Android facts guide the Android port; later the view's swizzle replacing the texture's, a texture at two colour slots on Vulkan, the
  manifest's configuration changes and the frame rate voted for the window; the subgroup size and gather precision given to the translator for
  Vulkan, and TMML's LOD as unsigned.
- **[S2Arm](https://github.com/jhaizhou-ops/S2Arm)** ([jhaizhou-ops](https://github.com/jhaizhou-ops), Apache-2.0; written anew from what it does,
  no code taken): the dirty tracking's `mprotect` results checked, the argument buffer's length from the reflection, the app's name in ASCII with
  the title per language, the phone's haptics-only engine; the unwritten attachments' stores dropped with the TBDR rewrites off; mid-submission
  commits held while the GPU is busy; texture uploads from a pool not yet resident read through a buffer of their own pages; how a half-float
  target rounds; the on-screen stick and camera ring as layers of their own; runs of short sleeps backed off; MetricKit's reports kept and named;
  the signature's expiry shown in the app; whether the GPU applies a sampler's LOD bias and min / max reduction; the install without local.env,
  the renewal by signing again, the data pushed by content.
- **[mush8](https://github.com/c22dev/mush8)** ([c22dev](https://github.com/c22dev), GPL-3.0; ideas only, no code taken): a stamp of the
  rewriter in the product, the rewriter refusing system registers it does not know, the SDK's abort text at a Break.

## Not taken (and why)

- S2Arm (11-09): its Tears of the Kingdom pacing to the display with the game's own interval (its own A/B: the picture moved 3.99× while frames went
  1.94×, the game ran fast; it agrees with the line above); fast-returning yields, the spin and clock-read gates (183, 243; its Kirby and Diablo III
  notes agree); SignalToAddress type 2 writing value − 2 (Atmosphère master still writes −1); the pass-end barrier relaxed (a per-game key there,
  full by default: batch 151 agrees); DebugMode off (only the translator's memory on the device gained, 200); its generic Ryujinx path (frame-level
  scheduling, transfer clears, the threaded renderer); one app for many games (two games fit a free account's three apps).
- A Metal binary archive on macOS 26 without guarding `serializeToURL` (LASRecomp turned it off after crashes; PR #145 closed).
- Compiling shaders at launch and skipping draws until ready, async shader loading with skipped draws (content drawn once would stay missing).
- Present pacing to the screen refresh (games that step per frame would run fast; only Tears of the Kingdom's real-time step can use it,
  `DM_PRESENT_HZ`).
- SignalToAddress −2 (MacNativeGames dab728a, LASRecomp's `value - 2`, justr4mzi's kind 2): Atmosphère wrote −2 from 7.0.0 until 5f2d713fe4
  (2022-06-08) made it −1, as ours.
- nx-port's clamp of fragment outputs to ±65504 (breaks R32F); GirlNet's per-draw conditional render (a CPU stall).
- nxnative's prebuilt .NET DLLs, Android / Vulkan and MoltenVK runtimes (dodola, imink/hades-ios), upstream's D2R-only parts.
- Upstream v2.0 (10-23): the arena out-parameter of PR #126 is our plan item 185; the Vulkan engine's NaN-ignoring min / max / clamp was first
  judged not needed (on the M5 `fast::min` / `fast::max` return the other operand and `fast::clamp(NaN, 0, 1)` gives 0, as Maxwell), then taken
  as plan item 260 when nxnative 56704a0 found an A20 Pro's `fast::max(0, NaN)` not 0 (one GPU agreeing says nothing of the next); the adaptive present tier does not apply (Tears of the Kingdom picks its present interval itself); the main
  thread's spin before short waits, event-read hooks, pool flags, the rewriter without a red zone, `DM_SECT` and the ucontext macros are for
  Android and HarmonyOS; separate base and update packages with title keys from tickets are in `tools/game_extract`; VP9 on iOS is ours since
  batch 54. For 190: its dropped-frame rule (a run counts only its first 3 frames unless it lasts 1 s) fits; its GPU-time controller skips
  CPU-bound frames, which is how the iPhone's power split looks (230).
- The ninth review (10-27): MacNativeGames' `TOTK_SYNC_LAG` (#680) and frame pipelining (#753, open), our 224 / 227 (declined on the phone; their
  own median improved and p95 worsened); its driver-segment cache (its own A/B found no gain) and present ring (#749: only when the game outruns
  the display); 4ppp44's GPU-time dynamic resolution (190's rule that does not fit); GirlNet's compute batching (no sustained benefit); nxnative's
  offline Metal 4 pipeline archive for now (615-850 MB of footprint while open, Tears of the Kingdom's 1,306 MiB); nxnative 82ab25b's present
  allocator by its own completion (268: here the drawable is taken first, and each of the layer's three returns only after the present that showed
  it, so the allocator three presents back has finished).
- The eleventh review (11-08): upstream 955f274's crash and thread exit and n2ray f697868's SIGALRM (ours, `host/runtime/crash.c`), upstream PR #246's
  staging reuse (ours), software BC decoding (Mali), fec76d1's pipeline guard (no parallel warm-up here), Turnip through adrenotools (a driver
  replacement), LASRecomp's frame generation (7 + 3 ms an in-between frame; the iPhone is GPU-bound), MacNativeGames' synchronous replay (Mac
  only) and mip-chain feedback view 6100b99 (it caused #1698), NX2Apple's and ns2ios-shell's own work (anonymous guest memory measured worse:
  2.97 of 5 GB compressed). nxport's 10,015 listed pipelines cost about 1,640 MB on NVIDIA: the offline archive stays not taken.
- Equal or better here: the Opus table leak (we run the SDK's decoder), macOS 27 fixed reservations (ours are anywhere), a Metal 3 backend, one
  descriptor pool, lordran's deswizzle and in-place re-upload (ours waits for the fence / keeps a spare, 16), the audio mixers and renderers of
  LASRecomp, SkyRecomp, MacNativeGames, nx-port, totk-mobile and blade2 (our in-process SDK renderer is closer), saves (transactional), the condvar
  and WaitSynchronization order, totk-mobile's arbiter (it refuses type 3), per-axis stick clamp, jump tables and virtual pools, MacNativeGames'
  aliased buffer copies, serial history, run-time MSL caches, ultrawide / stretch (154 took the aspect patch), UIScene lifecycle, audio ring trim
  and shader list (its 492 1.4.3 survey shaders are all in our set), T5eng's touch controls (58), iOS metallib flags and `__TEXT` embedding,
  nxnative's torch light (77), render conditions, integer outputs, fast clears, depth clamp, attachment-less passes, MetalFX, uniform renaming,
  menu capture (the overlay), BC render targets (`blk_aux`), render scale (`DM_RENDER_RES` is absolute) and translator phi fix, upstream's D2R UI,
  device lab tools and PR #86, GirlNet's GPU primitive restart, swkbd trimming, linear R8 / RG8 views and atlas re-uploads, LASRecomp's
  16-sampler limit (argument buffers) and stale-program detector, SkyRecomp's 120 fps step, bayonetta3's two signed main images, VP9 as AVC and
  launcher page, acnh-mobile's packaged texture copy (the SDK says ignored), conditional ledger and pool cache, dr3's program snapshot,
  super-mario-odyssey-ios's audio ring and touch ABI, armyja's system clock context, Touchable's texture paths, fs, mixer and NCA extraction.

## Ported before the plan

- Before the reviews: thread exit off the guest stack (014c5b3), crash report stderr trylock (1f1929e), dirty tracking of shared host pages
  (e4292e1), RomFS name index (af27ace), save full-disk robustness (eb86cfc, upstream b94670c), msl_retarget reflections (56fcd40, upstream 1784563).
- 2026-10-17, upstream: copy image stride (86b1315a84cec67cfef0b3eab125dbc3c4489cc4, `nvn_copy_pitch.h`, `t_copy_pitch`; d21348c), `UILaunchScreen`
  (21cf97b; ce466a9). Already here in the same form: weak imports stay NULL (b84657b25735d96a7263a1b4a7402159a17435a5), `DT_RELR`. Not applicable:
  D2R's Mac mouse (098d9f5e24407480da7cf0d21c5d88b871da6949); dc53e7a8eca1f170009b722d390fac7e0d050288 was reverted by 21cf97b.
- 2026-10-17, nxnative: pool lookup by last hit and a sorted index (`nvn_pool_find.h`, `t_pool_find`; d48fb4f), `tex_fit` views, 1D textures as
  one-row 2D, no write mask or blending for unwritten colour outputs (621bd6d), `DM_KEYS` timed by presented frames (9475f85);
  `tools/game_extract` after nx_extract (aa8cc5c…, LibHac 0.19.0 from NuGet, pinned by content hash; checked byte for byte with manosaba's NSPs;
  XCI input and a real personalized ticket not run).
- 2026-10-19 (each with a unit test and a broken version): shader chain 1e41705de87b (686b81e: per-stage driver segment reads, two translator
  fixes, vertex outputs from (0,0,0,1), `-fpreserve-invariance`, `-fmetal-math-mode=safe`, `gfx/tools/dmsh_mtlcache.py`); correctness 06c80bd
  (autorelease pool, c0 fills, block height, D16 clears, packaged texture data, primitive restart, 8-bit index widening, storage follow, textures
  sharing a storage, write-back on invalidate, QueueFinish, the GPU memory budget and pool checks); WaitSynchronization order 4dd984d;
  MixWithOthers 46b63f4; the RomFS overlay 601258f; pending writes across submissions, acquire sync, keep-alive, chunked commits, uniform
  snapshot and rename, the render-state filter, one compute encoder, upload staging, identical registrations, the dirty-tracking cost, layered
  passes. The encode stage was implemented and left off that day (on by default on iOS since batch 82).
- From the early plan list: the AudioRenderer and deferred texture binds (a267ec1), VP9 movies (a8c3ff1, `host/media/movie_webm.c`), small guest
  stacks (40, `host/runtime/hstack.S`), the BAHS shader split (`engines/bnsh/shader_extract.py`, 70cb843, 17766ae, 36), a texture at two colour slots
  (`rt_dup_slot`, through tomodachi-native's variant), face buttons by printed glyph (2; Kamisorara 8429dc1 was the earlier, wrong variant).

## Port when a game needs it

- S2Arm (written anew): IADD / ISCADD `.PO` as A + B + 1 (none of either game's words has it; with the next chain change); float RED / ATOM add as a
  compare-and-swap loop (none in either game); WaitEvent across two NVN queues (ours is a no-op, right for one queue: Ocarina of Time for Switch 2
  read last frame's depth); free space answered from the save's own size and one save under two mount names; guest memory backed by an unlinked file
  (dirty pages counted as file cache, not footprint; S2Arm measured 3-4 s stalls and 6-7.5 GB written in 2 minutes, so only when the pool would not
  fit: 280's 8 GB iPads); Switch 2 games (an RSA-3072 ACID in the NPDM, a 9 GiB pool, 8 cores, a 31.25 MHz tick, svc 0x80).
- Graphics, nxnative d6cd8a812a1047dff8f660f604d0a46033752242: cube textures as 6-layer 2D arrays for cube-array views; compressed render targets
  skipped with a red line instead of a Metal abort. The attachment snapshot for shaders that sample what they write, per shader by a profile key,
  with `DM_NVN_MAGSCAN` (20cba12c4299d77f05b6bbfd3dfa42d6c6035da4, `nvn_mtl.m`); the submission handed to the GPU every 64 draws for games that
  wait for the GPU every frame (`DM_NVN_CUT`, ff74840…).
- Diagnostics: `DM_NVN_KEYDUMP` and sampler / swizzle output in PASSDUMP (734a286328d18404fd5a67785a1e05d2149c5986).
- nn::ro .nro modules: nx-port `dm/host/hle/hle_ro.c` and loader changes (3e6366b…), with `R_AARCH64_TLSDESC` and symbol precedence.
- Older Unity bundles: nxnative `dm/gfx/tools/unityfs.py` (e3cd2ff8ff0eb84a62280ef70e90c60753e546e6).
- Tools, nxnative: nso_syms.py (aa8cc5c0cd8bd5d09bcacb019f7832df3a164452); nso_fnmap.py, carrying cuts to a new build of `main`
  (b5507f2aac7e1d1f5512de750f5810a550162b04, also for a manosaba update); build_ipa_from_nsp.py (0d763ec864344610cc41548a2ad01cd55def3182); DLC
  packing (e452f52b607bdf6f960eb843f20aeb29b803dec8); the app icon from the game's icon (1cd9bfb84b31fad5b413ab20baa977c16600f50a).
- An on-screen gamepad for games without touch: nxnative `dm/ios/Sources/DmGamepad.m` (dc8a49ad7471d3f0b767032a0b755f87a98c1912,
  d41fdd6ed1643ed7f97099f18ab6fc7360786a1b, 4a0f9acbcf8e5acad01b1d6b5919bfb3e3cd0998; the 3D layout b65c8b2cfe43d0ccbc2dcd24068d849ff4eb6baf).
- NVN / graphics from botw-mobile (no longer fetchable): depth and stencil sampling modes (b795ac15d620f42986df3d594e846bf03f83f1ea), mip import
  before partial texel writes (549fb9d7be4362e9b82138a674a5305a722ab1a5), native sampler defaults (767a276615a6c1c1d20a7b98aec17dd170d8e3b3),
  declining fast-clear hints (0d80bff4eec2668441ff4ddd075796e3ec674099), GPU timestamps (9eebd8d746621b851189a7b4679489388911e7b7), one dirty record
  per host page (1d69c8ae9cea2e83584314f8044b55c839c95c9e); nxnative: GPU timestamps on the guest clock (b6b3da202abbd9ca73f45057920c7a9fdfcc0eb6),
  depth / stencil clear mid-pass (7058f43497c6eb26eea351ec4e45db3bf0d417ae), Downsample as a Metal resolve (13ff4f51a10ff6c4207a1e909e430ddd9c0644d3),
  a per-game present interval (9745da2061b09fea0ea524c7d5369ad8e1357539), pool finalize dropping texture ID views
  (799df3c5edd00281c40ae8889a10c55cefe93616, the leak class of a593388); nx-port: geometry shader point-to-quad, alpha test, newer NVN object
  sizes (3e6366b…).
- From the eleventh review: nxnative f5cc73d (NVNbuffer offset and size at +0x18 / +0x20), a9d06b6 (32-byte linear rows on iOS), 92f223a (sRGB and
  linear twins over one storage), cac783a (copy strides as command-buffer state), ffdf67d (clearing every layer); SkyRecomp 598af40 (logic op OR as
  max blending; ours is a red line).
- An ad-hoc signature for LiveContainer (Kamisorara 95d6caa903bc19e602493bb5a5568650d39f52cf).
