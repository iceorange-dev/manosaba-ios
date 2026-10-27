# ios — iOS app

The iOS app is the host (`host/`, `gfx/`) plus a thin shell (`Sources/`): it finds the game data, starts the host on its own thread, and turns
UIKit touches, game controllers and the app lifecycle into what the host expects. The rewritten game modules are compiled into the app's
executable (their code in the signed `__TEXT` segment, remapped to the original layout at run time: iOS cannot load new executable code), and
the translated shaders are packaged with it. The game's RomFS is not in the app: you copy your own dump onto the device.

Requirements: iOS 26 (the graphics backend uses Metal 4; `DM_IOS_M3=1` builds for iOS 18 without it), an Apple account in Xcode, and
`xcodegen` (Homebrew).

## Setup

Copy `local.env.example` to `local.env` (ignored by git) and fill in: `DM_GAME`, the device (`DM_DEVICE` from `xcrun devicectl list devices`,
and `DM_IOS_UDID` for a free account), `DM_TEAM_ID`, your own `DM_BUNDLE_ID`, and `DM_FREE_ACCOUNT=1` for a free (personal team) account.

A free account cannot sign iCloud, extended virtual addressing or sustained execution entitlements; the build leaves them out. It does sign the
increased memory limit (on an iPhone 17 Pro the limit went from about 3.4 to 6.1 GB, 2026-10-21). Its profiles expire after 7 days (rebuild and
reinstall), and before the first launch you trust the developer in Settings → General → VPN & Device Management.

## run.sh

```sh
export DM_ROOT=~/dm-work
ios/run.sh build            # generate Gen/ and the Xcode project, build (log: $DM_ROOT/run/ios/build.log)
ios/run.sh ipa              # the same build unsigned: $DM_ROOT/run/ios/<game>-ios26.ipa (-ios18 with DM_IOS_M3=1), see below
                            # DM_IPA_DATA=1: with the RomFS, system fonts and RomFS overlay inside (<game>-ios26-data.ipa)
ios/run.sh install          # install on DM_DEVICE
ios/run.sh push-data [what] # copy the profile's RomFS, system fonts and RomFS overlay into Library/Application Support
                            # (what: some of romfs sysfont overlay; e.g. push-data overlay after games/totk/romfs_overlay.py)
ios/run.sh launch [s]       # launch, wait s seconds (default 30), fetch Documents/boot.txt
ios/run.sh log              # fetch the log now
```

`build` needs the game's product, shader stage map and iOS shader libraries (`b-<chain version>` under `workspace.msl_ios`) — see
[PORTING.md](../PORTING.md). `DM_DEVICE_FAMILY=1` builds for iPhone only (default iPhone and iPad). The app icon is the profile's
`workspace.icon` (1024×1024 PNG) when it exists.

What `build` puts into the app (`Gen/`, generated, not committed):

| Item | From |
|---|---|
| Host sources, include paths, frameworks | `host/Makefile`'s own compile command (`tools/gen_build.py`) |
| Game modules (`<module>.patched.bin` as assembly sections of the executable) and their `fixups.tsv` / `gates.tsv` | the product (`tools/gen_embed.py`) |
| `shader_map.tsv`, `c1_full.tsv` | the profile's workspace |
| `msl/b-<V>/` | iOS shader libraries of the current chain version |

## iOS Simulator

For the on-screen controls, safe areas and lifecycle without a phone (plan item 114):

```sh
python3 ios/tools/sim_metal.py $DM_ROOT/build/mano/shader_map.tsv $DM_ROOT/build/cache/metal_shader_cache/b-<V> $DM_ROOT/build/ios/sim/manosaba/b-<V>
ios/run.sh sim-build              # Metal 3, arm64, unsigned (log: $DM_ROOT/run/ios/build-sim.log)
DM_SIM_ENV="DM_TOUCH=pad" ios/run.sh sim-run 30    # iPhone 17 Pro (DM_SIM_DEVICE), a screenshot in $DM_ROOT/run/ios/sim.png
DM_SIM_BG=20,10 ios/run.sh sim-run 30              # a real trip to the background (the Settings app) and back, judged: LIFECYCLE OK / RED
ios/run.sh sim-stop
```

The Simulator is no device: the SDK (Xcode 27) has no Metal 4 and no MetalFX, so this is the Metal 3 build without MetalFX; its GPU is an
Apple family 2 stand-in (Apple's "Developing Metal apps that run in Simulator": argument buffers tier 1, no unified memory, no programmable
blending, at most 256 MB a buffer here), and the game's pictures come out wrong (manosaba's title screen, which the Mac shows at the same
frame, nearly black). Device metallibs do not
load there (`sim_metal.py` compiles the same MSL with the Simulator's SDK; the metallib header's platform byte says which: 7 the Simulator, 2 a
device). The game data is cloned into the app's container (APFS, no bytes copied): a link to a folder under the Desktop made the app wait in
`open()` for a permission answer. VP8 / VP9 movies do not play (no libvpx for the Simulator).

## Unsigned IPA

`run.sh ipa` builds without signing (`CODE_SIGNING_ALLOWED=NO`, its own derived data `run/ios/dd-ipa`, so the signed build stays installable),
signs the app ad hoc with one entitlement, the increased memory limit (`tools/ipa_sign.py`, plan item 211), and zips `Payload/DmIOS.app`. It
needs `DM_BUNDLE_ID` but no team. A signing tool (AltStore, SideStore, Sideloadly, your own certificate and profile) signs it on the way in; the
signer's profile decides the entitlements. AltStore reads the entitlements in the app's signature (AltSign `ALTApplication.entitlementsString`)
to decide which App ID features to turn on: without the ad hoc signature it turned on none, and the app kept the default memory limit
(LNPANDY/nxnative-romfs-archive c3bccb5: about 3,375 MB on a 12 GB iPhone). The packaging line gives the IPA's size, with a note above 4 GiB:
iOS refused a 16.6 GB IPA that held a whole Tears of the Kingdom RomFS (LNPANDY 13d9191). Without iCloud
entitlements there is no save sync (`host/cloud/cloud.m` asks for the default container at run time); everything else works as in a free-account
build. Tools that change the bundle ID are fine: nothing in the app depends on it. Tried on 2026-10-16 with LCSign on an iPad Pro (iOS 18.5):
it signed with every entitlement of its profile (increased memory limit included, `get-task-allow` false), and the app ran.

Which signers keep the increased memory limit (read 2026-10-27; measured by others unless said):
- Xcode with a free personal team: kept (here: an iPhone 17 Pro went from about 3.4 to 6.1 GB).
- AltStore 2.2 and later: kept. It turns the App ID's capability on from the app's own signature (AltSign `ALTAppleAPI.m`, `ALTApplication.mm`),
  which is why this IPA carries it ad hoc, and its signer drops a profile entitlement that signature lacks (`ALTSigner.mm`); about 6 GB with
  free accounts. AltStore 2.3.1 fixed the sign-in error -80045.
- SideStore 0.7.0 with a free Apple ID: dropped (SideSign's free features; SideStore#1616: about 3.3 GB), unless the App ID's capability is
  turned on once (hugeBlack/GetMoreRam, whose sign-in failed with -80045 from about 10-12) or a SideSign build with its PR #4 signs it.
- LiveContainer: an app inside it runs with LiveContainer's own entitlements (its README).
- zsign signers (Feather, KSign): what the imported profile has.
- Sideloadly: the sources disagree (NicholasDominici/totk-ios's README: kept; xinerqu/ns2ios-shell's table: dropped, about 3.4 GB).

The boot log says what a launch got: `boot: memory limit <N> MB (footprint … + … available), RAM … MB; the increased memory limit in the
app's signature: yes / no` (the entitlements read back from the executable's own signature, `Sources/codesig.h`).

- The iOS 26 build is compiled with `-mcpu=apple-a18`, the iOS 18 build (`DM_IOS_M3=1`) with `-mcpu=apple-a14`; build the one that fits the
  device's chip and iOS version.
- The IPA contains the rewritten game code and the game's icon (with `DM_IPA_DATA=1` the whole game): it is for your own devices, from your
  own dump. Do not publish it.
- Without `DM_IPA_DATA=1` the game data is copied in afterwards as usual (Files app → `Documents/RomFS`, `Documents/sysfont`). With it, the
  workspace's `romfs` and `sysfont` become `RomFS` and `sysfont` inside the app; the IPA is then as large as the game, and every install or
  update carries all of it.

## On the device

- **Game data**: RomFS in `Library/Application Support/romfs` (`push-data`), `Documents/RomFS` (Files app; file sharing is on) or the app's
  own `RomFS` (`run.sh ipa` with `DM_IPA_DATA=1`), the first complete one in that order; system fonts in `sysfont` in the same three places. The
  RomFS is checked against the profile before the game starts; if something is missing the app shows what and where. A RomFS of many files
  (Tears of the Kingdom: 304,243 files, about 17 GB) is quicker and safer to carry as parts: `tools/romfs_parts.py pack <RomFS> <out>` writes
  `romfs.000.dmrp` … and `romfs.dmri`; copy them into `Documents` (Files app or Finder), and the next launch checks each part's SHA-256,
  unpacks it into `Library/Application Support/romfs` and deletes it (16 parts in about 4.5 minutes on an iPhone 17 Pro; a launch cut short
  carries on at the next; `host/runtime/romfs_parts.c`).
- **RomFS overlay** (Tears of the Kingdom's quick-menu backgrounds and game over flow at 60 fps and up, `games/totk/romfs_overlay.py`):
  `romfs_overlay` in the same three places (`push-data overlay` copies the workspace's `build/<game>/romfs_overlay`), served when its
  `manifest.tsv` says it was made from the RomFS beside it (`host/runtime/romfs_validate.c`); `DM_ROMFS_OVERLAY` in env.txt names another, empty turns
  it off. Without it the 60 fps build draws the ability wheel (hold L) over black.
- **Mods**: `Documents/mods` (Files app), one folder per mod (`<mod>/romfs`, or as shipped for Atmosphère), or an SD card's `atmosphere`
  folder copied as is; earlier names win, a name starting with `-` turns a mod off; code mods are not applied (`host/game/mods.c`).
- **On-screen controls** (games with game.json `touch` "pad": Tears of the Kingdom; `DM_TOUCH=pad` / `screen` overrides the profile, e.g.
  to try them with manosaba): BotWRecomp's layout (`host/front/touchpad.c`): a floating stick on the left half, D-pad, ABXY, L / R / ZL / ZR, - / +,
  L3 / R3, and the right half outside the buttons turns the camera by the finger's speed. The eye button collapses them to itself (kept across
  launches); a connected game controller turns them off. Settings app → this app → Touch Camera: the camera swipe's sensitivity (0.25-3×,
  applied before the turn speed saturates) and inverted axes (`DM_TOUCH_LOOK=<sensitivity>[,x][,y]` at launch). `DM_TEST_TOUCHPAD=check:<s>`
  runs the input checks on the device and logs the result.
- **Saves**: `Documents/save` (transactional, committed atomically). With iCloud entitlements the profile's `save_sync` files are synced
  (`host/cloud/cloud.m`).
- **Log**: `Documents/boot.txt` (the last foreground session; the one before is `boot.previous.txt`). Settings app → this app → "Show log on
  screen" draws the last lines over the game while playing ("Only [red] lines", "Lines": 8 / 14 / 24; touches pass through). Launched with
  `DM_LOG_PORT=<port>`, the app also serves its log over USB: `iproxy <local> <port>`, then `tools/logport.py tail <local>`.
  Each line starts with its UTC time in milliseconds (`host/logstamp.h`; `DM_LOG_TIME=0` in env.txt turns it off). "Show frame rate" puts
  the game's frames per second, those reaching the screen and the GPU time per frame in the top right corner (`gfx/nvn/nvn_perfline.h`).
- **Diagnostics**: touch and hold the app's icon → Export Diagnostics: one text file with the device, GPU, memory, thermal state, shader
  chain, frame rate and switches, then the last 8 MiB of `boot.txt`, `boot.previous.txt`, `boot.interrupted.txt` and `perf.txt`, in the share
  sheet (`Sources/diag_export.m`); launched that way, the app exports before the game starts. The page shown when the game data is missing
  has the same button. A session that ends in the foreground without an orderly exit (killed for memory, a crash, a hang the system ended)
  is noticed at the next launch (`Documents/boot.running`, `host/rundiag.h`): a red line says so and its log is kept as
  `boot.interrupted.txt` until another such session replaces it.
- **Graphics settings** (Settings app → this app → Graphics): frame rate 60 / 30 fps (`DM_FPS`; the display link and ProMotion follow), render
  resolution (`DM_RENDER_HEIGHT`, only for games whose hooks apply it — manosaba: `games/manosaba/graphics.c`; Tears of the Kingdom built with
the code option `scene`: `games/totk/frame_step.c`, that many lines at 16:9; read at startup) and MetalFX
  spatial upscaling of the presented picture (`DM_UPSCALE=metalfx`, `gfx/nvn/nvn_mtl.m` `fx_apply`; for a game whose profile names its own upscale
  or compose pass, that pass too: `fxp_dock` / `fxp_scene`, plan item 63). Frame rate and MetalFX follow a change on
  returning to the game; a variable set at launch wins until the setting is changed. iOS has no V-Sync switch: `CAMetalLayer.displaySyncEnabled`
  is macOS-only, a layer always presents in step with the display.
  The frame rate applied is held at 30 in Low Power Mode and, while "Hold 30 fps when hot" is on (`dm.hot_hold`, the default), from the
  thermal state Serious up (`nvn_fps_policy`, plan items 135 / 135a); the boot line says which.
  The frame rate setting is a ceiling: a game paced at 30 fps by its own code stays there (Tears of the Kingdom's default product). Its 60 fps
  build for a phone (plan item 62l): `DM_PRODUCT=build/totk/product60 DM_OPTIONS=fps60,realtime,scene ios/run.sh build` (the game's controller
  then picks 60, 30 or 20 by its frame time; the options `output` and `aspect` stay out).
- **Lifecycle**: in the background the guest clock, audio and graphics are suspended and pending iCloud uploads are flushed.

## Files

| File | What it does |
|---|---|
| `Sources/main.m` | App and scene delegates, log, finding the game data, graphics settings, starting the host |
| `Sources/front_ios.m` | The front end (`host/front.h`): Metal layer, touches, on-screen controls (`host/front/touchpad.c`), controllers, lifecycle |
| `Sources/log_overlay.m`, `Settings.bundle` | On-screen log and frame rate line; the Settings app page (graphics settings and the log switches) |
| `Sources/diag_export.m` | Diagnostics export (the Export Diagnostics quick action) |
| `project.yml` | xcodegen spec; `tools/project_free.py` / `tools/project_m3.py` derive the free-account and iOS 18 variants |
| `run.sh`, `tools/local_env.sh` | Build / install / launch; reads `local.env` (or `~/.config/dm-ios/local.env`) |
| `tools/gen_build.py`, `tools/gen_embed.py` | Host sources from `host/Makefile`; the product embedded in the executable |
| `tools/logport.py` | Live log over USB |
