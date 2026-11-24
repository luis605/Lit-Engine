# Streaming, world deltas, portal bridges, GPU observation

Game agnostic. Tests: `tests/StreamingTests.cpp` (headless), `tests/ObservationGpuTests.cpp` (hidden window, needs Vulkan).

## Engine.Streaming (W-6)

`engine.enableStreaming(config)` adds a `streaming` PostUpdate system. Cell residency follows portal graph distance from the camera cell (`PortalSystem::cameraCell`, BFS over portal partners): cells within `loadHops` are loaded, cells beyond `keepHops` are evicted (hysteresis). A `CellLoader` has four callbacks:

- `prepare(cell)`: worker thread (`AsyncJobs`, priority = graph distance), returns an opaque payload. No World or GPU access.
- `commit(cell, payload, budget)`: main thread, called until it returns true. It must stop when `budget.exhausted()` and call `budget.spend(bytes)`.
- `evicting(cell)` then `unload(cell, budget)`: also budgeted and resumable.
- `resident(cell)`.

`makeBatchLoader(world, portals, deltas, build)` is the generic loader: `build(cell)` returns `CellContent` (keyed `EntityDesc` list) on a worker, commit creates entities within the budget and registers them with `DeltaStore`, unload destroys in batches.

Per-frame budget: `StreamingSystem::sharedBudget()` is the frame's single `StreamBudget` (reset after `recordFrame`); cells, assets and `TerrainStreamer` (docs/Terrain.md) all draw from it. `commitMillisPerFrame` and `uploadBytesPerFrame`, shared by commits, unloads and assets. At least one step runs per frame, so oversized items still make progress. Assets: `requestAsset({key, priority, bytes, load, finish})`, `load` on a worker, `finish` (GPU upload) on the main thread under the same budget.

`prime(cell)` loads synchronously (start-up or teleport, never a loading screen at runtime). Entities appear as they commit; a cell is `Resident` after its last step, which is also when deltas apply.

Metrics (`StreamingStats`): rolling frame time p50/p99/max over `window` frames (recorded from `Engine::tick`), `hitches` (frames above `hitchMillis`), `updateMillis` and `updateMaxMillis` (streaming cost per frame), bytes committed, cells loaded/unloaded/cancelled, pending, resident. With `enablePerfLog`, the CSV gains `frame_p99_ms`, `stream_update_ms`, `stream_pending`, `stream_resident`, `stream_hitches`, `stream_mb`, usable with `scripts/perf_check.py` limits.

## Unified residency (`ResidencyManager`)

One system decides what streams in, in which order and how much. `StreamingSystem::residency()` owns it; `Engine::enableTerrain` attaches the `TerrainStreamer` (`attachResidency`, also when `enableStreaming` comes later), and `MeshResidency` mirrors its GPU meshes into it. Domains still decide *what* is wanted (portal BFS and rings, terrain quadtree, mesh LOD requests); the manager decides order and budget.

Resident kinds (`ResidentKind`): `PortalCell`, `GridCell` (cell without portals), `TerrainTile`, `GpuMesh`, `ScatterBatch` (merged scatter meshes of a tile, accounted and pinned to the tile).

- **One metric** (`ResidencyManager::score`, lower = sooner, "estimated seconds until needed"): `(distance metres + hops * hopMetres) / max(minSpeed, closing speed)`, plus `outOfViewSeconds` when not in view and a per-kind bias, times `residentBias` (<1, hysteresis) for items already loaded or loading, minus a tiny size term for ties. Closing speed is the camera velocity (observed per space: cells use floating-origin local coordinates, terrain absolute) projected on the direction to the item. Cells take the better of their metric distance and their portal hop reading. At rest it reduces to ordering by distance. `ResidencyConfig::deterministic` ignores velocity.
- **One worker pool**: `AsyncJobs` of the manager, priority = `poolPriority(score)`. Cell prepares, asset loads and tile prepares compete in it. `AsyncJobs::submit(priority, fn(bool cancelled), token)`: setting the token before a worker picks the job up skips the work (the job still reports completion); `cancelledJobs()` counts them. Cells and tiles that become unwanted while preparing are cancelled this way and counted per kind.
- **One commit queue and budget**: producers `offer(owner, kind, score, step)` resumable commit steps (cell commit/unload, asset finish, tile commit). `arrive` flushes when every producer active last frame has arrived (so whichever of streaming and terrain runs last drains one queue in metric order across kinds; unlimited budgets flush at once). The queue stops at `StreamBudget::exhausted()`; the first step of a flush always runs. `sharedBudget()` is the manager's per-frame budget. Tile unloads still draw from the same budget directly.
- **One byte budget**: `ResidencyConfig::byteBudget` (0 = unlimited). Resident items register bytes (committed bytes of a cell or tile, mesh bytes, scatter batch bytes) with a score and a protected flag (wanted now / touched this frame). Above the budget, unprotected items are evicted least important first (highest score) across all kinds; pinned items count but are never chosen. Eviction requests the normal unload, so deltas are captured as usual.
- **Stats**: `StreamingStats::kinds[kind]` (`ResidencyKindStats`: resident, pending, loaded, unloaded, cancelled, evictedForMemory, bytesCommitted, residentBytes, commitMillis/Max, `hitches`), plus `residentBytes`, `memoryEvictions`, `jobsCancelled`. A hitch frame (`recordFrame` above `hitchMillis`) is attributed to the kind that spent the most commit time that frame.
- **Deterministic mode**: `StreamingConfig::deterministic` (and `TerrainStreamConfig::deterministic`): update waits for the workers and commits unlimited until nothing is pending, and the metric ignores camera velocity, so residency and commit order depend only on the camera path, not on timing or worker count (`tests/ResidencyTests.cpp`). `prime` is the same with unlimited budget.

Deltas, save and floating-origin shifts are unchanged (cells and tiles keep their own delta cells; scores use frame independent distances and velocities). `AssetRequest::distance` / `MeshStreamRequest::distance` give the metric a distance; without it `priority` is read as hops.

## Spatial streaming, mesh residency (open world)

`StreamingConfig::spatial` adds distance rings (load radius, keep radius, per-ring `lod`) on top of the portal graph; a cell is wanted if either criterion wants it. `MeshResidency` budgets GPU mesh uploads with LRU eviction and LOD requests. Floating origin keeps all of this consistent. See `docs/LargeWorld.md`.

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

Terrain deltas still use their own cells (`terrain/x_z_L`, docs/Terrain.md). Tile unloads are not queued (they draw from the shared budget directly), cell and tile *selection* (what is wanted, keep radius, evict delay) stays domain specific, `maxInFlight` is per producer, and the pool size is the StreamingSystem's `workers` once terrain is attached. Memory eviction of a loading item is not attempted (only resident items count).

Renderer use of `visibleCells` (per-cell lighting and shadow selection stays in Renderer.cpp, owned elsewhere), triangle-exact ID raster.
