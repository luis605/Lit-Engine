# Quick Start

## Requirements

A C++26 capable GCC, CMake 3.28 or newer, Ninja, a Vulkan capable GPU and driver for the windowed binaries, and Python 3 for the perf checks. Clone with submodules:

```bash
git clone --recurse-submodules --shallow-submodules -j2 https://github.com/luis605/Lit-Engine
cd Lit-Engine
```

## Build

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

Release is the default build type. Pass `-DCMAKE_BUILD_TYPE=Debug` for a debug build, or `-DLIT_SANITIZE=address,undefined` for a sanitizer build of the engine code.

Targets: `Editor`, `Sandbox`, `Headless`, `LitShot`, the test executables (including `LitBench`), and `run`.

## Run the editor

```bash
cmake --build build-release --target run
```

or run `build-release/src/Editor/Editor` from the build directory. Editor automation scripts live in `tests/editor/*.lit` (see [Editor.md](Editor.md)).

## Play the gallery

```bash
cd build-release && ./src/Sandbox/Sandbox gallery
```

`LIT_TESTSCENE=gallery` works too. First-person character with collision against floor, walls, arch, columns and boxes; the spheres are pushable rigid bodies. Controls: WASD move, mouse look, Space jump, Shift sprint, V toggle first/third person, F throw a sphere, R cycle time of day (sunrise, noon, golden hour, night), Esc release the mouse, left click recapture it. Falling off the courtyard respawns at the start. The scene lives in `Engine.GalleryScene` (`src/Engine/Tools/GalleryScene.cpp`) and is shared with `LitShot --testscene gallery`; `tests/GalleryTests.cpp` checks collisions headlessly.

## Run headless

No window or GPU is needed:

```bash
build-release/src/Headless/Headless --ticks 600 --seed 1
build-release/src/Headless/Headless --testscene crowd --metrics crowd.json
build-release/src/Headless/Headless --help
```

Determinism check (record with hash checkpoints, then verify):

```bash
build-release/src/Headless/Headless --testscene crowd --ticks 300 --record run.replay --hash-interval 30
build-release/src/Headless/Headless --testscene crowd --verify-replay run.replay
```

See [HeadlessCli.md](HeadlessCli.md) for every option, output line and exit code, [Tooling.md](Tooling.md) for replay, dump and perf logging, and [Determinism.md](Determinism.md).

## Screenshots on a real GPU

`LitShot` needs a display and a Vulkan GPU. It builds a test scene (`portal_torture`, `streaming_route`, `crowd`, `vista`, `showcase`) or loads `--scene`, applies render settings and saves a PNG:

```bash
build-release/src/Shot/LitShot --testscene showcase --frames 40 --screenshot out.png --timings out.txt --set reflectionMode=4 --set renderScale=0.5
scripts/render_check.sh outdir
```

`render_check.sh` renders every scene with each new render feature on and off. See [GpuValidation.md](GpuValidation.md) and [GpuTimings.md](GpuTimings.md).

## Run the tests

```bash
cd build-release
ctest -j8 --output-on-failure
```

Tests include the unit test executables (`WorldTests`, `AssetCacheTests`, `PortalTests`, `ToolingTests`, `JsonGoldenTests`, `NarrativeTests`, `SystemsTests`, `ValidatorTests`, `DeterminismTests`, `PhysicsTests`, `StreamingTests`, `IKTests`, `QualityTests`, `ReflectionTests`, `HeadlessRenderTests`, `NavTests`, `EdgeTests`, `ReviewTests`), the `TestScene_*` headless scenes, and the `HeadlessCli` plus `PerfCheck*` checks. `GpuSyncTests` and `ObservationGpuTests` need a GPU and are not registered with ctest.

## Full check and sanitizers

```bash
scripts/check.sh
cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLIT_SANITIZE=address,undefined
```

`check.sh` builds, runs ctest, then lists stray `//` comments and files over 500 lines (`CHECK_BUILD_DIR`, `CHECK_SKIP_BUILD=1`, `CHECK_SKIP_TESTS=1`). See [Build.md](Build.md).

## Benchmark

Headless micro-benchmarks (not in default ctest; `-DLIT_BENCH_CTEST=ON` adds a smoke test):

```bash
build-release/tests/LitBench --list
build-release/tests/LitBench --filter nav.,spatial. --json out.json --compare docs/bench-baseline.json --tolerance 10%
```

Exit 1 means a regression (median time or allocations per iteration beyond tolerance), 2 bad arguments. The baseline was recorded on a noisy machine, so re-record timings on a quiet one.

The editor benchmark:

```bash
scripts/bench.sh
```

Runs the editor through scripted camera views (`BENCH_FRAMES`, `BENCH_VIEWS`, `BENCH_BUILD_DIR`, `BENCH_SKIP_BUILD=1` tune it) and compares to `scripts/bench_baseline.txt`. For headless timing use `Headless --perf perf.csv` and `scripts/perf_check.py perf.csv thresholds.txt`.
