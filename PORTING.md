# Porting a Switch game

This is the workflow that took manosaba from a dump to running on iPhone. Each step names the tool and what to check before going on.

```
your console ──► ExeFS + RomFS + system fonts            (data/switch/…)
                  │
tools/new_game.py ─► games/<id>/game.json, rules.py
tools/build_product.sh ─► product (rewritten modules)   ─► verify/regress.sh
make -C host GAME=<id> ─► dmhost
shaders: capture ─► shmap_from_capture.py ─► dmsh_build.py ─► c1_guard.py
run on the Mac, fix what the log reports, repeat
ios/run.sh build / install / push-data / launch;  tools/mac_app.sh
```

## 0. Machine and workspace

- Apple silicon Mac, macOS 26 (the host uses Metal 4; `make -C host dmhost_m3` builds for macOS 15+), Xcode with the iOS SDK and the Metal
  toolchain (used: Xcode 27.0).
- Homebrew: `dotnet` (shader translator), `spirv-cross`, `xcodegen` (iOS project); `libvpx` for VP8 / VP9 movies (linked statically into the Mac
  host when installed; without it such movies cannot play); `libimobiledevice` only for the live iOS log (`iproxy`).
- Python 3 venv at `$DM_ROOT/build/cache/dm/venv` (the scripts' default interpreter, `DM_PY` overrides):
  `pip install -r tools/requirements.txt`, and for Unity games `pip install --no-deps -r engines/unity/requirements.txt`.
- The shader translator: `DOTNET_ROOT=$(brew --prefix dotnet)/libexec dotnet build -c Release gfx/tools/dmsh_xlate/dmsh_xlate.csproj -o $DM_ROOT/build/gfx/dmsh_xlate`
  (keep `DOTNET_ROOT` exported when running `dmsh_build.py`).
- For the Android app (`android/`, plan item 301): the casks `android-ndk` (NDK r30), `android-platform-tools` (adb) and
  `android-commandlinetools`, the formula `openjdk@21`, and for the settings panel's Java (classes.dex) `JAVA_HOME=$(brew --prefix openjdk@21)
  sdkmanager --sdk_root=$(brew --prefix)/share/android-commandlinetools "build-tools;35.0.1" "platforms;android-35"` (it asks you to accept the
  Android SDK licence).

`DM_ROOT` (default `~/dm-work`) holds everything that is not source: game data, build outputs, runs. Nothing under it goes into git.

```
$DM_ROOT/data/switch/<id>-<version>/ExeFS     main, sdk, subsdk*, main.npdm
$DM_ROOT/data/switch/<id>-<version>/RomFS
$DM_ROOT/data/switch/sysfont                  *.bfttf (shared by all games)
$DM_ROOT/build/…                              product, shaders, stage map (paths in game.json)
$DM_ROOT/run/…                                logs, screenshots, saves of test runs
```

## 1. Dump the game from your console

Only your own copy, from your own console. You need the game with its latest update applied, as ExeFS (`main`, `sdk`, `subsdk*`,
`main.npdm`) and RomFS. Two ways:

- **Packages + keys** (the usual way). On the console, run Lockpick_RCM (`prod.keys`), then in nxdumptool's `user titles menu` → the game
  → `nsp dump options`, dump the base game and the update as NSPs; keep `tik: remove console specific data` on (eShop titles have
  personalized tickets, which cannot be used off the console) and `nca/tik: remove titlekey crypto` off. On the Mac:

  ```sh
  tools/game_extract.sh <prod.keys> $DM_ROOT/data/switch/<id>-<ver> <base.nsp> <update.nsp>
  ```

  It takes `.nsp` and `.xci` in any mix, and the folder nxdumptool writes for a file over 4 GiB on the SD card. It reads only the keys
  common to all consoles from `prod.keys`, layers the newest update over the base game, checks every read against the NCA hash trees and
  prints the module build IDs (C#, LibHac; needs the .NET SDK). On manosaba it gave the same 667 files, byte for byte, as the console.
- **On the console**, no keys on the Mac: nxdumptool → `nca / nca fs dump options` → `dump update` → the Program NCA; dump the `ExeFS`
  section, then the `Patch RomFS` section with `use base/patch title` set to the base game. Output under
  `sdmc:/nxdt_rw_poc/NCA FS/User/Extracted/<title>/Program #0/<section>`.

**System fonts**: the `*.bfttf` files from the RomFS of system data titles `0100000000000810`–`0100000000000814` (nxdumptool). The host
serves them as `nn::pl` shared fonts; many games render all their text with them.

Keys, dumps and console backups stay out of the repository (`.gitignore` covers `keys/`, `dump/`, `work/` …).

## 2. Create the game directory

```sh
DM_ROOT=~/dm-work python3 tools/new_game.py <id> $DM_ROOT/data/switch/<id>-<ver>/ExeFS $DM_ROOT/data/switch/<id>-<ver>/RomFS \
    --title "<title>" --version <ver> --language <lang>
```

It reads the program ID (`main.npdm`), `main`'s build ID, the modules and the RomFS checks, and writes `games/<id>/game.json` and an empty
`rules.py`. Review `language`, `screen` and `save_sync` ([games/README.md](games/README.md) explains every field).

## 3. Rewrite the modules and verify

```sh
tools/build_product.sh            # DM_GAME=<id>; output: workspace.product
verify/regress.sh --fuzz          # must end with REGRESS OK
```

The rewrite is generic; it needs no game knowledge. Run both again whenever `rules.py` changes.

## 4. First run on the Mac

```sh
make -C host GAME=<id>
mkdir -p $DM_ROOT/run/<id>/{save,temp,shots}
```

On the first run there is no shader stage map yet. Give it an empty one and let the host capture every shader the game creates:

```sh
: > /tmp/empty_map.tsv
DM_GAME=<id> DM_ROOT=$DM_ROOT DM_NVN_SHMAP=/tmp/empty_map.tsv DM_SHDUMP=$DM_ROOT/<workspace.shader_capture> \
  DM_SAVE=$DM_ROOT/run/<id>/save DM_TEMP=$DM_ROOT/run/<id>/temp DM_WATCH=60 host/dmhost > first.log 2>&1
```

Nothing is visible yet (no shaders), but the log already shows how far the game gets. Every line with `[red]` is something to look at:
an unimplemented service function (named stub called), an NVN function the layer does not know, a failed check. `DM_WATCH=<s>` stops the run
(exit 142 is the watchdog, not a crash).

## 5. Shaders

A run only captures the shaders it reached. For Unity games, extract all of them statically instead
([engines/unity/README.md](engines/unity/README.md)). With `ws() { echo "$DM_ROOT/$(python3 tools/gen_game.py games/<id>/game.json --get workspace.$1)"; }`
and `PY=$DM_ROOT/build/cache/dm/venv/bin/python`:

```sh
$PY engines/unity/shader_extract.py "$(ws romfs)/Data" "$(ws exefs)/main" "$(ws shader_capture)"     # Unity only

python3 gfx/tools/shmap_from_capture.py "$(ws shader_capture)" "$(ws shader_map)"
IOSOUT="$(ws msl_ios)" $PY gfx/tools/dmsh_build.py "$(ws shader_capture)" "$(ws msl_mac)"
python3 gfx/tools/c1_guard.py "$(ws msl_mac)/b-$(python3 gfx/tools/dmsh_chain.py)" "$(ws c1)"
```

`dmsh_build.py` prints a summary; every shader should be `ok`. If `gfx/tools/dmsh_chain.py --check-tools` reports that your spirv-cross or
Xcode differs from the lock file, run `dmsh_chain.py --update-lock` and `--header`, then rebuild the host. Libraries are keyed by content, so
rebuilding only translates what changed.

## 6. Run, look, fix

```sh
DM_GAME=<id> DM_ROOT=$DM_ROOT DM_SAVE=… DM_TEMP=… DM_KEYS="A@10+0.2,A@12+0.2" DM_NVN_SHOT="600,1500:shots/s%d.png" DM_WATCH=60 host/dmhost
DM_WINDOW=1 DM_AUDIO=1 …                  # interactive, keyboard / mouse / gamepad
```

`DM_KEYS` scripts input (buttons, sticks, touches), `DM_NVN_SHOT` saves screenshots at given frames; with both, runs are repeatable. What usually
needs work for a new game:

- **Services** the game calls that the host does not implement yet: add an `HLE_FORCE` implementation in `host/hle_*.c` if it is generic, or in
  `games/<id>/` if it is not.
- **Graphics**: unimplemented NVN functions or paths ([gfx/nvn_metal_mapping.md](gfx/nvn_metal_mapping.md) section 4); object sizes that differ
  in the game's SDK version.
- **Game-specific changes**: `rules.py` cuts / observation gates / parameter slots, and C hooks in `games/<id>/`. For example, Unity games need
  one cut to turn on touch input.
- **Debugging**: `tools/fdis.py` / `symaddr.py` (disassembly with symbols), `DM_PROBE=module:offset` (Mac only: logs x0–x3, x8, x19–x23 and a
  backtrace when that instruction runs; probe function entries),
  `DM_THROWLOG=1`, `DM_HLECOUNT=<file>`, the crash handler's guest backtrace, `DM_CRASH_MEM=x27-0x28:0x80,...` (memory at registers when it
  crashes), `DM_WATCHPOINT=[heap+]addr:len` (Mac only: a hardware watchpoint on every thread that logs each store to the range with its pc and
  backtrace; guest heap offsets repeat between runs). For IL2CPP games a metadata dump maps addresses to C# methods.

Keep `make -C host/tests run`, `make -C gfx/tests run` and `verify/regress.sh` passing.

## 7. iPhone / iPad and the Mac app

```sh
cp ios/local.env.example ios/local.env     # team, bundle ID, device; DM_FREE_ACCOUNT=1 for a free account
ios/run.sh build && ios/run.sh install && ios/run.sh push-data && ios/run.sh launch
tools/mac_app.sh                           # $DM_ROOT/out/macapp/<title>.app
```

See [ios/README.md](ios/README.md). The app icon comes from `workspace.icon` (your own 1024×1024 PNG; never committed).
