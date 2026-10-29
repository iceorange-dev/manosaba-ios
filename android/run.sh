#!/bin/bash
# The Android app (plan item 301d): build, install and run it on the phone over adb. The game is DM_GAME (games/<id>/game.json); workspace paths
# are relative to DM_ROOT (default ../work). The phone: the first one adb lists, or ANDROID_SERIAL.
#   run.sh build              make -C android apk: build/<game>/<game>.apk (the host library, the android product inside, the icon)
#   run.sh install            adb install it over the installed one (same signing key, ~/.config/dm-android: the saves stay). HyperOS asks on
#                             the phone for every install over USB: the screen must be on and unlocked to say yes. DM_INSTALL_ROOT=1: through
#                             the phone's root (su -c pm install), which does not ask: the test phone only
#   run.sh push-data [what]   the player's RomFS and the console's fonts into the app's external files folder (Android/data/<package>/files);
#                             what: romfs, sysfont or both (default). From the phone's /data/local/tmp/dm when the adb shell host's copy is
#                             complete there (a copy on the phone), else from the workspace over USB. Start the app once first (it makes the
#                             folder its own). What adb writes there belongs to the shell user, so it is made readable to others (the folder
#                             above is the app's, 2770: no other app reaches into it)
#   run.sh env [file]         put a file of DM_ switches in as env.txt (read at launch, host/envfile.h); without a file, empty it
#   run.sh launch [seconds]   start the app (a running one is stopped first), wait, then fetch boot.txt ($DM_ROOT/run/android/boot.txt)
#   run.sh log                fetch boot.txt now;   run.sh stop: stop the app
set -u
cd "$(dirname "$0")"
t() { echo "[$(date +%T)] $*"; }
ROOT=${DM_ROOT:-$(cd .. && pwd)/work}
GAME=${DM_GAME:-manosaba}; GJ=../games/$GAME/game.json
[ -f "$GJ" ] || { t "[red] no game profile $GJ (DM_GAME=$GAME)"; exit 1; }
get() { python3 ../tools/gen_game.py "$GJ" --get "$1"; }
PKG=${DM_APK_PACKAGE:-io.github.iceorange_dev.$GAME}
APK=build/$GAME/$GAME.apk
EXT=/sdcard/Android/data/$PKG/files
R=$ROOT/run/android; mkdir -p "$R"
SERIAL=${ANDROID_SERIAL:-$(adb devices | awk 'NR > 1 && $2 == "device" { print $1; exit }')}
A() { [ -n "$SERIAL" ] || { t "[red] no phone (adb devices lists none)"; exit 1; }; adb -s "$SERIAL" "$@"; }
CMD=${1:-}
case "$CMD" in
build) make -j10 GAME="$GAME" PKG="$PKG" apk ;;   # (RELEASE=1: not debuggable)
install)
  [ -f "$APK" ] || { t "[red] no $APK: run.sh build first"; exit 1; }
  if [ "${DM_INSTALL_ROOT:-0}" = 1 ]; then t "installing $APK through root"; A push "$APK" /data/local/tmp/dm_app.apk > /dev/null && A shell "su -c 'pm install -r /data/local/tmp/dm_app.apk'; rm -f /data/local/tmp/dm_app.apk"
  else t "installing $APK (say yes on the phone if it asks)"; A install -r "$APK"; fi ;;
push-data)
  what=${2:-romfs sysfont}; A shell "mkdir -p $EXT" || exit 1
  for w in $what; do
    case $w in romfs) src=RomFS; ws=$(get workspace.romfs) ;; sysfont) src=sysfont; ws=$(get workspace.sysfont) ;; *) t "[red] push-data: $w?"; exit 1 ;; esac
    n_tmp=$(A shell "[ -d /data/local/tmp/dm/$src ] && find /data/local/tmp/dm/$src -type f | wc -l" | tr -d '\r ')
    n_ws=$(find "$ROOT/$ws" -type f | wc -l | tr -d ' ')
    A shell "rm -rf $EXT/$src.new" || exit 1
    if [ -n "$n_tmp" ] && [ "$n_tmp" = "$n_ws" ]; then t "$src: copying on the phone from /data/local/tmp/dm/$src ($n_tmp files)"; A shell "cp -r /data/local/tmp/dm/$src $EXT/$src.new" || exit 1
    else t "$src: pushing $ROOT/$ws over USB ($n_ws files)"; A push "$ROOT/$ws" "$EXT/$src.new" > /dev/null || exit 1; fi
    A shell "chmod -R o+rX $EXT/$src.new && rm -rf $EXT/$src && mv $EXT/$src.new $EXT/$src" || exit 1
    t "$src: $(A shell "find $EXT/$src -type f | wc -l" | tr -d '\r ') files in $EXT/$src"
  done ;;
env) A shell "mkdir -p $EXT" && if [ -n "${2:-}" ]; then A push "$2" "$EXT/env.txt" > /dev/null && A shell "chmod o+r $EXT/env.txt" && t "env.txt: $(wc -l < "$2" | tr -d ' ') lines"; else A shell ": > $EXT/env.txt" && t "env.txt emptied"; fi ;;
launch)
  A shell "am force-stop $PKG"; A shell "am start -n $PKG/android.app.NativeActivity" > /dev/null || exit 1; t "started $PKG"
  sleep "${2:-30}"; A pull "$EXT/boot.txt" "$R/boot.txt" > /dev/null && t "$R/boot.txt: $(grep -a -c '\[red\]' "$R/boot.txt") red lines" ;;
log) A pull "$EXT/boot.txt" "$R/boot.txt" > /dev/null && t "$R/boot.txt" ;;
stop) A shell "am force-stop $PKG" ;;
*) sed -n '2,15p' "$0"; exit 2 ;;
esac
