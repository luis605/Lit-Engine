# Large world: floating origin and spatial streaming

Tests: `tests/LargeWorldTests.cpp` (headless).

## Coordinate model

- **World position** (double): `local + origin`. Only the camera API and cell builders deal with it.
- **Local position** (float): what `World`, physics, portals, the spatial grid and the renderer see. `local = world - origin`.
- `World::origin()` (`glm::dvec3`), `toWorld(vec3)`, `toLocal(dvec3)`; Engine mirrors it: `engine.worldOrigin()`, `engine.cameraWorldPosition()`, `engine.setCameraWorldPosition(dvec3)`, `engine.originEpoch()`, `engine.lastOriginShift()`.
- Rebase: `engine.enableFloatingOrigin({threshold = 2000, snap = 1024})` adds a `floating_origin` PostUpdate system. When the camera's local position is farther than `threshold` from 0, the origin moves by the camera position rounded to `snap` (integers, so float subtraction is exact). `setCameraWorldPosition` also rebases when needed. `engine.rebaseOrigin(delta)` does it by hand; `delta` is rounded to float first and the origin moves by exactly that float, so world and local stay consistent.

What one rebase shifts (`Engine::rebaseOrigin`):

| System | Handling |
|---|---|
| `World` | `shiftOrigin`: every root entity position -= delta (children follow), spatial grid and GPU transforms updated through `setPositions` (one batch). |
| Camera | position -= delta; orientation untouched. |
| `PortalSystem::shiftOrigin` | cell AABBs, portal poses, portal `transform` translation (`t += R*s - s`), tracked-object `previous`. Ghost entities are roots, so the World shift covers them. |
| Physics | stateless w.r.t. position (bodies and colliders read entity transforms), velocities unchanged. Raycasts use local space. |
| `CharacterController::shiftOrigin` / `CameraRig` | position, probe and pivot. |
| `NavMesh::shiftOrigin`, `NavCrowd::shiftOrigin`, `navShiftAgents` | Grid origin, tile heights, bounds, obstacles, links and the BVH are translated; tile and node ids stay valid, `version()` is not bumped. Crowd path cache is shifted and in-flight queries restart; agent position, target and path points via `navShiftAgents`. |
| `AudioPropagation::shiftOrigin`, `AudioMixer::shiftOrigin` | Scene BVH, listener, fan hits and voice positions. The hop hook keeps working through an offset (it keeps seeing its original frame). Cached results hold directions and lengths only, so they stay valid. |
| `ObservationSystem::shiftOrigin` | Cached view origin (tracked entities are read from the World). |
| Streaming | cell residency uses cell AABBs (shifted with portals), so decisions are unchanged. |
| Deltas | live baselines of root entities are shifted with the same float op as the entity (no false diffs). Stored deltas and added prefabs keep root translations in **absolute world coordinates** (float; ~8 mm at 100 km), so they are origin independent and a save loads under any origin. |
| Save | camera saved with the origin (`camera.origin`), restored as absolute. Non-delta snapshots record `origin` and `World::shiftRoots` re-bases restored roots into the current frame. Old saves (no origin) are read as origin 0. |

Cell builders: `CellEntity::worldPosition` (double, with `hasWorldPosition`) or `desc.position` are interpreted as **world** coordinates for root entities; the batch loader converts with the origin at commit time. With origin 0 this equals the old behaviour.

## Renderer integration

Tests: `tests/LargeWorldRenderTests.cpp` (GPU, hidden window, skips without one; run from the build directory). Scene: `src/Shot/LargeWorldScene.cpp`, `LitShot --testscene largeworld [--origin-km 500] [--fly 6] [--rebase-frame 30]`.

`Engine` calls `Renderer::setWorldOrigin(worldOrigin())` once per frame before drawing. A changed origin (any number of rebases since the last frame) calls `Lighting::shiftOrigin(delta)` with the exact difference. The renderer keeps no screen-space history (no TAA, no reprojection), so a rebase only has to fix state that is cached in world space:

| State | Handling on rebase |
|---|---|
| Camera, object transforms, lights, probes, planes, portal views | rebuilt every frame from the shifted `World`; the camera moves with everything, so view-space data is identical before and after |
| Hi-Z / two-phase culling history, per-view visibility bits, per-portal Hi-Z | indexed by object id and view space; nothing to do |
| Virtual shadow maps | `vsm.allDirty`: every cached page re-renders once (light slots and the previous-object copy are in the old frame). Not shifted: the full re-render is the simple correct choice |
| Sun cascades | no cache (re-rendered each frame); texel snapping now happens in world space (`local + origin mod 4096`), so the shadow texel grid does not move |
| World-space reflection voxel grid | box hash is relative to the first box (a pure translation is not a scene change), `grid.minCorner -= delta`; `Engine` also re-collects the boxes in the new frame on the rebase frame |
| Reflection probe cubemaps | hold directions, not positions: valid as is; probe centres come from the shifted entities |
| Sky capture / env probe | keyed on world altitude (`local y + origin y`), so it is not re-captured |
| Clouds | wind offset carries `fmod(origin.xz, 1.26e6)`: `local + offset` is unchanged by a rebase (period of the cumulus noise); altitude uses world y |
| Fog | `fogHeight` is a world height: the shader sees `fogHeight - origin.y` |
| Observation GPU grid | per-request grid of entity ids (no world positions); in-flight readbacks stay valid |
| Procedural patterns (checker, triplanar) | `SurfaceInput.patternPosition = worldPosition + originModulo4096()`, `originModulo4096` = origin mod 4096 (float). Continuous across rebases when the pattern period divides 4096 (snap <= 1024 is fine). `worldPosition` stays the local position (camera-relative maths, derivatives). In shaders, GLSL `originModulo4096()` / `skyViewPos()` (scene.glsl) and `SceneUniforms::originParams` |

Not handled by the renderer: user buffers that persist positions (custom compute passes such as the particle example). They must listen to `engine.originEpoch()` and subtract `engine.lastOriginShift()`.

### Depth at 50-100 km: reversed Z

`Camera::getProjectionMatrix()` is reversed-Z (near -> 1, far -> 0) with the D32F scene depth. With standard Z, near 0.1 and a 100 km far plane one depth step is 0.6 m at 1 km and 150 m at 50 km; with reversed Z it follows 1/z (6e-8 relative, 3 mm at 50 km). Everything that touched the convention changed together:

- clear 0 (`kFarDepthValue`), scene depth tests `GREATER_EQUAL` (surfaces, overlay lines, custom draw passes, portal composite `GREATER`), portal depth/resolve/composite shaders, oblique portal clip (`obliqueProjection`: `row2 = row3 - scale * plane`, far corner at depth 0).
- camera Hi-Z keeps the **minimum** (separate mip PSO, `HIZ_KEEP_MIN`; the sun-cascade shadow Hi-Z stays standard Z and keeps max); `sphereOccluded` tests `nearest < farthest`. Hi-Z textures clear to 0.
- shaders use `isSkyDepth(d)` (`d <= 0`) and `FAR_DEPTH` (scene.glsl) instead of `depth >= 1`; the lighting pass takes sky ray directions from `worldViewDirection(uv)` (independent of the depth row, so valid under oblique portal projections). `linearViewDepth(uv, depth)`, `worldFromDepth`, `viewFromDepth` are matrix based; linear depth needs the pixel because an oblique depth row varies across the screen. Custom effects: `litIsSky(uv)` still works; raw `litDepth(uv)` is now 1 = near, 0 = far.
- cluster light culling (`light_cull.comp`) takes the near/far corners swapped; SSR marches on `1 - depth` (its ranges are <= 200 m, the old precision is enough) and `ssr_hiz.comp` builds that hierarchy from the reversed buffer.
- CPU: `World::screenRay` near/far swapped; `extractFrustumPlanes` / `extractPlanes` are symmetric (the plane order near/far is only a label); shadow cascades, VSM faces and cube faces keep their own standard-Z projections.

Near/far: default 0.1 / 100 unchanged. A 100 km view needs `camera.setFarPlane(2e5)` (the largeworld scene does); `shadowDistance` clamps cascades to `min(shadowDistance, far)`, the cluster z slices are log spaced over `far/near` (32 slices, 1.5x each at 1e6), the aerial-perspective LUT spans 32 km x `aerialDistanceScale` (3 -> 96 km), beyond which it saturates (full haze), which is the intended look at 50-100 km.

Camera-relative maths: the camera stays within `threshold` of local zero, so view matrices and near geometry carry at most 2000 m magnitudes (0.12 mm float error at the default); far geometry (50-100 km) has 4-8 mm absolute error but that is below a pixel at that range. Lower `threshold` (and `snap`) to tighten. The engine never needs a double matrix on the GPU.

Verification (GTX 1650, `LargeWorldRenderTests`): red wall 40 m in front of a blue wall at 50 km and 200 m at 100 km (standard Z cannot separate them) resolve correctly; after a forced rebase of (1024, 256, -2048) m the first frame differs from the pre-rebase frame by mean 0.491 / 255 versus a no-rebase control of 0.490 (0.004% of pixels over 8 levels, same as the control); a 6 m/s flight with rebases every ~40 frames shows no frame-difference spike at rebase frames.

## Spatial streaming

`StreamingConfig::spatial` (`SpatialStreamingConfig`):

- `loadRadius`: a cell is wanted when its AABB (XZ distance to the camera) is within it. `keepRadius` (>= loadRadius): eviction starts beyond it (hysteresis).
- `lodRadii`: ascending ring radii. A cell's detail level `lod` = number of radii smaller than its distance (0 = finest). Portal-wanted cells (graph distance <= `loadHops`) are always lod 0. `lodHysteresis` widens each ring by that many metres before the level changes. With `reloadOnLodChange` and a loader that sets `prepareLod`, a resident cell whose ring changed is unloaded (deltas captured first) and reloaded at the new level, through the normal budgeted unload/commit path.
- Combination with portals: wanted = portal criterion OR spatial criterion; evict only when BOTH say evict (graph distance > `keepHops` and distance > `keepRadius`). Load priority = min(graph hops, spatial ring).
- Cells are the existing `PortalCell`s (AABB in local space). `addGridCells(portals, world, worldMin, cellSize, nx, nz)` builds a grid of portal-less cells named `G<x>_<z>`; hand-authored portal cells work too.
- Use `streaming.update(cell, cameraPosition)` / `prime(cell, position)`; the engine system passes the camera position. `lod(cell)`, `spatialDistance(cell)`, `wanted(cell)` expose state; `StreamingStats::lodReloads` counts re-LODs.
- `makeBatchLoader(world, portals, deltas, build(cell, lod))` sets `prepareLod`; the `build` runs on a worker and chooses per-ring detail (entity count, simplified proxies).

## GPU mesh streaming

- `MeshResidency` (Engine.Streaming): `request({key, lod, priority, bytes, load(lod), upload(lod, payload), evict(lod)})`. `load` runs on a worker, `upload` on the main thread inside the shared per-frame upload budget (it goes through `requestAsset`). Lower lod number = finer. A request is ignored when the mesh is already resident at that or a finer level; a finer upload evicts the coarser one afterwards (no hole). `touch(key)` marks use; `advance()` (once per frame) evicts least-recently-used entries not touched this frame while `residentBytes() > budget()`.
- Engine: `engine.enableMeshStreaming(budget)`, `engine.requestMesh(name, lod, priority, estimatedBytes)`, `engine.onMeshResident(name, id, lod)`. Uses `AssetManager::loadProcessed` on the worker and `registerProcessedMesh` for the upload; lod > 0 uploads the mesh with its LOD chain starting at that level.

`Renderer::releaseMesh` now exists (`Engine::releaseMesh`): `Engine::requestMesh` evictions free the evicted level's GPU slots and buffer ranges; entities still using the evicted id draw nothing, so retarget them in `onMeshResident`. Residency bytes are estimates supplied by the caller.

## Not done / limits

- Navmesh, crowd, audio and observation are not owned by `Engine`: register `engine.addOriginShiftListener` and call their `shiftOrigin` (plus `navShiftAgents` on your agent arrays). `Engine::rebaseOrigin` shifts observation itself. Hop hooks that capture absolute positions of their own should use the frame they were registered in (the offset is applied for you).
- Non-root authored data in custom components holding raw positions are not shifted (components storing world positions must listen to `originEpoch()` and apply `lastOriginShift()`).
- Float precision of stored deltas at extreme distances (see above).
- Renderer: VSM pages are re-rendered after a rebase (not shifted); patterns whose period does not divide 4096 m jump when the origin-mod-4096 wraps; `kMaxCloudOffset` is the noise period of cumulus, cirrus (110 km) jumps once per wrap; Hi-Z, sky and cloud shaders were verified on the Vulkan device only.

## Unified residency

Cell streaming, terrain tiles, GPU mesh residency and scatter batches share one residency manager (docs/Streaming.md): one priority metric (time-to-needed from distance, portal hops and camera velocity), one worker pool with cancellation, one commit/unload budget and queue, one byte budget with eviction in metric order, and per-kind stats. Floating-origin shifts do not disturb it: scores use distances and velocities, which are frame independent, and teleports above 1000 m/s reset the velocity estimate.
