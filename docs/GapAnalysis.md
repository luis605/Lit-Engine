# Gap analysis

Audit of the engine against spec sections 4.1, 4.3 and 6. Status: have, partial, missing.

## 4.1 Baseline

- Have: Vulkan (via Diligent), GPU frustum plus two-pass Hi-Z culling, compute LOD, cluster LOD for dense geometry (import-time meshlets + simplification DAG in `.litmesh`, GPU per-instance BVH traversal over the DAG (LOD bound, frustum and two-pass Hi-Z per node), then per-cluster DAG cut, frustum, cone and Hi-Z culling into a compute-expanded index buffer; shadow cascades and local shadow maps pick the cut from shadow-texel error; `Engine.meshlet`, `RendererMeshlets.cpp`, `meshlet_*.comp`, settings `clusterLod`/`clusterErrorPixels`/`clusterHierarchy`/`clusterShadowTexelLod`/`clusterShadowErrorTexels`, LitShot `--testscene dense --stats`, [ClusterLod.md](ClusterLod.md)), instancing, indirect multi-draw with count, optional mesh-shader path for custom draws, job system, SPIR-V disk cache, per-pass GPU timestamps, stress scene generator (100k to 8M), glTF import through Assimp.
- Have: render graph (`Engine.Render.graph`: declared resources, dependency order, culling, transient reuse across lifetimes and frames, per-pass states and timing scopes, text/dot dump; the whole frame including portal views, VSM, meshlet passes and skinning runs on it, `renderGraph=0` keeps the immediate path; [RenderGraph.md](RenderGraph.md)), pipeline cache (SPIR-V disk cache plus a persisted `VkPipelineCache` via Diligent `IPipelineStateCache`).
- Partial: bindless (device audit logged at start-up, `LIT_BINDLESS=1` enables Diligent bindless features; the surface path still uses texture arrays, steps in RenderGraph.md), render graph scope (passes keep Diligent `TRANSITION` calls as a fallback for undeclared resources; one queue; reuse needs identical descriptions, no memory aliasing between formats), D3D11/D3D12 backends still enabled on Windows, platform layer (GLFW types appear in `Engine::window()`).
- Missing: multi-queue with timeline semaphores, VMA (Diligent allocator is used), macOS/MoltenVK verification, zero-allocation audit of the frame loop.

## 4.3 Modules

- platform: have GLFW window, keyboard and mouse, `InputActions`, `std::filesystem`, time service. Missing gamepad, isolated platform layer.
- core: have `Jobs`, sparse-set component pools, glm math, `Lit::Log`, `Engine.Rng` (PCG32 streams), `Engine.Json`. Missing structured log output, memory arenas.
- render: have GPU-driven scene, shadows, post, custom compute/draw/effect passes, materials, surface shaders, shader hot reload. Render graph and portal views have landed (RenderGraph.md, Portals.md).
- world: have `World` (handles, hierarchy, layers, tags, prefabs, scripts, timers, spatial grid, raycast, `.litscene` v2, snapshot/restore, state dump). Cell graph, portals, per-cell parameters, streaming and world deltas have landed (`docs/Portals.md`, `docs/Streaming.md`).
- physics: have lite solver (sphere, box, plane, sleeping, layers, raycast). Missing backend ADR, portal-aware layer, character controller, concave dynamic bodies.
- observation, params: have `Engine.Params` (smoothed named floats, JSON curves, hot reload, shader block). Observation queries have landed (GPU ID grid readback plus CPU fallback).
- anim: have transform clips and animators, skeletal blending, two-bone/foot/look-at IK, ARKit-52 blendshape clips, eye system (`Engine.Rig`, `Engine.Face`). GPU skinning, morph targets and procedural deform have landed (`docs/Skinning.md`). Missing hand IK pose library.
- ai: have behavior graphs (utility, tree, fsm), blackboard, decision trace, tiled navmesh with obstacle rebuild, off-mesh links, agent steering (`Engine.AI`, `Engine.Nav`). Missing Recast tile cache, overlay panel.
- audio: have mixer, buses, ducking, spatialization, voice scheduler, BVH propagation with ray budget and hop hook, Null/WAV backends (`Engine.Audio`, `docs/adr/Audio.md`). Missing device backend, adaptive music.
- narrative: missing.
- save: have `.litscene` save/load and editor autosave. Versioned atomic slots, delta saves and RNG persistence have landed (`Engine.Save`, `Engine.Delta`).
- tools: have headless mode, fixed-step determinism, input record/replay, autopilot (screenshots, frame capture, dump, perf), state dump, perf CSV, threshold script, parameter bus. Missing test scene library.

## 6 Requirements

- World and portals: W-1 to W-7 missing.
- Physics: PH-1 to PH-5 missing except the lite solver (partial for PH-1 only as a placeholder).
- Attention: AT-1 missing, AT-2 and AT-3 are game layer, AT-4 have (`Engine.Params`, overlay graph missing), AT-5 and AT-6 missing.
- Characters: CH-1 and CH-2 have (headless), CH-3 to CH-5 missing.
- AI: AI-1 to AI-3 have (headless), AI-4 and AI-5 game layer.
- Audio: AU-1 (no device backend), AU-2, AU-4 have; AU-3 via bus param binding; AU-5 missing.
- Narrative, save, input: NA-1 to NA-3 missing (save partial), NA-4 game layer, IN-1 partial (fly camera, no gamepad, no third person).
- Tooling: TL-1 to TL-5 have, TL-6 partial (shaders and params reload, other data files not yet), TL-7 partial (editor HUD and profiler, no params or portal panels), TL-8 have (CSV plus `scripts/perf_check.py`), TL-9 missing.
