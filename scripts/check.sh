#!/usr/bin/env bash
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

build_dir="${CHECK_BUILD_DIR:-build-release}"
line_limit="${CHECK_LINE_LIMIT:-500}"
status=0
locker=()
[[ -n "${CHECK_LOCK:-}" ]] && locker=(flock "$CHECK_LOCK")

if [[ "${CHECK_SKIP_BUILD:-0}" != 1 ]]; then
    cmake -S . -B "$build_dir" >/dev/null || status=1
    if ! "${locker[@]}" cmake --build "$build_dir" -- -k 0 2>&1 | grep -E "error|FAILED"; then
        echo "build: ok"
    else
        status=1
    fi
fi

if [[ "${CHECK_SKIP_TESTS:-0}" != 1 ]]; then
    (cd "$build_dir" && ctest -j"$(nproc)" --output-on-failure) || status=1
fi

first_party=(src/Engine src/Editor src/Sandbox tests)
existing=()
for directory in "${first_party[@]}"; do
    [[ -d "$directory" ]] && existing+=("$directory")
done

echo "--- stray comments"
if grep -rnE '(^|[^:"A-Za-z])//' "${existing[@]}" --include='*.cpp' --include='*.cppm' --include='*.hpp' --include='*.h' | grep -v '"[^"]*//[^"]*"'; then
    status=1
else
    echo "none"
fi

echo "--- files over $line_limit lines"
find "${existing[@]}" -type f \( -name '*.cpp' -o -name '*.cppm' -o -name '*.hpp' -o -name '*.h' \) -print0 \
    | xargs -0 wc -l | awk -v limit="$line_limit" '$2 != "total" && $1 > limit {print $1, $2}' | sort -rn

exit "$status"
