# Changelog, overnight of 2026-11-04

Summary of the overnight work. Detail per item lives in [OvernightLog.md](OvernightLog.md); the rules this work followed are in [OvernightPlan.md](OvernightPlan.md). Nothing was committed: all of it is in the working tree.

Ground rules that shaped everything: no temporal accumulation anywhere (no TAA, no history buffers, no reprojection), minimal compute, intuitive APIs.

Perf-number caveat: most timings were taken on a shared machine while other agents were building, so they are noisy (up to about 2x on earlier runs). Allocation counts are exact. Where a figure is a median of 3 runs it says so. Re-record `docs/bench-baseline.json` on a quiet machine before trusting timing compares. GPU timings are from one GTX 1650 run, about +-10 percent.

## Renderer

### No temporal accumulation (audit)
- SSR, GTAO GI, colour bleed and portal views used to read last frame's lit colour through `prevViewProjection`. All removed. SSR now runs after lighting on the current frame; GI and bleed use a current-frame estimate (gbuffer albedo times a sun and ambient estimate). Portal views lost their history pyramid.
- Kept on purpose: eye adaptation (scalar EV smoothing only), cached shadow cascades (scheduled, not image accumulation), Hi-Z occlusion history (geometry visibility).
- No TAA, jitter, history denoiser or temporal shadow was found.

### Reflection modes
`RenderSettings::reflectionMode`: Off, SSR, SSRProbes (default), SSRPlanar, WorldSpace. Plus `reflectionQuality` (Low, Medium, High), `reflectionIntensity`, `reflectionTraceDistance`.
- SSR: hierarchical Hi-Z march, roughness cones via colour pyramid mips, edge, facing and distance fade; a miss falls back to probe or sky.
- Probes: `ReflectionProbe` component, parallax-corrected box projection, baked cubemaps, rebaked on scene, sky or sun change.
- Planar: `PlanarReflection` component, plane is the entity's local +Y; a screen-space mirrored gather, not a second scene render.
- WorldSpace: coarse 64^3 voxel grid of visible opaque bounds traced with a DDA when SSR misses.
- Legacy `ssr 0/1` still parses.

### Colour bleed
Spatial one-bounce gather inside the GTAO pass, filtered by the existing bilateral blur. Keys: `colorBleed`, `colorBleedIntensity`, `colorBleedRadius`, `colorBleedQuality`. See [ColorBleeding.md](ColorBleeding.md).

### Lighting quality
Cascade cross-fade (`shadowCascadeBlend`), clamped slope normal offset, half PCF taps on far cascades, screen-space contact shadows (`contactShadows`, `shadowContactLength`), wider GTAO denoise (`aoDenoise` 0..3), bent normals (`bentNormals`) for diffuse IBL and specular occlusion, area lights (`LightComponent::sourceRadius` / `sourceLength`, `areaLights`), cloud haze (`cloudAerialStrength`). See [Lighting.md](Lighting.md).

### Upscaler and quality governor
`renderScale` (0.5 to 1, step 0.05) renders smaller, then EASU and RCAS (`upscaleSharpness`) and FXAA at window size. The governor (`qualityGovernor`, `targetFrameMs`, `governorMinScale`) adjusts the scale to hit a frame time; logic is in `Engine.Render.governor`. Fixed: debug lines were drawn against a cleared window-sized depth buffer when renderScale < 1; `depth_upscale.frag` now copies scene depth over.

### GPU measurements (GTX 1650, 1900x1008, ms per frame, single run)

| Scene | default | reflections Off | World space | renderScale 0.5 |
| --- | --- | --- | --- | --- |
| showcase | 19.13 | 16.01 | 20.46 | 7.40 |
| vista | 13.06 | 12.06 | 12.74 | 5.97 |
| crowd | 13.39 | 10.43 | 13.95 | 6.05 |
| portal_torture | 17.60 | 16.24 | 18.09 | 7.00 |

Full per-pass tables: [GpuTimings.md](GpuTimings.md). The first real GPU run (56 runs, 4 scenes x 14 variants) had no Diligent errors, no black or NaN frames: [GpuValidation.md](GpuValidation.md).

## IK and the spider
New modules `Engine.IK.Solvers` (FABRIK with hinge and cone limits, CCD, analytic two-bone with pole; fixed scratch, no allocation) and `Engine.IK` (`IkChain`, `Walker`, `makeSpider`, recipes, hierarchy detection, JSON rigs, debug draw, serialization). Systems run right after `animation`. Editor commands `ik.spider`, `ik.chain`, `ik.walker`, `ik.debug`. See [IK.md](IK.md). NaN and unbounded-iteration bugs found in review were fixed.

## Navigation
Dynamic obstacles repath only agents whose path crosses a rebuilt tile; resumable A* with an octile heuristic and an isolated-goal probe; new `NavCrowd` with ORCA avoidance, path cache and time-sliced repaths; corner cutting. See [Nav.md](Nav.md).

| Bench (noisy, median of 3) | before | after |
| --- | --- | --- |
| nav.rebuild_all (64 m arena) | 46.4 ms | 10.9 ms |
| nav.find_path_x100 | 1177 ms | 35.5 ms (see caveat) |
| nav.step_agents_200 | 13.0 ms | 0.147 ms, 0 allocs |
| 1k agents per step | 0.6 ms | 0.41 ms |
| 5k agents per step | 14 ms | 5.1 ms |
| 0.7 m counterflow lanes arriving | 22/40 | 39/40 |

Caveat: much of the find_path gain comes from the bench's two unreachable goals; reachable-only paths gained less and have not been re-measured.

## World performance

| Bench (median ms, quiet-ish runs) | before | after |
| --- | --- | --- |
| spatial.raycast, 1000 rays, 100k entities | 72-108 | 15.8, 0 allocs |
| overlapSphere x1000 | 7.4 (5325 allocs) | 1.9, 0 allocs |
| frustum x100 | 88 (14.7 MB) | 31, 0 allocs |
| portal raycast x500 | 33 | 3-7 |
| portal overlap x500 | 60 (352k allocs) | 30 (1.3k allocs) |
| iterate position 1M | 5.2 | 1.46 |
| spawn 1M | 92 | 75 |
| save capture 20k | 11.5 | 10.0 (allocs 37 to 23) |
| save apply 20k | 11.0 | 7.5 |

Raycast is a face-only grid DDA; queries have out-parameter overloads that reuse caller storage; names and tags moved to a `TextTable`; `World::snapshot` builds one string. Not done: SoA spawn path, parallel transform propagation (determinism risk).

## Json
`Json::dump` writes straight into a reserved string and `Json::parse` is a SAX builder (API unchanged; added `Json::array`, `Json::object`, `Json::reserve`, `readTextFile`). Output is byte-identical to nlohmann (golden tests with 300 random documents and 200k floats). Parse depth is limited to 512 and an embedded NUL is rejected.

| Bench (noisy, 3 runs) | before | after |
| --- | --- | --- |
| dump 50k entities | 205 ms, 1.2M allocs | 81-96 ms, 17 allocs |
| parse 50k entities | 405 ms, 1.8M allocs | 217-272 ms, 250k allocs |

## Asset cache
`AssetManager::loadProcessed` / `loadProcessedMany` with an on-disk `.litcache/*.litmesh` cache (content-hashed, versioned by `kImportSettingsVersion`, atomic writes, corruption falls back to reimport) and parallel import through `JobSystem`.

| Bench (180k-tri OBJ; 8 x 28k-tri OBJs; noisy; median of 3) | time |
| --- | --- |
| single cold | 555-594 ms |
| single warm | 15-20 ms (about 30x) |
| batch of 8, serial cold | about 650 ms |
| batch of 8, parallel cold | about 240 ms |
| batch of 8, parallel warm | 8.6-10.4 ms |

Not wired yet: `Engine::loadMesh` still uses the old bake path, and `Renderer::uploadMesh` recomputes LODs.

## Save, delta and async autosave
Delta capture compares in place and applies only changed fields; JSON codec split into `Save/DeltaCodec`. `SaveSystem::saveAsync`, `waitPending`, `pendingWrite`, `autosaveAsync` write on a worker; the engine autosave system now uses `autosaveAsync`. The save file format is unchanged.

| Bench (noisy, median of 3) | before | after |
| --- | --- | --- |
| delta capture+apply 2k of 20k | 9.1 ms, 40k allocs | 2.2 ms, 0 allocs |
| delta json roundtrip | 24.1 ms, 46k allocs | 9.2 ms, 6k allocs |

## Determinism and replay verify
`Engine.WorldHash` (`hashWorldState`, `StateHasher`), replay hash checkpoints, `ReplayVerifier`. Fixed sources of nondeterminism: script run order, `overlapSphere` and `findByTag` result order (now by entity index). See [Determinism.md](Determinism.md).

## Editor
- Profiler panel (flame graph, GPU passes, per-system cost, JSON export), hot reload with error overlay and rollback to the last good scene, Diagnostics panel backed by `Engine.SceneValidator`, and an "explain this frame" tool.
- Palette: `profiler.toggle`, `profiler.export`, `scene.validate`, `frame.explain`, `reload.scene`, `render.reflections`, `render.reflectionQuality`, `render.scale`, `render.governor`, `render.colorBleed`, `render.contactShadows`, `render.bentNormals`, `render.areaLights`, `ik.*`.
- Large editor files were split into cohesive units.

## Tests, bench, sanitizer, tooling
- `LitBench` (51 benchmarks, JSON output and regression compare, counts allocations), `LitShot` (renders frames on a real GPU to PNG), `Headless` gained `--help`, `--hash-interval`, `--verify-replay`.
- Scripts: `scripts/check.sh`, `scripts/render_check.sh`; `.clang-tidy`.
- Sanitizer build (`-DLIT_SANITIZE=address,undefined`, dir `build-san`) ran every ctest test with zero ASan or UBSan reports. Not sanitizer findings: StreamingTests thresholds fail only under sanitizer slowdown.
- New test executables: AssetCacheTests, DeterminismTests, EdgeTests, HeadlessRenderTests, IKTests, JsonGoldenTests, NavTests, QualityTests, ReflectionTests, ReviewTests, ValidatorTests. ctest was 25/25 at last full check. Big test files were split into per-topic files under 400 lines.

## Bug fixes
Json deep-nesting stack overflow and NUL handling; IK and walker NaN poisoning and unbounded solver iterations; spider option NaN and UB casts; `World::raycast` NaN input, rays longer than 4096 cells, and entities beyond the 21-bit spatial key aliasing into wrong cells; `overlapSphere` NaN; validator missed NaN lights; editor left the profiler on after Explain-this-frame or closing the window; empty scene path was watched.

## Review rounds 2 and 3

Round 2 (Nav, Asset, Save, Streaming, Geometry): about 25 bugs fixed, 603 checks in Review2Tests. Nav config and NaN handling, `cellOf` UB, crowd repaths for the wrong agent, BVH out-of-bounds, asset cache OOM on forged sizes, save forged counts, async save exceptions and temp file collisions, streaming worker exceptions.

Round 3 (AI, Audio, Animation, Portal, Physics, Storylet, Observation, State, Params): about 25 bugs fixed, 112 checks in Review3Tests. AI repeat cast, NaN and reload out-of-bounds, WAV parsing and audio NaN, animation bad parents and short poses, portal caps and escaping, state `fromJson` atomicity, storylet reentrancy, observation dangling pointer, params NaN.

## Raycast

- Rays that miss the scene bounds are clipped to the occupied cell bounds: 451 / 2222 ms per 1000 rays down to 0.05 ms. Ray-sphere uses double precision. `WorldSpatialFarTests` cover it.
- Coarse 8x8x8 occupancy blocks let the grid walk skip empty neighbourhoods (sparse max100 -30%, clustered -8%, dense uniform unchanged). `RayBoxTest` was rewritten with plain arrays and a precomputed parallel-axis flag (-1.6%).
- Honest numbers: the dense 100k-entity case (`spatial.raycast_100k_x1000`) is about 15.8 ms per 1000 rays and reproduces across runs. The earlier "4.5 ms" claim was wrong and did not reproduce.
- Nav: the `find_path_x100` gain (424 -> 35.5 ms) came mostly from unreachable goals. Reachable-only paths cost about 0.31 ms each (30.6 ms per 100). No hierarchical graph yet.

## Robustness

Atomic `SaveSystem::apply` and `load` with rollback, load-robust streaming test assertions (work units instead of wall clock), mutex in `NavMesh::ensureLinks`, `StreamingSystem::prime` timeout. ASan, UBSan and LSan were clean over all tests.

## Mesh cache and shader cache

- Meshes: `Engine::loadMesh` goes through `AssetManager::loadProcessed` and a `.litcache` directory; `Renderer::uploadProcessedMesh` skips re-optimising. 180k-triangle OBJ cold 767 ms, warm 15.5 ms. `preloadMeshes` imports in parallel (8 meshes 874 ms serial cold, 8.6 ms parallel warm). Unwritable or corrupt cache falls back to a fresh import.
- Shaders: SPIR-V cache in `.litcache/shaders` with parallel prefetch. Window-to-buffers startup 2143 ms cold, about 400 ms warm. `LIT_NO_SHADER_CACHE=1` disables it.

## Reflection probes, sky visibility, clouds, lights

- `ReflectionProbe` has a `resolution` (32/64/128/256, old scenes load as 128). Probes no longer rebake automatically; use `Lighting::bakeProbes()`, `Engine::bakeReflectionProbes()` or the Inspector "Bake now" button. A bake also runs when the probe count or resolution changes.
- A world-space sky-visibility volume (`sky_volume.comp`, 64^3 R8) darkens ambient and reflection light in enclosed rooms, fixing the interior haze (white wall luma 178 -> 100; open outdoor views unchanged). Static boxes only, 0.8 m cells.
- The environment capture no longer rebuilds every frame while clouds animate; it refreshes after 7.5 units of cloud travel (Sky+IBL 1.8 ms to 0.21 ms on non-refresh frames).
- Default `LightComponent` intensity is 1e6 lm so a new light is visible in daylight.
- Fixed black renders: `litgpu::makeBinding` called unqualified `bind`, and ADL picked `std::bind`, so no resources were bound. Now `litgpu::bind` is qualified.
- `SaveSystem::apply` now takes a rollback capture first: `data.save_apply_20k` 9.1 -> 18.8 ms by design.

## Known gaps / unverified (needs GPU eyeball)
- The new shaders compiled and ran on a GTX 1650 without errors, but image quality has only been judged from screenshots by automated checks. Eyeball: reflection modes (especially WorldSpace, which shows a hard rectangular edge in the glossy floor reflection and stair-stepping on wall edges), planar, probes, colour bleed, bent normals, area lights, EASU/RCAS sharpness, governor stability.
- The editor profiler, hot reload and diagnostics panels were never launched visually.
- Planar reflections are a screen-space gather, one plane active, off-screen content falls back. WorldSpace voxel hits are unshadowed.
- Light units are photometric; the default point light is now 1e6 lm.
- Sky-visibility volume covers static boxes only (0.8 m cells); dynamic objects do not occlude sky.
- No stale mesh/shader cache pruning.
- Hierarchical pathfinding, bulk scene loader fix-up, G-buffer packing, depth pre-pass: not done.
- A scene with a reflection probe has not been rendered; the "Bake now" inspector button has not been seen.
- Spawned IK rigs are not undoable.
- ASan+UBSan ctest passes (29/29); uninitialised-memory scan not done. Stale `build/` (33 GB) and `build-asan/` (4.6 GB) were left for you to delete.
- Bench baseline re-recorded at load 0.8-2.0 (moderately quiet).

## How to try each feature

Build once: `cmake -S . -B build-release -G Ninja && cmake --build build-release`. Run the editor from `build-release/` (`src/Editor/Editor`). Press Space for the palette and type the command id or title. See [FeatureGuide.md](FeatureGuide.md) for more.

| Feature | Command |
| --- | --- |
| Reflection mode | palette `render.reflections` (cycles), `render.reflectionQuality`; setting `reflectionMode 4`; `LitShot --testscene showcase --set reflectionMode=4 --screenshot out.png` |
| Colour bleed | palette `render.colorBleed`; `colorBleedIntensity`, `colorBleedRadius` |
| Lighting quality | palette `render.contactShadows`, `render.bentNormals`, `render.areaLights`; `aoDenoise`, `shadowCascadeBlend` |
| Upscaler, governor | palette `render.scale`, `render.governor`; `renderScale`, `targetFrameMs` |
| Spider / IK | palette `ik.spider`, `ik.chain`, `ik.walker`, `ik.debug` |
| Validator, explain frame | palette `scene.validate`, `frame.explain` |
| Profiler | palette `profiler.toggle`, `profiler.export` |
| Hot reload | edit the scene file or a surface shader while the editor runs; palette `reload.scene`, `render.reload` (F9) |
| Replay verify | `Headless --scene s.litscene --ticks 600 --record run.replay --hash-interval 30` then `Headless --scene s.litscene --verify-replay run.replay` |
| Benchmarks | `build-release/tests/LitBench --filter nav. --json out.json --compare docs/bench-baseline.json --tolerance 10%` |
| GPU feature sweep | `scripts/render_check.sh outdir` |
| Sanitizers | `cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLIT_SANITIZE=address,undefined` |
| Full check | `scripts/check.sh` |

## Gallery test scene
- `LitShot --testscene gallery` renders a courtyard exercising colour bleed, polished floor, material sweeps, glass, emissives, area, point and spot lights, a reflection probe, sun and clouds. See docs/images/gallery.png.
