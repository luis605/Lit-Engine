# Overnight Plan (approved with revisions)

Hard rules from the owner
- No temporal accumulation anywhere: no TAA, no history-buffer denoising, no temporally accumulated SSR/GTAO/GI/shadows. Remove any that exist. Upscaling, if any, must be spatial only.
- Reflections: selectable modes, not SSR only. Better SSR plus world-space options (planar, parallax-corrected probes, world-space ray tracing against a scene structure), all without temporal accumulation.
- Global illumination stays; add color bleeding.
- IK built into the engine and intuitive: a spider (multi-leg procedural walker) must be set up in a few lines or one editor command.
- No commits unless the owner asks. Work stays in the tree. Changelog in docs/OvernightLog.md.

A. Foundation
1. Green build and tests after every batch.
2. Sanitizer build (ASan/UBSan) in a fresh build dir, run all tests, fix findings.
3. scripts/check.sh, .clang-format, .clang-tidy; check for stray comments and oversized files.
4. Fix stale build dirs (build, build-asan point to a wrong source path).

B. Performance (measure first)
5. Headless benchmark suite with JSON output and regression compare.
6. CPU: SoA pools, dirty-cell spatial updates, job batching for transforms/nav/audio.
7. GPU: two-pass Hi-Z, meshlet LOD, indirect compaction, packed formats, fewer G-buffer targets.
8. Startup: shader cache, parallel import, cached mesh optimisation.
9. Memory: frame arenas, counting-allocator test for zero per-frame allocation.

C. Realism at low cost (all non-temporal)
10. Lighting: area lights, sky/cloud integration, better shadows (no temporal), better GTAO (spatial only).
11. Reflections: improved SSR (hierarchical depth tracing, roughness cones, edge fade), planar reflections, parallax-corrected probes, world-space tracing option, mode selector in RenderSettings and editor.
12. GI color bleeding on top of existing GI.
13. Spatial-only upscaling option (FSR1-style) and a quality governor that hits a target frame time.

D. Intuitive
14. Command palette coverage, in-editor profiler with flame graph and GPU timings.
15. Hot reload for shaders, scenes, params, with error overlays.
16. Docs: architecture, module map, quick starts, headless CLI help.
17. Scene validator and "explain this frame" tool.

E. Features
18. IK system: chains (two-bone, FABRIK n-bone, CCD), auto rig setup from bone names, look-at, foot placement, procedural gait for N legs (spider, quadruped, biped), terrain adaptation, editor commands and a one-line spider recipe.
19. Navigation: dynamic obstacles, avoidance quality, multi-agent scaling tests.
20. Save/replay determinism checks with replay hash in tests.

F. Removals
21. Dead code and unused declarations.
22. Duplicated helpers into shared internals.
