#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
cmake --build build-release --target Editor -j"$(nproc)" 2>&1 | grep -E "error|FAILED" || true

export LIT_CAM_POS="${LIT_CAM_POS:-200,1000,200}"
export LIT_CAM_PITCH="${LIT_CAM_PITCH:--74.2}"
export LIT_CAM_YAW="${LIT_CAM_YAW:--135}"
export LIT_SEED="${LIT_SEED:-1}"

seconds="${BENCH_SECONDS:-25}"
log="$(mktemp)"
timeout "$seconds" build-release/src/Editor/Editor >"$log" 2>&1 || true

created="$(grep -oE 'Created [0-9]+ objects in [0-9.]+ ms' "$log" | head -1 || true)"
python3 - "$log" "$created" <<'PY'
import re, statistics, sys
text = open(sys.argv[1], errors="ignore").read()
samples = [float(m) for m in re.findall(r"GPU Total: ([0-9.]+) ms", text)][5:35]
print(sys.argv[2] or "scene creation time not found")
if not samples:
    sys.exit("no GPU Total samples found")
print(f"GPU Total median {statistics.median(samples):.2f} ms over {len(samples)} samples (min {min(samples):.2f}, max {max(samples):.2f})")
PY
rm -f "$log"
