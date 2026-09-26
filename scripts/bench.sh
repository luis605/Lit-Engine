#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

if [[ "${BENCH_SKIP_BUILD:-0}" != 1 ]]; then
    cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
    cmake --build build-release --target Editor -j"$(nproc)" 2>&1 | grep -E "error|FAILED" || true
fi

frames="${BENCH_FRAMES:-300}"
views="${BENCH_VIEWS:-top inside edge}"
baseline="$root/scripts/bench_baseline.txt"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
printf 'frames 60\nmeasure %s bench\nquit\n' "$frames" > "$work/bench.lit"

camera() {
    case "$1" in
        top) echo "200,1000,200 -74.2 -135" ;;
        inside) echo "0,0,0 0 -90" ;;
        edge) echo "-200,0,25 0 0" ;;
        *) echo "unknown view $1" >&2; exit 1 ;;
    esac
}

cd build-release
for view in $views; do
    read -r pos pitch yaw <<< "$(camera "$view")"
    LIT_SEED="${LIT_SEED:-1}" LIT_CAM_POS="$pos" LIT_CAM_PITCH="$pitch" LIT_CAM_YAW="$yaw" LIT_AUTOPILOT="$work/bench.lit" \
        timeout 300 ./src/Editor/Editor >"$work/$view.log" 2>&1 || true
done
cd "$root"

python3 - "$work" "$baseline" "${BENCH_UPDATE:-0}" $views <<'PY'
import os, re, statistics, sys
work, baseline_path, update, views = sys.argv[1], sys.argv[2], sys.argv[3] == "1", sys.argv[4:]
baseline = {}
if os.path.exists(baseline_path):
    for line in open(baseline_path):
        parts = line.split()
        if len(parts) == 3 and not line.startswith("#"):
            baseline[(parts[0], parts[1])] = float(parts[2])
results = {}
for view in views:
    text = re.sub(r"\x1b\[[0-9;]*m", "", open(os.path.join(work, view + ".log"), errors="ignore").read())
    gpu = re.search(r"Selected GPU: (.*)", text)
    measure = re.search(r"measure bench: \d+ frames, median ([0-9.]+) ms .*?p95 ([0-9.]+) ms", text)
    if not measure:
        print(f"{view}: no measurement (see the Editor log)")
        continue
    results[(view, "frame")] = float(measure.group(1))
    results[(view, "p95")] = float(measure.group(2))
    passes = {}
    for name, value in re.findall(r"([A-Za-z][A-Za-z+ -]*?): ([0-9.]+) ms", text):
        passes.setdefault(name.strip(), []).append(float(value))
    for name, values in passes.items():
        if name in ("CPU Fence Wait",) or len(values) < 4:
            continue
        results[(view, name.replace(" ", "_"))] = statistics.median(values[2:])
    print(f"== {view}  ({gpu.group(1) if gpu else 'GPU not reported'})")
    for (v, key), value in results.items():
        if v != view:
            continue
        old = baseline.get((v, key))
        delta = f"  ({value - old:+.2f} vs baseline {old:.2f})" if old is not None else ""
        print(f"  {key:16s} {value:7.2f} ms{delta}")
if update and results:
    with open(baseline_path, "w") as out:
        out.write("# view metric milliseconds, written by BENCH_UPDATE=1 scripts/bench.sh\n")
        for (view, key), value in results.items():
            out.write(f"{view} {key} {value:.2f}\n")
    print(f"baseline written to {baseline_path}")
PY
