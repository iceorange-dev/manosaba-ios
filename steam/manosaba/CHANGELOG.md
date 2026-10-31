# Changelog

All notable changes to this project are documented here. Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased] - 2026-10-26

### Added
- Empty Unity 6000.0.48f1 shell project with an iOS (IL2CPP, Metal, landscape) and a macOS build entry point, used to obtain the iOS player and to diff data layouts.
- `tools/patch_player_lib.py`: NOPs the iOS player's target-platform check in `SerializedFile::ReadMetadata` (idempotent, verifies surrounding instructions).
- `tools/gen_icall_registration.py`: registers every engine internal call defined by `libiPhone-lib.a`.
- `tools/make_game_framework.sh`: retags the Steam macOS `GameAssembly.dylib` as an iOS `ManosabaGame.framework`.
- `tools/gen_steam_stub.py`: offline `steam_api.dylib` stub covering all 974 Steam entry points the game references.
- `tools/stage_data.sh`: APFS-clone staging of the Steam data into the Xcode project; `SKIP_VOICE=1` for smoke-test builds.
- `tools/patch_xcode.rb`: rewires the generated Xcode project to link and embed the transplanted game code.
- Tests for the tooling (`tests/test_tools.py`).

### Fixed
- Recursive deletes now have explicit guards instead of being saved by luck. Each case has a test that fails without its guard and that really deleted a decoy folder.
  - `stage_data.sh` deletes `<xcode-dir>/Data` only inside a Unity-generated Xcode project. An empty argument made it `/Data`, and a wrong folder put that folder's `Data` at risk.
  - `drive.sh` takes only a plain folder name as its tag. A tag such as `../../../x` deleted a folder next to the repository.
  - The iCloud sync ignores manifest entries that are not plain save file names.
- iOS 27 launch assert (UIScene life cycle required): the app's main executable records SDK 26.0 before signing.
- Crash in `-[UnityAppController startUnity:]`: the iOS player invoked `AudioSettings.Mobile` callbacks that only exist in the mobile managed assemblies; the proxies now return 0.
- iOS player has no software BC decoder: `tools/transcode_textures.py` re-encodes all 630 BC/crunched textures to ASTC 4x4 / 6x6 (BC4 -> R8), min PSNR 32.7 dB.
- Loading bar stuck at 75%: on iOS the game takes its mobile branch and indexes the empty `ResolutionCoefficientsForMobile`; `tools/patch_game_config.py` fills it from the PC resolution levels.
- Portrait layout: the trampoline is pinned to landscape.
- Silence after connecting or disconnecting a Bluetooth output: FMOD does not recover from the route change (same 48 kHz, IO buffer 42.7 → 10 ms). `AudioRouteLogger.mm` now restarts it 0.3 s after a device route change by posting `AVAudioSessionInterruptionTypeEnded`, the same workaround Unity's trampoline uses after dictation.
- Invalid app signature after an incremental device build: Xcode skipped CodeSign, yet the SDK-26 phase still rewrote the signed executable. The phase now leaves an executable that already records SDK 26.0 untouched.
- Mac saves now load as same-version saves: `stage_data.sh` sets `CFBundleShortVersionString` to the Steam build's version (1.1.2) instead of the shell's 1.0. The game's VersioningManager records the version in every save.
- `.ruby-version` pins Ruby 3.3.9, the version that has the `xcodeproj` gem `patch_xcode.rb` needs.

### Added (simulator)
- Simulator path: `ShellProject` `BuildShell.IOSSim`, `tools/patch_player_dylib.py` (same patches on the simulator dylib, plus `Debug.isDebugBuild` -> false because Unity ships the simulator player only as a Development build), `PLATFORM=iossim` for the game framework, sim-aware `patch_xcode.rb`.
- `tools/sim_run.sh` (build, install, launch, screenshots) and `tools/drive.sh` + `tools/uitest` (XCUITest driver: rotate, tap, screenshot), since Xcode 27.1 ships no Simulator.app.

### Added (device)
- Cable-free install: `tools/ota/server.py` is a local-network install page, published over HTTPS (for example with `tailscale serve`). It captures the UDID through a Profile Service mobileconfig and installs over the air via `itms-services`, with Range support for multi-GB IPAs.
- `tools/devunlock`: tiny placeholder app with the game's bundle id, served by the install page as `placeholderunlockdev.ipa`, separately from your game build (`manosaba.ipa`). Installing it makes the Developer Mode switch appear on a device that has never been connected to Xcode.
- Memory telemetry: `MemoryLogger.mm` logs footprint, graphics and headroom. `MetalAllocLogger.mm` logs every Metal allocation ≥ 16 MB.
- The device build requests `com.apple.developer.kernel.increased-memory-limit`.
- App icon: `tools/make_app_icon.py` builds the iPhone/iPad/App Store icon set from the game's `PlayerIcon.icns`, replacing Unity's default. It crops the macOS template's margin and shadow, fills the rounded corners from the artwork, and drops alpha.
- iCloud save sync between iPhone and iPad (`ICloudSaveSync.mm` plus a unit-tested core, `ICloudSaveSyncCore.mm`). It is opt-in with `ICLOUD_CONTAINER=iCloud.<bundle id>` for `patch_xcode.rb`, and needs that container attached to the App ID in the developer account.
  - Before the game reads its saves at launch, a full two-way sync runs, capped at 6 s.
  - When the app goes to the background, every 2 minutes, and on exit, local changes are pushed.
  - Save files are compared by hash against the last sync. When both sides changed, the newer edit wins.
  - A save deleted on the other device is moved to a local trash folder, not removed. An emptied iCloud folder is refilled from the local copy instead of being mirrored.
  - The game's `Settings.json` (volume, text speed, auto-play delay, font) syncs as well; the newer change wins.

### Added (settings)
- A manosaba page in the iOS Settings app (`Settings.bundle`, read by `ManosabaSettings.mm`):
  - play sound in Silent Mode (Playback instead of Ambient, mixing with other apps);
  - lower the frame rate when hot;
  - maximum frame rate (follow the game / 60 / 40 / 30);
  - iCloud save sync;
  - diagnostics logging;
  - version.

### Changed
- `tools/patch_character_ppu.py` lowers `PixelsPerUnit` from 100 to 75 for all 42 actor entries in `CharactersConfiguration`. Each layered character is composited every frame into its own render target of `RenderCanvas.Size` (15×30) × PPU, which is 1500×3000 colour + depth + HDR. Scenes with many characters saturated the A12Z (16–24 fps) and overheated it. At 75 PPU the targets are 1125×2250 (−44% pixels), and characters keep their on-screen size, since size = pixels / PPU. Measured in the same scene: 42.9 → 54.2 fps nominal, 23.9 → 30.0 fair, 16.5 → 21.6 serious.
- `ThermalFrameCap.mm` caps the frame rate as the device heats up: the game's own target (60) while nominal, 40 fps at fair, 30 fps at serious/critical. `patch_xcode.rb` routes `callbackFramerateChange:` through it.
- `tools/transcode_audio.py` re-encodes the FMOD Vorbis clips in the bundles. Voice clips are identical-channel "stereo" at quality 100 (4.6 GB). They become mono at q45 (~64 kbps), with +3.01 dB to offset Unity's constant-power pan of mono sources. Music stays stereo at q63, and SFX go mono at q63 when L == R. Every clip is decoded back and checked for duration, correlation and peak level, and a bundle is written only if all of its clips pass. Only setup headers in FMOD's CRC table are emitted. `Song_004_001` (300 s, decompress-on-load) now streams.
- `tools/patch_font_atlases.py` shrinks the dynamic TextMeshPro atlases of SourceHanSerifSC and TsukushiMincho from 4096² to 2048². Unity's Metal backend allocates a fresh texture for every full atlas upload. With 4096² atlases, a page of new Chinese text meant hundreds of 16 MB allocations in a burst (250–300 ms hitches, about +1 GB of GPU memory). Multi-atlas is on, so a smaller atlas only adds 4 MB pages.
- The Metal logger records call stacks for those uploads, counts A8 atlas uploads of every size, and reports the frame rate (`[fps]`, every 10 s).
- `transcode_textures.py` rebuilds each bundle's `.resS` with the converted data instead of writing it inline beside the original. That drops 0.8 GB of dead BC/RGBA data and lets textures stream into GPU memory without an inline CPU copy. Character sprite atlases and the large UI atlases (≥ 2048 px) use ASTC 6x6 instead of 4x4, which saves up to ~390 MiB of GPU memory at PSNR ≥ 44 dB.
- `transcode_textures.py` now also compresses large uncompressed textures: RGBA32 → ASTC 4x4, RGB24 → ASTC 6x6. It skips readable textures and textures under 128 px. When ASTC drops below 32 dB, it keeps the original.

### Notes
- Bundle id, team, iCloud container, OTA origin and simulator come from environment variables (README, Configuration). The generated `tools/**/*.xcodeproj` are no longer tracked; `xcodegen generate` recreates them.
- Steam: the game uses only the stock Steamworks.NET `SteamManager`, which calls `RestartAppIfNecessary`, `InitEx`, `RunCallbacks` and `Shutdown`. It has no achievements, DLC/ownership checks, Steam language or remote-storage calls, and nothing reads the init result. The stub must keep `SteamAPI_RestartAppIfNecessary` returning false, otherwise the game calls `Application.Quit`. Saves reach Steam only through Auto-Cloud (`Saves_v1`); on iOS, iCloud save sync replaces it.
- macOS 27 ships without Rosetta, and Unity 6000.0.48f1's `UnityPackageManager` is x86_64-only. The editor is used from an APFS clone (`unity-6000.0.48f1/`) whose package server binary is replaced by a shim running `app/app.js` on arm64 node 20.
