# Render graph

The frame is recorded as a render graph (`Engine.Render.graph`, `src/Engine/Render/RenderGraph.cppm/.cpp`) and then executed. Passes declare the textures and buffers they read and write; the graph derives the dependencies, culls passes whose results nobody uses, places transient resources in a pool that reuses physical resources, moves every declared resource into the state its pass declared before the pass runs, and brackets passes with the per-pass GPU timestamp scopes. The graph is the default; the previous immediate path stays behind a switch for A/B comparisons (see Verification).

- Setting `renderGraph` (default `1`), palette `render.graph`, `--set renderGraph=0` in LitShot.
- `LIT_RENDER_GRAPH=0|1` overrides the setting for the whole process (A/B runs without touching settings files).
- No temporal accumulation: the graph holds no history; per-view Hi-Z and visibility bits are the only cross-frame state and stay imported resources owned by the portal history (as before).

## Module

`Engine.Render.graph` does not know Diligent. A `rg::Backend` creates resources, issues barriers and records timestamps, so the graph logic is unit tested without a GPU (`tests/RenderGraphTests.cpp`, ctest `RenderGraphTests`).

- Resources: `importTexture` / `importBuffer` (external objects, by name; a resolver callback variant looks the object up when a pass runs, for resources another system may recreate mid-frame such as the shadow map after a resolution change), `createTexture` / `createBuffer` (transients, described by `rg::TextureDesc` / `rg::BufferDesc` with backend format and bind flags).
- Passes: `addPass(name, setup, execute)`. `setup` runs immediately and declares accesses with `PassBuilder::read / write (handle, rg::Usage)`, `access(rg::ExternalAccess)` (resources of another module imported by name, used for Lighting's internal textures), `sideEffect()` and `timing(scope)`. `execute` runs later, in order, and gets a `PassContext` to resolve handles to native objects.
- `rg::Usage` maps to a Diligent resource state (`ShaderResource`, `UnorderedAccess`, `RenderTarget`, `DepthWrite`, `DepthRead`, `CopySource`, `CopyDest`, `IndirectArgument`, `VertexBuffer`, `IndexBuffer`, `ConstantBuffer`). `None` declares the dependency only, for resources a pass uses in several states (mip-by-mip pyramids, sampled-then-written targets).
- `beginView(label)` / `endView()` group passes per portal view (dump clusters); `rg::viewResourceName(key, name)` builds view-keyed resource names (`view<key>/color`).

### Compile

1. Dependencies from declaration order: a read depends on the last writer (RAW), a write on the last writer (WAW) and on every reader since (WAR).
2. Culling: passes with `sideEffect()` or a write to an imported resource are roots; producers of every version a live pass reads (or partially overwrites) are live; everything else is culled and allocates nothing.
3. Order: Kahn topological sort of the live passes; ties go to the earlier declaration, so a valid declaration order is kept exactly (the renderer relies on this: CPU-side state such as cascade fitting and uniform uploads happens inside `execute` in the same order as before).
4. Lifetimes: first and last position of every transient in the execution order.

### Execute

- Transient placement: at its first use a transient takes the lowest free pool entry with an identical description, else a new one is created; after its last use the entry is free again for later transients of the frame. Entries survive frames (same frame structure gives the same placement, so cached bindings stay valid) and are destroyed after 8 frames without use. Diligent has no placed resources on this path, so aliasing is reuse of whole resources of the same size and format, not memory aliasing of different formats.
- States: before each pass the backend transitions every declared resource to its declared state (`TransitionResourceStates`, tracked old state). Already-correct states are skipped (UAV keeps its write-after-write barrier). A texture that leaves an attachment state is unbound from the framebuffer first. A declaration that does not match the resource's bind flags is skipped and logged once. The pass code still calls Diligent with `RESOURCE_STATE_TRANSITION_MODE_TRANSITION`, so for declared resources those calls find the state already set and do nothing; undeclared or `None` resources keep the old behaviour.
- Timing: consecutive passes with the same scope share one timestamp pair; a pass without a scope closes the open one. The scopes are the `GpuPass` ids, so `Engine::gpuTimings()` keeps the names of [GpuTimings.md](GpuTimings.md) (`Upload`, `Transform`, `Sky+IBL`, `Shadows`, `Cull`, `CmdGen`, `GBuffer`, `Hi-Z`, `Retest Cull`, `Retest CmdGen`, `Retest GBuffer`, `Trans Cull`, `Trans CmdGen`, `GTAO`, `SSR`, `Lighting`, `Trans Draw`, `Post`, `UI`, `Instance Cull`, `Instance Retest Cull` ([Instancing.md](Instancing.md))). Portal views stay untimed per pass (they have their own per-view query, `Engine::portalStats()`), as before.
- `LIT_GPU_LABELS=1` wraps every pass in a debug group (RenderDoc, Nsight).

### Dump

- `LIT_RENDER_GRAPH_DUMP=<prefix>` writes `<prefix>.txt` and `<prefix>.dot` on frame `LIT_RENDER_GRAPH_DUMP_FRAME` (default 10).
- Palette `render.graphDump` (Editor) or `Engine::requestRenderGraphDump(path)` dumps the next frame to `render_graph.txt/.dot`; `Engine::renderGraphInfo()` returns the statistics of the last frame.
- Text: header with pass, culled, resource, transient, physical and barrier counts; every pass in declaration order with its execution position, view, timing scope and accesses (`read sceneDepth (ShaderResource) <- GBuffer`); every resource with its description, lifetime and physical entry. Graphviz: passes as boxes (clustered per portal view), textures as ellipses, buffers as hexagons. `dot -Tsvg render_graph.dot -o render_graph.svg`.

## Frame (RendererGraph.cpp, RendererPortalViews.cpp)

Every pass wraps the same renderer / lighting call as the immediate path (`RendererFrame.cpp`), in the same order:

| Passes | Timing scope | Declared (main resources) |
|---|---|---|
| Upload, Transforms, Skinning | Upload, Transform | object buffers, vertex buffer (skinning compute rewrites dynamic meshes) |
| Frame Setup, Custom Before Scene | - | scene uniforms |
| Sky+IBL | Sky+IBL | atmosphere LUTs, environment, irradiance, light clusters |
| Portal Root Setup, Portal Save Main Bits, per view (below), Portal Restore Main | - | visibility bits, scene uniforms |
| Cascade Cull Upload, Cull, CmdGen + Meshlet Cull, GBuffer + Meshlet GBuffer, Hi-Z, Retest Cull, Retest CmdGen + Meshlet Retest Cull, Retest GBuffer + Meshlet Retest GBuffer | Cull ... Retest GBuffer | previous / current Hi-Z (SRV), G-buffer and HDR (RT), depth (DepthWrite), per-phase draw buffers |
| Portal Stats, Observation | - | side effects |
| Virtual Shadows, Local Shadows, Sun Cascades | Shadows | depth (SRV, page requests), VSM atlas, local shadow map and cascades (DepthWrite) |
| Trans Cull, Trans CmdGen + Meshlet Trans Cull | Trans Cull, Trans CmdGen | |
| Portal Resolve | - | children's depth + G-buffer (SRV), resolve scratch (transient) |
| GTAO, Lighting, Portal Composite Color, Reflections | GTAO, Lighting, SSR | depth, G-buffer (SRV), AO chain (UAV), HDR (UAV), shadow maps and VSM atlas (SRV), children's colour (SRV) |
| Portal Save Effects Depth, Portal Composite | - | effects depth (transient) |
| Custom After Lighting, Trans Draw, Portal Restore Effects Depth, Custom Before Post | Trans Draw | OIT targets (RT), depth |
| Post, Bind Back Buffer, UI, Frame End | Post, UI | bloom, god rays, LDR (RT), exposure |

The meshlet (cluster LOD) culling and draws, the skinning compute, VSM and the sun cascades are passes of their own (the cluster hierarchy traversal runs inside the Meshlet Cull passes; its node queue and counters are internal to that pass, [ClusterLod.md](ClusterLod.md)) (before they were tails of `buildVisibilityDraws`, `drawSurfaces` and `drawShadows`; those functions keep the old behaviour for the immediate path).

### Portal views

Each view of the `PortalFrame` adds its passes into the same frame graph inside `beginView(label)`, children first (same recursion as `renderPortalNode`): Portal View Setup (history bits in, view uniforms, cascade refit, light culling), the geometry passes against the view's own Hi-Z history (`view<key>/hiz0|1`, imported), shadows, transparent cull, Portal Mask, Portal Resolve, Portal Capture, the shade passes, Portal Transparency, Portal Copy Out, Portal View End (bits out, timing).

The view outputs are view-keyed transients: `view<key>/color` (HDR copy), `view<key>/depth`, `view<key>/gbuffer0..2`, plus `resolveScratch` / `maskScratch`. They live from the view's own passes to the parent's resolve and composite, so the pool reuses them across sibling subtrees and across the main view's effects depth. portal_torture at 1280x720: 30 transients in 11 physical textures (119.5 MB requested, 45.7 MB allocated); before, every view slot kept its own colour, depth and three G-buffer copies alive permanently. With one graph for the frame (rather than one graph instance per view) lifetimes can cross views, which is what makes this reuse possible.

## Pipeline cache

`VkPipelineCache` persistence through Diligent's `IPipelineStateCache` (`PsoCache.hpp`, `RendererInit.cpp`): created at start-up from `<shader cache dir>/pipelines_vk.bin` (`.litcache/shaders`, `LIT_SHADER_CACHE_DIR`), passed as `pPSOCache` to every PSO creation (renderer, lighting, custom passes, UI, ImGui), written back atomically (temp file + rename) at shutdown. Diligent checks the header (vendor, device, `pipelineCacheUUID`) and starts empty on a mismatch, so a cache from another GPU or driver is ignored. `LIT_NO_PIPELINE_CACHE=1` disables it. This complements the SPIR-V disk cache (shader compilation) with driver pipeline compilation.

## Bindless

Texture table for surface materials (the lighting compute, buffers, shadow maps and probe cubes are not bindless).

- Switch: `LIT_BINDLESS_TEXTURES=1|0` (unset: `kBindlessTexturesDefault` in `GpuHelpers.hpp`, on). The table is used only when the device was created with Diligent's `BindlessResources` and `ShaderResourceRuntimeArrays` (the device requests them whenever the switch is not `0`); otherwise the legacy 16-layer arrays are used and a warning is logged. `LIT_BINDLESS=1` still only forces the feature request and the start-up `Bindless audit:` line (`VulkanAudit.cpp`).
- Table (`Lighting`): 1024 slots (`kBindlessTextureCapacity`), one native-size Texture2D with a full mip chain per slot (sRGB or linear chosen at load), a lowest-free-slot allocator with a free list (`TextureSlots.hpp`, unit tested), a placeholder texture for empty slots. `Engine::loadTexture` returns the slot, `Engine::releaseTexture` frees it (the texture is destroyed once no binding references it). `Material::baseColorTexture` etc. and `MaterialGpu.textures` store slot indices (layout unchanged). Terrain packs layer ids 6 bits each (63 = none), so terrain textures need slots below 63.
- Shaders: with `LIT_BINDLESS` (a pixel macro of the G-buffer and forward surface variants) `surface.frag` declares `uniform sampler2D u_textures[]` and `litTexture(color, slot, uv)` samples `u_textures[nonuniformEXT(slot)]`; legacy builds sample the arrays. One immutable anisotropic wrap sampler is shared (combined image samplers).
- Pipelines: Diligent cannot declare an unsized array in an implicit layout and a PSO cannot mix an implicit layout with signatures, so the bindless surface PSOs use one explicit `IPipelineResourceSignature` each, built from the shaders' reflection by `litgpu::makeBindlessSignature` (same rules as the implicit layout) plus `u_textures` (`PIPELINE_RESOURCE_FLAG_RUNTIME_ARRAY`, 1024 descriptors). Their SRBs are created through the signature. Shadow variants are unchanged.
- Updates: every surface SRB is filled with all 1024 entries (placeholder for empty slots) when created. A load or release bumps a table epoch and a per-slot epoch; `Renderer::syncTextureTable()` (start of each frame) writes only the changed slots into the current frame slot's SRBs, whose fence has just been waited on, so a load costs one descriptor write per SRB and never rebuilds bindings or idles the GPU. The vendored Diligent Vulkan backend does not create `UPDATE_AFTER_BIND` or `PARTIALLY_BOUND` layouts (no such flags anywhere in it), so those Vulkan flags are not used: correctness relies on the frame-slot fence and the full prefill instead.
- Limits: legacy mode resamples textures to 1024x1024 (nearest) and caps at 16 layers per array; the table keeps native sizes, so output only matches legacy for 1024x1024 sources (the `textures` scene uses such). The table has 1024 slots, and each draw commit walks all 1024 entries of each surface SRB (not measured).
- Verification: `scripts/bindless_check.sh` (legacy a, legacy b, bindless per scene), ctest `BindlessTextureParity` (scene `textures`, requires identical pixels within legacy noise, table active, no Diligent errors) and `TextureTableTests` (allocator). See the log below.

## Verification

`scripts/render_graph_check.sh [outdir]` renders every scene three times with `LitShot --deterministic` (fixed 1/60 s renderer and simulation clock, `LIT_FIXED_DT`; fixed-size non-resizable window, `LIT_FIXED_WINDOW`, so Hyprland and other tiling window managers float it at the requested size instead of tiling it to an arbitrary size): graph off twice (run-to-run noise of the GPU itself) and graph on. Results are in the Verification log below.

### Verification log (2026-11-23, GTX 1650, driver 595.91.07, 1280x720, 30 frames)

`scripts/render_graph_check.sh`; differing pixels (any channel) against the first graph-off run:

| Scene | off vs off (noise) | on vs off | Notes |
|---|---|---|---|
| showcase | 14 px, max 5 | 6 px, max 2 | |
| vista | 0 | 0 | bit-identical |
| gallery | 58 px, max 2 | 33 px, max 1 | |
| portal_torture | 714178 px, max 25 | 714179 px, max 25 | an earlier pair of runs differed by 3 px: run-to-run variation of the scene itself |
| vsm_mixed | 370604 px, max 80 | 442443 px, max 163 | 3 more runs per mode: off/off up to 435785 px max 149, on/on up to 447491 px max 133, on/off 421149 to 442443 px; indistinguishable |
| skinning | 29 px, max 2 | 33 px, max 3 | |
| terrain | 0 | 66196 px, max 102 | 3 more runs: off/off 84500 px max 102, on/on 63969 px max 102, on/on 3 px; distant terrain tiles differ per run in both modes (streaming timing) |
| dense | 73564 px, max 17 | 66842 px, max 17 | |
| light_stress | 125920 px, max 19 | 138704 px, max 19 | |
| largeworld | 0 | 0 | bit-identical |

Every scene stays within the run-to-run noise of the immediate path; the two scenes without GPU-side nondeterminism (vista, largeworld) are bit-identical. No `[ERROR]`, `[WARN]`, validation or render-graph bind-flag messages in any graph-on log. `ctest` in build-release: 41/41 (including `RenderGraphTests`). Several scenes (portal_torture, vsm_mixed, terrain, dense, light_stress) are not reproducible between runs even with `--deterministic`; that is independent of the graph and makes a stricter pixel check impossible there.

GPU timings (sum of the GpuPass scopes, ms, same runs): see [GpuTimings.md](GpuTimings.md#render-graph-on-vs-off). Graph on is within the off-vs-off spread in every scene.

Pipeline cache: the first run after building writes `.litcache/shaders/pipelines_vk.bin` (3.0 MB for the full pipeline set); later runs log `Pipeline cache: .litcache/shaders/pipelines_vk.bin (3033426 bytes loaded)`. Start-up `scene pipelines` phase: 83.5 ms (cache disabled, `LIT_NO_PIPELINE_CACHE=1`, first run) vs 43.5 ms (cache), but a second run with the cache disabled also took 46.7 ms: the NVIDIA driver keeps its own on-disk pipeline cache, so on this machine the gain of the engine cache is not separable from the driver's. It matters on drivers without one and after driver cache eviction.

Bindless audit (start-up log): adapter `BindlessResources=enabled`, `ShaderResourceRuntimeArrays=optional`; the device as created reports `BindlessResources=enabled`, `ShaderResourceRuntimeArrays=disabled` (without `LIT_BINDLESS`); Vulkan 1.4.329: `runtimeDescriptorArray`, `shaderSampledImageArrayNonUniformIndexing`, `descriptorBindingPartiallyBound`, `descriptorBindingVariableDescriptorCount`, `descriptorBindingSampledImageUpdateAfterBind` all supported; `maxPerStageDescriptorSampledImages` 1048576, `maxPerStageDescriptorUpdateAfterBindSampledImages` 1048576, `maxDescriptorSetUpdateAfterBindSampledImages` 1048576. The device can host a bindless texture table far beyond the engine's needs; the remaining work is the steps above.

### Bindless texture table (2026-11-24, GTX 1650, 1280x720, 30 frames, `scripts/bindless_check.sh`)

Differing pixels against legacy run a:

| Scene | legacy a vs b (noise) | bindless vs legacy | Diligent errors |
|---|---|---|---|
| textures | 14 px, max 2 | 4 px, max 1 | 0 |
| showcase | 5 px, max 2 | 8 px, max 2 | 0 |
| gallery | 26 px, max 1 | 26 px, max 5 | 0 |
| terrain | 29902 px, max 51 | 46523 px, max 51 | 0 |
| skinning | 35 px, max 2 | 37 px, max 2 | 0 |

Terrain is not reproducible even legacy against legacy; bindless stays at the same maximum. `glslangValidator` accepts `surface.frag` with and without `-DLIT_BINDLESS` and `-DTRANSPARENT`.
