#!/bin/bash
# Package the Mac host as a double-clickable app: mac_app.sh [output directory] → <output>/<game title>.app (default $DM_ROOT/out/macapp).
# The game is DM_GAME (games/<id>/game.json, default manosaba); its workspace paths (relative to DM_ROOT) give the product, the shader stage map,
# the Mac shaders and the shared fonts. The RomFS is not packaged: the player chooses it on first launch (host/front/app_mac.m).
# DM_MAC_LINK=1 (default): the package holds only the host and the shader stage map, and Resources/paths.txt points at the product, shaders and fonts
#   on this machine — everything derived from the game stays out of the package, which then runs only here. DM_MAC_LINK=0 copies them in.
# DM_MAC_M3=1: older-API build (no Metal 4, minimum macOS = MACOS_MIN_M3 of host/Makefile); its shaders are recompiled for that system.
# DM_MAC_VERSION (default 1.0) / DM_MAC_BUILD (default month-day-hour-minute); DM_MAC_BUNDLE_ID (default local.dm.<game id>). Ad-hoc signed.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd); DM=$HERE/..; ROOT=${DM_ROOT:-$HOME/dm-work}
GAME=${DM_GAME:-manosaba}; GJ=$DM/games/$GAME/game.json
[ -f "$GJ" ] || { echo "[red] no game profile $GJ"; exit 2; }
get() { python3 "$HERE/gen_game.py" "$GJ" --get "$1"; }
ws() { echo "$ROOT/$(get "workspace.$1")"; }
OUT=${1:-$ROOT/out/macapp}; M3=${DM_MAC_M3:-}
TITLE=$(get title); NAME="$TITLE${M3:+ M3}"; BID=${DM_MAC_BUNDLE_ID:-local.dm.$GAME${M3:+.m3}}; EXE=$GAME
P=$(ws product); [ -f "$P/main.patched.bin" ] || { echo "[red] no product at $P"; exit 2; }
make -s -C "$DM/host" GAME=$GAME ${M3:+dmhost_m3} > /dev/null 2>&1 || { echo "[red] host build failed"; exit 2; }
MIN=$(make -s -C "$DM/host" GAME=$GAME print-macos-min${M3:+-m3})   # the same minimum system the host is compiled for
APP="$OUT/$NAME.app"; RES="$APP/Contents/Resources"; rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$RES"
cp "$DM/host/dmhost${M3:+_m3}" "$APP/Contents/MacOS/$EXE"
LINK=${DM_MAC_LINK-1}; [ "$LINK" != 0 ] || LINK=; : > "$RES/paths.txt"
item() {   # item <name> <path>: copy into Resources/<name>, or record it in paths.txt
  if [ -n "$LINK" ]; then echo "$1=$2" >> "$RES/paths.txt"; else cp -R "$2" "$RES/$1"; fi
}
item product "$P"
cp "$(ws shader_map)" "$RES/shader_map.tsv"
C1=$(ws c1); [ ! -f "$C1" ] || cp "$C1" "$RES/c1_full.tsv"   # read next to the shader stage map
FONT=$(ws sysfont); [ ! -d "$FONT" ] || item sysfont "$FONT"
# Shaders of the current translation chain version (gfx/tools/dmsh_build.py); the older-API build needs them recompiled for its minimum system
V=$(sed -n 's/.*DMSH_CHAIN "\(.*\)".*/\1/p' "$DM/gfx/nvn/dmsh_chain.h")
MSLROOT=$(ws msl_mac)
if [ -n "$M3" ]; then
  MSL_SRC=$MSLROOT bash "$DM/gfx/tools/msl_retarget.sh" "$MIN" "$OUT/msl_min$MIN" > /dev/null || { echo "[red] recompiling the shaders for macOS $MIN failed"; exit 2; }
  MSLROOT=$OUT/msl_min$MIN
fi
[ -d "$MSLROOT/b-$V" ] || { echo "[red] no shaders at $MSLROOT/b-$V"; exit 2; }
if [ -n "$LINK" ]; then echo "msl=$MSLROOT" >> "$RES/paths.txt"
else python3 "$DM/gfx/tools/shader_pack.py" "$(ws shader_map)" "$MSLROOT/b-$V" "$RES/msl/b-$V" "$DM/games/$GAME/fout.tsv" > /dev/null || exit 2; fi   # the game's libraries only (plan item 311b)
[ -s "$RES/paths.txt" ] || rm -f "$RES/paths.txt"
VER=${DM_MAC_VERSION:-1.0}; BUILD=${DM_MAC_BUILD:-$(date +%m%d%H%M)}
echo "version $VER build $BUILD product $(basename "$P") commit $(git -C "$DM" rev-parse --short HEAD 2>/dev/null || echo unknown)" > "$RES/build.txt"
# Game Mode off (LSSupportsGameMode false; seedds/nxnative cb59a67, MIT): in fullscreen Game Mode macOS held the GPU at its lowest clock under load
# (Tears of the Kingdom 10.5 -> 19.7 ms a frame, 338 MHz at 94 % busy against 924 MHz without it; a plain Metal app the same). A bundle macOS
# already took for a game may need its Game Overlay switched off once. iOS keeps Game Mode (ios/project.yml). tools/tests/t_mac_app.py
cat > "$APP/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>$NAME</string>
<key>CFBundleDisplayName</key><string>$NAME</string>
<key>CFBundleIdentifier</key><string>$BID</string>
<key>CFBundleExecutable</key><string>$EXE</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VER</string>
<key>CFBundleVersion</key><string>$BUILD</string>
<key>LSMinimumSystemVersion</key><string>$MIN</string>
<key>NSHighResolutionCapable</key><true/>
<key>LSApplicationCategoryType</key><string>public.app-category.games</string>
<key>LSSupportsGameMode</key><false/>
<key>GCSupportsControllerUserInteraction</key><true/>
</dict></plist>
PL
# Checks: no Metal 4 symbol in the older-API build; the executable's minimum system equals Info.plist's; arm64 only
[ -z "$M3" ] || { N4=$(nm "$APP/Contents/MacOS/$EXE" | grep -c MTL4 || true); [ "$N4" = 0 ] || { echo "[red] the older-API build still references $N4 Metal 4 symbols"; exit 3; }; }
BIN_MIN=$(otool -l "$APP/Contents/MacOS/$EXE" | awk '$1=="minos"{print $2; exit}')
[ "$BIN_MIN" = "$MIN" ] || { echo "[red] minimum system: executable ${BIN_MIN:-?}, Info.plist $MIN"; exit 3; }
[ "$(lipo -archs "$APP/Contents/MacOS/$EXE")" = arm64 ] || { echo "[red] the executable is not arm64 only"; exit 3; }
codesign --force -s - "$APP" > /dev/null 2>&1 || { echo "[red] ad-hoc signing failed"; exit 3; }
echo "MACAPP $APP (${M3:+older API, }version $VER build $BUILD, macOS $MIN+, product $(basename "$P"), $(du -sh "$APP" | cut -f1)${LINK:+, links to files on this machine})"
