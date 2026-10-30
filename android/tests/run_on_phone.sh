#!/bin/sh
# run_on_phone.sh <adb serial> "<data files>" <test>...: android/Makefile run-tests. Pushes the data files and build/tests/<test> to
# /data/local/tmp/dm_tests on the phone and runs each there, in that folder; a test passes when it exits 0. The negative controls (name:failed checks expected) must exit 1 with exactly that many checks failed.
CONTROLS="t_sha256_63:4 t_hthread_nov:1 t_input_ydown:2 t_movie_uvswap:2 t_vk_be_early:2 t_vk_be_wind:3 t_vk_be_grid:1 t_vk_be_stale:1"
SERIAL=$1; DATA=$2; shift 2
[ -n "$SERIAL" ] || { echo "run_on_phone.sh: no phone (adb devices lists none)"; exit 2; }
D=/data/local/tmp/dm_tests
adb -s "$SERIAL" shell "mkdir -p $D" || exit 2
for f in $DATA; do adb -s "$SERIAL" push "$f" "$D/" > /dev/null || { echo "$f: push failed"; exit 2; }; done
bad=0
for t in "$@"; do
  adb -s "$SERIAL" push "build/tests/$t" "$D/$t" > /dev/null || { echo "$t: push failed"; bad=1; continue; }
  out=$(adb -s "$SERIAL" shell "cd $D && chmod 755 $t && ./$t; echo rc=\$?" 2>&1); rc=$(echo "$out" | sed -n 's/^rc=//p' | tr -d '\r')
  want=""; for c in $CONTROLS; do [ "${c%%:*}" = "$t" ] && want=${c#*:}; done
  if [ -z "$want" ]; then
    if [ "$rc" = 0 ]; then echo "$t: ok ($(echo "$out" | grep -c '✓') checks)"; else echo "$out"; echo "$t: FAILED (rc $rc)"; bad=1; fi
  else
    got=$(echo "$out" | sed -n 's/.* checks, \([0-9]*\) failed.*/\1/p' | tr -d '\r')
    if [ "$rc" = 1 ] && [ "$got" = "$want" ]; then echo "$t: negative control fails $got as expected"; else echo "$out"; echo "$t: unexpected negative-control result (rc $rc, $got failed, $want expected)"; bad=1; fi
  fi
done
exit $bad
