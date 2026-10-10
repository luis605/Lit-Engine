# Streaming, world deltas, portal bridges, GPU observation

Game agnostic. Tests: `tests/StreamingTests.cpp` (headless), `tests/ObservationGpuTests.cpp` (hidden window, needs Vulkan).

## Engine.Streaming (W-6)

`engine.enableStreaming(config)` adds a `streaming` PostUpdate system. Cell residency follows portal graph distance from the camera cell (`PortalSystem::cameraCell`, BFS over portal partners): cells within `loadHops` are loaded, cells beyond `keepHops` are evicted (hysteresis). A `CellLoader` has four callbacks:

- `prepare(cell)`: worker thread (`AsyncJobs`, priority = graph distance), returns an opaque payload. No World or GPU access.
- `commit(cell, payload, budget)`: main thread, called until it returns true. It must stop when `budget.exhausted()` and call `budget.spend(bytes)`.
- `evicting(cell)` then `unload(cell, budget)`: also budgeted and resumable.
- `resident(cell)`.

`makeBatchLoader(world, portals, deltas, build)` is the generic loader: `build(cell)` returns `CellContent` (keyed `EntityDesc` list) on a worker, commit creates entities within the budget and registers them with `DeltaStore`, unload destroys in batches.

Per-frame budget: `commitMillisPerFrame` and `uploadBytesPerFrame`, shared by commits, unloads and assets. At least one step runs per frame, so oversized items still make progress. Assets: `requestAsset({key, priority, bytes, load, finish})`, `load` on a worker, `finish` (GPU upload) on the main thread under the same budget.

`prime(cell)` loads synchronously (start-up or teleport, never a loading screen at runtime). Entities appear as they commit; a cell is `Resident` after its last step, which is also when deltas apply.

Metrics (`StreamingStats`): rolling frame time p50/p99/max over `window` frames (recorded from `Engine::tick`), `hitches` (frames above `hitchMillis`), `updateMillis` and `updateMaxMillis` (streaming cost per frame), bytes committed, cells loaded/unloaded/cancelled, pending, resident. With `enablePerfLog`, the CSV gains `frame_p99_ms`, `stream_update_ms`, `stream_pending`, `stream_resident`, `stream_hitches`, `stream_mb`, usable with `scripts/perf_check.py` limits.

## Engine.Delta and Engine.Save (W-7)

`DeltaStore` keeps player changes on top of cell data. Base entities are registered by stable key (`registerBase(cell, key, handle)`), with a baseline snapshot of name, tag, mesh, material, shader, alpha, layer, render flags, visibility, local matrix and serialized components. `captureCell` diffs live state against the baseline into:

- `changed`: full state of entities that differ,
- `removed`: keys of destroyed base entities,
- `added`: prefab (subtree) of entities passed to `trackAdded(cell, root)`,
- cell params (`PortalCell::params`, baseline from `snapshotParams()`).

`applyCell` runs when a cell finishes committing (batch loader does it), so unload then reload, or save then fresh process load, reproduces the world. Eviction captures first, so changes survive streaming.

`SaveContext::deltas` switches `SaveSystem` to delta mode: the body stores `deltas` instead of the world snapshot (time, camera, RNG, state, storylets, params, observation and sections are unchanged), and `apply` loads deltas into the resident cells; non-resident cells receive theirs when streamed in. `engine.enableSave()` picks up whatever was enabled before it (call it last).

Limits: entities are matched by key, so cell builders must emit stable keys; observation entity handles are restored only if still alive; loading into a session where a base entity was already destroyed cannot revive it.

## Engine.PortalBridge

- `visibleCells(frame, shadowDepth)`: cells in the last `PortalFrame` with depth, `sun`/`ambient` scale and a `shadows` flag (depth <= `shadowDepth`). `engine.visibleCells()` is refreshed in `updatePortals`. Data only; the renderer does not consume it yet.
- `PortalNavLinks::sync(nav, portals)`: one bidirectional `NavLink` per upright portal pair, between the bottom centres stood off in front of each quad (`userData` = portal index). Rolled portals are skipped.
- `portalAudioHook(portals)`: `AudioHopFn` for `AudioPropagation::setHopHook`; per portal of the cell containing `from`, entry at the near quad and exit at the partner quad, so rays continue through portals (`maxHops` bounds recursion).

## GPU observation (AT-1)

`engine.enableObservation(config, gpu = true)` adds an `observation` PostUpdate system (CPU fallback unchanged) and feeds the renderer each `update()`. `Engine.Render.observation` (`ObservationPass.cpp`, `resources/shaders/observation_ids.comp`) runs one compute dispatch after the main view's depth is final: for each cell of a 96x54 grid, 4 sub-rays are tested against the tracked entities' world bounding spheres and kept only when the scene depth at that pixel lies inside the sphere span (so real geometry occludes, sky never matches). The nearest hit writes `entity.index + 1`. The grid is copied to one of three staging buffers and read back at the next use of the same frame slot, after the existing frame fence wait, never stalling: latency is three frames. Samples carry the issuing observation frame and time, and `submitGpuIds` rejects anything beyond `maxLatencySeconds` or `maxLatencyFrames`. IDs are sphere proxies, not rasterized triangles; up to 4096 tracked entities.

Renderer.cpp changes are limited to the poll after the fence wait, one record call after the retest draw, and three small methods.

## Engine optional systems

`enableState(schema)`, `enableStorylets(path)` (adds `storylets` Update system), `enableObservation`, `enableDeltas`, `enableStreaming`, `enableSave(dir, seconds, slots)` (adds `autosave` when `seconds > 0`). Params are always on (`engine.params()`).

## Not done

Renderer use of `visibleCells` (per-cell lighting and shadow selection stays in Renderer.cpp, owned elsewhere), triangle-exact ID raster, streaming of GPU meshes (the loader decides what `commit` uploads).
