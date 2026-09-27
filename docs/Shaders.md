# Shader API

Lit renders with a deferred PBR pipeline driven by the GPU culler. You extend it at four points, each a plain GLSL file that hot-reloads when saved (F9 forces a reload):

| Extension | File | Runs | Registered with |
|---|---|---|---|
| Surface shader | `resources/shaders/surfaces/<name>.surface` | per vertex and per pixel of every entity that uses it, in the G-buffer, shadow and transparency passes | `engine.loadShader(name, path = {})` |
| Compute job | any `.comp` | before the scene, after lighting or before post | `engine.addCompute(ComputeDesc)` |
| Custom draw | `.vert` + `.frag`, or `.mesh` (+ `.task`) + `.frag` | after lighting, into the HDR target with scene depth | `engine.addDraw(DrawDesc)` |
| Post effect | `resources/shaders/effects/<name>.effect` | `hdr` stage: after transparency, before exposure, bloom and tonemapping; `ldr` stage: after tonemapping, before FXAA | `engine.addEffect(name, path = {})` |

Compiled SPIR-V is cached in `resources/cache/shaders/`, keyed by a hash of the source, every resolved `#include`, the macros and the stage, so only changed shaders compile on the next launch. Delete the folder or set `LIT_NO_SHADER_CACHE=1` to bypass it.

With `engine.setShaderWatch(true)` (the editor and sandbox turn it on) the engine polls `resources/shaders/`, plus every loaded surface file that lives elsewhere, twice a second and reloads all shaders when a file changes.

## Surface shaders

A surface file contains up to two entry points plus any helper functions and constants they need. Everything else (G-buffer packing, shadows, transparency, point-sprite LODs) comes from the templates in `resources/shaders/surface/`.

```glsl
#pragma lit bounds 0.35

float bands(float x) { return floor(x * 4.0) / 4.0; }

void vertex(inout VertexData v, in VertexContext c) {
    v.position += v.normal * sin(c.time * 2.0 + v.position.y * 6.0) * 0.1;
}

void surface(in SurfaceInput i, inout Surface s) {
    float n = litFbm(i.objectPosition * 3.0 + i.time * 0.2, 4);
    s.baseColor *= 0.6 + 0.4 * bands(n);
    s.roughness = mix(0.3, 0.9, n);
    s.emissive += vec3(1.0, 0.4, 0.1) * pow(n, 4.0) * 4000.0;
}
```

`surface` receives `s` already filled from the entity's material (base color and texture, opacity, normal map, metallic, roughness, occlusion, emissive), so a shader only changes what it cares about. `vertex` is optional and runs before the world transform, in object space. Only the helpers `vertex` actually calls are compiled into the vertex stage.

Pragmas:

- `#pragma lit bounds <units>` grows the culling sphere by that many object-space units. Use it when `vertex` pushes geometry outside the mesh bounds, otherwise displaced parts pop when the original bounds leave the screen or pass behind an occluder.
- `#pragma lit twosided` disables back-face culling for the surface.

Built-in surfaces, loaded at startup in this order (their ids): `Standard`, `Checker`, `Glass`, `Lava`, `Marble`, `Toon`, `Unlit`. At most 16 surfaces can be loaded.

### Inputs

| `VertexData` | `VertexContext` | `SurfaceInput` | `Surface` (output) |
|---|---|---|---|
| `position` | `world` (mat4) | `worldPosition`, `objectPosition` | `baseColor`, `opacity` |
| `normal` | `entity`, `material` | `viewDirection`, `geometricNormal` | `normal` (world space) |
| `uv` | `time` | `uv`, `screenUv` | `metallic`, `roughness`, `occlusion` |
| | `custom0`, `custom1` | `entity`, `material`, `time` | `emissive` (nits, pre-exposure) |
| | | `custom0`, `custom1`, `frontFacing` | `tangentAngle` (anisotropy direction) |
| | | | `lightingModel`, `toonBands`, `toonRim`, `toonRimWidth` |

`custom0` and `custom1` are the two free `vec4`s on `Material`, so one surface shader can serve many materials with different parameters, and the inspector edits them live.

### Lighting models

`s.lightingModel` picks how the pixel is lit. It defaults to `LIT_STANDARD` and works the same in the deferred path and in the transparency (forward) path.

| Value | Result |
|---|---|
| `LIT_STANDARD` | the full PBR model: sun with shadows, clustered point and spot lights, image-based lighting, SSR and GTAO |
| `LIT_UNLIT` | no lighting, shadows, AO or fog: the pixel shows `baseColor` as a display-referred color (written straight to the exposed HDR image, so it looks the same at any exposure; tonemapping still applies) plus `emissive` in nits |
| `LIT_TOON` | cel shading: the sun and every punctual light are quantized into `toonBands` steps (1 to 255, default 3), a hard specular highlight is sized by `roughness`, ambient is flat SH irradiance, and a rim light of strength `toonRim` (0 to 1, default 0.5) covers the outer `toonRimWidth` (0 to 1, default 0.3) of the silhouette |

```glsl
void surface(in SurfaceInput i, inout Surface s) {
    s.lightingModel = LIT_TOON;
    s.toonBands = 2.0;
    s.toonRim = 0.8;
}
```

The G-buffer stores the model in the 2-bit alpha of the normal target. Toon pixels reuse the metallic and anisotropy bytes for their parameters, so toon surfaces are always dielectric and isotropic, and `toonRim`/`toonRimWidth` are stored with 16 levels. `resources/shaders/surfaces/toon.surface` reads bands, rim and rim width from `custom0.xyz` (0 keeps the default), and `unlit.surface` is the minimal unlit shader.

### Helpers

- Noise: `litHash(p)`, `litNoise(p)`, `litFbm(p, octaves)`, `litVoronoi(p)` (returns the distance to the nearest cell point and a random cell id in `[0, 1)`), `litHsvToRgb(hsv)`.
- Normals: `litBumpNormal(i, N, height, strength)` perturbs a normal from a scalar height field using screen-space derivatives; `litPerturbNormal(N, p, uv, tangentNormal)` applies a tangent-space normal without precomputed tangents.
- Textures: `litSample(color, layer, i, triplanar, scale)` and `litTriplanar(color, layer, p, n, scale)` read the engine texture arrays (`color = true` for sRGB albedo, `false` for data such as normal or ORM maps). Layers come from `engine.loadTexture(path, srgb)`.

Opacity below 1 (from the material or written by `surface`) sends the entity through the order-independent transparency pass automatically; the same `surface` function runs there, lit by the forward path with the same lighting model.

## Compile errors

User code is spliced into the templates with `#line` directives, so every error names your file and line, for example `resources/shaders/surfaces/rusty.surface:12: 'rougness' : undeclared identifier`. Errors are printed to the log and queued for the host: `engine.takeShaderErrors()` returns the new `ShaderError { source, message }` entries since the last call (the editor shows each as an error toast), and `engine.shaderErrors()` lists the surfaces that currently fail.

When a surface fails:

- If it compiled before, the previous version keeps running.
- If it never compiled (a new surface, or a broken file at startup or scene load), its entities draw with the `Standard` surface until the file compiles; `engine.shaderStatus(name)` reports `fallback`. The surface stays registered, so saving a fix hot-reloads it.

`engine.shaderStatus(name)` returns `ok`, `fallback`, `failed` (no fallback was possible) or `missing`. Custom passes that fail to build are skipped (their `PassInfo::valid` is false); on reload a pass keeps its previous pipeline if the new source fails.

## Compute jobs

```cpp
ComputeDesc sim;
sim.name = "Particle simulation";
sim.path = "resources/shaders/compute/particles.comp";
sim.groups = {80, 1, 1};
sim.stage = ComputeStage::BeforeScene;
const uint32_t id = engine.addCompute(sim);
engine.setShaderBuffer("Particles", particles);
engine.setPassParam(PassKind::Compute, id, 0, glm::vec4(center, count));
```

Stages: `ComputeStage::BeforeScene` (before culling and the G-buffer), `AfterLighting` (after deferred lighting, before custom draws and transparency) and `BeforePost` (after transparency, before post effects).

Include `lit.glsl` for the frame uniforms. Buffers are bound by the name of the GLSL block: `layout(std430) buffer Particles { ... }` receives the buffer created by `setShaderBuffer("Particles", ...)`, and several passes can share one buffer by name. `readShaderBuffer(name)` copies a buffer back for tools and tests.

### Textures

Custom passes (compute jobs, draws and effects) bind textures by uniform name too:

```cpp
engine.createShaderTexture("u_trails", 1024, 1024, ShaderTextureFormat::RGBA16F);
engine.loadShaderTexture("u_lut", "resources/textures/grade_warm.png", false);
```

- `createShaderTexture(name, width, height, format)` makes a GPU texture (`RGBA8`, `RGBA16F`, `RGBA32F` or `R32F`) that a compute job writes as `layout(rgba16f) uniform image2D u_trails;` and any pass reads as `uniform sampler2D u_trails;` (bilinear, clamped). Calling it again with a different size or format recreates it.
- `loadShaderTexture(name, path, srgb)` uploads an image file (bilinear, wrapped) for sampling.
- The engine texture arrays from `engine.loadTexture` are available as `uniform sampler2DArray u_colorTextures;` (sRGB) and `u_dataTextures;` (linear), with anisotropic wrap sampling.

A pass that declares a resource nobody provided logs `No resource bound to '<name>'` and is skipped until the resource exists, instead of drawing with an unbound slot. Texture contents, like buffer contents, are not saved with the scene.

### Indirect dispatch and draws

Set `indirectBuffer` to the name of a shader buffer (and optionally `indirectOffset`, in bytes) to take the launch size from the GPU:

```cpp
ComputeDesc spawn{"Spawn", "resources/shaders/compute/spawn.comp"};
spawn.indirectBuffer = "SpawnArgs";
engine.setShaderBuffer("SpawnArgs", 16, 1, nullptr);
```

The buffer holds `uvec3 groups` for a compute job, `uint vertexCount, instanceCount, firstVertex, firstInstance` for a vertex draw and `uvec3 groups` for a mesh-shader draw. Every buffer made with `setShaderBuffer` can be used as indirect arguments, so a compute job can fill it for a later pass in the same frame. When the named buffer does not exist the pass falls back to `groups`, `vertexCount`/`instanceCount` or `meshGroups`.

### Always available, by name

| Name | Contents |
|---|---|
| `SceneData` | camera matrices (`view`, `projection`, `viewProjection` and `prevViewProjection`, the only previous-frame matrix; `projection[3][3]` is 1 for an orthographic camera), screen size, time, sun, fog and cascade data (see `scene.glsl`) |
| `LitParams` | `litParams[4]` set with `setPassParam`, plus `litTime()`, `litDeltaTime()`, `litFrame()` |
| `WorldMatrixBuffer`, `CullSphereBuffer`, `RenderableBuffer` | per-entity GPU state, indexed by entity index |
| `MaterialBuffer`, `LightBuffer`, `ExposureBuffer` | materials, the clustered light list, current exposure (`litExposure()`, `litExpose(radiance)`) |
| `u_depth` | scene depth (after the scene stages; not in draws, which use it as their depth target) |
| `u_gbuffer0`, `u_gbuffer1` | base color and occlusion; encoded normal, roughness and lighting model |
| `u_colorTextures`, `u_dataTextures` | the engine texture arrays |
| `u_hdr` | the lit HDR image as a storage image (compute jobs in the `AfterLighting` and `BeforePost` stages) |

## Custom draws

`DrawDesc` takes either `vertexPath` + `fragmentPath` or `meshPath` (+ optional `taskPath`) + `fragmentPath`, a primitive, vertex and instance counts or mesh groups, blending (`Opaque`, `Alpha`, `Additive`), depth state and an optional indirect buffer. Draws render after lighting into the HDR target and test against scene depth, so they composite with everything else and go through exposure, bloom and tonemapping. Write radiance in nits (the particle example multiplies by a few thousand); the same resources as compute jobs are available. Mesh-shader draws are silently skipped on GPUs without `VK_EXT_mesh_shader`; check `engine.meshShadersSupported()` to offer a fallback.

## Post effects

An effect file defines one function; the engine wraps it in `effects/post_template.frag`:

```glsl
#pragma lit stage ldr

vec3 effect(vec2 uv, vec3 color) {
    float edge = length(litNormal(uv) - litNormal(uv + litTexel() * vec2(1.0, 0.0)));
    return mix(color, vec3(0.0), saturate(edge * litParams[0].x));
}
```

`#pragma lit stage` chooses where the effect runs:

- `hdr` (the default): scene-referred color in the exposed HDR image, before bloom, exposure metering and tonemapping. Use it for effects that should behave like light: fog, underwater absorption, heat haze.
- `ldr`: display-referred sRGB color in `[0, 1]` after tonemapping, vignette and grain, before FXAA. Use it for outlines, sharpening, color grading, film grain and screen-space UI effects. The result is clamped to `[0, 1]`.

`#pragma lit param <index> <x> [y z w]` gives `litParams[index]` its starting value when the effect is added. Values set later with `setPassParam` or loaded from a `.litscene` replace it, and every value, including 0, is used as is.

Available in both stages: `litColor(uv)` (the stage's current color), `litDepth(uv)`, `litLinearDepth(uv)`, `litWorldPosition(uv)`, `litNormal(uv)`, `litBaseColor(uv)`, `litIsSky(uv)`, `litTexel()`, `litLdr()` (true in the `ldr` stage), plus everything from `lit.glsl` and any texture bound by name (declare `uniform sampler2D <name>;` in the effect). Effects of a stage run in the order they were added and ping-pong between two targets.

`resources/shaders/effects/` ships:

| Effect | Stage | `litParams[0]` |
|---|---|---|
| `outline` | `ldr` | |
| `sharpen` | `ldr` | `x`: amount (default 0.6) |
| `chromatic` | `ldr` | `x`: strength (default 1) |
| `grade` | `ldr` | `x`: LUT blend (default 1), `y`: grain (default 0.03); reads a 16³ strip LUT from `u_lut` |
| `underwater` | `hdr` | |

The editor loads `resources/textures/grade_warm.png` as `u_lut`, and every custom pass gets a `Toggle ...` command in the palette (Space).

## GPU timings

While GPU profiling is on (always in the editor), every enabled custom pass gets its own timestamp pair. `engine.gpuTimings()` lists the built-in passes followed by one entry per custom pass (the Look panel shows them), `engine.customPasses()` carries `PassInfo::gpuMilliseconds`, the profiling HUD prints them, and the periodic GPU log adds a `Custom` line. `ldr` effects run inside the built-in `Post` pass, so they are not added to the GPU total a second time.

## Persistence

Custom passes, their enabled state, `litParams` and indirect buffer names, the surface shaders an entity references, materials and render settings are saved in `.litscene` files (see `docs/World.md`). Buffer and texture contents are not; seed them from code after loading, the same way the pass was created.
