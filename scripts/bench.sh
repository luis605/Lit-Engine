#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

if [[ "${BENCH_SKIP_BUILD:-0}" != 1 ]]; then
    cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
    cmake --build build-release --target Editor -j"$(nproc)" 2>&1 | grep -E "error|FAILED" || true
fi

export LIT_CAM_POS="${LIT_CAM_POS:-200,1000,200}"
export LIT_CAM_PITCH="${LIT_CAM_PITCH:--74.2}"
export LIT_CAM_YAW="${LIT_CAM_YAW:--135}"
export LIT_SEED="${LIT_SEED:-1}"

seconds="${BENCH_SECONDS:-25}"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
timeout "$seconds" build-release/src/Editor/Editor >"$log" 2>&1 || true

python3 - "$log" <<'PY'
import re, statistics, sys
text = re.sub(r"\x1b\[[0-9;]*m", "", open(sys.argv[1], errors="ignore").read())
gpu = re.search(r"Selected GPU: (.*)", text)
created = re.search(r"Created [0-9]+ objects in [0-9.]+ ms", text)
print(gpu.group(1) if gpu else "GPU not reported")
print(created.group(0) if created else "scene creation time not found")
passes = {}
for name, value in re.findall(r"([A-Za-z][A-Za-z -]*?): ([0-9.]+) ms", text):
    passes.setdefault(name.strip(), []).append(float(value))
total = passes.pop("GPU Total", [])[5:35]
if not total:
    sys.exit("no GPU Total samples found")
print(f"GPU Total median {statistics.median(total):.2f} ms over {len(total)} samples (min {min(total):.2f}, max {max(total):.2f})")
for name, values in passes.items():
    values = values[5:35]
    if values and name not in ("Frame time", "CPU Fence Wait"):
        print(f"  {name}: {statistics.median(values):.2f} ms")
PY
