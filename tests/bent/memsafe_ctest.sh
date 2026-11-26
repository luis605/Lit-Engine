#!/bin/sh
root="$1"
bin="${BENT_MEMSAFE_BIN:-$root/build-san/src/Bent/bent}"
[ -x "$bin" ] || { echo "BentMemSafe: $bin not built (cmake -B build-san -DLIT_SANITIZE=address,undefined && ninja -C build-san bent)"; exit 77; }
exec "$bin" memsafe --laws "$root/tests/bent/laws" --cases "${BENT_MEMSAFE_CASES:-3}" --seed 1 --jobs 2
