#!/usr/bin/env bash
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-release}"
out="${1:-$build/lag_check}"
frames="${FRAMES:-460}"
scenes="${SCENES:-orb_run showcase portal_torture streaming_route crowd skinning terrain dense light_stress largeworld instancing vista}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
litshot="${LITSHOT:-$build/src/Shot/LitShot}"
cd "$build" || exit 1
status=0
for scene in $scenes; do
    rm -f "$out/$scene.csv"
    if ! timeout 600 "$litshot" --deterministic --testscene "$scene" --frames "$frames" --size "${SIZE:-1280x720}" --perf "$out/$scene.csv" ${PACED:+--paced} ${PRESET:+--set "preset=$PRESET"} --screenshot "$out/$scene.png" >"$out/$scene.log" 2>&1; then
        echo "$scene: LitShot failed, see $out/$scene.log"
        status=1
        continue
    fi
    python3 "$root/scripts/lag_report.py" "$out/$scene.csv" || status=1
done
exit $status
