# Lighting, shadows, AO, area lights and upscaling

Everything here is spatial or per-frame. No history buffers, no reprojection, no temporal accumulation.
All options are `RenderSettings` fields, so they work from settings files (`renderScale 0.75`), the params bus, `setRenderSetting`, the Look panel (Shadows and Screen space sections) and palette commands.

## Shadows
- Cascades are texel-snapped (stable under camera motion) with per-texel Vogel rotation, so the noise sticks to the shadow map rather than the screen.
- Normal-offset bias scales with cascade texel size and a clamped slope term (no huge offsets at grazing angles).
- PCSS blocker search plus a variable filter radius (`pcss`, `shadowSoftness`). Far cascades (2 and up) use half the PCF taps.
- `shadowCascadeBlend` (0..0.5): fraction of each cascade range that smoothly cross-fades into the next cascade (smoothstep). 0 disables the blend.
- Contact shadows (`contactShadows`, `shadowContactLength` in metres): 12-step screen-space march toward the sun in the current depth buffer, thickness-limited, fading toward the ray end. Cheap blocker-search style detail for small casters the cascade cannot resolve.

## GTAO
- Half-resolution bitmask GTAO with GI and colour bleed (see ColorBleeding.md).
- `aoDenoise` (0..3): separable bilateral blur radius 0, 2, 4, 6 taps per side. Weights combine a Gaussian, a depth tolerance and a local depth-slope plane fit, so slanted floors blur without halos and edges stay sharp. The same weights filter the bent normal.
- `bentNormals`: the GTAO pass writes a cosine-weighted average of unoccluded sector directions (RGBA8, half resolution). The lighting pass upsamples it with the depth-aware AO upsample, then
  - diffuse IBL samples the SH irradiance along the bent normal instead of the surface normal,
  - specular occlusion intersects the visible cone (from AO) with the specular cone (from roughness) around the bent normal (Jimenez-style) and blends it with the Lagarde term.

## Area lights
- `LightComponent` has `sourceRadius` and `sourceLength` (metres). Radius 0 and length 0 keep the old punctual behaviour. Radius > 0 is a sphere light; length > 0 makes a tube along the entity forward axis. Point lights only (spot lights keep their cone parameters). Both fields are serialised (older files load with zeros) and editable in the Inspector.
- Shading uses the representative-point method: the closest point on the sphere or segment to the reflection ray gives the light vector, the GGX roughness is widened by `radius / (2 d)` and the lobe is energy-normalised by `(alpha / alpha')^2`. Distance attenuation adds `radius^2` to d^2 so near-field intensity stays finite.
- `areaLights` toggles it globally (palette: render.areaLights).
- Packing: for non-spot lights `GpuLight.params.x` = radius, `params.y` = half length, `directionCos.xyz` = tube axis.

## Sky and clouds
- `cloudAerialStrength` (0..2) scales the distance haze that blends cloud layers into the horizon sky colour (aerial perspective for clouds). Terrain uses the existing aerial-perspective froxel LUT (`fog`, `aerialDistanceScale`).

## Spatial upscaling
- `renderScale` (0.5..1.0, quantised to 0.05) renders the scene (G-buffer, depth, Hi-Z, lighting, post) at `scale * window` and presents at window size.
- Compute passes (`post/easu.comp`, `post/rcas.comp`, FSR1-style, own implementation): edge-adaptive 4x4 Lanczos-like resample with local min/max clamp (no ringing), then contrast-adaptive sharpening driven by `upscaleSharpness` (0 disables). FXAA then runs at output resolution on the sharpened image. With `renderScale = 1` the passes are skipped entirely.
- Editor overlay lines and the profiling HUD draw at window resolution; while scaled they use a separate window-sized depth buffer, so debug lines are not depth-tested against the scene.

## Quality governor
- `qualityGovernor` (default off), `targetFrameMs` (4..100), `governorMinScale` (0.5..1).
- Input: total GPU ms when GPU timings are on (full profiling), otherwise the CPU frame time. Exponential smoothing (0.12).
- Ladder (levels 0..8): each level lowers `renderScale` by 0.05 down to `governorMinScale`; reflection quality drops from level 3; shadow resolution halves from level 4 (never below 512). Values are relative to the user baseline; editing any governed setting while active re-baselines. Disabling the governor restores the baseline.
- Anti-oscillation: step down only after 1.5 s continuously above 106 % of the target; step up only after 6 s continuously below 72 % (dead band); 2.5 s cooldown after every change; if a step up is followed by a step down within 12 s the up-hold doubles (up to 4x). One level per decision.
- Code: `src/Engine/Render/QualityGovernor.cppm` (pure CPU, deterministic), driven from `Renderer::updateQualityScale` (`RendererQuality.cpp`). Tests: `tests/QualityTests.cpp`.

## Commands
`render.contactShadows`, `render.bentNormals`, `render.areaLights`, `render.governor`, `render.scale` (cycles 1.0, 0.9, ... 0.5).

## Verification status
Shaders validate with `glslangValidator -V --auto-map-bindings -Iresources/shaders/include`. GPU output was not visually inspected in this environment.
