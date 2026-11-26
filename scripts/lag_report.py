#!/usr/bin/env python3
import argparse
import csv
import os
import statistics
import sys

def percentile(values, q):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * q / 100.0))]

def load(path, skip):
    with open(path, newline="") as handle:
        return [{key: float(value) for key, value in row.items()} for row in csv.DictReader(handle)][skip:]

def report(path, skip, worst):
    rows = load(path, skip)
    name = os.path.splitext(os.path.basename(path))[0]
    if not rows:
        print(f"{name}: no frames after skipping {skip}")
        return 1
    cpu = [row["cpu_ms"] for row in rows]
    median = statistics.median(cpu)
    limit = max(median * 1.5, median + 4.0)
    hitches = [row for row in rows if row["cpu_ms"] > limit]
    line = f"{name:16s} frames {len(rows):4d}"
    for column in ("cpu_ms", "sim_ms", "gpu_ms"):
        values = [row[column] for row in rows]
        line += f" | {column[:3]} p50 {statistics.median(values):6.2f} p99 {percentile(values, 99):6.2f} max {max(values):7.2f}"
    portals = [row.get("portal_gpu_ms", 0.0) for row in rows]
    if max(portals) > 0.0:
        line += f" | portals p50 {statistics.median(portals):6.2f} max {max(portals):6.2f}"
    print(f"{line} | hitches {len(hitches)}")
    columns = [column for column in rows[0] if column == "sim_ms" or column.startswith(("gpu:", "portal:"))]
    medians = {column: statistics.median(row[column] for row in rows) for column in columns}
    for row in sorted(hitches, key=lambda r: -r["cpu_ms"])[:worst]:
        grown = sorted(((row[c] - medians[c], c) for c in columns), reverse=True)[:3]
        detail = ", ".join(f"{c} +{delta:.2f}" for delta, c in grown if delta > 0.2) or "no measured pass grew"
        print(f"    frame {int(row['frame']):5d} cpu {row['cpu_ms']:7.2f} gpu {row['gpu_ms']:6.2f}: {detail}")
    return 0

def main():
    parser = argparse.ArgumentParser(description="Frame time percentiles and hitches from performance CSV files.")
    parser.add_argument("csv", nargs="+")
    parser.add_argument("--skip", type=int, default=60, help="warmup frames to ignore")
    parser.add_argument("--worst", type=int, default=3, help="hitch frames to detail per file")
    args = parser.parse_args()
    return max(report(path, args.skip, args.worst) for path in args.csv)

if __name__ == "__main__":
    sys.exit(main())
