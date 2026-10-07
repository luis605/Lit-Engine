#!/usr/bin/env python3
import argparse
import csv
import math
import sys

STATS = ("min", "mean", "median", "p95", "p99", "max")


def percentile(values, q):
    ordered = sorted(values)
    if not ordered:
        return math.nan
    return ordered[min(len(ordered) - 1, int(len(ordered) * q / 100.0))]


def stat(values, name):
    if not values:
        return math.nan
    if name == "min":
        return min(values)
    if name == "max":
        return max(values)
    if name == "mean":
        return sum(values) / len(values)
    if name == "median":
        return percentile(values, 50)
    return percentile(values, float(name[1:]))


def load(path, skip):
    with open(path, newline="") as handle:
        rows = list(csv.DictReader(handle))[skip:]
    columns = {}
    for row in rows:
        for key, value in row.items():
            try:
                columns.setdefault(key, []).append(float(value))
            except (TypeError, ValueError):
                pass
    return columns


def parse_limits(path):
    limits = []
    with open(path) as handle:
        for number, line in enumerate(handle, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.rsplit(None, 2)
            if len(parts) != 3 or parts[1] not in STATS:
                sys.exit(f"{path}:{number}: expected '<column> <{'|'.join(STATS)}> <limit>'")
            limits.append((parts[0], parts[1], float(parts[2])))
    return limits


def main():
    parser = argparse.ArgumentParser(description="Fail when performance CSV statistics exceed thresholds.")
    parser.add_argument("csv")
    parser.add_argument("thresholds", nargs="?")
    parser.add_argument("--skip", type=int, default=0, help="warmup rows to ignore")
    parser.add_argument("--baseline", help="previous CSV to compare against")
    parser.add_argument("--tolerance", type=float, default=10.0, help="allowed regression versus baseline, percent")
    args = parser.parse_args()
    if not args.thresholds and not args.baseline:
        parser.error("give a thresholds file, --baseline, or both")

    current = load(args.csv, args.skip)
    failures = 0
    checked = 0

    if args.thresholds:
        for column, name, limit in parse_limits(args.thresholds):
            values = current.get(column)
            if not values:
                print(f"FAIL {column}: column missing or empty")
                failures += 1
                continue
            value = stat(values, name)
            checked += 1
            ok = value <= limit
            failures += not ok
            print(f"{'ok  ' if ok else 'FAIL'} {column} {name} {value:.3f} (limit {limit:.3f})")

    if args.baseline:
        base = load(args.baseline, args.skip)
        for column, values in current.items():
            if column == "frame" or column not in base:
                continue
            for name in ("median", "p95"):
                old = stat(base[column], name)
                new = stat(values, name)
                if not math.isfinite(old) or old < 0.05:
                    continue
                checked += 1
                allowed = old * (1.0 + args.tolerance / 100.0)
                ok = new <= allowed
                failures += not ok
                print(f"{'ok  ' if ok else 'FAIL'} {column} {name} {new:.3f} vs baseline {old:.3f} (+{args.tolerance:g}% = {allowed:.3f})")

    print(f"{checked} checks, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
