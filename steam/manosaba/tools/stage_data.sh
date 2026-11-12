#!/bin/zsh
# Replace the shell's Data/ with the Steam build's data (APFS clones, so no extra disk).
# Keeps the iOS engine's own "unity default resources" and Managed/mono config.
# Usage: stage_data.sh <manosaba.app> <xcode-dir>
set -euo pipefail

app=${1:?usage: stage_data.sh <manosaba.app> <Unity-generated Xcode project dir>}
xcode=${2:?usage: stage_data.sh <manosaba.app> <Unity-generated Xcode project dir>}
# "$xcode/Data" is deleted below: only ever inside a Unity-generated iOS Xcode project.
[[ -d "$xcode/Unity-iPhone.xcodeproj" ]] || { echo "not a Unity-generated Xcode project (no Unity-iPhone.xcodeproj): $xcode" >&2; exit 1; }
src="$app/Contents/Resources/Data"
dst="$xcode/Data"
keep=$(mktemp -d)
trap 'rm -rf "$keep"' EXIT

[[ -f "$src/globalgamemanagers" ]] || { echo "no game data at $src" >&2; exit 1; }
echo "[stage_data] $src -> $dst"

/bin/cp -c "$dst/Resources/unity default resources" "$keep/"
/bin/cp -cR "$dst/Managed/mono" "$keep/mono"
rm -rf "$dst"
mkdir -p "$dst/Resources" "$dst/Managed"

for f in globalgamemanagers globalgamemanagers.assets globalgamemanagers.assets.resS level0 \
         resources.assets resources.assets.resS sharedassets0.assets sharedassets0.assets.resS \
         RuntimeInitializeOnLoads.json ScriptingAssemblies.json boot.config; do
  /bin/cp -c "$src/$f" "$dst/$f"
done
/bin/cp -c "$src/Resources/unity_builtin_extra" "$dst/Resources/"
/bin/cp -c "$keep/unity default resources" "$dst/Resources/"
/bin/cp -cR "$src/il2cpp_data/Metadata" "$dst/Managed/Metadata"
/bin/cp -cR "$src/il2cpp_data/Resources" "$dst/Managed/Resources"
/bin/cp -cR "$keep/mono" "$dst/Managed/mono"
# iOS serves StreamingAssets from Data/Raw; Addressables paths keep their StandaloneOSX folder.
/bin/cp -cR "$src/StreamingAssets" "$dst/Raw"
# SKIP_VOICE=1: drop the voice bundles (4.3 GB as shipped, ~1.1 GB after transcode_audio.py) for quick smoke tests.
if [[ "${SKIP_VOICE:-0}" == 1 ]]; then
  rm -f "$dst"/Raw/aa/StandaloneOSX/general-voice-*
  echo "[stage_data] SKIP_VOICE=1: voice bundles omitted"
fi

# Saves record the game version that wrote them (VersioningManager); report the Steam
# build's version instead of the shell's "1.0" so Mac saves load as same-version saves.
ver=$(plutil -extract CFBundleShortVersionString raw "$app/Contents/Info.plist")
plutil -replace CFBundleShortVersionString -string "$ver" "$xcode/Info.plist"
echo "[stage_data] CFBundleShortVersionString=$ver"

find "$dst" -maxdepth 2 | sort | sed "s|$dst|Data|"
du -sh "$dst" | cut -f1
