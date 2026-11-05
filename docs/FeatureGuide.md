# Feature Guide

Short how-tos. Palette commands: press Space in the editor and type the id or title. Settings keys are `RenderSettings` names; set them from a settings file line (`reflectionMode 4`), an autopilot `set` step, `setRenderSetting(engine.renderSettings(), key, value)` or `LitShot --set key=value`. Run the editor from `build-release/` (`src/Editor/Editor`).

## Reflection modes
1. Palette `render.reflections` cycles Off, SSR, SSRProbes, SSRPlanar, WorldSpace; `render.reflectionQuality` cycles Low, Medium, High.
2. Or set `reflectionMode` (0 to 4), `reflectionQuality` (0 to 2), `reflectionIntensity`, `reflectionTraceDistance`.
3. Probes: attach `ReflectionProbeComponent` (`halfExtents`, `intensity`, `blendDistance`) to an entity, e.g. `world.add<ReflectionProbeComponent>(entity)`. The component is registered as `ReflectionProbe` for scene files and `addComponentByName`.
4. Planar: attach `PlanarReflectionComponent{halfSize}`; the plane is the entity's local +Y, and mode SSRPlanar uses the nearest one facing the camera.
5. WorldSpace traces a 64^3 voxel grid of visible opaque bounds when SSR misses.
6. Try: `LitShot --testscene showcase --set reflectionMode=4 --screenshot out.png`.

## Colour bleed
Palette `render.colorBleed` toggles it (it needs ambient occlusion, `render.ssao`, on). Tune `colorBleedIntensity`, `colorBleedRadius`, `colorBleedQuality`, or use the Look panel sliders. Details in [ColorBleeding.md](ColorBleeding.md).

## Lighting quality
- `render.contactShadows` with `shadowContactLength`, `render.bentNormals`, `render.areaLights`; `aoDenoise` 0 to 3; `shadowCascadeBlend` 0 to 0.5.
- Area lights: set `sourceRadius` (sphere) and `sourceLength` (tube) on a light in the Inspector. Light intensity is photometric, so use roughly 1e6 to 1e7 for a point light to show in daylight.
- Reference: [Lighting.md](Lighting.md).

## Upscaler and quality governor
Palette `render.scale` cycles `renderScale` from 1.0 down to 0.5; `upscaleSharpness` sets RCAS strength. Palette `render.governor` toggles `qualityGovernor`, which moves the scale between `governorMinScale` and 1 to hit `targetFrameMs` (default 16.6).

## IK spider and walkers
1. Palette `ik.spider` spawns a walking spider. `ik.chain` builds a chain from the selection and creates a draggable target. `ik.walker` turns a selected hierarchy with `leg*` children into a walker. `ik.debug` draws bones, targets and feet.
2. In code: `makeSpider(world, glm::vec3(0.0f), options)`, then `setWalkerTarget(world, spider, point)` or `setWalkerInput(world, spider, glm::vec2(0, 1))`.
3. One-line recipe: `makeWalkerFromRecipe(world, pos, "quadruped length=1.0 speed=1.2")` (spider, hexapod, quadruped, biped).
4. Reference: [IK.md](IK.md).

## Navigation
Build the nav mesh, add agents and step with `navStepAgents`; crowds use ORCA avoidance and dynamic obstacles repath only the agents whose path crosses a rebuilt tile. Details: [Nav.md](Nav.md). Benchmark: `LitBench --filter nav.`.

## Scene validator and frame explainer
Palette `scene.validate` opens the Diagnostics panel (click an issue to select and fly to the entity). Palette `frame.explain` profiles a few frames and lists the top reasons the frame is slow, with settings advice. In code: `validateScene(world, limits)`, `gatherSceneFacts` and `explainFrame` from `Engine.SceneValidator`; they work headless.

## Profiler
Palette `profiler.toggle` opens the profiler panel (flame graph, GPU passes, per-system cost, history, pause and inspect); `profiler.export` writes `resources/profile.json`. F10 (`profile.cpu`) toggles the CPU overlay and F11 (`profile.trace`) writes a Chrome trace.

## Hot reload
With the editor running, saving the scene file reloads it (a failed parse rolls back to the last good scene and shows an error overlay); palette `reload.scene` forces it. Surface shaders reload on save; F9 (`render.reload`) reloads all shaders. Shader errors stay on screen until the file is fixed.

## Replay and determinism verification
```
Headless --scene s.litscene --ticks 600 --record run.replay --hash-interval 30
Headless --scene s.litscene --verify-replay run.replay
```
Exit 1 and `MISMATCH tick ...` lines mean the simulation diverged. See [Determinism.md](Determinism.md) for the rules simulation code must follow.

## Asset mesh cache
`AssetManager::loadProcessed(source, options, report)` caches optimized meshes and LODs in `.litcache/`; `loadProcessedMany(paths, options, jobs)` imports in parallel. Set `CacheOptions::directory` empty to disable. Bump `kImportSettingsVersion` when import settings change. Not yet used by `Engine::loadMesh`. Benchmark: `LitBench --filter asset.`.

## Saving and autosave
`SaveSystem::saveAsync(path, label)` snapshots on the caller and writes on a worker; `waitPending()` blocks, `pendingWrite()` queries. `Engine::enableSave` autosave uses `autosaveAsync`. The editor autosaves scenes every 60 s (`LIT_AUTOSAVE_SECONDS`).

## Screenshots, GPU timings, feature sweep
`LitShot --testscene showcase --frames 40 --screenshot out.png --timings out.txt --set renderScale=0.5` (needs a display and Vulkan GPU). `scripts/render_check.sh outdir` renders every scene with each feature on and off. Pass timings are also in the Look panel and the profiler panel.

## Benchmarks
`build-release/tests/LitBench --list`; `--filter a,b`; `--json out.json`; `--compare docs/bench-baseline.json --tolerance 10%`. Allocation counts are exact, timings are noisy on a busy machine.

## Checks and sanitizers
`scripts/check.sh` builds, runs ctest, flags stray `//` comments and files over 500 lines. Sanitizer build: `cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLIT_SANITIZE=address,undefined`. See [Build.md](Build.md).
