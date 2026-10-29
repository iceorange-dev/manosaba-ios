# Switch games on Apple silicon, natively

This repository runs Nintendo Switch games on iPhone, iPad and Mac without emulating the CPU. A Switch and an Apple device use the same class of
processor, so the game's own aarch64 code executes directly. Only the few instructions that cannot run under Apple's kernel are rewritten
offline; the console's system services and its NVN graphics API are reimplemented by a small host on Apple's frameworks and Metal; the game's
shaders are translated to Metal ahead of time.

The games are **魔法少女ノ魔女裁判** (manosaba), Switch version 2.0.4b, and **The Legend of Zelda: Tears of the Kingdom** 1.4.3, which plays
on the Mac and on the iPhone and is newer and less tested ([status](#status-tears-of-the-kingdom-143)). The code is organized so that another
game is a new directory under `games/` plus whatever services and graphics paths that game needs: see [PORTING.md](PORTING.md). Found a
problem? [Report it](#reporting-problems).

It is a fork of [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile), which ported Diablo II: Resurrected the same way; the D2R-specific code
has been removed here.

**No game content is in this repository.** You dump your own copy from your own console; everything derived from it (rewritten modules,
translated shaders, the app icon) is generated on your machine and stays out of git.

## Status (manosaba 2.0.4b)

Verified on a Mac (Apple silicon, macOS 26) and on an iPhone 17 Pro (iOS 26), including the reorganized iOS app:

- boot, title screen, new game, story scenes with sprites, CG, text and UI, the opening movie, a trial including presenting evidence;
- native touch input (one rewriter rule turns on Unity's touchscreen device): scripted touches on the Mac, by hand on the iPhone;
- saving and loading, with transactional commits; iCloud sync of the save files (needs a paid developer account's entitlements; not tested);
- memory over a long session: finalized render targets used to stay alive in the texture ID view cache (about 800 MB within three minutes of a
  trial on the Mac; an iPhone session ended without a crash report after about 40 minutes near the memory limit). Fixed: a 16-minute iPhone
  session through scene changes and a trip to the background stayed between 1.2 and 1.5 GB resident, with no upward trend;
- repeated scene loads: the game may finalize a texture object right after recording commands that use it, and replay used to read the object
  when the commands were submitted (garbage texture formats in the log, now and then a crash in Metal's copy encoder). Fixed: 54 reloads of a
  trial on the Mac without a crash or a warning;
- graphics settings in the Settings app (frame rate 60 / 30 fps, render resolution, MetalFX upscaling): 720p with MetalFX used on the iPhone;
  30 fps not yet tried there. On the Mac the render resolution barely changes this game's GPU load; 30 fps is the setting that lowers it;
- rewrite equivalence: `verify/regress.sh --fuzz` passes on all three modules.

Not verified or known open: compute shader dispatch at run time (the game's compute shaders are Unity's built-in skinning, blend shape, virtual
texturing and probe debug kernels; no dispatch happens in the story or in a trial); 13 NVN functions the game looks up that the layer does not
know (returned as NULL: this build stores the pointers but never calls them); an intermittent texture crash seen early on the iPhone, likely
fixed by an NVN struct size correction (not seen since, unconfirmed); keyboard, mouse and game controller input not tested with this game.
Memory on 8 GB iPads: without the increased memory limit the app may use about 3.4 GB on an iPad mini (A17 Pro); a free account can sign that
limit now (6.1 GB on the iPhone 17 Pro), not yet tried on the iPad mini. A trial's loading adds about 1.4 GB for a moment (the character
render targets), and the game's own memory grows by some 30 MB per scene load over a long session,
so after about two hours of play a trial's loading went past the limit and the system ended the app; restarting the app before then avoids it.
The growth is the game's own: every save load builds a new set of Naninovel's script and resource managers and the old ones stay referenced,
with their parsed scripts and UI objects (a census of the managed heap after 9 reloads of one save held up to 12 copies of the same scripts).
The Switch version is reported to end with an error now and then in long sessions too, and version 2.0.4's notes say such errors were made less
likely.

On Android (plan item 301, under way): on a Redmi K100 Pro Max (Android 17) manosaba runs as an app of this repository's own (no Android SDK:
`android/tools/apk.py` writes and signs the APK) at 60 fps, with sound (AAudio), touch, game controllers and saves, but nothing is drawn yet:
the Vulkan backend is the next step.

## Status (Tears of the Kingdom 1.4.3)

Played on a Mac (Apple silicon, macOS 26) in a window and on an iPhone 17 Pro (iOS 26.4, free developer account, handheld mode):

- the title screen, the prologue with its tutorial and pre-rendered cutscenes (VP9 movies, decoded with libvpx, also on the iPhone), the dive
  from the Great Sky Island and play after it;
- frame rate: the default product runs at 30 fps; a product with the frame-rate rules runs at 60 fps (on the iPhone the game's own controller
  then picks 60, 30 or 20 by its frame time) with a RomFS overlay that `games/totk/romfs_overlay.py` builds from your own RomFS: without it
  the quick menus (the ability wheel) draw over black;
- on the iPhone: on-screen controls (a floating stick, the buttons and a camera swipe; a game controller turns them off), the RomFS carried
  as 1 GiB parts and unpacked at the first launch ([ios/README.md](ios/README.md#on-the-device)), and the increased memory limit signed by a
  free account (6.1 GB on the iPhone 17 Pro).

Not verified or known open: other iPhones and iPads (iPhones with an A14, A15 or A16 have no BC texture support: BC1 is turned into ASTC, the
other BC formats are decompressed, so the game needs more memory there); the iOS 18 (Metal 3) build with this game; stencil views are not
modelled; motion controls and a controller's rumble are implemented but not yet checked with a controller. A crash after skipping the blood
moon movie (Mac, 2026-10-19) did not come back when the movie was skipped on the iPhone with the 60 fps product (2026-10-22); with the 30 fps
product it has not been tried again. Details: [DEVELOPMENT.md](DEVELOPMENT.md#tears-of-the-kingdom).

## How it works

| Part | Directory | What it does |
|---|---|---|
| Rewriter | `tools/patch_nso.py` | Rewrites x18, thread pointer, `svc` and timer instructions into jumps to small patch blocks; the rest of the code is untouched |
| Verification | `verify/` | Proves the rewritten modules equivalent to the originals (structure, per site, per function, gates) |
| Host | `host/` | Loader (rtld), kernel calls, `nn::` services (file system, input, audio, time, movies, fonts, …) |
| Graphics | `gfx/` | NVN on Metal, record and replay; offline shader translation (Ryujinx translator → SPIR-V → MSL → metallib) |
| Apps | `ios/`, `tools/mac_app.sh` | iOS app; Mac command-line host and `.app` |
| Per game | `games/<id>/`, `engines/<engine>/` | Game profile, rewriter rules, hooks; engine-specific tools (Unity shader extraction) |

## Getting started

1. Requirements and workspace: [PORTING.md § 0](PORTING.md#0-machine-and-workspace).
2. Dump the game (ExeFS, RomFS, system fonts) from your console into `$DM_ROOT/data/switch/` ([§ 1](PORTING.md#1-dump-the-game-from-your-console));
   for manosaba the expected paths are in [games/manosaba/game.json](games/manosaba/game.json).
3. Build and check the product, the shaders and the host:

   ```sh
   export DM_ROOT=~/dm-work
   tools/build_product.sh && verify/regress.sh
   # shaders: PORTING.md § 5
   make -C host
   DM_SAVE=$DM_ROOT/run/save DM_TEMP=$DM_ROOT/run/temp DM_WINDOW=1 DM_AUDIO=1 host/dmhost
   ```

4. iPhone / iPad: [ios/README.md](ios/README.md). Mac app: `tools/mac_app.sh`.
5. Android (not drawing yet, see above): `DM_TARGET=android tools/build_product.sh $DM_ROOT/build/android/manosaba.b301`, then
   `android/run.sh build`, `install`, `launch` once, `push-data`, `launch` (NDK r30; [DEVELOPMENT.md § Testing](DEVELOPMENT.md#testing)).

## Reporting problems

Problems are welcome as [GitHub issues](https://github.com/iceorange-dev/manosaba-ios/issues/new/choose). Please say:

- the game and its version, the device and its iOS or macOS version;
- the build: the commit, iOS 26 or iOS 18 (`DM_IOS_M3=1`), and for Tears of the Kingdom the 30 or 60 fps product;
- what happened and how to get there (where in the game, what you pressed);
- on iOS, attach the diagnostics file: touch and hold the app's icon → Export Diagnostics (device, GPU, memory, settings and the last logs;
  [ios/README.md](ios/README.md#on-the-device)). On the Mac, the log: `~/Library/Logs/<game id>/host.log` for the `.app`, the terminal
  output for `host/dmhost`.

Do not attach game files, keys, dumps, rewritten modules or IPAs.

## Documentation

| Document | Covers |
|---|---|
| [PORTING.md](PORTING.md) | The workflow for a new game, from the dump to the device |
| [DEVELOPMENT.md](DEVELOPMENT.md) | Restoring a working copy, test procedures, where the work stands, open work |
| [docs/sources.md](docs/sources.md), [docs/plan-archive.md](docs/plan-archive.md) | Community ports drawn from and how far each was read; the finished plan items cited in the code |
| [games/README.md](games/README.md) | Game directory: `game.json`, `rules.py`, C hooks |
| [host/README.md](host/README.md) | Loader, services, files, environment variables |
| [gfx/README.md](gfx/README.md), [gfx/nvn_metal_mapping.md](gfx/nvn_metal_mapping.md), [gfx/tex/README.md](gfx/tex/README.md) | Graphics layer, shader chain, NVN → Metal mapping, textures |
| [ios/README.md](ios/README.md) | iOS app: setup, signing, data on the device |
| [verify/README.md](verify/README.md) | Rewrite equivalence checks |
| [tools/README.md](tools/README.md), [engines/unity/README.md](engines/unity/README.md) | Tools; Unity-specific notes |
| [third_party/README.md](third_party/README.md) | Bundled third-party code and licenses |

## Legal

Use this only with games you own, dumped from your own console. This is an independent, non-commercial project, not affiliated with or
endorsed by Nintendo or any game publisher. Nintendo Switch is a trademark of Nintendo.

## Acknowledgements

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
  every GPU (`precise::`), and the SDK's address-region node area cut and two SDK return types, credited in the code.
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
  the guest, the live command channel, the sead heap walk and list watch and the soak measurement, ported by hand and credited in the code.
- **[LASRecomp](https://github.com/peiikai/LASRecomp)** ([Peiikai](https://github.com/peiikai), MIT), a static recompilation of Link's Awakening:
  its rule that a texture is uploaded from guest memory until the GPU renders into it, behind the CPU-filled textures of Tears of the Kingdom,
  and the order of WaitSynchronization's checks; GPU time as the union of command buffers; the ring of lock / condition variable calls, the late
  condition variable wait and the layout of its CI checks; its SHF / I2I instruction vectors.
- **Single fixes from other ports**, credited in the code: [Kamisorara/d2r-mobile](https://github.com/Kamisorara/d2r-mobile) (controller face
  buttons by the printed letter, also upstream's), [jiangjifeng/nxnative](https://github.com/jiangjifeng/nxnative) (scaled vertex formats),
  [GirlNet/d2r-mobile](https://github.com/GirlNet/d2r-mobile) and [lordran-native](https://github.com/Nishikori-Yui/lordran-native) (sampler LOD
  bias and reduction; lordran-native also the alpha test and occlusion counts across submissions), [SkyRecomp](https://github.com/Apolinario-coder/SkyRecomp) (the handles of a WaitSynchronization in thread dumps, user mod folders),
  [BotWRecomp](https://github.com/IshaanKalra2103/BotWRecomp) (game data out of backups, GCSupportsGameMode),
  [4ppp44/d2r-mobile-ios17](https://github.com/4ppp44/d2r-mobile-ios17) (what an iOS 17 build needs), [GirlNet/nxnative](https://github.com/GirlNet/nxnative)
  (the host built for every game on a CI runner), [RogelioLB/nxnative-android](https://github.com/RogelioLB/nxnative-android) (a thread's stack
  reported without bionic's guard page).
- **[nx2android](https://github.com/shirai91/nx2android)** ([shirai91](https://github.com/shirai91), MIT as its README states), nxnative's Android port:
  the address-region refusals logged and the node area cut for a Unity 6 sdk build, the rewriter's Android target (thread slots in bionic's
  TLS_SLOT_APP), an APK written and signed without the Android SDK (binary manifest, aligned ZIP, APK Signature Scheme v2), credited in the
  code; its Android facts guide the Android port.
- **[mush8](https://github.com/c22dev/mush8)** ([c22dev](https://github.com/c22dev), GPL-3.0; ideas only, no code taken): a stamp of the
  rewriter in the product, the rewriter refusing system registers it does not know, the SDK's abort text at a Break.
- **[LibHac](https://www.nuget.org/packages/LibHac/0.19.0)** ([Alex Barney](https://github.com/Thealexbarney), MIT; the NuGet package, as its
  GitHub repository is no longer reachable): reads NSP, XCI and NCA containers in `tools/game_extract`.
- **[Ryujinx](https://ryujinx.app)** (MIT; source on the project's own Forgejo instance): the shader translator in `third_party/ryujinx`.
- **[deko3d](https://github.com/devkitPro/deko3d)** (devkitPro, zlib): the texture layout size algorithm in `gfx/nvn/nvn_tex.c`.
- **nvn.h** ([h1k421](https://github.com/h1k421), MIT): NVN declarations in `gfx/nvn/include/nvn.h`.
- **[bcdec](https://github.com/iOrange/bcdec)** ([iOrange](https://github.com/iOrange), MIT or Unlicense): BC texture decoding.
- **[libvpx](https://github.com/webmproject/libvpx)** (WebM Project, BSD-3-Clause): VP8 / VP9 movie decoding, linked statically from Homebrew
  when installed.
- Tools: [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross), [XcodeGen](https://github.com/yonaskolb/XcodeGen),
  [.NET](https://dotnet.microsoft.com/), [Capstone](https://github.com/capstone-engine/capstone),
  [Unicorn](https://github.com/unicorn-engine/unicorn), [python-lz4](https://github.com/python-lz4/python-lz4),
  [UnityPy](https://github.com/K0lb3/UnityPy), [hactool](https://github.com/SciresM/hactool),
  [nxdumptool](https://github.com/DarkMatterCore/nxdumptool), [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice).

## License

MIT, see [LICENSE](LICENSE). Third-party code keeps its own license in its own directory.
