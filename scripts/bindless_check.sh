#!/usr/bin/env bash
# Pixel comparison of the bindless texture table (LIT_BINDLESS_TEXTURES=1) against the legacy 16-layer texture arrays (=0).
# Every scene renders three times with LitShot --deterministic: legacy twice (a, b: the run-to-run noise of the GPU) and bindless.
# Writes PNGs, logs and summary.txt to the output directory. See docs/RenderGraph.md (Bindless).
#   --assert   exit 1 unless bindless matches legacy (differing pixels <= 2x the noise of legacy a vs b + 64, max delta <= noise max + 4), 77 (ctest skip) when LitShot is
#              missing or cannot render, and always require "Bindless texture table: on" in the bindless log and no Diligent errors.
set -u
assert=0
[ "${1:-}" = "--assert" ] && { assert=1; shift; }
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-release}"
out="${1:-$build/bindless_check}"
frames="${FRAMES:-30}"
size="${SIZE:-1280x720}"
scenes="${SCENES:-textures showcase gallery terrain skinning}"
litshot="${LITSHOT:-$build/src/Shot/LitShot}"
[ -x "$litshot" ] || { echo "LitShot not built ($litshot)"; [ $assert = 1 ] && exit 77; exit 1; }
mkdir -p "$out"
cd "$root" || exit 1

shot() {
    local scene="$1" name="$2" mode="$3"
    local png="$out/${scene}_${name}.png"
    for attempt in 1 2 3; do
        rm -f "$png"
        LIT_BINDLESS_TEXTURES="$mode" timeout 300 "$litshot" --deterministic --testscene "$scene" --frames "$frames" --size "$size" \
            --screenshot "$png" >"$out/${scene}_${name}.log" 2>&1
        [ -s "$png" ] && return 0
        sleep 2  # shared GPU: retry after an out-of-memory or device-lost exit
    done
    return 1
}

compare() {
    python3 - "$1" "$2" <<'PY'
import sys
from PIL import Image
import numpy as np
a = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(int)
b = np.asarray(Image.open(sys.argv[2]).convert('RGB')).astype(int)
if a.shape != b.shape:
    print("size-mismatch 999999999 255")
    sys.exit(0)
d = np.abs(a - b).max(axis=2)
print(f"{int((d > 0).sum())} {int(d.max())}")
PY
}

errors() { grep -ciE "\[ERROR\]|Diligent Engine: (ERROR|WARNING)|VUID|validation error" "$1" 2>/dev/null; }

summary="$out/summary.txt"
: >"$summary"
status=0
for scene in $scenes; do
    if ! shot "$scene" legacy_a 0 || ! shot "$scene" legacy_b 0 || ! shot "$scene" bindless 1; then
        echo "$scene: render failed" | tee -a "$summary"
        [ $assert = 1 ] && status=77
        continue
    fi
    read -r noisePx noiseMax < <(compare "$out/${scene}_legacy_a.png" "$out/${scene}_legacy_b.png")
    read -r diffPx diffMax < <(compare "$out/${scene}_legacy_a.png" "$out/${scene}_bindless.png")
    on=$(grep -c "Bindless texture table: on" "$out/${scene}_bindless.log")
    errs=$(errors "$out/${scene}_bindless.log")
    printf '%-10s noise(legacy a/b): %s px max %s   bindless vs legacy: %s px max %s   table on: %s   errors legacy=%s bindless=%s\n' \
        "$scene" "$noisePx" "$noiseMax" "$diffPx" "$diffMax" "$on" "$(errors "$out/${scene}_legacy_a.log")" "$errs" | tee -a "$summary"
    if [ $assert = 1 ]; then
        [ "$on" -ge 1 ] || { echo "  FAIL: bindless table was not active (device lacks the features?)"; status=1; }
        [ "$errs" -eq 0 ] || { echo "  FAIL: errors or warnings in the bindless log"; status=1; }
        # Tolerance: a few dozen pixels of slack on top of 2x the legacy a/b noise, and a max channel delta within 4 levels of the noise.
        { [ "$diffPx" -le $((noisePx * 2 + 64)) ] && [ "$diffMax" -le $((noiseMax + 4)) ]; } || { echo "  FAIL: bindless differs from legacy beyond the legacy run-to-run noise"; status=1; }
    fi
done
echo "PNGs, logs and summary.txt are in $out"
exit $status
