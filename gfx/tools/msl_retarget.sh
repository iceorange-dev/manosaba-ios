#!/bin/bash
# Recompile the Mac shader libraries for a given minimum system version: msl_retarget.sh <minimum system, e.g. 15.0> <output root>   produces <output root>/b-<translation chain version>/
# Why it exists: dmsh_build.py compiles the libraries without a minimum-version flag, so the target recorded in them is the SDK of the build machine (macOS 26); loading such a library on an older system simply fails
# (the same matter as the minimum system version of the executable, in a different place). The older-API package (tools/mac_app.sh DM_MAC_M3=1) carries the libraries recompiled here.
# Where the sources come from (the shared cache is read-only): the .metal next to each _b library; the sources of _bf libraries (the readable-tile variants) are in the content-addressed cache and are matched by the content of the library file.
# The compile command is dmsh_build.py's with its own -mmacosx-version-min (until plan item 311 it also lacked -fpreserve-invariance and
# -fmetal-math-mode=safe, so the older-API package's libraries were compiled with fast math). About 7000 libraries in 3 minutes.
set -u; MIN=$1; OUTROOT=$2
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=${DM_ROOT:-$HOME/dm-work}
SRC=${MSL_SRC:-$ROOT/build/cache/metal_shader_cache}   # the shader cache root the libraries are taken from (mac_app.sh passes the game's)
V=$(sed -n 's/.*DMSH_CHAIN "\(.*\)".*/\1/p' "$HERE/../nvn/dmsh_chain.h"); S=$SRC/b-$V; CAS=$SRC/cas/variant
[ -d "$S" ] || { echo "[red] no shader libraries at ${S} (run dmsh_build.py first)"; exit 2; }
D=$OUTROOT/b-$V; rm -rf "$D"; mkdir -p "$D"
# The reflection .json files (thousands) are copied by directory, not passed as a "$S"/*.json glob: with a deep workspace path the expanded list
# exceeds the system argument-length limit, cp fails as a whole and copies nothing, and the package then has libraries without reflections (every
# shader fails to load; upstream d2r-mobile 1784563, #60). The check at the end makes sure every _b library has its reflection
rsync -a --include='*.json' --exclude='*' "$S/" "$D/" || { echo "[red] copying the reflection json files failed"; exit 3; }
one() { src=$1; dst=$2; t=$(mktemp -d); if xcrun -sdk macosx metal -c -mmacosx-version-min="$MIN" -fpreserve-invariance -fmetal-math-mode=safe "$src" -o "$t/a.air" 2>"$t/err" && xcrun -sdk macosx metallib "$t/a.air" -o "$t/a.metallib" 2>>"$t/err"; then mv "$t/a.metallib" "$dst"; else echo "failed $(basename "$dst"): $(head -1 "$t/err" | cut -c1-120)"; fi; rm -rf "$t"; }
export -f one; export MIN D S
BAD=$(ls "$S" | grep '_b\.metal$' | sed 's/\.metal$//' | xargs -P 10 -I{} bash -c 'one "$S/{}.metal" "$D/{}.metallib"')
T=$(mktemp); for r in "$CAS"/*/; do [ -f "$r/mac.metallib" ] && [ -f "$r/metal" ] && echo "$(shasum -a 256 "$r/mac.metallib" | cut -c1-64) $r"; done > "$T"
miss=0
for f in "$S"/*_bf.metallib; do [ -f "$f" ] || continue; h=$(shasum -a 256 "$f" | cut -c1-64); r=$(grep -m1 "^$h " "$T" | cut -d' ' -f2-)
  if [ -n "$r" ]; then m=$D/$(basename "${f%.metallib}").metal; cp "$r/metal" "$m"; BAD="$BAD$(one "$m" "$D/$(basename "$f")")"; rm -f "$m"; else miss=$((miss+1)); fi; done
rm -f "$T"
have=$(ls "$D" | grep -c '\.metallib$'); want=$(ls "$S" | grep -c '\.metallib$')
nojson=$(ls "$D" | sed -n 's/_b\.metallib$//p' | while read -r k; do [ -f "$D/${k}_b.json" ] || echo "$k"; done | wc -l | tr -d ' ')
[ -z "$BAD" ] && [ "$miss" = 0 ] && [ "$have" = "$want" ] && [ "$nojson" = 0 ] || { echo "[red] recompiling incomplete: ${have} / ${want} libraries, ${miss} _bf sources not found, ${nojson} reflection json files missing; ${BAD:0:300}"; exit 3; }
echo "MSL_RETARGET ${D} (${have} libraries, macOS ${MIN})"
