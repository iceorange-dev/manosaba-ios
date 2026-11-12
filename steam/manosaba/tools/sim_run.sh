#!/bin/zsh
# Build the simulator project, install it, launch with console capture, and take screenshots.
# Usage: sim_run.sh <tag> [seconds=40] [sim-udid]
set -euo pipefail

tag=${1:?tag}
secs=${2:-40}
sim=${3:-${MANOSABA_SIM:?pass a simulator UDID or set MANOSABA_SIM}}
bundle=${MANOSABA_BUNDLE_ID:-com.example.manosaba}
root=${0:A:h:h}
logs="$root/logs"
mkdir -p "$logs/shots"

echo "[sim_run] build"
(cd "$root/build/xcode-sim" && xcodebuild -project Unity-iPhone.xcodeproj -scheme Unity-iPhone -configuration Release \
  -sdk iphonesimulator -destination "id=$sim" -derivedDataPath "$root/build/dd-sim" \
  CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM= PROVISIONING_PROFILE_SPECIFIER= build \
  > "$logs/xcodebuild-sim-$tag.log" 2>&1) || { command grep -E 'error:' "$logs/xcodebuild-sim-$tag.log" | head; exit 1; }

app="$root/build/dd-sim/Build/Products/Release-iphonesimulator/manosaba.app"
xcrun simctl bootstatus "$sim" -b >/dev/null
xcrun simctl terminate "$sim" "$bundle" 2>/dev/null || true
xcrun simctl install "$sim" "$app"
echo "[sim_run] launch for ${secs}s"
timeout "$secs" xcrun simctl launch --console-pty "$sim" "$bundle" > "$logs/sim-$tag.log" 2>&1 &
pid=$!
half=$(( secs / 2 ))
timeout $(( half + 2 )) zsh -c "sleep $half" || true
xcrun simctl io "$sim" screenshot "$logs/shots/sim-$tag-a.png" >/dev/null 2>&1
wait $pid || true
xcrun simctl io "$sim" screenshot "$logs/shots/sim-$tag-b.png" >/dev/null 2>&1 || true
for s in a b; do sips -Z 1000 "$logs/shots/sim-$tag-$s.png" --out "$logs/shots/sim-$tag-$s-small.png" >/dev/null 2>&1 || true; done
echo "[sim_run] log: $logs/sim-$tag.log ($(wc -l < "$logs/sim-$tag.log") lines)"
command grep -vE 'memorysetup-|UnityMemory\] Configuration|^\s*$|^UnityEngine\.|^\[\./Runtime|Player connection|ASTC.* not supported' "$logs/sim-$tag.log" | cut -c1-220 | tail -25
