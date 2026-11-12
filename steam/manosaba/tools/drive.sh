#!/bin/zsh
# Drive the installed game in the simulator with a step script (see tools/uitest/Drive/DriveTests.swift).
# Usage: drive.sh <tag> "<steps>" [launch=1] [sim-udid]
#   e.g. drive.sh title "rotate:left;wait:25;shot:title;tap:0.22,0.80;wait:5;shot:newgame"
set -euo pipefail

tag=${1:?tag}
# logs/drive/<tag> is deleted and recreated below, so the tag must be a plain folder name.
[[ $tag =~ '^[A-Za-z0-9][A-Za-z0-9._-]*$' ]] || { echo "drive.sh: tag must be a plain name (letters, digits, . _ -): $tag" >&2; exit 2; }
steps=${2:?steps}
launch=${3:-1}
sim=${4:-${MANOSABA_SIM:?pass a device/simulator UDID or set MANOSABA_SIM}}
root=${0:A:h:h}
out="$root/logs/drive/$tag"
rm -rf "$out"; mkdir -p "$out"

cd "$root/tools/uitest"
[[ -d ManosabaDrive.xcodeproj ]] || MANOSABA_BUNDLE_ID=${MANOSABA_BUNDLE_ID:-com.example.manosaba} xcodegen generate --quiet
# Simulators take ad-hoc signing; a physical device (UDID 0000XXXX-...) needs the team.
if [[ "$sim" == 0000* ]]; then
  sign=(-allowProvisioningUpdates CODE_SIGN_STYLE=Automatic DEVELOPMENT_TEAM=${DEVELOPMENT_TEAM:?set DEVELOPMENT_TEAM})
  # Screenshots are written on the device; pull them back with devicectl afterwards.
else
  sign=(CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO)
fi
TEST_RUNNER_APP_BUNDLE_ID="${MANOSABA_BUNDLE_ID:-com.example.manosaba}" TEST_RUNNER_STEPS="$steps" TEST_RUNNER_OUT_DIR="$out" TEST_RUNNER_LAUNCH="$launch" \
  xcodebuild test -project ManosabaDrive.xcodeproj -scheme Drive -destination "id=$sim" \
  -derivedDataPath "$root/build/dd-drive" "${sign[@]}" -resultBundlePath "$out/result.xcresult" \
  > "$root/logs/drive-$tag.log" 2>&1 || { command grep -E 'error|failed|Fail' "$root/logs/drive-$tag.log" | head -10; }
# Pull screenshot attachments out of the result bundle (works for devices and simulators).
xcrun xcresulttool export attachments --path "$out/result.xcresult" --output-path "$out/attachments" >/dev/null 2>&1 || true
if [[ -f "$out/attachments/manifest.json" ]]; then
  python3 -I - "$out" <<'PY'
import json, os, shutil, sys
out = sys.argv[1]; att = os.path.join(out, "attachments")
for test in json.load(open(os.path.join(att, "manifest.json"))):
    for a in test.get("attachments", []):
        name = (a.get("suggestedHumanReadableName") or a["exportedFileName"]).split("_")[0]
        shutil.copy(os.path.join(att, a["exportedFileName"]), os.path.join(out, name + ".png"))
PY
fi
for f in "$out"/*.png(N); do sips -Z 1000 "$f" --out "${f%.png}-small.jpg" -s format jpeg >/dev/null 2>&1; done
command grep -E '\[drive\]|Test Suite .* (passed|failed)|\*\* TEST' "$root/logs/drive-$tag.log" | tail -12
ls "$out"
