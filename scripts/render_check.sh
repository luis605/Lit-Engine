#!/usr/bin/env bash
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-release}"
out="${1:-$root/build-release/render_check}"
frames="${FRAMES:-40}"
scenes="${SCENES:-showcase vista crowd}"
mkdir -p "$out"
litshot="${LITSHOT:-$build/src/Shot/LitShot}"
cd "$build" || exit 1

shot() {
    local name="$1" scene="$2"
    shift 2
    local args=()
    for pair in "$@"; do args+=(--set "$pair"); done
    rm -f "$out/${scene}_${name}.png"
    "$litshot" --testscene "$scene" --frames "$frames" --screenshot "$out/${scene}_${name}.png" --timings "$out/${scene}_${name}.timings.txt" ${args[@]+"${args[@]}"} >"$out/${scene}_${name}.log" 2>&1
    local status=$?
    local errors
    errors=$(grep -ciE "Error:|Warning:|\[ERROR\]|\[WARN|failed" "$out/${scene}_${name}.log")
    local produced=ok
    [ -s "$out/${scene}_${name}.png" ] || produced=MISSING
    printf '%-18s %-16s exit=%d log_errors=%s png=%s\n' "$scene" "$name" "$status" "$errors" "$produced"
}

for scene in $scenes; do
    shot baseline "$scene"
    for mode in 0 1 2 3 4; do shot "refl$mode" "$scene" reflectionMode=$mode; done
    shot bleed_off "$scene" colorBleed=0
    shot contact_off "$scene" contactShadows=0
    shot bent_off "$scene" bentNormals=0
    shot arealights_off "$scene" areaLights=0
    shot aodenoise0 "$scene" aoDenoise=0
    shot scale05 "$scene" renderScale=0.5
    shot scale05_sharp0 "$scene" renderScale=0.5 upscaleSharpness=0
    shot governor "$scene" qualityGovernor=1 targetFrameMs=8
done
echo "PNG, logs and GPU pass timings are in $out"
