#!/bin/zsh
# Turn the Steam macOS GameAssembly.dylib (IL2CPP game code) into an iOS framework.
# Usage: [PLATFORM=ios|iossim] make_game_framework.sh <GameAssembly.dylib> <out-dir>
set -euo pipefail

src=${1:?usage: make_game_framework.sh <GameAssembly.dylib> <out-dir>}
out=${2:?usage: make_game_framework.sh <GameAssembly.dylib> <out-dir>}
name=ManosabaGame
platform=${PLATFORM:-ios}   # ios = device (2), iossim = simulator (7)
case $platform in ios) bundle_platform=iPhoneOS;; iossim) bundle_platform=iPhoneSimulator;; *) echo "bad PLATFORM $platform" >&2; exit 2;; esac
fw="$out/$name.framework"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "[make_game_framework] $src -> $fw"
lipo "$src" -thin arm64 -output "$tmp/ga"
codesign --remove-signature "$tmp/ga"
# platform 1 (macOS) -> iOS / iOS simulator; arm64 Apple ABI is identical.
vtool -arch arm64 -set-build-version "$platform" 15.0 27.1 -replace -output "$tmp/ga.ios" "$tmp/ga"
install_name_tool \
  -id "@rpath/$name.framework/$name" \
  -change /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation /System/Library/Frameworks/Foundation.framework/Foundation \
  -change /System/Library/Frameworks/Security.framework/Versions/A/Security /System/Library/Frameworks/Security.framework/Security \
  -change /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation /System/Library/Frameworks/CoreFoundation.framework/CoreFoundation \
  "$tmp/ga.ios" 2>&1 | command grep -v 'invalidate the code signature' || true

rm -rf "$fw"
mkdir -p "$fw"
cp "$tmp/ga.ios" "$fw/$name"
cat > "$fw/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>$name</string>
  <key>CFBundleIdentifier</key><string>${MANOSABA_BUNDLE_ID:-com.example.manosaba}.game</string>
  <key>CFBundleName</key><string>$name</string>
  <key>CFBundlePackageType</key><string>FMWK</string>
  <key>CFBundleShortVersionString</key><string>1.1.2</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleSupportedPlatforms</key><array><string>$bundle_platform</string></array>
  <key>MinimumOSVersion</key><string>15.0</string>
</dict>
</plist>
EOF

vtool -show-build "$fw/$name" | command grep -E 'platform|minos'
otool -L "$fw/$name" | tail -n +2

# ad-hoc sign so it also runs where nothing re-signs it (simulator, tests)
codesign -f -s - "$fw/$name" >/dev/null 2>&1
