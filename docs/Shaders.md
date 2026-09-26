# Shader API

Lit renders with a deferred PBR pipeline driven by the GPU culler. You extend it at four points, each a plain GLSL file that hot-reloads when saved (F9 forces a reload):

| Extension | File | Runs | Registered with |
|---|---|---|---|
| Surface shader | `resources/shaders/surfaces/<name>.surface` | per vertex and per pixel of every entity that uses it, in the G-buffer, shadow and transparency passes | `engine.loadShader(name)` |
| Compute job | any `.comp` | before the scene, after lighting or before post | `engine.addCompute(ComputeDesc)` |
| Custom draw | `.vert` + `.frag`, or `.mesh` (+ `.task`) + `.frag` | after lighting, into the HDR target with scene depth | `engine.addDraw(DrawDesc)` |
| Post effect | `resources/shaders/effects/<name>.effect` | after transparency, before exposure and tonemapping | `engine.addEffect(name)` |

Compiled SPIR-V is cached in `resources/cache/shaders/`, keyed by a hash of the source, every resolved `#include`, the macros and the stage, so only changed shaders compile on the next launch. Delete the folder or set `LIT_NO_SHADER_CACHE=1` to bypass it.

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

`surface` receives `s` already filled from the entity's material (base color and texture, opacity, normal map, metallic, roughness, occlusion, emissive), so a shader only changes what it cares about. `vertex` is optional and runs before the world transform, in object space.

Pragmas:

- `#pragma lit bounds <units>` grows the culling sphere by that many object-space units. Use it when `vertex` pushes geometry outside the mesh bounds, otherwise displaced parts pop when the original bounds leave the screen or pass behind an occluder.
- `#pragma lit twosided` disables back-face culling for the surface.

### Inputs

| `VertexData` | `VertexContext` | `SurfaceInput` | `Surface` (output) |
|---|---|---|---|
| `position` | `world` (mat4) | `worldPosition`, `objectPosition` | `baseColor`, `opacity` |
| `normal` | `entity`, `material` | `viewDirection`, `geometricNormal` | `normal` (world space) |
| `uv` | `time` | `uv`, `screenUv` | `metallic`, `roughness`, `occlusion` |
| | `custom0`, `custom1` | `entity`, `material`, `time` | `emissive` (nits, pre-exposure) |
| | | `custom0`, `custom1`, `frontFacing` | `tangentAngle` (anisotropy direction) |

`custom0` and `custom1` are the two free `vec4`s on `Material`, so one surface shader can serve many materials with different parameters, and the inspector edits them live.

### Helpers

- Noise: `litHash(p)`, `litNoise(p)`, `litFbm(p, octaves)`, `litVoronoi(p)` (returns the distance to the nearest cell point and a random cell id in `[0, 1)`), `litHsvToRgb(hsv)`.
- Normals: `litBumpNormal(i, N, height, strength)` perturbs a normal from a scalar height field using screen-space derivatives; `litPerturbNormal(N, p, uv, tangentNormal)` applies a tangent-space normal without precomputed tangents.
- Textures: `litSample(color, layer, i, triplanar, scale)` and `litTriplanar(color, layer, p, n, scale)` read the engine texture arrays (`color = true` for sRGB albedo, `false` for data such as normal or ORM maps). Layers come from `engine.loadTexture(path, srgb)`.

Opacity below 1 (from the material or written by `surface`) sends the entity through the order-independent transparency pass automatically; the same `surface` function runs there, lit by the forward path.

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

Include `lit.glsl` for the frame uniforms. Buffers are bound by the name of the GLSL block: `layout(std430) buffer Particles { ... }` receives the buffer created by `setShaderBuffer("Particles", ...)`, and several passes can share one buffer by name. `readShaderBuffer(name)` copies a buffer back for tools and tests.

Always available, by name:

| Name | Contents |
|---|---|
| `SceneData` | camera matrices (`view`, `projection`, `viewProjection`, previous frame matrices), screen size, time, sun, fog and cascade data (see `scene.glsl`) |
| `LitParams` | `litParams[4]` set with `setPassParam`, plus `litTime()`, `litDeltaTime()`, `litFrame()` |
| `WorldMatrixBuffer`, `CullSphereBuffer`, `RenderableBuffer` | per-entity GPU state, indexed by entity index |
| `MaterialBuffer`, `LightBuffer`, `ExposureBuffer` | materials, the clustered light list, current exposure (`litExposure()`, `litExpose(radiance)`) |
| `u_depth` | scene depth (after the scene stages) |
| `u_hdr` | the lit HDR image as a storage image (`AfterLighting` and `BeforePost` stages) |

## Custom draws

`DrawDesc` takes either `vertexPath` + `fragmentPath` or `meshPath` (+ optional `taskPath`) + `fragmentPath`, a primitive, vertex and instance counts or mesh groups, blending (`Opaque`, `Alpha`, `Additive`) and depth state. Draws render after lighting into the HDR target and test against scene depth, so they composite with everything else and go through exposure, bloom and tonemapping. Write radiance in nits (the particle example multiplies by a few thousand); the same resources as compute jobs are available. Mesh-shader draws are silently skipped on GPUs without `VK_EXT_mesh_shader`; check `engine.meshShadersSupported()` to offer a fallback.

## Post effects

An effect file defines one function; the engine wraps it in `effects/post_template.frag`:

```glsl
vec3 effect(vec2 uv, vec3 color) {
    float edge = length(litNormal(uv) - litNormal(uv + litTexel() * vec2(1.0, 0.0)));
    return mix(color, vec3(0.0), saturate(edge * litParams[0].x));
}
```

Available: `litColor(uv)` (HDR, pre-exposure), `litDepth(uv)`, `litLinearDepth(uv)`, `litWorldPosition(uv)`, `litNormal(uv)`, `litBaseColor(uv)`, `litIsSky(uv)`, `litTexel()`, plus everything from `lit.glsl`. Effects run in the order they were added and ping-pong between two HDR targets. `resources/shaders/effects/` ships `outline`, `sharpen`, `chromatic` and `underwater`; every custom pass gets a `Toggle ...` command in the editor palette (Space).

## Persistence

Custom passes, their enabled state and `litParams`, the surface shaders an entity references, materials and render settings are saved in `.litscene` files (see `docs/World.md`). Buffer contents are not; seed them from code after loading, the same way the pass was created.
