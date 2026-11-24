# GPU instancing (`InstanceBatch`)

One mesh, one material and one surface shader drawn at N places, with all per-instance work (culling, LOD, distance fade, indirect draw counts) on the GPU and one entity per batch instead of one per instance. Terrain scatter uses it by default ([Terrain.md](Terrain.md)); the stress scene is `instancing` ([Tooling.md](Tooling.md)).

## API

- Component `InstanceBatch` (`Engine.Instancing`, `src/Engine/Render/Instancing.cppm`): `mesh`, `material`, `shader`, `settings` (`drawDistance`, `fadeStart`, `lodBias`, `castShadows`), `instances` (`InstanceData`: position relative to the entity, uniform scale, quaternion, RGBA8 `color`, 24-bit `seed`, `flags` `Dead` / `NoShadow`), optional `keys` (one stable `uint64` per instance).
- Edits: `assign`, `update(first, span)` (dirty range), `remove(index)` / `revive` (free mask: indices stay valid), `swapRemove(index)` (last instance moves into the hole, returns its key), `indexOfKey`, `setSettings`. `version` / `layoutVersion` and the dirty range tell the renderer what to upload.
- Engine: `createInstanceBatch(mesh, material, span<InstanceData>, settings, shader, position, parent)` (returns the entity), `updateInstances`, `removeInstance`, `swapRemoveInstance`, `destroyInstanceBatch` (= `World::destroy`), `instanceBatch(entity)`, `instanceStats()`. The entity's transform is the batch transform (parenting, floating origin: instances are relative to it, so rebases move whole batches exactly).
- Serialization: component `InstanceBatch` (registered by `registerCoreComponents` / `registerInstanceBatchComponent`): text with mesh id, material, shader, settings and every instance (scene files, snapshots, prefabs, editor component text, DeltaStore component diffs). Mesh ids are stored raw. A scene line holds at most 64 KB, so batches above roughly 900 instances cannot be loaded back from a `.litscene` (procedural content such as scatter is regenerated instead).
- Settings: `instancing` (on), `instanceOcclusion` (Hi-Z for cells and instances), `instanceCellCull` (reject 64-instance cells first), `instanceDistanceScale` (multiplies every draw distance and fade start). LitShot `--stats f` writes `instance*` counters.

## Renderer (RendererInstancing.cpp)

`Engine::syncInstanceBatches` (every frame, before drawing) mirrors the components: new and vanished components, owner entity index (after compaction), visibility (`World::isVisible` of the batch entity), dirty ranges and layout changes. Upload is the `Instance Upload` graph pass.

- Pool: one persistent structured buffer of 32-byte instance records, sub-allocated in cells of 64 (`InstanceAllocator`, best fit over a free list, high water lowered when the tail is freed). It grows by doubling with a GPU copy (limit 4M instances), never shrinks. A batch's instances are put in 3D Morton order before they are cut into cells, so each cell is spatially compact.
- Cell records (32 bytes: batch-local bounding sphere of the live instances, batch, count, live count) and batch records (64 bytes: owner entity, mesh slot, packed renderable, flags, mesh sphere plus surface `bounds`, LOD bias, fade, cells). An edit repacks only the cells its dirty range touches (records and bounds); removing one instance re-uploads one cell (2 KB).

### Culling (resources/shaders/instance_cells.comp, instance_cull.comp)

Per visibility phase two dispatches append to the phase's visible entries and draw-bucket counts, exactly like objects, after the object cull:

1. `instance_cells.comp`, one thread per pool cell: batch active / visible / casts shadows, draw distance, frustum (camera planes, cascade planes, or local shadow faces as in `cull.comp`), camera Hi-Z. Accepted cells (with their local view mask) go to a list; `dispatch_args.comp` turns its count into an indirect dispatch.
2. `instance_cull.comp`, one 64-thread workgroup per accepted cell: free mask, `NoShadow`, frustum, distance cut and seed-dithered fade between `fadeStart` and `drawDistance`, Hi-Z, LOD. Camera LOD: `lodForDiameter(lodBias * batchLodBias * projectedDiameterPx)` like objects (LOD slots 0..5, slot 6 is the point impostor); shadows: from cascade texels per unit or local view texels as `cull.comp`. Entry id = `kInstanceEntryBit (1 << 28) | pool slot`, LOD in bits 29..31, bucket = shader x mesh slots + mesh + LOD (+ view x view buckets for local shadow batches). Bucket counts are only added for entries that fit (visible capacity = objects + 512K instance entries per phase; overflow is counted).

Prefix sum, scatter and `command_gen.comp` are unchanged: every (shader, mesh, LOD) bucket is one indirect instanced draw whose instance count is computed on the GPU, no CPU readback; instances and objects with the same mesh share a draw. The point impostor LOD goes through the point pipeline.

Two-phase occlusion: the per-view visibility buffer gets one bit per pool slot after the objects' bits (128K words reserved). Early phase (`Instance Cull`, previous Hi-Z): only instances whose bit is set; failures clear it; a cell rejected as a whole clears its two words. Retest (`Instance Retest Cull`, current Hi-Z): the others; survivors set the bit. Portal views copy the whole buffer in and out of their history, so every view keeps its own instance history and culls with its own camera, clipped frustum and Hi-Z (the passes are added inside `addGeometryPasses`, which every view runs).

Shadows: `dispatchCascadeCull` (early cascade pass only; instances are not re-tested against the cascade's shadow Hi-Z) and `drawLocalShadows` (per batch of local views, faces from the same caster spheres / cones as objects) run the `INSTANCE_CASCADE` / `INSTANCE_LOCAL` variants. Grass-like batches turn shadows off per batch (`castShadows`) or per instance (`NoShadow`).

Cluster LOD: an instance of a cluster mesh lands in that mesh's bucket like an object; `meshlet_instances` / `meshlet_nodes` / `meshlet_cull` take the instance's world matrix and renderable from the batch, so instances x clusters become work items of the existing traversal (no extra pass).

### Shaders

- `include/instancing.glsl`: records, decode (`instanceLocal`, `instanceWorld`, `instanceColorOf`, `instanceSeedOf`, `instanceHash01`), included by `surface.vert`, `surface_point.vert` and the meshlet shaders; their bindings name `InstanceBuffer`, `InstanceCellBuffer`, `InstanceBatchBuffer`.
- Surface API: `VertexContext.instanceColor` / `instanceSeed` and `SurfaceInput.instanceColor` / `instanceSeed` (white and an entity-id hash for ordinary objects). `entity` is the batch entity for instances. `litWindSway(position, time, seed, direction, amplitude, height, frequency)` bends a mesh with height above its base; `foliage.surface` uses it (material `custom0` = amplitude, reference height, frequency) and multiplies `instanceColor`.
- Reversed Z: the Hi-Z tests are the objects' (`hiz.glsl`, minimum = farthest).

## Render graph

`Instance Upload` (scope Upload), `Instance Cull` and `Instance Retest Cull` (own timing scopes `Instance Cull` / `Instance Retest Cull` in `Engine::gpuTimings()`, main view only), declared after `Cull` / `Retest Cull` with reads of Hi-Z, scene uniforms, objects and `instancePool` and writes of the visibility bits and the phase. Shadow instance culls run inside `Local Shadows` / `Sun Cascades` (scope Shadows). `renderGraph=0` runs the same culls inline in `cullVisibilityPhase`.

## Verification

See the measurement log below (GTX 1650). `InstancingTests` (headless): allocator (best fit, merge, high water, random alloc/free without overlap), component edits (dirty ranges, free mask, swap-remove keys, growth, settings), GPU record packing round trip, spatial cells (conservative bounds, several times tighter than insertion order), CPU reference cull (cell cull removes no visible instance and tests far fewer, fade, distance scale, forced LOD), serializer / snapshot / editor text. `TerrainTests` `testInstancedScatter`: batches per species, removal marks dead without rebuilding, moves update in place, both survive unload/reload, saved deltas applied to a resident tile.

## Measurements (2026-11-24, GTX 1650 shared with the desktop, window 1888x1048, LitShot 90 frames, last frame)

`LIT_INSTANCING_SPEED=0 LitShot --testscene instancing --frames 90 --timings f --stats f [--set ...]`: 1,242,529 instances in 768 batches, 19,768 pool cells (38 MB pool).

| Variant | Instance Cull | Instance Retest Cull | GBuffer | Shadows | sum of passes | cells accepted | instances tested (early + retest) | drawn (early + retest) | Hi-Z occluded |
|---|---|---|---|---|---|---|---|---|---|
| default | 0.036 ms | 0.031 ms | 0.65 ms | 0.74 ms | 19.88 ms | 431 | 10,923 + 13,333 | 10,912 + 2 | 2,931 |
| `instanceCellCull=0` | 0.275 | 0.281 | 0.65 | 0.82 | 20.07 | 19,768 | 10,916 + 1,231,613 | 10,916 + 1 | 2,928 |
| `instanceOcclusion=0` | 0.035 | 0.029 | 0.69 | 0.73 | 20.06 | 431 | 13,845 + 10,400 | 13,845 + 0 | 0 |
| `instanceDistanceScale=2` | 0.044 | 0.037 | 0.73 | 0.73 | 18.65 | 731 | 21,176 + 21,662 | 21,159 + 1 | 6,798 |
| `instancing=0` (ground only) | - | - | 0.25 | 0.46 | 16.15 | - | - | - | - |

- Cull cost of 1.24M instances: 0.067 ms per frame for both camera phases (cells: one thread per cell, then 64-thread groups only for accepted cells). Without the cell level every instance is tested: 0.56 ms (8x). Shadow culls (4 cascades) are inside `Shadows`: 79,072 cell tests, 196 cells accepted, 4,991 shadow instances drawn.
- Drawn per LOD slot (default): 1131 / 1578 / 2856 / 4342 / 790 / 0 / 217 (slot 6 = point impostor). Occlusion: with Hi-Z off 13,845 drawn = 10,914 drawn + 2,931 occluded with it on (consistent counters across the two runs).
- Determinism: `--deterministic --set autoExposure=0`, 60 frames, flying camera: two runs bit-identical (0 differing pixels), graph on vs `renderGraph=0` bit-identical. No Diligent errors or warnings in any instancing run.
- Terrain (`--testscene terrain --frames 300`, `autoExposure=0`): instanced scatter 275 batches / 10,585 instances, Instance Cull 0.021 + 0.021 ms, CmdGen 0.045 ms, Shadows 1.49 ms, sum 19.8-20.0 ms over two runs; merged meshes (`LIT_TERRAIN_INSTANCED=0`): CmdGen 0.072 ms, Shadows 1.74 ms, sum 20.4 ms, but the merged runs hit out-of-device-memory while growing the shared vertex buffer for the per-tile merged meshes (the desktop compositor held 1.5 GB of the 4 GB), so they are not a clean comparison; the instanced path uploads one mesh per species instead of one per tile and species.

## Gaps

- Virtual shadow maps (`vsm_*`, local lights with `virtualShadows`) do not draw instances: VSM caches pages per object (moved-object invalidation, stamps), which has no per-instance equivalent yet. Instances receive VSM shadows but do not cast them.
- Cascades: one frustum-only pass per cascade (no shadow Hi-Z retest for instances).
- Transparent materials: instances are opaque only (translucent batches are not drawn).
- Picking / CPU raycasts and the World spatial index see the batch entity, not individual instances; there is no per-instance CPU bounds query.
- No impostor billboards beyond the existing point LOD; no GPU-driven per-instance animation besides the vertex hook (wind).
- Pool and visibility bits are capped at 4M instances; visible entries at 512K per phase (overflow counted in stats, instances beyond are not drawn that frame).
- Per-frame sync visits every batch (cheap, ~1 µs per batch); layout changes re-upload the whole batch.
