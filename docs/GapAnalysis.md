# Gap analysis

Audit of the engine against spec sections 4.1, 4.3 and 6. Status: have, partial, missing.

## 4.1 Baseline

- Have: Vulkan (via Diligent), GPU frustum plus two-pass Hi-Z culling, compute LOD, instancing, indirect multi-draw with count, optional mesh-shader path for custom draws, job system, SPIR-V disk cache, per-pass GPU timestamps, stress scene generator (100k to 8M), glTF import through Assimp.
- Partial: pipeline cache (SPIR-V only, no VkPipelineCache), D3D11/D3D12 backends still enabled on Windows, platform layer (GLFW types appear in `Engine::window()`).
- Missing: render graph, bindless descriptor indexing, multi-queue with timeline semaphores, VMA (Diligent allocator is used), macOS/MoltenVK verification, zero-allocation audit of the frame loop.

## 4.3 Modules

- platform: have GLFW window, keyboard and mouse, `InputActions`, `std::filesystem`, time service. Missing gamepad, isolated platform layer.
- core: have `Jobs`, sparse-set component pools, glm math, `Lit::Log`, `Engine.Rng` (PCG32 streams), `Engine.Json`. Missing structured log output, memory arenas.
- render: have GPU-driven scene, shadows, post, custom compute/draw/effect passes, materials, surface shaders, shader hot reload. Missing render graph, portal views (in progress elsewhere).
- world: have `World` (handles, hierarchy, layers, tags, prefabs, scripts, timers, spatial grid, raycast, `.litscene` v2, snapshot/restore, state dump). Missing cell graph, portals, streaming, world deltas, per-cell parameters.
- physics: have lite solver (sphere, box, plane, sleeping, layers, raycast). Missing backend ADR, portal-aware layer, character controller, concave dynamic bodies.
- observation, params: have `Engine.Params` (smoothed named floats, JSON curves, hot reload, shader block). Missing observation queries (object-ID readback, Attended/Seen/Unobserved).
- anim: have transform clips and animators, skeletal blending, two-bone/foot/look-at IK, ARKit-52 blendshape clips, eye system (`Engine.Rig`, `Engine.Face`). Missing skinning render path, hand IK pose library.
- ai: have behavior graphs (utility, tree, fsm), blackboard, decision trace, tiled navmesh with obstacle rebuild, off-mesh links, agent steering (`Engine.AI`, `Engine.Nav`). Missing Recast tile cache, overlay panel.
- audio: have mixer, buses, ducking, spatialization, voice scheduler, BVH propagation with ray budget and hop hook, Null/WAV backends (`Engine.Audio`, `docs/adr/Audio.md`). Missing device backend, adaptive music.
- narrative: missing.
- save: have `.litscene` save/load and editor autosave. Missing versioned atomic save slots, delta saves, RNG seed persistence.
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
