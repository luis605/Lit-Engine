# Architecture

Lit Engine is a C++ modules codebase built with CMake and Ninja. The library target `Engine` (`src/Engine/CMakeLists.txt`) is a static library of C++ named modules. Three executables link it: `Editor` (`src/Editor`), `Sandbox` (`src/Sandbox`) and `Headless` (`src/Headless`). Tests live in `tests/`.

```
Editor / Sandbox / Headless
          |
        Engine  (Engine.engine facade: owns World, Renderer, Input, Physics, Params, ...)
          |
   modules below
```

The simulation (`Engine::tick`) never touches the renderer, so the same code runs windowed and headless. See [Tooling.md](Tooling.md) for the loop, replay, dump and perf tools.

## Module map

Dependencies list the direct `import` lines of each module interface. The partition modules of `Engine.World` are listed together. Names are the exact module names from the `.cppm` files.

### Foundation

| Module | Purpose | Imports |
| --- | --- | --- |
| `Engine.glm` | Single place that exposes GLM types to the rest of the engine. | none |
| `Engine.Render.entity` | `Entity` id and `EntityHandle` (index plus generation). | none |
| `Engine.Events` | Typed `EventBus` for publish and subscribe between systems. | none |
| `Engine.Jobs` | `JobSystem` thread pool and parallel-for. | none |
| `Engine.Profiler` | CPU `Profiler` and `ProfileScope` timing records and summaries. | none |
| `Engine.Rng` | Deterministic seedable random number generator. | none |
| `Engine.Json` | `Json` value, parse and dump (SAX parser in `JsonParse.cpp`, direct writer in `JsonDump.cpp`, `readTextFile`). | none |
| `Engine.LineEditor` | Single-line text editing state used by consoles and palettes. | none |

### Scene and world

| Module | Purpose | Imports |
| --- | --- | --- |
| `Engine.Render.component` | ECS component structs (`TransformComponent`, `HierarchyComponent`, `RenderableComponent`, ...). | `Engine.Render.entity`, `Engine.glm` |
| `Engine.Render.scenedatabase` | `SceneDatabase` storage and dirty tracking for renderable data. | `Engine.Render.entity`, `Engine.Render.component` |
| `Engine.World` (partitions `:Types`, `:Pool`, `:Core`, `:Builder`) | Entity world: creation, hierarchy, transforms, spatial queries, attributes, scripts, scene load and save, validation, fluent builder. `:Types` holds `TextTable`, `ReflectionProbeComponent` and `PlanarReflectionComponent`. Implementation split across `World/WorldRaycast.cpp` (grid DDA), `WorldSpatial*.cpp` (dirty-cell refresh, frustum and sphere queries), `WorldScene.cpp` / `WorldSceneLoad.cpp` (text scene format), `WorldReflection.cpp` (probe and planar gather), `WorldValidate.cpp`. | `Engine.Render.entity`, `Engine.Render.component`, `Engine.Render.scenedatabase`, `Engine.Events`, `Engine.Jobs`, `Engine.Profiler`, `Engine.camera`, `Engine.Rng`, `Engine.glm`, `Engine.Render.reflection` |
| `Engine.Selection` | Editor selection set over world entities. | `Engine.Render.entity`, `Engine.World` |
| `Engine.History` | Undo and redo history of world edits. | `Engine.Render.entity`, `Engine.World`, `Engine.glm` |
| `Engine.Geometry` | `Aabb`, triangle soups and a `Bvh` built from collider geometry. | `Engine.glm`, `Engine.World`, `Engine.Render.entity` |
| `Engine.GizmoMath` | Pure math for editor gizmo manipulation. | `Engine.World`, `Engine.glm` |
| `Engine.asset` | `AssetManager` namespace: mesh and texture loading, `loadProcessed` / `loadProcessedMany` (parallel import) and the on-disk `.litcache` mesh cache (`AssetCache`). Files `Asset/AssetManager.cpp`, `AssetImport.cpp`, `AssetCache.cpp`, `AssetBatch.cpp`. | `Engine.mesh`, `Engine.Jobs` |

### Rendering

| Module | Purpose | Imports |
| --- | --- | --- |
| `Engine.camera` | Camera matrices and frustum. | `Engine.glm` |
| `Engine.mesh` | Mesh data and GPU mesh handles. | none |
| `Engine.Render.material` | Material parameter structs. | `Engine.glm` |
| `Engine.Render.settings` | `RenderSettings` quality and feature switches (reflection mode, colour bleed, contact shadows, `renderScale`, governor, ...), visitor-based so settings files, params and `set` share one key list. | `Engine.glm` |
| `Engine.Render.reflection` | CPU side of reflections: `ReflectionTier`, probe selection and planar plane choice (`Render/Reflection.cppm`). | `Engine.glm`, `Engine.Render.settings` |
| `Engine.Render.governor` | `QualityGovernor`: pure logic that adjusts render scale to hit `targetFrameMs` (`Render/QualityGovernor.cppm`, driven by `Renderer::updateQualityScale`). | `Engine.Render.settings` |
| `Engine.Render.lighting` | Light data, shadows and lighting passes, reflection composite, EASU/RCAS upscale (`LightingReflections.cpp`, `LightingUpscale.cpp`). | `Engine.glm`, `Engine.Render.settings`, `Engine.Render.material`, `Engine.Render.reflection` |
| `Engine.Render.custom` | User compute, draw and post-effect passes (`ComputeDesc`, `DrawDesc`, `PostEffectDesc`) with shader error reporting. | `Engine.glm` |
| `Engine.Render.observation` | GPU `ObservationPass` that renders object ids for visibility queries. | `Engine.glm` |
| `Engine.renderer` | Diligent based `Renderer`: frame graph, culling, Hi-Z, shadows, portal views, overlays. | `Engine.camera`, `Engine.Render.scenedatabase`, `Engine.Render.entity`, `Engine.mesh`, `Engine.UI.manager`, `Engine.UI.imgui`, `Engine.Render.lighting`, `Engine.Render.custom`, `Engine.Render.settings`, `Engine.Render.governor`, `Engine.Render.material`, `Engine.Portal.view`, `Engine.Render.observation`, `Engine.glm` |
| `Engine.UI.manager` | `UIManager` for in-game UI. | `Engine.glm` |
| `Engine.UI.text` | `Text` description for UI text. | `Engine.glm` |
| `Engine.UI.imgui` | Dear ImGui integration layer. | none |
| `Engine.Overlay` | `DebugOverlay` drawing engine statistics, observation and AI debug data. | `Engine.engine`, `Engine.Observation`, `Engine.AI` |

### Portals, streaming and saves

| Module | Purpose | Imports |
| --- | --- | --- |
| `Engine.Portal.view` | Portal surface and view-node structs shared with the renderer. | `Engine.glm` |
| `Engine.Portal` | Cell and portal graph, visibility traversal and portal frames. See [Portals.md](Portals.md). | `Engine.glm`, `Engine.camera`, `Engine.World`, `Engine.Render.entity`, `Engine.Portal.view` |
| `Engine.PortalBridge` | Links portals to navigation and audio tracing. | `Engine.glm`, `Engine.Nav`, `Engine.Portal`, `Engine.Portal.view`, `Engine.AudioTrace` |
| `Engine.Streaming` | `StreamingSystem` that loads and unloads cells under a budget. See [Streaming.md](Streaming.md). | `Engine.Delta`, `Engine.Portal`, `Engine.Render.entity`, `Engine.World` |
| `Engine.Delta` | `DeltaStore` of per-entity changes relative to authored cell content (in-place compare and apply); JSON codec in `Save/DeltaCodec.cpp`. | `Engine.glm`, `Engine.Json`, `Engine.Portal`, `Engine.Render.entity`, `Engine.World` |
| `Engine.Save` | `SaveSystem` writing and reading full game saves, including `saveAsync` / `autosaveAsync` (snapshot on the caller, write on a worker). | `Engine.Delta`, `Engine.Json`, `Engine.Observation`, `Engine.Params`, `Engine.State`, `Engine.Render.entity`, `Engine.Storylet`, `Engine.World` |

### Physics, characters and animation

| Module | Purpose | Imports |
| --- | --- | --- |
| `Engine.Physics` | Rigid body and collision simulation. See [Physics.md](Physics.md). | `Engine.Render.entity`, `Engine.World`, `Engine.glm` |
| `Engine.PortalPhysics` | Physics casts and body transfer through portals. | `Engine.glm`, `Engine.Render.entity`, `Engine.World`, `Engine.Portal`, `Engine.Physics` |
| `Engine.Character` | `CharacterController` kinematic movement. | `Engine.glm`, `Engine.Render.entity`, `Engine.World`, `Engine.Portal`, `Engine.Physics`, `Engine.PortalPhysics`, `Engine.CameraRig` |
| `Engine.CameraRig` | First person, third person and orbit camera rig with collision query. | `Engine.glm`, `Engine.Render.entity`, `Engine.World` |
| `Engine.Animation` | Transform animation clips and playback on world entities. | `Engine.Render.entity`, `Engine.World`, `Engine.glm` |
| `Engine.IK.Solvers` | Pure IK math: FABRIK, CCD, analytic two-bone, limits and poles, fixed scratch (`IK/IkSolvers.cppm`). | `Engine.glm` |
| `Engine.IK` | `IkChain` and `Walker` components, `updateIk` / `updateWalkers`, `makeSpider`, recipes, hierarchy detection, JSON rigs, debug draw. See [IK.md](IK.md). | `Engine.IK.Solvers`, `Engine.glm`, `Engine.Json`, `Engine.Render.entity`, `Engine.Render.component`, `Engine.World` |
| `Engine.Rig` | Skeletons, skeletal clips, pose sampling and blending. | `Engine.glm`, `Engine.Animation`, `Engine.World`, `Engine.Render.entity` |
| `Engine.Face` | Blendshape clips and players, eye settings. | `Engine.glm`, `Engine.Json`, `Engine.Rng` |

### Gameplay systems

| Module | Purpose | Imports |
| --- | --- | --- |
| `Engine.Params` | `ParamBus` of named, curve-mapped scalar parameters. | none |
| `Engine.State` | `StateStore` of typed game variables with change events. | `Engine.Events`, `Engine.Json` |
| `Engine.Storylet` | `StoryletEngine`: conditions, effects and line playback. See [Narrative.md](Narrative.md). | `Engine.Events`, `Engine.Json`, `Engine.Rng`, `Engine.State` |
| `Engine.Observation` | `ObservationSystem` tracking what the player observes. | `Engine.glm`, `Engine.Json`, `Engine.camera`, `Engine.Render.entity`, `Engine.World` |
| `Engine.AI` | Utility AI evaluation and execution. See [AI.md](AI.md). | `Engine.Json`, `Engine.Rng`, `Engine.Params` |
| `Engine.Nav` | Navigation mesh build, pathfinding and agents. Split into `Nav/NavBuild.cpp` (tile build), `NavPath.cpp` (resumable A*), `NavQuery.cpp`, `NavAgent.cpp`, `NavCrowd.cpp` (spatial hash, path cache, sliced repaths), `NavOrca.cpp` / `NavAvoid.cpp` (avoidance). See [Nav.md](Nav.md). | `Engine.glm`, `Engine.Geometry`, `Engine.World` |
| `Engine.AudioTrace` | `AudioPropagation`: path, reflection and reverb estimation. | `Engine.glm`, `Engine.Geometry` |
| `Engine.Audio` | `AudioMixer`, voice scheduling and backends (null and WAV). | `Engine.glm`, `Engine.Params`, `Engine.AudioTrace` |

### Input and tooling

| Module | Purpose | Imports |
| --- | --- | --- |
| `Engine.input` | Raw keyboard and mouse state. | `Engine.glm` |
| `Engine.inputactions` | `InputActions` named action bindings. | `Engine.input`, `Engine.Replay` |
| `Engine.Replay` | Input recording and playback files. | `Engine.glm` |
| `Engine.Dump` | Deterministic world and RNG state dumps, `hashHex`. | `Engine.World`, `Engine.Json`, `Engine.Rng` |
| `Engine.WorldHash` | `hashWorldState` and `StateHasher` for the deterministic world digest; replay checkpoints use it. See [Determinism.md](Determinism.md). | `Engine.World`, `Engine.State`, `Engine.Storylet`, `Engine.Params`, `Engine.Nav` |
| `Engine.SceneValidator` | `validateScene` severity-sorted issue list and `explainFrame` slow-frame ranking (`Tools/SceneValidator.cpp`, `FrameExplain.cpp`). Pure CPU. | `Engine.World`, `Engine.Portal`, `Engine.Render.entity` |
| `Engine.Perf` | `PerfLog` per-frame CSV timing log. | none |
| `Engine.TestScenes` | Built-in test scenes with metrics. | `Engine.engine`, `Engine.glm` |
| `Engine.engine` | `Engine` facade that owns and ticks all systems (split across `Engine.cpp`, `EngineScene.cpp`, `EngineTooling.cpp`, `EngineRendering.cpp`, `EngineFeatures.cpp`). | most of the modules above |

## Layering rules

- Lower layers never import higher ones. `Engine.World` does not know about rendering beyond `Engine.camera` and the entity and component types.
- Modules must stay acyclic. Shared helpers belong in a foundation module (`Engine.glm`, `Engine.Json`, `Engine.Rng`) instead of being copied.
- `Engine.engine` is the only module that composes systems; everything else is usable on its own in tests.
- Determinism: no wall clock or unseeded randomness in the simulation path. Headless runs with the same seed and inputs produce the same state hash.

## Editor

`src/Editor` is a separate set of modules (`Editor.application`, `commands`, `context`, `gizmo`, `hierarchy`, `hud`, `inspector`, `look`, `palette`, `viewport`, `autopilot`, plus `Editor.profilerpanel` (flame graph, GPU passes), `Editor.hotreload` (scene and data reload, error overlay) and `Editor.diagnostics` (validator panel)) built on `Engine`. Palette commands are registered in `Application/Application*Commands.cpp`. See [Editor.md](Editor.md).

## Other targets

- `Headless` (`src/Headless`): windowless simulation runner, see [HeadlessCli.md](HeadlessCli.md).
- `LitShot` (`src/Shot`): renders frames on a real GPU to PNG with optional pass timings.
- `LitBench` (`tests/bench`): headless micro-benchmarks with JSON compare.
- Tests in `tests/`, scripts in `scripts/` (`check.sh`, `bench.sh`, `render_check.sh`, `perf_check.py`).
