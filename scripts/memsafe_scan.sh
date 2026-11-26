#!/bin/bash
# Static memory-safety scan with a ratchet (docs/Bent.md, "Memory safety"). Pattern finder, not a proof.
#   scripts/memsafe_scan.sh                  fail (exit 1) only on findings that are not in scripts/memsafe_baseline.txt
#   scripts/memsafe_scan.sh --update-baseline  rewrite the baseline from the current findings
#   scripts/memsafe_scan.sh --list           print the current findings (file:check, one per occurrence) and a per-check summary
# Tools: clang++ --analyze (core, cplusplus, security incl. ArrayBound, unix, deadcode), clang++ -W warnings (array-bounds, sign-compare,
# conversion, ...), and clang-tidy (bugprone, cert, cppcoreguidelines-pro-bounds-*) when installed. Exit 77 when no clang++ exists.
# Scope: src/Engine/**/*.cpp translation units that do not `import` a project module (clang cannot read GCC's module BMIs, so module
# consumers are listed as skipped in <out>/skipped.txt). Headers outside src/Engine (vendors, system) are never reported.
# Env: MEMSAFE_BUILD_DIR (flags source, default build-san, else build-release), MEMSAFE_JOBS (default 2), MEMSAFE_OUT (work dir).
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
baseline="$root/scripts/memsafe_baseline.txt"
mode=check
case "${1:-}" in --update-baseline) mode=update ;; --list) mode=list ;; "") ;; *) echo "unknown option $1"; exit 2 ;; esac
clang="${CLANGXX:-$(command -v clang++ || true)}"
[ -n "$clang" ] || { echo "memsafe_scan: clang++ not found, skipping"; exit 77; }
tidy="$(command -v clang-tidy || command -v clang-tidy-21 || true)"
bdir="${MEMSAFE_BUILD_DIR:-$root/build-san}"
[ -f "$bdir/build.ninja" ] || bdir="$root/build-release"
[ -f "$bdir/build.ninja" ] || { echo "memsafe_scan: no configured build dir for include flags, skipping"; exit 77; }
jobs="${MEMSAFE_JOBS:-2}"
out="${MEMSAFE_OUT:-${TMPDIR:-/tmp}/memsafe-scan-$USER}"
rm -rf "$out"; mkdir -p "$out/raw"

# Include/define flags from the build (ninja -t compdb works without CMAKE_EXPORT_COMPILE_COMMANDS); GCC-only flags are dropped.
ninja -C "$bdir" -t compdb 2>/dev/null | python3 -I -c '
import json, shlex, sys
root = sys.argv[1]
seen = {}
for e in json.load(sys.stdin):
    f = e["file"]
    if not f.startswith(root + "/src/Engine/") or not f.endswith(".cpp") or f in seen:
        continue
    toks = shlex.split(e["command"]); keep = []; i = 0
    while i < len(toks):
        t = toks[i]
        if t in ("-I", "-isystem", "-D") and i + 1 < len(toks):
            keep += [t, toks[i + 1]]; i += 1
        elif t.startswith(("-I", "-isystem", "-D")):
            keep.append(t)
        i += 1
    seen[f] = keep
for f, k in sorted(seen.items()):
    print(f + "\t" + " ".join(shlex.quote(x) for x in k))
' "$root" > "$out/units.tsv"

: > "$out/files.txt"; : > "$out/skipped.txt"
while IFS=$'\t' read -r f flags; do
  if grep -qE '^(export )?import [A-Za-z_][A-Za-z0-9_.:]*;|^(export )?module [A-Za-z_]' "$f"; then echo "${f#$root/}" >> "$out/skipped.txt"
  else printf '%s\t%s\n' "$f" "$flags" >> "$out/files.txt"; fi
done < "$out/units.tsv"

scan_one() {  # $1 = line number in files.txt
  IFS=$'\t' read -r f flags < <(sed -n "${1}p" "$out/files.txt"); id="$(echo "${f#$root/}" | tr '/' '_')"
  w="-Wall -Wextra -Warray-bounds -Wstringop-overflow -Wsign-compare -Wshift-count-overflow -Wreturn-stack-address -Wdangling -Wuninitialized -Wconversion -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-implicit-int-float-conversion -Wno-float-conversion -Wno-unused-parameter"
  eval "$clang -std=c++2c -fsyntax-only -fno-color-diagnostics $w $flags \"$f\"" > "$out/raw/$id.warn" 2>&1
  eval "$clang -std=c++2c --analyze -Xclang -analyzer-output=text -fno-color-diagnostics -Xclang -analyzer-checker=core,cplusplus,security,unix,deadcode -Xclang -analyzer-disable-checker=security.insecureAPI.DeprecatedOrUnsafeBufferHandling $flags \"$f\"" > "$out/raw/$id.analyze" 2>&1
  if [ -n "$tidy" ]; then
    eval "$tidy --quiet --checks='-*,bugprone-*,-bugprone-easily-swappable-parameters,-bugprone-exception-escape,cert-arr39-c,cert-ctr56-cpp,cert-err33-c,cert-str34-c,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-pointer-arithmetic,clang-analyzer-security.*' \"$f\" -- -std=c++2c $flags" > "$out/raw/$id.tidy" 2>&1
  fi
}
export -f scan_one; export root out clang tidy
n=$(wc -l < "$out/files.txt")
echo "memsafe_scan: scanning $n translation units ($(wc -l < "$out/skipped.txt") module consumers skipped), -j$jobs, tidy=${tidy:-none}"
seq 1 "$n" | xargs -P "$jobs" -I{} bash -c 'scan_one {}'

# Normalise: file:check per occurrence, no line numbers, only src/Engine files, sorted (multiset, so a second occurrence is a new finding).
cat "$out"/raw/* 2>/dev/null | python3 -I -c '
import re, sys
root = sys.argv[1] + "/"
pat = re.compile(r"^(/[^:\n]+):\d+:\d+: (?:warning|error): .*\[([A-Za-z0-9_.,-]+)\]\s*$")
for line in sys.stdin:
    m = pat.match(line.rstrip("\n"))
    if m and m.group(1).startswith(root + "src/Engine/"):
        print(m.group(1)[len(root):] + ":" + m.group(2))
' "$root" | LC_ALL=C sort > "$out/current.txt"
total=$(wc -l < "$out/current.txt")
echo "memsafe_scan: $total findings; by check:"; sed 's/^[^:]*://' "$out/current.txt" | sort | uniq -c | sort -rn | head -15

case "$mode" in
  update) cp "$out/current.txt" "$baseline"; echo "baseline updated: $baseline ($total entries)"; exit 0 ;;
  list) exit 0 ;;
esac
[ -f "$baseline" ] || { echo "no baseline ($baseline); run with --update-baseline"; exit 1; }
LC_ALL=C comm -13 "$baseline" "$out/current.txt" > "$out/new.txt"
if [ -s "$out/new.txt" ]; then
  echo "memsafe_scan: NEW findings (not in baseline):"; sort "$out/new.txt" | uniq -c
  echo "details: grep the check name in $out/raw/*; if intended, run scripts/memsafe_scan.sh --update-baseline"
  exit 1
fi
echo "memsafe_scan: ok (no new findings; $(LC_ALL=C comm -23 "$baseline" "$out/current.txt" | wc -l) baseline entries no longer occur)"
