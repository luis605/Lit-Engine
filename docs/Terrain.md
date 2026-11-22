# Terrain (`Engine.Terrain`, heightfield collider in `Engine.Physics`)

Tiles are ordinary entities with an ordinary `Mesh`; `engine.enableTerrain(TerrainSetup)` streams them around the camera and renders them with `resources/shaders/surfaces/terrain.surface` (morph + splat). Tests: `tests/TerrainTests.cpp` (headless), test scene `terrain` (`LitShot --testscene terrain`, `Headless --testscene terrain`).

## Height sources

- `makeNoiseHeight(NoiseParams)`: seeded value-noise fBm with domain warp, ridged mixing and derivative damping (steep areas lose small octaves, the usual cheap "erosion" look), then `flatten` shaping. A pure function of world `(x, z)`, so neighbouring tiles agree on shared edges exactly and the world is unbounded.
- `HeightImage` (`fromRaw16`, `fromPgm`, `loadRaw16`, `loadPgm`; 16-bit raw little or big endian, binary PGM `P5` up to 65535) and `makeImageHeight(image, placement)`: bilinear, clamped at the edge, mapped to `[minHeight, maxHeight]`.
- `TerrainConfig` holds the `HeightFn`, `tileSize`, `tileCells`, height bounds (used for culling), skirt depth, UV scale and `SplatRules`.

## Tiles and LOD

A `TileKey {x, z, level}` covers `size = tileSize << level` metres from `(x * size, z * size)`. Every tile has `tileCells` cells, so level `L` has cell size `tileSize / tileCells * 2^L`.

`selectTerrainTiles(config, cameraPosition, LodParams)` is a pure function (no state, deterministic order) returning `SelectedTile {key, distance, morph, boundsMin, boundsMax}`. Quadtree: a node splits while `distance(camera, node AABB) < splitFactor * nodeSize`, nodes farther than `viewDistance` are dropped. `morph` goes 0 to 1 over the last `1 - morphStart` of the leaf's range (0 for the coarsest level). A renderer lerps vertex `y` by `morph * TerrainTileMesh::morph[v]`.

`buildTileMesh(config, key, TileMeshOptions)` returns an indexed grid (engine layout: position, normal, uv; positions local to `tileOrigin`) plus:

- skirts on all four edges (depth `skirtDepth * 2^level`, normals and UVs copied) to hide cracks between levels,
- `splat` per vertex, `vec4` weights (grass, rock, sand, snow) from height and slope (`splatWeights`), `dominant` layer for picking a tile material,
- `morph` per vertex, coarse height minus fine height for odd lattice vertices,
- bounds including skirts.

Vertex format has no colour channel, so splat weights are side data; `bakeTileSplat` makes an RGBA8 texture from the same rules. UVs are world xz times `uvScale`. Winding is CCW seen from above (`flipWinding` to invert if the pipeline wants it). Triangulation matches the collider: cell `(i, j)` splits `(00, 01, 11)` and `(00, 11, 10)`.

## Scatter

`scatterTile(config, ScatterConfig, key)` returns `ScatterInstance`s for `ScatterSpecies` (tree, rock, grass kinds; mesh and material handles, spacing, density, height range, minimum normal Y, biome band, clumping, scale range, slope alignment). Candidates are on a global jittered grid per species, each with its own `Pcg32(seed, species, cell)` that always draws the same numbers, so output depends only on the seed: independent of tile borders, LOD and call order (the four level-0 children produce exactly the level-1 tile's instances). `terrainBiome` is low-frequency noise in `[0, 1]`.

`scatterEntities` converts instances to `CellEntity` (stable keys `scatter/x_z_L/species/cx_cz`), consumable by `makeBatchLoader`/`CellContent` directly.

## Collider (`Engine.Physics`)

`HeightfieldCollider {shared_ptr<const HeightfieldData>, restitution}`: entity position is the min x/z corner and the height origin; entity rotation and scale apply (docs/Physics.md). Data is shared with the tile so physics and render use the same samples. Free functions (local coordinates): `heightfieldHeightAt`, `heightfieldNormalAt`, `heightfieldRaycast` (grid DDA, two triangles per cell), `heightfieldSphereContact`, `heightfieldSweepSphere` (conservative advancement on the exact distance to the triangles, bisected).

Integrated into the world level API, so everything built on them works unchanged:

- `physicsRaycast`, `physicsSweepSphere`, `physicsOverlapCapsule` (capsule = spheres along the axis),
- `stepPhysics`: spheres and boxes (3x3 sample points on the bottom face) collide with restitution and `Collision` events, after planes in each solver iteration,
- `CharacterController` (capsule overlap, step probe, ground snap, slope limit) walks terrain.

The solid is everything below the surface; outside the tile's xz extent nothing collides. Not serialized (data is regenerated from the generator).

## Streaming

`makeTerrainLoader(world, TerrainLoaderConfig)` returns a `CellLoader` (docs/Streaming.md): `prepare` runs on a worker (mesh, collider heights, scatter; no World or GPU access), `commit` runs on the main thread, resumable under `StreamBudget` (mesh upload through the `uploadMesh` callback, normally `Engine::uploadMesh`, tile entity with collider, then scatter entities in slices), `unload` destroys in batches. `tileOfCell(cell)` maps a streaming cell id to a `TileKey` (or nothing). Colliders are built up to `colliderMaxLevel`, scatter up to `scatterMaxLevel` (both default 0).

## GPU rendering (`Engine::enableTerrain`)

- `TerrainStreamer` (`TerrainStreamer.cpp`): each update `selectTerrainTiles` (now with frustum culling via `LodParams::frustumCull/planes`, margin and keep radius so shadow casters behind the view stay, and 2:1 neighbour balance by splitting too-coarse neighbours), worker `prepare`, budgeted `commit`, visibility swap (a tile is shown only when it or a resident ancestor/descendants cover it, so no holes and no overlap), delayed eviction. Unload destroys entities then calls `Engine::releaseMesh` for every GPU mesh of the tile. `maxGpuMeshes` bounds mesh slots (the renderer has 4096, 7 per mesh).
- `Renderer::releaseMesh` / `uploadTerrainMesh`: freed vertex/index ranges and whole 7-slot LOD blocks are reused (best fit; tiles all have the same size). `uploadTerrainMesh` is raw (no simplification, no point impostor). `MeshResidency` eviction (`Engine::requestMesh`) releases the evicted level's GPU mesh; entities still using it draw nothing, retarget them in `onMeshResident`. Cluster DAG of a released mesh is detached (the cluster store itself is append-only).
- Morphing: `TileMeshOptions::packMorph` stores the morph delta in the normal length (`|n| = 1 + delta/256`). `terrain.surface` `vertex()` computes the factor per vertex from the camera distance with the same ranges as `selectTerrainTiles` (`range = 2*split*size`, from `morphStart`), so it also applies in the cascade shadow pass. Per-level materials carry `custom0 = (nodeSize, splitFactor, morphStart, uvScale)` (negative size = coarsest, no morph). Other shaders just see a slightly longer normal.
- Splat: `surface()` recomputes the weights of `splatWeights` from height (object-space y) and slope, with `custom1 = (sandMax, snowMin, albedoLayers, normalLayers)`; procedural grass/rock/sand/snow with noise breakup, triplanar rock, procedural bump. With `TerrainSetup::albedoTextures/normalTextures` (5 bits per layer id) texture arrays are used instead (triplanar rock albedo and whiteout normal blend, planar for the rest).
- Scatter: species with `geometry` are merged per tile and species into one static mesh (`scatterBatches`), prepared on the worker (LOD chain with point impostor; cluster DAG when `scatterClusters`), one entity per batch. Species without geometry keep one entity per instance.
- Winding: CCW from above matches the pipeline (`FrontCounterClockwise`), verified on screen; `flipWinding` is not needed.
- Shadows: cascade shadows use the same vertex path (morph included). Local-light VSM (`vsm_caster.vert`) draws the unmorphed tile, so a shadow edge can differ by the morph delta during a transition.

## Not done

- Not instanced per entity on the GPU: there is no instance buffer primitive, so scatter is static batching per tile (culling and LOD per tile batch, not per tree).
- No virtual texturing or baked splat texture use; splat is procedural per pixel (texture arrays optional).
- VSM casters ignore morph. Cluster store is append-only, so `scatterClusters` leaks while streaming.
- Loader does not register scatter with `DeltaStore`. Physics notes: heightfield is static, boxes use sample points; erosion is a noise shaping trick.
