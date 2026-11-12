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

**Own the Steam version instead?** [steam/manosaba](steam/manosaba/README.md) builds an iPad and iPhone app from the Steam macOS version
(1.1.2). The game's IL2CPP code runs on the matching Unity iOS player, with no console, keys or HLE layer. It is independent of the Switch
path: nothing outside `steam/` uses it, and it uses nothing outside `steam/`.

## Status (manosaba 2.0.4b)

Verified on a Mac (Apple silicon, macOS 26), an iPhone 17 Pro (iOS 26) and an iPhone Air (iOS 27):

- boot, title screen, new game, story scenes with sprites, CG, text and UI, the opening movie, a trial including presenting evidence;
- native touch input (one rewriter rule turns on Unity's touchscreen device): scripted touches on the Mac, by hand on the iPhone;
- saving and loading, with transactional commits; iCloud sync of the save files (needs a paid developer account's entitlements; not tested);
- long sessions and repeated scene loads: a 16-minute iPhone session through scene changes and a trip to the background stayed between 1.2
  and 1.5 GB resident with no upward trend; 54 reloads of a trial on the Mac ran without a crash or a warning;
- graphics settings in the Settings app (frame rate 60 / 30 fps, render resolution, MetalFX upscaling): 720p with MetalFX used on the iPhone;
  30 fps not yet tried there. On the Mac the render resolution barely changes this game's GPU load; 30 fps is the setting that lowers it;
- rewrite equivalence: `verify/regress.sh --fuzz` passes on all three modules.

Not verified or known open: compute shader dispatch (none happens in the story or in a trial); 13 NVN functions the game looks up and never
calls; keyboard, mouse and game controller input with this game. Memory over hours of play: the game's own memory grows by some 30 MB per
scene load (every save load builds a new set of Naninovel's script and resource managers and the old ones stay referenced), and a trial's
loading adds over a gigabyte for a moment, so after about two hours a trial's loading went past an 8 GB iPad's limit without the increased
memory limit; restarting the app before then avoids it, and a free account can sign that limit (6.1 GB on the iPhone 17 Pro). The Switch
version is reported to end with an error now and then in long sessions too.

On Android (plan item 301, under way): on a Redmi K100 Pro Max (Android 17) manosaba runs as an app of this repository's own (no Android SDK:
`android/tools/apk.py` writes and signs the APK) at 60 fps, with sound (AAudio), touch, game controllers and saves, drawn by a Vulkan backend
of its own (`gfx/nvn/vk/`, the game's shaders from the same translation as Metal's): the title, the prologue and the first trial checked so far,
at 60 fps; what is left (the on-screen controls among it) is plan item 301 in [docs/plan.md](docs/plan.md).

## Status (Tears of the Kingdom 1.4.3)

Played on a Mac (Apple silicon, macOS 26) in a window, on an iPhone 17 Pro (iOS 26.4) and an iPhone Air (iOS 27), both with a free developer
account, in handheld mode:

- the title screen, the prologue with its tutorial and pre-rendered cutscenes (VP9 movies, decoded with libvpx, also on the iPhone), the dive
  from the Great Sky Island and play after it;
- frame rate: the default product runs at 30 fps; a product with the frame-rate rules runs at 60 fps (on the iPhone the game's own controller
  then picks 60, 30 or 20 by its frame time) with a RomFS overlay that `games/totk/romfs_overlay.py` builds from your own RomFS: without it
  the quick menus (the ability wheel) draw over black. On the Great Sky Island the iPhone 17 Pro held about 59.5 fps at a 1280x720 scene; the
  thinner iPhone Air runs near 60 until it is hot, a minute or two in, and at 30 from then on;
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
5. Android (see above): `DM_TARGET=android tools/build_product.sh $DM_ROOT/build/android/manosaba.b301`, the shader bundle
   (`DM_GAME=manosaba gfx/tools/dmsh_vk.py <shader captures> $DM_ROOT/build/android/manosaba.dmvk`), then
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
| [DEVELOPMENT.md](DEVELOPMENT.md) | Restoring a working copy, test procedures, where the work stands, how Tears of the Kingdom runs |
| [docs/plan.md](docs/plan.md), [docs/plan-done.md](docs/plan-done.md) | What is left to do; every finished plan item in a few lines (the "plan item N" of code comments) |
| [docs/sources.md](docs/sources.md) | Community ports drawn from, how far each was read, what was taken from each and what not |
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

What was taken from each, item by item: [docs/sources.md § Taken from each project](docs/sources.md#taken-from-each-project); every
port is also credited where it lives in the code.

- **[d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)** ([jhaizhou](https://github.com/jhaizhou-ops), MIT): the architecture and most of
  the host, graphics layer and verification tools, and later fixes; its contributors [SepheTT](https://github.com/SepheTT) and
  [BossaMelon](https://github.com/BossaMelon).
- **[nxnative](https://github.com/seedds/nxnative)** ([seedds](https://github.com/seedds), MIT) and **botw-mobile**
  ([Tommy Lau](https://github.com/TommyLau), MIT; no longer on GitHub), ports built on d2r-mobile: host and replay fixes, the extraction tool's
  model, and most of what Tears of the Kingdom's system modules needed (they are Breath of the Wild 1.9.0's).
- **[tomodachi-native](https://github.com/kuosuko/tomodachi-native)** ([kuosuko](https://github.com/kuosuko), MIT), a fork of nxnative: the
  duplicated colour slot, memory snapshots, a translator fix, attachment copies, code options.
- **[MacNativeGames](https://github.com/ly05010419/MacNativeGames)** ([ly05010419](https://github.com/ly05010419), MIT), a Tears of the Kingdom
  port on the same lineage: where the game's shaders are, virtual cores, the movie decoder's behaviour, the 60 fps rules and the real-time
  step, uniform snapshots and much of the graphics layer's handling of this game.
- **[LASRecomp](https://github.com/peiikai/LASRecomp)** ([Peiikai](https://github.com/peiikai), MIT), a static recompilation of Link's
  Awakening: CPU-filled textures, wait and lock rules, GPU time, instruction test vectors.
- **[nx2android](https://github.com/shirai91/nx2android)** ([shirai91](https://github.com/shirai91), MIT as its README states), nxnative's
  Android port: the rewriter's Android target, the APK written without the Android SDK, facts for the Android port.
- **[S2Arm](https://github.com/jhaizhou-ops/S2Arm)** ([jhaizhou-ops](https://github.com/jhaizhou-ops), Apache-2.0; written anew from what it
  does, no code taken): checks and measurements for the iPhone (bandwidth, sleeps, diagnostics, the install flow, the wide screen's route).
- **[mush8](https://github.com/c22dev/mush8)** ([c22dev](https://github.com/c22dev), GPL-3.0; ideas only, no code taken): three rewriter
  safeguards.
- **Single fixes from other ports**: [Kamisorara/d2r-mobile](https://github.com/Kamisorara/d2r-mobile),
  [jiangjifeng/nxnative](https://github.com/jiangjifeng/nxnative), [GirlNet/d2r-mobile](https://github.com/GirlNet/d2r-mobile),
  [lordran-native](https://github.com/Nishikori-Yui/lordran-native), [SkyRecomp](https://github.com/Apolinario-coder/SkyRecomp),
  [nxport](https://github.com/nxporting/nxport), [BotWRecomp](https://github.com/IshaanKalra2103/BotWRecomp),
  [4ppp44/d2r-mobile-ios17](https://github.com/4ppp44/d2r-mobile-ios17), [GirlNet/nxnative](https://github.com/GirlNet/nxnative),
  [RogelioLB/nxnative-android](https://github.com/RogelioLB/nxnative-android),
  [PKShield-Android](https://github.com/simipoB/PKShield-Android), [doom-port](https://github.com/Desm0nt/doom-port).
- **[LibHac](https://www.nuget.org/packages/LibHac/0.19.0)** ([Alex Barney](https://github.com/Thealexbarney), MIT; the NuGet package, as its
  GitHub repository is no longer reachable): reads NSP, XCI and NCA containers in `tools/game_extract`.
- **[Ryujinx](https://ryujinx.app)** (MIT; source on the project's own Forgejo instance): the shader translator in `third_party/ryujinx`.
- **[deko3d](https://github.com/devkitPro/deko3d)** (devkitPro, zlib): the texture layout size algorithm in `gfx/nvn/nvn_tex.c`; its format table
  told the bit order of the 16-bit formats (`gfx/tex/nvn_fmt_mtl.c`).
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
