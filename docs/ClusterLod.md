# Cluster LOD (dense geometry)

Import-time meshlets plus a simplification DAG (`Engine.meshlet`, `MeshletBuild.cpp`, stored in `.litmesh`), drawn through a GPU per-cluster cut, frustum, cone and two-pass Hi-Z cull into a compute-expanded index buffer (`RendererMeshlets.cpp`, `resources/shaders/meshlet_*.comp`). Every phase (G-buffer early and retest, transparency, cascades, local shadow maps, portal views) draws clusters with its normal surface pipelines.

Settings: `clusterLod` (on), `clusterErrorPixels` (1), `clusterHierarchy` (on), `clusterShadowTexelLod` (on), `clusterShadowErrorTexels` (2). LitShot: `--testscene dense --dense-grid N --stats FILE`.

## Hierarchy (BVH over the DAG)

Built on the CPU when a cluster mesh is uploaded (`buildClusterHierarchy`, `MeshletHierarchy.cpp`; the `.litmesh` format is unchanged):

- Clusters of each DAG level are split into bands of their parent error (powers of two). A cluster whose group could not be simplified is carried to a later pass: it keeps its creation level but gets a much larger parent error, and mixed into the leaves of its level it made every leaf pass the LOD test at far distances.
- Each (level, band) range is split top-down along the longest axis of the cluster centres, at multiples of 64, into leaves of at most 64 consecutive clusters (one `meshlet_cull` workgroup each) under nodes of up to 8 children. A root over all (level, band) subtrees (up to 64 children per node) closes the tree. Clusters are reordered inside their level so leaves are contiguous; clusters reference no other cluster by index, so nothing else changes.
- Node (48 bytes, `ClusterNode`): culling sphere around the descendants' culling spheres, LOD sphere around their self and parent LOD spheres, minimum self error, maximum parent error, first child or cluster, count (leaf bit).
- Node test: a descendant can be in the cut only if `minSelfError` projected at the LOD sphere's farthest distance is within the threshold and `maxParentError` projected at its nearest distance exceeds it. Projected error falls with distance and the spheres contain the descendants' spheres (also after the instance transform, radius times the largest axis scale), so the test is conservative and the selected clusters are exactly those of the flat per-cluster test.
- dense scene meshes: 1.31M triangles, 38725 clusters, 24 levels: 682 leaves, depth 6; 328k triangles, 9859 clusters: 215 leaves, depth 5.

GPU traversal per visibility phase, inside the existing `Meshlet Cull` / `Meshlet Retest Cull` / `Meshlet Trans Cull` / shadow builds (no new render-graph passes; the node queue, counters and arguments are internal buffers that live only inside `buildMeshletDraws`, like the work items before):

1. `meshlet_instances`: one thread per visible cluster-mesh instance queues (entry slot, root node).
2. `meshlet_node_args` + `meshlet_nodes`, once per tree level (level-synchronous, indirect dispatch sized by the level's queue counter, two queue halves of 2^18 items): LOD bound on the LOD sphere, frustum and Hi-Z on the culling sphere. Accepted internal nodes queue their children, accepted leaves become work items. In the early opaque phase a node rejected only by the previous-frame Hi-Z is recorded in the retest list (`tail.w = 1`); `meshlet_retest` turns those records into queue items of the retest phase, which resumes the traversal from that node against the new Hi-Z (cluster-mask records work as before).
3. `meshlet_args` + `meshlet_cull`: unchanged per-cluster cut, frustum, cone and Hi-Z per leaf.

The Hi-Z test on a sphere that contains a smaller sphere is at least as conservative as on the smaller one (coarser or equal mip, superset footprint, nearer nearest depth), so node-level Hi-Z rejects nothing the per-cluster test would draw.

`clusterHierarchy 0` keeps the flat path (one thread per instance loops over all 64-cluster chunks of its mesh) for A/B runs. Both paths use the reordered clusters.

Stats (`--stats`): `NodesTested` per camera and shadow views besides the existing counters.

## Shadow LOD from shadow-texel error

Shadow phases select the cut in shadow texels of the view being rendered, not with the camera metric:

- Sun cascades: orthographic, `error_world * cascade.texelsPerUnit` (no distance term; every cluster of a mesh sees the same scale, the cut is still a closed DAG cut because errors are monotonic).
- Local shadow maps (ranked cube/spot maps): perspective from the light, `error_world * texelsAtUnitDistance / distance`, per view of the batched build (view = command slot / shader count).
- Threshold `clusterShadowErrorTexels` (2) divided by the LOD bias. `clusterShadowTexelLod 0` restores the camera metric times 4.
- `RendererShadows.cpp` sets the views with `Renderer::setMeshletShadowLod` before each shadow visibility build; `meshlet_instances` (flat chunks), `meshlet_nodes` and `meshlet_cull` read them from `MeshletConstants.u_lodViews` when `MESHLET_FLAG_VIEW_LOD` is set.
- Virtual shadow maps do not draw clusters: they draw the triangle LOD chain, which `vsm_cull.comp` already picks from the page's texel size. Cluster casters per VSM page are not implemented.

## Interactions

- Skinning / morph / deform: dynamic meshes get their own mesh slots without a cluster DAG, so they never take the cluster path; unchanged.
- Terrain scatter (`scatterClusters`): batches go through `uploadClusterMesh` like any mesh and get a hierarchy; release clears the mesh slot (root included). The cluster store, now including nodes, stays append-only (leaks while streaming, as documented in Terrain.md). No test scene enables `scatterClusters`, so this path was not run on the GPU.
- Portal views: every view runs the same builds with its own scene uniforms and Hi-Z history, so the traversal uses the view's camera, clipped frustum and Hi-Z.

## Verification (2026-11-23, GTX 1650, 1280x720, `LitShot --deterministic`)

Pixel equivalence, dense grid 24, 30 frames, `autoExposure=0` (auto exposure reads back the luminance without waiting, so with it on two runs of the same binary differ by about one level over the whole image whenever GPU timing changes):

| Run | differing pixels vs flat path (`clusterHierarchy=0 clusterShadowTexelLod=0`) |
|---|---|
| flat again (noise) | 0 |
| hierarchy, camera shadow metric (x2 runs) | 0 (bit-identical) |
| hierarchy + texel shadow LOD 1 / 2 / 4 | 14162 / 10416 / 13561 px, max 65 (speckles of rock self-shadowing) |

Against a fine shadow reference (`clusterShadowErrorTexels=0.25`): old camera metric 19483 px, texel 1: 9621, texel 2: 13654, texel 4: 26745. Texel 2 is closer to the reference than the old metric.

Camera triangles and clusters drawn are identical between the paths (grid 48: 129368 clusters, 5911992 triangles).

`MeshletTests`: hierarchy structure and conservative bounds (`validateClusterHierarchy`), traversal cut equal to the flat cut for 3 thresholds x 8 distances x 3 directions (perspective) and 8 cascade scales (orthographic, also crack-free and monotonic), leaf/fanout variants (64/8, 16/2, 64/4), a single-leaf mesh and the empty mesh. From outside views the traversal tests about 12 % of the clusters (13283 of 108675).

### Cost, `LitShot --testscene dense`, 1280x720, 60 frames

Before = the binary at the start of this work (flat chunk loop over creation-order clusters, camera shadow metric). The GPU is shared with other processes; repeated runs spread by up to 2x when another job is active, the numbers below are the quietest runs (ms, last frame). `CmdGen` contains the object command generation plus the meshlet expansion and cluster cull of the early phase.

| grid (instances, source triangles) | path | CmdGen | Retest CmdGen | camera clusters tested | camera work items | nodes tested |
|---|---|---|---|---|---|---|
| 24 (576, 0.3 G) | before | 2.82 | 0.83 | 2.78 M | 43786 | - |
| 24 | hierarchy | 1.20 | 0.42 | 0.75 M | 20537 | 50012 |
| 48 (2304, 1.2 G) | before | 5.79 | 2.25 | 7.89 M | 131548 | - |
| 48 | flat on reordered clusters (`clusterHierarchy 0`) | 1.72 | 0.70 | 1.92 M | 41219 | - |
| 48 | hierarchy | 2.07 | 0.91 | 1.24 M | 56506 | 181767 |

Camera output is identical in all rows (grid 48: 129368 clusters, 5.91 M triangles drawn). Most of the gain comes from the spatial reordering of clusters inside their (level, error band), which also tightens the flat path's 64-cluster chunks. At this scale the flat loop over the reordered chunks is as fast as the traversal (the hierarchy tests 35 % fewer clusters but emits more, smaller leaves: carried-cluster bands leave partially filled leaves, and each leaf costs a 64-lane cull workgroup); the traversal's per-instance cost grows with the log of the cluster count instead of linearly, which matters for meshes with many more chunks or many more instances. Node tests themselves are cheap (6 levels x 2 small dispatches per build).

Shadows (grid 48, sun cascades only): before 8.89 ms with 3.98 M shadow triangles; reordered clusters with the old camera metric 4.3-4.6 ms (same triangles); texel LOD 1 / 2 / 4: 14.3 / 6.8 / 4.1 ms with 19.8 / 8.4 / 3.8 M triangles. The default (2 texels) is closer to a fine reference than the old metric (see above) and costs about 2 ms more than the old metric on the reordered clusters, still below the original 8.9 ms.

Other scenes (`autoExposure=0`, flat/old twice vs hierarchy + texel LOD): skinning 87 px noise / 28 px; portal_torture 0 / 0; showcase 3 / 3; gallery 46 / 9; terrain 49280 / 50709 (streaming noise); vsm_mixed (has one cluster mesh, skinned actors and VSM) 568910 / 563428 px (scene is not reproducible run to run). No validation errors or overflows. `ctest` 41/41.

### Gaps

- Leaf fill: error bands leave partially filled leaves, so the hierarchy emits more cull workgroups than the flat chunks at grid 48. Packing several small leaves into one cull workgroup (or a mesh-shader path) would remove that.
- Level-synchronous traversal: one indirect dispatch pair per tree level and build (about 12 builds per frame in the dense scene); a persistent-thread queue would need one dispatch.
- Queue capacity 2^18 (instance, node) items per level: overflows are counted (`Overflows`) and lose geometry; enough for about 10k visible dense instances.
- Shadow phases have no cluster frustum culling (the cascade frusta do not include the caster extrusion); VSM pages draw the triangle LOD chain, not clusters.
