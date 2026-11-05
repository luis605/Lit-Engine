# Build directories

All build directories live inside the repository root (`engine/`) and are generated, never committed.

## Fresh configure

    cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake -S . -B build-debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake -S . -B build-san     -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLIT_SANITIZE=address,undefined

Build with `cmake --build <dir> -- -k 0`, test with `(cd <dir> && ctest -j8 --output-on-failure)`.
`LIT_SANITIZE` instruments the Engine target and everything linking it; on GCC the module interface
units are compiled without UBSan because GCC cannot yet instrument them.
CI uses `g++-15` (`-DCMAKE_CXX_COMPILER=g++-15`). The default build type is Release.

## Stale directories

`build` (Debug) and `build-asan` (RelWithDebInfo, `LIT_SANITIZE=address,undefined`) were configured when
the project root was `/home/lixt/Desktop/private/projects/Lit-Engine`, one level above the current
repository. Their `CMAKE_HOME_DIRECTORY` and `CMAKE_CACHEFILE_DIR` therefore point at the old parent
directory, so CMake refuses to reuse them against `engine/`, and they hold 33 GB and 4.6 GB of objects built
from the old layout. They are safe to delete by hand once nothing needs them; use `build-debug` and
`build-san` above instead. They were left in place on purpose.

## Checks

`scripts/check.sh` builds, runs ctest, then lists stray `//` comments in first-party sources and files over
500 lines. Environment: `CHECK_BUILD_DIR`, `CHECK_SKIP_BUILD=1`, `CHECK_SKIP_TESTS=1`, `CHECK_LOCK=/tmp/lit-build.lock`.
Formatting config is `.clang-format`; static analysis config is `.clang-tidy` (bugprone, performance, readability subset).
