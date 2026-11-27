# Lighting, shadows, AO, area lights and upscaling

Everything here is spatial or per-frame. No history buffers, no reprojection, no temporal accumulation.
All options are `RenderSettings` fields, so they work from settings files (`renderScale 0.75`), the params bus, `setRenderSetting`, the Look panel (Shadows and Screen space sections) and palette commands.

## Depth convention
Scene depth is reversed Z (near 1, far 0, `GREATER_EQUAL`, float D32) so 50-100 km views stay usable (docs/LargeWorld.md). Shaders test sky with `isSkyDepth`; the camera Hi-Z keeps the minimum, the shadow Hi-Z (standard Z) the maximum. Cascade and local shadow maps stay standard Z (clear 1, `LESS_EQUAL` in the surface shadow PSO); using the scene's `GREATER_EQUAL` there leaves every shadow map empty.

## Shadows
- Cascades are texel-snapped in world space (stable under camera motion and floating-origin rebases) with per-texel Vogel rotation, so the noise sticks to the shadow map rather than the screen.
- Normal-offset bias scales with cascade texel size and a clamped slope term (no huge offsets at grazing angles).
- PCSS blocker search plus a variable filter radius (`pcss`, `shadowSoftness`). Far cascades (2 and up) use half the PCF taps.
- Portal views share the main camera's cascades when those cover the view (`Lighting::useCascades`; `cellParams.z > 0` makes `sunShadow` pick the finest cascade containing the point instead of using the view-depth splits, no cross-fade). See docs/Portals.md, "Shared sun cascades".
- `shadowCascadeBlend` (0..0.5): fraction of each cascade range that smoothly cross-fades into the next cascade (smoothstep). 0 disables the blend.
- `shadowOcclusionCulling` (default off): two-pass Hi-Z occlusion culling of sun cascade casters. Off draws every caster inside the cascade in one pass and builds no shadow Hi-Z; the image is the same.
- Contact shadows (`contactShadows`, `shadowContactLength` in metres): 12-step screen-space march toward the sun in the current depth buffer, thickness-limited, fading toward the ray end. Cheap blocker-search style detail for small casters the cascade cannot resolve.

## GTAO
- Half-resolution bitmask GTAO (defaults: 3 slices, 8 steps per side). Occluders fade out smoothly between `aoRadius` and twice that (the occluded arc shrinks toward the open end of the slice) instead of a hard cut at 2R, which used to draw a halo band around objects.
- GI and colour bleed are a separate pass after lighting that gathers this frame's lit colour (see ColorBleeding.md). `ssgiIntensity` default is 1 (physically one bounce); it was 2.5 when the bounce was a fake sunlit albedo. `ssgiStepCount` (0..24) sets the GI march steps; 0 (default) is automatic.
- `aoDenoise` (0..3): separable bilateral blur radius 0, 2, 4, 6 taps per side. Weights combine a Gaussian, a depth tolerance and a local depth-slope plane fit, so slanted floors blur without halos and edges stay sharp. The same weights filter the bent normal.
- `bentNormals`: the GTAO pass writes a cosine-weighted average of unoccluded sector directions (RGBA8, half resolution). The lighting pass upsamples it with the depth-aware AO upsample, then
  - diffuse IBL samples the SH irradiance along the bent normal instead of the surface normal,
  - specular occlusion intersects the visible cone (from AO) with the specular cone (from roughness) around the bent normal (Jimenez-style) and blends it with the Lagarde term.

## Area lights
- `LightComponent` has `sourceRadius` and `sourceLength` (metres). Radius 0 and length 0 keep the old punctual behaviour. Radius > 0 is a sphere light; length > 0 makes a tube along the entity forward axis. Point lights only (spot lights keep their cone parameters). Both fields are serialised (older files load with zeros) and editable in the Inspector.
- Shading uses the representative-point method: the closest point on the sphere or segment to the reflection ray gives the light vector, the GGX roughness is widened by `radius / (2 d)` and the lobe is energy-normalised by `(alpha / alpha')^2`. Distance attenuation adds `radius^2` to d^2 so near-field intensity stays finite.
- `areaLights` toggles it globally (palette: render.areaLights).
- Packing: for non-spot lights `GpuLight.params.x` = radius, `params.y` = half length, `directionCos.xyz` = tube axis.

## Clustered local lights
- Up to 32768 point/spot/area/tube lights per frame (`kMaxLights`, collector cap `kMaxSceneLights` in `Engine.cpp`), uploaded to one GPU light buffer.
- `light_cull.comp` runs per view (main and every portal view, with that view's clipped frustum planes): a per-light pass frustum-culls with a tight cone bounding sphere for spots and compacts view-space spheres, then one 64-thread group per froxel cluster (32 x 18 screen tiles x 32 exponential depth slices) tests them against the cluster AABB and appends up to 512 indices per cluster to a variable-length list sized for the worst case (the cluster buffer is about 41 MB, so the list cannot run out). When more than 512 lights touch a cluster, the ones with the highest estimated contribution at the cluster are kept (ties broken by light index, so the choice is the same every run) and a warning is logged once. `ClusterBuffer` layout is documented in `lights.glsl` (`clusterRange`).
- Debug view 10 shows lights per cluster (heatmap, 32 = red).
- Portal cells: lights are filtered per view by cell (`cellParams.x`), and lights of a neighbouring cell reach a cell through its portals as images with an aperture (`lightAperture()` in `lights.glsl`, apertures after `kMaxLights` in the light buffer, VSM faces clipped at the portal plane). See docs/Portals.md, "Light through portals".
- Cell grade: `SceneUniforms::cellTint` multiplies ambient (SH irradiance, prefiltered environment, reflection fallbacks) and the sun illuminance of a portal cell's views (docs/Portals.md, "Cell lighting grade").

## Virtual shadow maps (local lights)
`virtualShadows` (default on, palette `render.virtualShadows`) shadows every local light; off falls back to the ranked `localShadowCount` cube/spot maps. Code: `LightingVirtualShadows.cpp`, shaders `lighting/vsm_*.comp|vert`, `include/vsm.glsl`.
- Faces: a spot light owns one perspective face, a point light six cube faces (32768 face slots). Each face has virtual mips of 64..1024 texels (`vsmMaxMip` 0..4) cut into 64x64 pages (341 page-table entries per face).
- Stable slots: the CPU matches each light to last frame's light with identical parameters (cache stays valid) or, for a moving light, to the nearest previous light of the same colour/range/type within 4 m (slot kept, face marked dirty). Lights in the main frustum are assigned first, then the rest in distance order (portal views see shadows of nearby off-screen lights).
- Per view, after its depth pass (same frame, no history): `vsm_request` walks the cluster lights of one pixel per 2x2 block (footprint widened by the block) and marks the page at the mip where one shadow texel is no larger than one screen pixel (`vsmResolutionScale` scales that), widened by the filter footprint, plus mip 0 of the face as a fallback. Pages are stamped with the frame; the first stamp appends them to a request list.
- Page management (`vsm_pages.comp`): physical pages not requested in this or the previous frame return to a free stack; requested pages without valid content are queued for rendering. Overflow is deterministic: every face gets one of 8 priority tiers on the CPU (rank by projected size `range / distance`, lights outside the main frustum last, ties by light index); `HIST` counts the requests that still need a page per (mip, tier), `PLAN` grants whole groups, coarse mips first and by tier within a mip, while free pages last, and `ALLOCATE` pops pages only for granted groups. Ungranted pages count as overflow and lookups fall back to coarser mips (then unshadowed), so the finest pages of the least important faces go missing first, independent of thread order.
- Cached pages depend only on light geometry (position, range, spot direction and cone, portal clip plane): colour or intensity changes never redraw them.
- `vsmMoveTolerance` (metres, default 0.02): a light that moved less than this since its pages were rendered keeps them, so bobbing or jittering lights do not redraw their shadows every frame; and the same threshold applies to moving shadow casters; shadows lag by at most that distance. 0 redraws on any movement.
- `vsmAutoScale` (default on) multiplies `vsmResolutionScale` by a factor from the visible light count versus the pool (`pool pages / 32` lights at 1, +0.5 per doubling, clamped to 4); `VirtualShadowStats::resolutionScale` reports the effective value.
- Cache invalidation is scheduled, not image-based: the first view of a frame compares every object's bounds, transform, renderable word (mesh, material, shader, flags) and deform stamp with a copy from the previous frame; the swept sphere of every changed object dirties overlapping faces, as does a moved or new light. The deform stamp is a per-entity counter written by `skin_bounds.comp` whenever a dynamic mesh's pose, morphs or deform rewrote its vertices, so a skinned or deformed mesh invalidates pages even when its refit bounds stay the same (see Skinning.md). `vsmCache 0` re-renders every requested page every frame.
- Rendering: one group per queued page culls object spheres against the page sub-frustum, instances are bucketed per mesh LOD (prefix sum + scatter) and drawn with one indirect multi-draw into the D32 atlas (`vsmPoolSize` 2048/4096/8192 = 1024/4096/16384 pages). Where the adapter can write the viewport index from the vertex shader, each page is drawn through its own viewport and scissor (16 per draw) with one clip distance for the portal plane; otherwise (or with `LIT_NO_VIEWPORT_INDEX=1`) the vertex shader maps the face projection onto the page's atlas tile in clip space and cuts the rest with four clip distances; pages are cleared with one quad each.
- Lookup: the page table at the pixel's mip (falling back to the finest coarser resident page), 4 rotated bilinear-compare taps (neighbouring pages looked up per tap), normal-offset and slope bias scaled with the texel size, `localShadowSoftness` widens the kernel.
- Stats: `Lighting::virtualShadowStats()`; `LIT_VSM_STATS=1` logs faces, requests, rendered/resident pages, overflow, caster instances and moved objects every 30 frames.
- Limits: casters have no custom vertex code or alpha test in virtual shadows; dynamic (skinned, morphed, deformed) meshes are drawn from their per-instance vertex range in the shared vertex buffer, which the skinning pass rewrites before any shadow pass, so they cast their current pose. Cluster LOD meshes are drawn from their triangle LOD chain (the renderer keeps an unpatched mesh info buffer for this; cluster LOD itself zeroes those slots for the main views). Sun cascades and the ranked local shadow maps do draw clusters, with the cut chosen from shadow-texel error (`clusterShadowTexelLod`, `clusterShadowErrorTexels`, [ClusterLod.md](ClusterLod.md)). Point-sprite LODs are drawn as the coarsest mesh LOD, transparent surfaces use whatever pages opaque pixels requested (mip 0 at least), the sun keeps its cascades (not paged, redrawn every frame so they follow deformation).

## Stress scene and measurements
`LitShot --testscene light_stress --lights 4096` builds a 160 m field of 900 pillars, 256 bobbing spheres (moving casters) and N moving lights (3/4 point, 1/4 spot) at night; animation is driven by the frame index.
GTX 1650, 1266x1022, GPU shared with other processes (numbers are noisy), 4096 lights all moving, 256 moving casters:
- `virtualShadows 1`: 19456 faces, 65k page requests, pool (16384 pages) saturated, 16.3k pages and ~85k caster instances rendered per frame; Shadows 19 ms, Lighting 25 ms.
- `vsmResolutionScale 2`: 22.5k requests, 6.2k overflow; Shadows 19 ms, Lighting 17 ms.
- `virtualShadows 0` (ranked, 4 lights): Shadows 0.75 ms, Lighting 14 ms. Static lights and casters re-render nothing (showcase: 1025 resident pages, 0 rendered per frame once cached).
Use `vsmResolutionScale` 2..4 for thousands of visible shadowed lights.

## Fog and aerial perspective
- The in-scattered term of height fog and aerial perspective is scaled by the receiver's sky openness (`mix(skyVisibility, 1, 0.15)`), so rooms under a roof are not veiled by the full outdoor sky colour. Extinction is unchanged.

## Screen-space reflections
- A ray that only passes behind geometry is a miss (probe / environment fallback). It used to return the occluder's colour at 45 %, which drew dim ghosts and streaks of foreground objects into reflections.

## Sky and clouds
- `cloudAerialStrength` (0..2) scales the distance haze that blends cloud layers into the horizon sky colour (aerial perspective for clouds). Terrain uses the existing aerial-perspective froxel LUT (`fog`, `aerialDistanceScale`).

## Spatial upscaling
- `renderScale` (0.5..1.0, quantised to 0.05) renders the scene (G-buffer, depth, Hi-Z, lighting, post) at `scale * window` and presents at window size.
- Compute passes (`post/easu.comp`, `post/rcas.comp`, FSR1-style, own implementation): edge-adaptive 4x4 Lanczos-like resample with local min/max clamp (no ringing), then contrast-adaptive sharpening driven by `upscaleSharpness` (0 disables). While scaled, FXAA runs at render resolution before the resample, so single-pixel noise is removed before it can be enlarged. With `renderScale = 1` the upscale passes are skipped and FXAA runs on the full-resolution image.
- Editor overlay lines and the profiling HUD draw at window resolution; while scaled they use a separate window-sized depth buffer, so debug lines are not depth-tested against the scene.

## Quality presets
- `--set preset=NAME` (LitShot), `setRenderSetting(settings, "preset", NAME)`, a `preset NAME` settings line, the Look panel Quality buttons, or palette `quality.NAME`. Nothing is applied unless selected. A preset is a plain list of settings, so later `--set` overrides win. Values are in `src/Engine/Render/QualityPresets.hpp`.
- `low`, `medium`, `high`, `ultra`: general tiers. `default`: restores the engine defaults for every setting a preset manages.
- `balanced`: defaults plus `renderScale` 0.85, `ssgiStepCount` 8 (automatic is 12), `aoSlices` 2, `aoSteps` 6, `clusterShadowErrorTexels` 4, `contactShadows` off.
- `performance`: `balanced` with `renderScale` 0.75 and `ssgiStepCount` 6.
- Measured on a GTX 1650 at 1280x720 over the 12 `scripts/lag_check.sh` scenes (`PRESET=performance scripts/lag_check.sh DIR`): median GPU frame 1.4 to 3.8 ms lower with `balanced` and 2.3 to 5.8 ms lower with `performance`; PSNR against the default image 31 to 45 dB (`balanced`) and 33 to 44 dB (`performance`). Most of both the gain and the difference comes from `renderScale`. Thin edges and small bright details are softer, and screen-space GI on surfaces a few pixels wide can change with the render scale.

## Quality governor
- `qualityGovernor` (default off), `targetFrameMs` (4..100), `governorMinScale` (0.5..1).
- Input: total GPU ms when GPU timings are on (full profiling), otherwise the CPU frame time. Exponential smoothing (0.12).
- Ladder (levels 0..8): each level lowers `renderScale` by 0.05 down to `governorMinScale`; reflection quality drops from level 3; shadow resolution halves from level 4 (never below 512). Values are relative to the user baseline; editing any governed setting while active re-baselines. Disabling the governor restores the baseline.
- Anti-oscillation: step down only after 1.5 s continuously above 106 % of the target; step up only after 6 s continuously below 72 % (dead band); 2.5 s cooldown after every change; if a step up is followed by a step down within 12 s the up-hold doubles (up to 4x). One level per decision.
- Code: `src/Engine/Render/QualityGovernor.cppm` (pure CPU, deterministic), driven from `Renderer::updateQualityScale` (`RendererQuality.cpp`). Tests: `tests/QualityTests.cpp`.

## Commands
`render.contactShadows`, `render.bentNormals`, `render.areaLights`, `render.governor`, `render.scale` (cycles 1.0, 0.9, ... 0.5), `render.virtualShadows`, `render.vsmCache`, `render.vsmPool`.

## Verification status
Shaders validate with `glslangValidator -V --auto-map-bindings --target-env vulkan1.1 -Iresources/shaders/include` (the cull/scatter/compaction shaders use subgroup ops, which need SPIR-V 1.3; the runtime compiles for Vulkan 1.1+). GPU output is inspected with `LitShot --testscene <name>` screenshots.
