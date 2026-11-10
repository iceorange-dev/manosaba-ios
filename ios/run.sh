#!/bin/bash
# iOS app: build, install and run on a device. The game is DM_GAME (games/<id>/game.json); its workspace paths (product, shaders, icon, data) are
# relative to DM_ROOT. Device and signing settings come from local.env (see local.env.example).
#   run.sh build              generate Gen/ and the Xcode project, build the app (log: $DM_ROOT/run/ios/build.log)
#   run.sh ipa                the same build unsigned, packaged as $DM_ROOT/run/ios/<game>-ios26.ipa (-ios18 with DM_IOS_M3=1) to sign yourself;
#                             DM_IPA_DATA=1 also puts the workspace's RomFS, shared fonts and RomFS overlay into the app (<game>-ios26-data.ipa)
#   run.sh install            install the built app: its signature checked (expiry, this device), the app's Documents backed up first (ios/tools/install_check.py)
#   run.sh resign             sign the built app again with a fresh development profile instead of building it again (a free account's lasts 7 days;
#                             ios/tools/resign.py; Xcode is asked for a new profile through ios/tools/prov when there is none, or first with
#                             DM_RESIGN_RENEW=1), then install it
# Without local.env (plan item 319): the team is the one Xcode is signed in with (a paid one first), the phone the one connected (ios/tools/acct.py),
# the bundle ID dm.<game>.<team in lower case>, a free team's build with DM_FREE_ACCOUNT=1
#   run.sh launch [seconds]   launch it (ends a running instance), wait, then fetch Documents/boot.txt
#   run.sh log                fetch Documents/boot.txt (or boot.launch.txt) now
#   run.sh lifecycle [at] [len]  launch with a trip to the background simulated in the app at <at> s for <len> s (DM_TEST_BG, default 40 and 20),
#                             fetch boot.txt and judge it (tools/lifecycle_check.py: LIFECYCLE OK / RED). The real home button and app switcher
#                             need UI automation on the phone and are not this
#   run.sh push-data [what]   copy the game's RomFS, shared fonts and RomFS overlay (build/<game>/romfs_overlay, when the workspace has one)
#                             from the workspace into the app (Library/Application Support); what: some of romfs sysfont overlay (default all)
#   run.sh sim-build          the same build for the iOS Simulator (plan item 114: the on-screen controls, safe areas and lifecycle without a phone):
#                             Metal 3 (the Simulator SDK has no Metal 4 and no MetalFX), arm64, unsigned, the Simulator's shaders
#                             ($DM_ROOT/build/ios/sim/<game>/b-<chain>, ios/tools/sim_metal.py), no libvpx
#   run.sh sim-run [seconds]  on a Simulator (DM_SIM_DEVICE, default "iPhone 17 Pro"; booted without a window): install, clone the workspace's RomFS
#                             and fonts into the app (once; DM_SIM_DATA=fresh again), launch with DM_SIM_ENV="A=1 B=2" (DM_NVN_TBDR=0 unless given), wait, then a screenshot
#                             ($DM_ROOT/run/ios/sim.png) and Documents/boot.txt (sim_boot.txt). DM_SIM_BG=<at>,<len>: at <at> s the Settings app
#                             is opened (this one goes to the background) and after <len> s this one again, then tools/lifecycle_check.py judges it
#   run.sh sim-stop           shut the Simulators down (a booted one keeps its memory; sim-run boots it again)
# Build switches: DM_FREE_ACCOUNT=1 (free developer account: no iCloud / extended VA / sustained-execution entitlements), DM_IOS_M3=1 (older-API build,
# iOS 18), DM_DEVICE_FAMILY=1 (iPhone only), DM_ENV_FILE=<file> (DM_* switches read at launch, as Documents/env.txt is), DM_PRODUCT=<dir> (another
# product than the profile's workspace.product, relative to DM_ROOT or absolute; e.g. Tears of the Kingdom's build/totk/product60 with
# DM_OPTIONS=fps60,realtime,scene for 60 fps on a phone, plan item 62l), DM_OPTIONS (the code options written into the app, else the product's
# options.default; ios/tools/gen_embed.py).
set -u
cd "$(dirname "$0")"
source tools/local_env.sh
t() { echo "[$(date +%T)] $*"; }
ROOT=${DM_ROOT:-$HOME/dm-work}
GAME=${DM_GAME:-manosaba}; GJ=../games/$GAME/game.json
[ -f "$GJ" ] || { t "[red] no game profile $GJ (DM_GAME=$GAME)"; exit 1; }
get() { python3 ../tools/gen_game.py "$GJ" --get "$1"; }
ws() { local v; v=$(get "workspace.$1") || return 1; echo "$ROOT/$v"; }
R=$ROOT/run/ios; mkdir -p "$R"
# What local.env does not say (plan item 319, ios/tools/acct.py): the team Xcode is signed in with, the bundle ID derived from it, the phone connected
if [ -z "${DM_TEAM_ID:-}" ] && [ "${1:-}" != ipa ] && [ "${1:-}" != sim-build ] && [ "${1:-}" != sim-run ] && [ "${1:-}" != sim-stop ]; then
  if read -r DM_TEAM_ID _free < <(python3 tools/acct.py team); then
    export DM_TEAM_ID; [ "$_free" = 1 ] && [ -z "${DM_FREE_ACCOUNT:-}" ] && export DM_FREE_ACCOUNT=1
    t "team: the one Xcode is signed in with ($([ "$_free" = 1 ] && echo "a free personal team" || echo "a paid team"); set DM_TEAM_ID in local.env to choose)"
  fi
fi
if [ -z "${DM_BUNDLE_ID:-}" ] && [ -n "${DM_TEAM_ID:-}" ]; then DM_BUNDLE_ID=$(python3 tools/acct.py bundle "$GAME" "$DM_TEAM_ID"); export DM_BUNDLE_ID; t "bundle ID $DM_BUNDLE_ID (set DM_BUNDLE_ID in local.env to choose)"; fi
find_phone() {   # DM_DEVICE / DM_IOS_UDID from the one phone connected, when local.env names none
  [ -n "${DM_DEVICE:-}" ] && return 0
  xcrun devicectl list devices --json-output "$R/devices.json" > /dev/null 2>&1
  read -r DM_DEVICE DM_IOS_UDID < <(python3 tools/acct.py device "$R/devices.json") || return 1
  export DM_DEVICE DM_IOS_UDID; t "the phone connected: $DM_DEVICE"
}
# Why xcodebuild stopped, in words (plan item 319; S2Arm's findings): a keychain prompt nobody answered, a missing iOS platform, a locked phone
xcb_explain() {
  grep -aq 'BUILD INTERRUPTED' "$1" && t "[red] the keychain asked to let Xcode use your signing key and nobody answered: on this Mac's screen click Always Allow in the keychain dialog (your Mac login password if it asks), then run again"
  grep -aq 'is not installed. Please download and install the platform' "$1" && t "[red] this Xcode has no iOS platform (an Xcode update does not bring it): run xcodebuild -downloadPlatform iOS, then again"
  grep -aqi 'The device needs to be unlocked' "$1" && t "[red] unlock the phone and run again"
  return 0
}
DD=${DM_IOS_DD:-$R/dd}
APP=$DD/Build/Products/Release-iphoneos/DmIOS.app
CMD=${1:-}

if [ "$CMD" = build ] || [ "$CMD" = ipa ] || [ "$CMD" = sim-build ]; then
  if [ "$CMD" != build ]; then local_env_need DM_BUNDLE_ID || exit 1; else local_env_need DM_TEAM_ID DM_BUNDLE_ID || exit 1; fi
  OUT=$(ws product); MODS=$(get modules); TITLE=$(get title)
  if [ -n "${DM_PRODUCT:-}" ]; then case "$DM_PRODUCT" in /*) OUT=$DM_PRODUCT;; *) OUT=$ROOT/$DM_PRODUCT;; esac
    [ -f "$OUT/main.patched.bin" ] || { t "[red] DM_PRODUCT=$DM_PRODUCT: no product at $OUT"; exit 1; }
    t "product $OUT (DM_PRODUCT), code options ${DM_OPTIONS:-its options.default}"; fi
  rm -rf Gen; mkdir -p Gen/dm Gen/msl
  # Host sources, header paths and frameworks from host/Makefile; the game's modules as embedded code
  python3 tools/gen_build.py ../host Gen "$GAME" > Gen/dm.xcconfig || { t "[red] gen_build failed"; exit 1; }
  F=$(python3 tools/gen_embed.py "$OUT" Gen/embed.S $MODS) || { t "[red] gen_embed failed ($OUT)"; exit 1; }
  # VP8 / VP9 movies (plan item 62b): the iOS libvpx when it has been built (tools/build_libvpx.sh), checked first; host/hle/hle_movie.c finds its headers
  VPX=$ROOT/build/ios/libvpx; VPXL=; VPXI=
  if [ "$CMD" = sim-build ]; then t "Simulator: no libvpx (the iOS one is built for the device): VP8 / VP9 movies will not play"
  elif [ -f "$VPX/lib/libvpx.a" ]; then python3 tools/vpx_check.py "$VPX/lib/libvpx.a" 18.0 || exit 1; VPXL="\"$VPX/lib/libvpx.a\""; VPXI="\"$VPX/include\""
  else t "no iOS libvpx in $VPX: VP8 / VP9 movies will not play (ios/tools/build_libvpx.sh builds it)"; fi
  { echo "OTHER_LDFLAGS = \$(inherited) \$(DM_FRAMEWORKS) $F $VPXL"; echo "DM_VPX_INC = $VPXI"; echo "DM_GAME_NAME = $GAME"; } >> Gen/dm.xcconfig
  python3 tools/app_names.py Gen/Names "$GAME" "$TITLE" $(get apple_localizations) > /dev/null || exit 1   # the title per language (plan item 314d)
  PF=$(python3 tools/product_files.py "$OUT" $MODS) || { t "[red] the product's module lists are incomplete"; exit 1; }   # fixups, gates, option lists
  while IFS= read -r f; do cp "$f" Gen/dm/ || exit 1; done <<< "$PF"   # one path a line: the workspace path may hold a space
  cp "$(ws shader_map)" Gen/dm/shader_map.tsv || { t "[red] no shader stage map"; exit 1; }
  # The pipeline warm-up's seed (plan item 209, tools/pso_merge.py): pso_seed.tsv beside the map goes in as pso_manifest.tsv, which the warm-up
  # reads before the player's own list. The Mac host does not read pso_seed.tsv (its prologue: 55 long frames with the seed warming, 29 without)
  PM="$(dirname "$(ws shader_map)")/pso_seed.tsv"; rm -f Gen/dm/pso_manifest.tsv
  if [ -f "$PM" ]; then cp "$PM" Gen/dm/pso_manifest.tsv && t "pipeline warm-up seed: $(grep -vc '^#' "$PM") rows ($PM)"; fi
  if [ -n "${DM_ENV_FILE:-}" ]; then cp "$DM_ENV_FILE" Gen/dm/env.txt || exit 1; t "env switches baked in from $DM_ENV_FILE"; fi   # read at launch (host/envfile.h)
  C1=$(ws c1) && [ -f "$C1" ] && cp "$C1" Gen/dm/c1_full.tsv
  # iOS shaders of the current translation chain version (the Simulator's own: ios/tools/sim_metal.py)
  MSL=$(ws msl_ios); V=$(sed -n 's/^#define DMSH_CHAIN "\(.*\)"/\1/p' ../gfx/nvn/dmsh_chain.h)
  [ "$CMD" = sim-build ] && MSL=$ROOT/build/ios/sim/$GAME
  [ -n "$V" ] && ls "$MSL/b-$V" 2>/dev/null | grep -q "_b.metallib$" || { t "[red] no iOS shaders in $MSL/b-$V (gfx/tools/dmsh_build.py with IOSOUT)"; exit 1; }
  python3 tools/shader_check.py "$(ws shader_map)" "$MSL/b-$V" "../games/$GAME/fout.tsv" || { t "[red] build the game's iOS shaders first (gfx/tools/dmsh_build.py with IOSOUT)"; exit 1; }   # every shader of the map (plan item 174)
  # Only the game's libraries, a library of the same bytes as another once (plan item 311b: the chain's directory holds both games'; manosaba's IPA
  # carried Tears of the Kingdom's 28,652), then checked again as packed
  PK=$(python3 ../gfx/tools/shader_pack.py "$(ws shader_map)" "$MSL/b-$V" "Gen/msl/b-$V" "../games/$GAME/fout.tsv") || { t "$PK"; exit 1; }; t "$PK"
  python3 tools/shader_check.py "$(ws shader_map)" "Gen/msl/b-$V" "../games/$GAME/fout.tsv" > /dev/null || { t "[red] the packed shader directory is not complete"; exit 1; }
  # MSL sources for sampler border variants (plan item 214, tools/border_msl.py): the game's listed shaders, from the Mac's MSL
  if [ "$CMD" != sim-build ] && [ -f "../games/$GAME/border_msl.txt" ]; then
    python3 tools/border_msl.py "../games/$GAME/border_msl.txt" "$(ws msl_mac)/b-$V" "Gen/msl/b-$V" || { t "[red] the MSL of a border variant shader is missing (see above)"; exit 1; }
  fi
  # ... and for sampler LOD bias variants (plan item 318: a GPU that ignores a sampler's LOD bias, or an OS before 26)
  if [ "$CMD" != sim-build ] && [ -f "../games/$GAME/lodbias_msl.txt" ]; then
    python3 tools/border_msl.py "../games/$GAME/lodbias_msl.txt" "$(ws msl_mac)/b-$V" "Gen/msl/b-$V" "LOD bias" || { t "[red] the MSL of an LOD bias variant shader is missing (see above)"; exit 1; }
  fi
  # App icon: the game's icon from the workspace (1024x1024 opaque PNG; game art is never committed); without one the app has no icon
  mkdir -p Gen/AppAssets.xcassets/AppIcon.appiconset; echo '{"info":{"version":1,"author":"xcode"}}' > Gen/AppAssets.xcassets/Contents.json
  ICON=$(ws icon 2>/dev/null)
  if [ -n "$ICON" ] && [ -f "$ICON" ]; then
    cp "$ICON" Gen/AppAssets.xcassets/AppIcon.appiconset/AppIcon1024.png
    echo '{"images":[{"filename":"AppIcon1024.png","idiom":"universal","platform":"ios","size":"1024x1024"}],"info":{"version":1,"author":"xcode"}}' > Gen/AppAssets.xcassets/AppIcon.appiconset/Contents.json
  else
    t "no icon at ${ICON:-(workspace.icon not set)}: building without an app icon"
    echo '{"images":[],"info":{"version":1,"author":"xcode"}}' > Gen/AppAssets.xcassets/AppIcon.appiconset/Contents.json
  fi
  # Variants of the spec go next to project.yml (xcodegen resolves paths relative to the spec) and are removed afterwards
  SPEC=project.yml; FREE=
  python3 tools/project_lang.py $SPEC project_lang.yml $(get apple_localizations) || exit 1; SPEC=project_lang.yml   # the per-app language setting (plan item 170)
  if [ "${DM_FREE_ACCOUNT:-0}" = 1 ]; then python3 tools/project_free.py $SPEC project_free.yml || { rm -f project_lang.yml; exit 1; }; SPEC=project_free.yml; FREE=free; fi
  if [ "${DM_IOS_M3:-0}" = 1 ] || [ "$CMD" = sim-build ]; then python3 tools/project_m3.py $SPEC project_m3.yml || { rm -f project_lang.yml project_free.yml; exit 1; }; SPEC=project_m3.yml; fi
  xcodegen generate -q --spec $SPEC >/dev/null; XG=$?; rm -f project_lang.yml project_free.yml project_m3.yml
  [ $XG = 0 ] || { t "[red] xcodegen failed"; exit 1; }
  BN=$(git log -1 --format=%ct 2>/dev/null); [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ] && BN=1; BN=${BN:-1}
  if [ "$CMD" = sim-build ]; then
    t "building $TITLE ($GAME) for the Simulator (Metal 3, arm64, unsigned), build number $BN"
    xcodebuild -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' \
      -derivedDataPath "$R/dd-sim" ARCHS=arm64 ONLY_ACTIVE_ARCH=NO CODE_SIGNING_ALLOWED=NO DM_BUILD_NUMBER=$BN build > "$R/build-sim.log" 2>&1 || {
        t "[red] build failed (see $R/build-sim.log)"; grep -E 'error:|BUILD FAILED' "$R/build-sim.log" | head -20; exit 1; }
    t "built $R/dd-sim/Build/Products/Release-iphonesimulator/DmIOS.app"; exit 0
  fi
  if [ "$CMD" = ipa ]; then
    # Built without signing; packaged signed ad hoc with the increased memory limit alone (below). Whoever installs it signs it with their own
    # certificate and profile, which decide what the app may use (iCloud save sync, ...). Its own derived data, so the signed build of run.sh
    # build stays installable
    DDI=$R/dd-ipa; OSV=ios26; [ "${DM_IOS_M3:-0}" = 1 ] && OSV=ios18
    t "building $TITLE ($GAME) unsigned, build number $BN"
    xcodebuild -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination 'generic/platform=iOS' -derivedDataPath "$DDI" \
      CODE_SIGNING_ALLOWED=NO DM_BUILD_NUMBER=$BN ${DM_DEVICE_FAMILY:+DM_DEVICE_FAMILY=$DM_DEVICE_FAMILY} build > "$R/build.log" 2>&1 || {
        t "[red] build failed (see $R/build.log)"; grep -E 'error:|BUILD FAILED' "$R/build.log" | head -20; exit 1; }
    W=$R/ipa; IPA=$R/$GAME-$OSV.ipa; [ "${DM_IPA_DATA:-0}" = 1 ] && IPA=$R/$GAME-$OSV-data.ipa
    rm -rf "$W" "$IPA"; mkdir -p "$W/Payload"
    ditto "$DDI/Build/Products/Release-iphoneos/DmIOS.app" "$W/Payload/DmIOS.app" || { t "[red] copying the app failed"; exit 1; }
    if [ "${DM_IPA_DATA:-0}" = 1 ]; then
      # The game data inside the app (Sources/main.m looks there after Application Support and Documents); the app checks it at start as usual
      for x in romfs:RomFS sysfont:sysfont; do
        src=$(ws "${x%%:*}") && [ -d "$src" ] || { t "[red] no ${x%%:*} at ${src:-(workspace.${x%%:*} not set)}"; exit 1; }
        src=$(cd "$src" && pwd -P)
        t "adding $src as ${x##*:}"; ditto "$src" "$W/Payload/DmIOS.app/${x##*:}" || { t "[red] copying ${x%%:*} failed"; exit 1; }
      done
      # The game's RomFS overlay when the workspace has one (Tears of the Kingdom's quick menus and game over at 60 fps; main.m serves it when it
      # was made from the RomFS beside it)
      ov=$ROOT/build/$GAME/romfs_overlay
      if [ -d "$ov" ]; then t "adding $ov as romfs_overlay"; ditto "$ov" "$W/Payload/DmIOS.app/romfs_overlay" || { t "[red] copying the overlay failed"; exit 1; }; fi
    fi
    xattr -cr "$W/Payload"
    # Signed ad hoc with the increased memory limit (plan item 211, tools/ipa_sign.py): AltStore asks for the App ID features the app's own signature
    # names; another signer replaces this signature with its own
    python3 tools/ipa_sign.py sign "$W/Payload/DmIOS.app" || { t "[red] signing the app ad hoc failed"; exit 1; }
    # Files already compressed are stored, not deflated again (nxnative's zip_store, MIT): 130 MB of Tears of the Kingdom's .zs took 1.66 s to
    # deflate into a 17 KB larger archive, 0.26 s to store. DM_ZIP_STORE replaces the suffix list
    ZS=${DM_ZIP_STORE:-.zs:.webm:.mp4:.png:.jpg:.ogg:.opus}
    (cd "$W" && zip -qryX -n "$ZS" "$IPA" Payload -x '*/.DS_Store') || { t "[red] zip failed"; exit 1; }
    t "stored without deflate ($ZS): $(zipinfo "$IPA" | awk '$6 == "stor" && $NF !~ /\/$/' | wc -l | tr -d ' ') files"
    rm -rf "$W"; t "packaged $IPA ($(python3 tools/ipa_sign.py size "$IPA"))"; exit 0
  fi
  DEST='generic/platform=iOS'; REG=()
  if [ -n "$FREE" ]; then
    [ -n "${DM_IOS_UDID:-}" ] || find_phone
    if [ -n "${DM_IOS_UDID:-}" ]; then DEST="id=$DM_IOS_UDID"; else t "free account: set DM_IOS_UDID and connect the phone, or signing has no device to target"; fi
  fi
  # a device to sign for: Xcode may register it with the team and waits 2 minutes for it (a locked phone has time to be unlocked; S2Arm's flags)
  [ "$DEST" != 'generic/platform=iOS' ] && REG=(-allowProvisioningDeviceRegistration -destination-timeout 120)
  t "building $TITLE ($GAME), build number $BN"
  xcodebuild -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination "$DEST" -derivedDataPath "$DD" -allowProvisioningUpdates ${REG[@]+"${REG[@]}"} \
    DM_BUILD_NUMBER=$BN ${DM_DEVICE_FAMILY:+DM_DEVICE_FAMILY=$DM_DEVICE_FAMILY} build > "$R/build.log" 2>&1 || {
      t "[red] build failed (see $R/build.log)"; grep -E 'error:|BUILD FAILED' "$R/build.log" | head -20; xcb_explain "$R/build.log"; exit 1; }
  t "built $APP"; exit 0
fi

if [ "$CMD" = sim-stop ]; then xcrun simctl shutdown all; t "Simulators shut down"; exit 0; fi
if [ "$CMD" = sim-run ]; then
  local_env_need DM_BUNDLE_ID || exit 1; B=$DM_BUNDLE_ID; WAIT=${2:-60}
  APPS=$R/dd-sim/Build/Products/Release-iphonesimulator/DmIOS.app; [ -d "$APPS" ] || { t "[red] no Simulator build: run.sh sim-build"; exit 1; }
  NAME=${DM_SIM_DEVICE:-iPhone 17 Pro}
  SIM=$(xcrun simctl list devices available -j | python3 -c 'import json,sys; n=sys.argv[1]; print(next((d["udid"] for r in json.load(sys.stdin)["devices"].values() for d in r if n in (d["name"], d["udid"])), ""))' "$NAME")
  [ -n "$SIM" ] || { t "[red] no available Simulator device \"$NAME\" (xcrun simctl list devices available)"; exit 1; }
  xcrun simctl boot "$SIM" 2>/dev/null; xcrun simctl bootstatus "$SIM" -b > /dev/null || { t "[red] the Simulator $NAME did not boot"; exit 1; }
  xcrun simctl install "$SIM" "$APPS" || { t "[red] install failed"; exit 1; }
  DATA=$(xcrun simctl get_app_container "$SIM" "$B" data) || { t "[red] no data container"; exit 1; }
  mkdir -p "$DATA/Library/Application Support" "$DATA/Documents"
  for x in romfs:romfs sysfont:sysfont; do   # the workspace's data cloned in (APFS: no bytes copied); a link would make the Simulator ask for the folder's
    dst="$DATA/Library/Application Support/${x##*:}"   # files (the Desktop's, say: the app then waits in open() for that answer). Once; DM_SIM_DATA=fresh again
    { [ "${DM_SIM_DATA:-}" = fresh ] || [ -L "$dst" ]; } && rm -rf "$dst"; [ -d "$dst" ] && continue
    src=$(ws "${x%%:*}") && [ -d "$src" ] || { t "skipped ${x%%:*}: no ${src:-(workspace.${x%%:*} not set)}"; continue; }
    t "cloning $(cd "$src" && pwd -P) → Library/Application Support/${x##*:}"
    cp -cR "$(cd "$src" && pwd -P)" "$dst.part" && mv "$dst.part" "$dst" || { t "[red] cloning ${x%%:*} failed (another volume?)"; rm -rf "$dst.part"; exit 1; }
  done
  rm -f "$DATA/Documents/boot.txt"
  export SIMCTL_CHILD_DM_NVN_TBDR=0   # the TBDR rewrites' pass merges load readable-tile libraries the Simulator's shader directory leaves out
  for kv in ${DM_SIM_ENV:-}; do export "SIMCTL_CHILD_$kv"; done
  xcrun simctl launch --terminate-running-process "$SIM" "$B" > "$R/sim-launch.log" 2>&1 || { t "[red] launch failed"; cat "$R/sim-launch.log"; exit 1; }
  if [ -n "${DM_SIM_BG:-}" ]; then
    AT=${DM_SIM_BG%%,*}; LEN=${DM_SIM_BG##*,}
    t "launched on $NAME; to the background at ${AT}s for ${LEN}s (the Settings app opened), waiting $((AT + LEN + WAIT))s in all"
    sleep "$AT"; xcrun simctl launch "$SIM" com.apple.Preferences > /dev/null; sleep "$LEN"; xcrun simctl launch "$SIM" "$B" > /dev/null
  else t "launched on $NAME, waiting ${WAIT}s"; fi
  sleep "$WAIT"
  xcrun simctl io "$SIM" screenshot "$R/sim.png" > /dev/null 2>&1 && t "screenshot: $R/sim.png"
  cp "$DATA/Documents/boot.txt" "$R/sim_boot.txt" 2>/dev/null || cp "$DATA/Documents/boot.launch.txt" "$R/sim_boot.txt" 2>/dev/null || { t "[red] no log"; exit 1; }
  t "log: $R/sim_boot.txt"; tail -20 "$R/sim_boot.txt"
  if [ -n "${DM_SIM_BG:-}" ]; then python3 -I ../tools/lifecycle_check.py "$R/sim_boot.txt"; exit $?; fi
  exit 0
fi

find_phone
local_env_need DM_DEVICE DM_BUNDLE_ID || exit 1
D=$DM_DEVICE; B=$DM_BUNDLE_ID
pull() {   # $1 = file under Documents, $2 = destination
  xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source "Documents/$1" --destination "$2" >/dev/null 2>&1
}
case "$CMD" in
install)
  # Before installing (plan item 173, after Touchable's launcher): the signature valid and listing this device, and the app's Documents (the save)
  # backed up with a SHA-256 manifest when the app is there already; a backup that fails stops the install
  U=${DM_IOS_UDID:-}
  if [ -z "$U" ]; then xcrun devicectl list devices --json-output "$R/devices.json" > /dev/null 2>&1; U=$(python3 tools/install_check.py udid "$R/devices.json" "$D"); fi
  python3 tools/install_check.py profile "$APP" "$U" || exit 1
  if xcrun devicectl device info apps --device "$D" --json-output "$R/apps.json" > /dev/null 2>&1 && python3 tools/install_check.py installed "$R/apps.json" "$B"; then
    BK=$R/backup/$GAME-$(date +%Y%m%d-%H%M%S); mkdir -p "$BK"
    xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source Documents --destination "$BK" > "$R/backup.log" 2>&1 \
      && python3 tools/install_check.py manifest "$BK" > /dev/null || { t "[red] backing up the app's Documents failed (see $R/backup.log): not installed"; exit 1; }
    t "the app's Documents backed up to $BK ($(python3 -c "import json,sys; print(len(json.load(open(sys.argv[1]))))" "$BK/manifest.json") files)"
  fi
  xcrun devicectl device install app --device "$D" "$APP" > "$R/install.log" 2>&1 || { t "[red] install failed"; tail -5 "$R/install.log"; exit 1; }
  t "installed"; exit 0 ;;
resign)
  # Signed again instead of built again (plan item 319): a fresh development profile for this app and phone, else Xcode is asked for one through
  # the minimal app of tools/prov (with the entitlements of the last build), and resign.py tried again
  [ -d "$APP" ] || { t "[red] no built app at $APP: run.sh build first"; exit 1; }
  local_env_need DM_TEAM_ID || exit 1
  U=${DM_IOS_UDID:-}; [ -n "$U" ] || { xcrun devicectl list devices --json-output "$R/devices.json" > /dev/null 2>&1; U=$(python3 tools/install_check.py udid "$R/devices.json" "$D"); }
  T0=$(date +%s); rc=3; [ "${DM_RESIGN_RENEW:-0}" = 1 ] || { python3 tools/resign.py "$APP" "$B" "$DM_TEAM_ID" "$U"; rc=$?; }   # DM_RESIGN_RENEW=1: ask Xcode first
  if [ $rc = 3 ]; then
    t "asking Xcode for a development profile (the minimal app of tools/prov)"
    PV=$(mktemp -d "$R/prov.XXXXXX"); cp tools/prov/project.yml tools/prov/main.m "$PV/" || exit 1
    if [ -f Sources/DmIOS.entitlements ]; then cp Sources/DmIOS.entitlements "$PV/Prov.entitlements"; else printf '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict/></plist>\n' > "$PV/Prov.entitlements"; fi
    sed -i '' -e "s/DMBID/$B/" -e "s/DMTEAM/$DM_TEAM_ID/" "$PV/project.yml"
    (cd "$PV" && xcodegen generate -q > /dev/null && xcodebuild -project Prov.xcodeproj -scheme Prov -configuration Debug -destination "id=$U" -derivedDataPath dd \
       -allowProvisioningUpdates -allowProvisioningDeviceRegistration -destination-timeout 120 build > "$R/prov.log" 2>&1) || {
      t "[red] Xcode made no profile (see $R/prov.log)"; grep -a 'error:' "$R/prov.log" | tail -3; xcb_explain "$R/prov.log"; mv "$PV" "$R/prov.failed" 2>/dev/null; exit 1; }
    mv "$PV" ~/.Trash/ 2>/dev/null
    python3 tools/resign.py "$APP" "$B" "$DM_TEAM_ID" "$U"; rc=$?
  fi
  [ $rc = 0 ] || { t "[red] signing again failed"; exit 1; }
  t "signed again in $(( $(date +%s) - T0 )) s; installing"; exec bash ./run.sh install ;;   # (run.sh is in the directory it changed to)
launch)
  WAIT=${2:-30}
  xcrun devicectl device process launch --device "$D" --terminate-existing "$B" > "$R/launch.log" 2>&1
  grep -q Launched "$R/launch.log" || { t "[red] launch failed"; tail -8 "$R/launch.log"; exit 1; }
  t "launched, waiting ${WAIT}s"; sleep "$WAIT"
  pull boot.txt "$R/boot.txt" || pull boot.launch.txt "$R/boot.txt" || { t "[red] no log yet"; exit 1; }
  tail -40 "$R/boot.txt"; t "log: $R/boot.txt"; exit 0 ;;
log)
  pull boot.txt "$R/boot.txt" || pull boot.launch.txt "$R/boot.txt" || { t "[red] no log"; exit 1; }
  t "log: $R/boot.txt"; tail -40 "$R/boot.txt"; exit 0 ;;
lifecycle)
  AT=${2:-40}; LEN=${3:-20}; WAIT=$((AT + LEN + 40))
  DEVICECTL_CHILD_DM_TEST_BG="$AT,$LEN" xcrun devicectl device process launch --device "$D" --terminate-existing "$B" > "$R/launch.log" 2>&1
  grep -q Launched "$R/launch.log" || { t "[red] launch failed"; tail -8 "$R/launch.log"; exit 1; }
  t "launched; to the background at ${AT}s for ${LEN}s, waiting ${WAIT}s"; sleep "$WAIT"
  pull boot.txt "$R/lifecycle_boot.txt" || { t "[red] no log"; exit 1; }
  t "log: $R/lifecycle_boot.txt"; python3 -I ../tools/lifecycle_check.py "$R/lifecycle_boot.txt"; exit $? ;;
push-data)
  # The RomFS overlay (games/<game>/romfs_overlay.py) goes with the data, as MacNativeGames ships its overlays: without it Tears of the Kingdom's
  # 60 fps product drew the ability wheel over black on the iPhone (2026-10-22). "push-data overlay" copies it alone
  WANT=" ${*:2} "; [ "$WANT" = "  " ] && WANT=" romfs sysfont overlay "
  for x in romfs:romfs sysfont:sysfont overlay:romfs_overlay; do
    case "$WANT" in *" ${x%%:*} "*) ;; *) continue ;; esac
    if [ "${x%%:*}" = overlay ]; then src=$ROOT/build/$GAME/romfs_overlay; else src=$(ws "${x%%:*}") || continue; fi
    [ -d "$src" ] || { t "skipped ${x%%:*}: no $src"; continue; }
    src=$(cd "$src" && pwd -P)   # the workspace often links to the data elsewhere; devicectl does not follow a symlinked source directory ("Is a directory")
    # By content (plan item 319, tools/push_hash.py): only what the phone lacks, holds at another size, or what changed since the last push
    # (its SHA-256 in $R/pushed) is linked into a staging directory and copied; devicectl merges it into the destination
    DST="Library/Application Support/${x##*:}"; M=$R/pushed/${D}_${B}_${x##*:}.json; mkdir -p "$R/pushed"
    xcrun devicectl device info files --device "$D" --domain-type appDataContainer --domain-identifier "$B" --subdirectory "$DST" --json-output "$R/have.json" > /dev/null 2>&1 || echo '{}' > "$R/have.json"
    ST=$(mktemp -d "$R/stage.XXXXXX")
    MSG=$(python3 tools/push_hash.py stage "$src" "$M" "$R/have.json" "$ST") || { t "[red] staging the files to copy failed"; exit 1; }; t "$src → $DST: $MSG"
    SRC=$ST; [ -f "$ST/.dm_all" ] && SRC=$src   # every file to copy: the source itself
    if [ -n "$(ls -A "$ST")" ]; then
      xcrun devicectl device copy to --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source "$SRC" --destination "$DST" > "$R/push.log" 2>&1 || {
        t "[red] copy failed; run push-data again to go on (what is on the phone at its size and not changed is not copied again)"; tail -5 "$R/push.log"; mv "$ST" ~/.Trash/ 2>/dev/null; exit 1; }
    fi
    python3 tools/push_hash.py commit "$M"; mv "$ST" ~/.Trash/ 2>/dev/null
  done
  t "done"; exit 0 ;;
*)
  sed -n '2,16p' "$0"; exit 1 ;;
esac
