# Dynamic meshes: GPU skinning, morph targets and procedural deform

Modules `Engine.Skin` (pure CPU data and reference math, `Animation/Skin.cpp`), `Engine.SkinRig` (bridge to `Engine.Rig` and `Engine.Face`, procedural clips) and the renderer part in `Render/RendererSkinning.cpp` with `resources/shaders/skin.comp`, `skin_normals.comp`, `skin_bounds.comp`, `include/deform.glsl`.

## How it fits the renderer

The renderer draws every mesh from one shared vertex and index buffer addressed by `MeshInfo` (`baseVertex`, `firstIndex`). A dynamic mesh *instance* owns its own mesh id (7 LOD slots, all pointing at the same data) and a vertex range in that buffer. A compute pass rewrites that range, so the G-buffer, shadow, Hi-Z, retest, transparent and portal-view passes draw it like any ordinary mesh, with no special case in cull, command generation or the draw shaders. The source (rest vertices, 4-influence skin weights, morph deltas, adjacency, indices) is uploaded once and shared by any number of instances.

Per frame, after the transform pass and before any culling (so portal views and shadows see it), `Renderer::updateDynamicMeshes` runs for each dirty instance: `skin.comp` (morph targets, then linear blend skinning with the bone palette, then built-in deform) into a per-instance buffer, optionally a user deform shader, optionally `skin_normals.comp` (normal recompute), then a copy into the vertex buffer. Instances whose palette, morph weights or deform settings did not change are skipped. `skin_bounds.comp` then writes the refit bounding sphere of every instance into the entity's cull sphere (after `transform.comp`), so culling, shadow casters, Hi-Z and portals use the deformed extent.

## API

```cpp
DynamicMesh mesh = makeProceduralCharacter();            // or AssetManager::importSkinned("x.gltf")
uint32_t source = engine.createDynamicSource(mesh);      // kInvalidMeshId when there is no window
uint32_t instance = engine.createDynamicInstance(source);
EntityDesc d; d.mesh = engine.dynamicMeshId(instance);   // create the entity with this mesh id
EntityHandle e = world.create(d);
engine.setDynamicEntity(instance, e);                    // lets the refit bounds drive culling
// every frame
Skeleton skeleton = skeletonFromSkin(mesh);
computeSkinPalette(skeleton, animator.pose(), mesh, palette);
engine.setDynamicPose(instance, palette);                // jointModel * inverseBind
engine.setDynamicMorph(instance, morphWeights);          // morphWeightsFromBlendshapes() maps ARKit-52 names
engine.setDynamicDeform(instance, deformSettings);
```

The entity transform places the character; the palette is in mesh-local space. All calls are no-ops headless.

## Pieces

- Import: `AssetManager::importSkinned` (Assimp: up to 4 bone weights per vertex normalised, joint hierarchy with `aiBone::mOffsetMatrix` as inverse bind, `aiAnimMesh` morph targets, animation clips). `normalizeSkinWeights` repairs negative, NaN, out of range and non-unit weights.
- Procedural: `makeProceduralCharacter()` (17 joints, smooth weights, ARKit-named head morphs `jawOpen`, `mouthSmileLeft/Right`, `eyeBlinkLeft/Right`, `browInnerUp`, `cheekPuff`), `makeWalkClip`, `makeWaveClip`, `makeTalkClip`, `makeClothSheet`.
- Morph targets: up to 64 per mesh, position (and optional normal) deltas, weights clamped to [-4, 4].
- Deform (`DeformSettings`): `Wave` (travelling sine along the normal), `Cloth` (same ripple scaled by distance below `pinY`, so the top edge stays pinned) and `User`. A user shader is any compute shader declaring buffer `DeformVertices` (floats, 8 per vertex: position, normal, uv, read/write) and uniform block `DeformParams { uint vertexCount; float time; uint pad[2]; vec4 user[4]; }`; see `resources/shaders/deform/twist_wave.comp`. Set `userBounds` to the largest displacement.
- Refit: bounds are conservative and CPU computed (`computeDynamicBounds`): one rest-space box per bone (vertices with any weight), expanded by the summed morph reach, transformed by the palette, plus the deform reach. No GPU readback and no latency. `recomputeNormals` runs the normal pass (area weighted smooth normals from a vertex to triangle adjacency).
- `deformMeshCpu` is a CPU reference of the whole pass and is what the tests check.

## Limits

Four influences per vertex, 256 joints, 64 morph targets, no dual quaternion skinning, no sparse morph storage (a 52 target face of N vertices costs 32 N 52 bytes), normals under non-uniform bone scale are not corrected, the copy into the shared vertex buffer costs one `CopyBuffer` per instance per update, the point (impostor) LOD uses the rest-pose normal samples, and shader hot reload does not rebuild the skinning pipelines.

## Demo and checks

`build-release/src/Shot/LitShot --testscene skinning --frames 60 --screenshot skin.png`, `Headless --testscene skinning --ticks 120` (metrics: bounds, palette, morph weights), `build-release/tests/SkinningTests`.

## Shadows, invalidation and cluster LOD

- Virtual shadow maps, cascades and local shadow maps draw dynamic instances from their vertex range in the shared buffer, so they cast the current pose. Dynamic meshes are never cluster meshes (`uploadClusterMesh` is only reached from imported static meshes) and keep all LOD slots.
- Each instance has a version bumped whenever its vertices are rewritten. `skin_bounds.comp` writes it to a per-entity stamp buffer (`DynamicStampBuffer`); the VSM motion pass compares stamp, renderable word and bounds/transform with the previous frame, so deforming inside unchanged bounds, or a material or mesh change, invalidates the overlapping shadow pages.
- Test scenes: `skinning` has a back wall, a spot and two orbiting point lights (virtual shadows, run `LitShot --testscene skinning --set timeOfDay=23`); LitShot `vsm_mixed` combines the portal loop scene, cluster LOD rocks, skinned characters in two rooms and `--lights N` shadowed lights.
