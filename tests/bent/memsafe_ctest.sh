#!/bin/sh
# ctest BentMemSafe: runs `bent memsafe` from a sanitizer build. Skips (77) if it is not built; never builds it.
root="$1"
bin="${BENT_MEMSAFE_BIN:-$root/build-san/src/Bent/bent}"
[ -x "$bin" ] || { echo "BentMemSafe: $bin not built (cmake -B build-san -DLIT_SANITIZE=address,undefined && ninja -C build-san bent)"; exit 77; }
exec "$bin" memsafe --laws "$root/tests/bent/laws" --cases "${BENT_MEMSAFE_CASES:-3}" --seed 1 --jobs 2
