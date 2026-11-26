#!/usr/bin/env bash
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-release}"
out="${1:-$build/render_graph_check}"
frames="${FRAMES:-30}"
size="${SIZE:-1280x720}"
scenes="${SCENES:-showcase vista gallery portal_torture vsm_mixed skinning terrain dense light_stress largeworld}"
litshot="${LITSHOT:-$build/src/Shot/LitShot}"
mkdir -p "$out"
cd "$build" || exit 1

shot() {
    local scene="$1" name="$2" graph="$3"
    local png="$out/${scene}_${name}.png"
    for attempt in 1 2 3; do
        rm -f "$png"
        LIT_RENDER_GRAPH="$graph" timeout 300 "$litshot" --deterministic --testscene "$scene" --frames "$frames" --size "$size" \
            --screenshot "$png" --timings "$out/${scene}_${name}.timings.txt" >"$out/${scene}_${name}.log" 2>&1
        [ -s "$png" ] && return 0
        sleep 2
    done
    return 1
}

compare() {
    python3 - "$1" "$2" <<'EOF'
import sys
from PIL import Image
import numpy as np
a = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(int)
b = np.asarray(Image.open(sys.argv[2]).convert('RGB')).astype(int)
if a.shape != b.shape:
    print(f"size-mismatch {a.shape[1]}x{a.shape[0]} vs {b.shape[1]}x{b.shape[0]}")
    sys.exit(0)
d = np.abs(a - b).max(axis=2)
print(f"diffpix={int((d > 0).sum())} max={int(d.max())}")
EOF
}

errors() { grep -ciE "\[ERROR\]|Diligent Engine: ERROR|VUID|validation error" "$1" 2>/dev/null; }

summary="$out/summary.txt"
: >"$summary"
for scene in $scenes; do
    shot "$scene" off_a 0 || { echo "$scene: off_a failed" | tee -a "$summary"; continue; }
    shot "$scene" off_b 0 || { echo "$scene: off_b failed" | tee -a "$summary"; continue; }
    shot "$scene" on 1 || { echo "$scene: on failed" | tee -a "$summary"; continue; }
    noise=$(compare "$out/${scene}_off_a.png" "$out/${scene}_off_b.png")
    graph=$(compare "$out/${scene}_off_a.png" "$out/${scene}_on.png")
    line=$(printf '%-15s noise(off/off): %-26s graph(on/off): %-26s errors off=%s on=%s' "$scene" "$noise" "$graph" "$(errors "$out/${scene}_off_a.log")" "$(errors "$out/${scene}_on.log")")
    echo "$line" | tee -a "$summary"
done
echo "PNGs, logs, timings and summary.txt are in $out"
