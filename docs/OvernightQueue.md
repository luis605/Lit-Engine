# Overnight Queue (lead maintains)

Status: TODO / RUNNING / DONE / BLOCKED

Wave 3 (started)
- W3-1 RUNNING sanitizer build + check.sh + clang configs (A2-A4)
- W3-2 RUNNING benchmark suite (B5)
- W3-3 RUNNING reflections modes + temporal removal audit (C11, no-temporal rule)
- W3-4 RUNNING GI color bleeding (C12)
- W3-5 RUNNING IK system + spider (E18)
- W3-6 RUNNING docs + dead-code audit (D16, F21-22)

- W3-4 DONE (needs build+test): colour bleed in gtao.comp
- W4-0 TODO: colour bleed + existing SSGI read last frame's lit pyramid via reprojection (history read). Owner says no temporal at all: switch to a current-frame lit source (e.g. lit after a depth-prepass-only ambient estimate, or prior-pass low-res lit of this frame) and remove prevViewProjection reprojection
- LOCK RULE: sanitizer build must use its own lock, never hold /tmp/lit-build.lock for from-scratch builds

Check-in 2026-11-04: usage limit hit earlier; W3-1, W3-2, W3-3 (also owns W4-0 temporal removal), W3-5, W3-6 resumed and RUNNING. Build broken only by CMake entry for missing Render/LightingReflections.cpp (W3-3 fixing). After they report: build, ctest, then launch wave 4.

- W3-3 DONE: reflections modes (Off/SSR/SSRProbes/SSRPlanar/WorldSpace), temporal reads removed (SSR, GI, bleed, portal history); needs GPU eyeball. Known gaps: planar is screen-space mirrored gather (no mirrored-camera render), voxel hits unshadowed
- W3-6 DONE: docs, --help, readTextFile consolidation
- W3-2 bench, W3-1 sanitizers, W3-5 IK still RUNNING (IK must fix IKTests + WorldTests system order)

Wave 4 launched:
- W4-1 RUNNING lighting/shadow/GTAO quality + spatial upscaler + governor (owns Render Lighting/Renderer/shaders)
- W4-2 RUNNING editor profiler, hot reload, scene validator, explain-frame (owns Editor + Tools/SceneValidator)
- W4-3 RUNNING nav dynamic obstacles/avoidance/scaling (owns Nav)
- W4-4 RUNNING determinism: world hash, replay verify (owns Replay/Save/State/Params)
Still TODO after bench baseline: CPU perf B6/B9, GPU perf B7, startup B8 (all need W3-2 numbers; B7 must wait for W4-1 to finish Render)

- W3-2 DONE: LitBench (27 benchmarks) + docs/bench-baseline.json (re-record on a quiet machine)
- W4-5 RUNNING world/portal query perf (owns World, Portal)
- W4-6 RUNNING JSON/serialization perf (owns Tools/Json)
- Nav hotspots forwarded to W4-3
Remaining TODO: GPU perf B7 (after W4-1), startup B8 shader cache/parallel import, audio/propagation alloc trims, AI exec allocs, Editor perf, bench re-baseline on quiet machine, final full build+ctest+ASan pass

- W3-5 DONE: IK (solvers, IkChain, Walker, makeSpider, editor ik.* commands), docs/IK.md
- W4-6 DONE: Json dump 205->~90 ms (17 allocs), parse 405->~240 ms; golden tests
- W4-7 RUNNING: AI/Audio/Animation alloc + perf (owns AI, Audio, Animation)
- TODO: Delta capture EntityState copies (Save, after W4-4), GPU perf B7 (after W4-1), startup B8, bench re-baseline, final ASan pass

- W4-1 DONE: shadows/GTAO/area lights/upscaler/governor (QualityTests); gap: debug lines not depth tested when renderScale<1
- W4-8 RUNNING: GPU perf + headless safety + renderScale debug depth (owns Render, shaders)

- W4-4 DONE: WorldHash, replay verify (--hash-interval, --verify-replay), DeterminismTests, docs/Determinism.md; uninitialised-memory scan NOT done (TODO: ASan/MSan-style pass via build-san)

- W4-3 DONE: nav (dynamic obstacles, sliced queries, NavCrowd, caches); step_agents 13 ms->0.19 ms 0 allocs, rebuild 46->11 ms. TODO later: path search still ~4 ms/path (hierarchical/JPS-like), jam at 0.7 m counterflow lanes
- WorldTests raycast distance failure forwarded to W4-5

- W4-2 DONE: profiler panel, hot reload, SceneValidator + explainFrame, palette cmds; editor never launched (unverified visually)
- W3-1 DONE: build-san clean (0 ASan/UBSan reports), scripts/check.sh, .clang-tidy, docs/Build.md. Stale build/ (33 GB) and build-asan/ (4.6 GB) left for owner to delete.
- W5-1 RUNNING: split big tests + UI/Manager, coverage gaps
- W4-5 DONE (queries): raycast 72-108->4.5 ms, overlapSphere 0 allocs, frustum 88->31 ms, portal queries faster; new out-param overloads
- W5-2 RUNNING: World core storage perf (spawn/destroy/iterate/propagate/refresh)
- CHECK: NavTests showed 2 failures in W4-5's full ctest run (verify at next build)
- W4-8 DONE (partial): overlay depth fix for renderScale<1 (depth_upscale.frag), HeadlessRenderTests (35 checks); GPU waste review not done (no GPU to measure); build clean, 22/22 ctest
- W5-3 RUNNING: real/software GPU validation + screenshot tool (owns Render + shaders)
- W4-7 DONE: AI/anim/audio steady-state 0 allocs (utility tick 0.49->0.32 ms, skeletal 0.37->0.31, audio prop 0.238->0.203); new bench ai/anim cases
- W5-4 RUNNING: asset import parallel + mesh cache (Asset, Geometry)
- W5-5 RUNNING: Save/Delta/Streaming perf
- W5-6 RUNNING: Nav hierarchical pathfinding + counterflow fix
- KNOWN BUILD ISSUES in progress by owners: EdgeTests compile error (tests/EdgeHistoryProfilerTests.cpp:164, W5-1), LitShot build error (src/Shot/main.cpp:91, W5-3)
- CHECK-IN: full build clean, ctest 24/24 pass (NavTests, EdgeTests, HeadlessRenderTests included). Running: W5-2, W5-3, W5-4, W5-5, W5-6
- W5-1 DONE: tests split <400 lines each, UI/Manager 501->180, EdgeTests 172 checks (no source bugs found)
- W5-2 DONE (partial): position iterate 5.2->1.46 ms (inline valid/getPosition + compact m_position), names/tags TextTable, spawn 1M 92->75 ms; skipped parallel/dirty-depth propagation (determinism risk)
- W5-7 RUNNING: cross-review/fuzz of tonight's new code (IK, validator, hash, Json SAX, raycast/queries, editor tools)
- TODO: shader/pipeline cache (Render, after W5-3), bench re-baseline on quiet machine (after everything idle), GPU waste review items once a GPU path exists, final summary
- W5-4 DONE: asset parallel import + .litcache mesh cache (warm 15-20 ms vs 560-590 ms cold; 8 OBJs parallel warm ~9 ms). NOT WIRED: Engine::loadMesh (EngineScene.cpp) must call AssetManager::loadProcessed, and Renderer::uploadMesh must accept cached ProcessedMesh LODs instead of recomputing optimized()/LODs -> TODO W6-1 after W5-3 frees Render
- W5-5 DONE (partial): delta capture+apply 9.1->2.2 ms 0 allocs, delta JSON roundtrip 24->9 ms, async autosave (saveAsync); save capture/apply unchanged -> W6-2
- W6-2 RUNNING: World snapshot/restore/capture perf + direct scene writer (owns WorldScene*.cpp)
- W5-6 DONE: nav path search find_path_x100 424->35.5 ms (mostly unreachable-goal probe; reachable paths gained less), ORCA avoidance 0.7 m lanes 39/40 (was 22/40), 5k agents 14->5.1 ms/step, ctest 25/25. NOTE: no hierarchical graph yet; typical reachable-path cost still to measure (TODO W7: bench with only reachable goals)
- W5-7 DONE: cross-review found+fixed 17 bugs (Json nesting stack overflow, NUL, IK/walker NaN, unbounded iterations, spatial key aliasing, raycast range/NaN, editor profiler left on); ReviewTests 2621 checks; ctest 25/25
- W6-2 DONE (partial): save capture 11.5->10.0 ms, apply 11.0->7.5 ms, allocs ~halved; bulk insert/hierarchy fixup not done
- RUNNING: W5-3 GPU validation; W7-1 review round2 (Nav/Asset/Save/Streaming/Geometry); W7-2 review round2 (AI/Audio/Anim/Portal/Physics/Storylet/Observation/State/Params); W7-3 raycast fallback perf for huge scenes; W7-4 docs refresh + changelog
- TODO next: wire asset cache into Engine::loadMesh + Renderer::uploadMesh (after W5-3), shader/pipeline cache (after W5-3), nav benchmark with reachable goals only, bench re-baseline on quiet machine (after all idle), final summary
- W5-3 DONE: REAL GPU AVAILABLE (GTX 1650, Vulkan 1.4). LitShot + scripts/render_check.sh; 56 renders (4 scenes x 14 variants) clean, no shader errors; frame 13-19.5 ms default, 6-7.4 ms at renderScale 0.5. Findings: WS reflection hard edge, SSR 3.8-7 ms and mis-attributed when disabled, interiors veiled by sky ambient/haze, default light intensity 1 invisible (photometric)
- RUNNING (wave 8, GPU-verified): W8-1 reflection visual fixes + SSR perf; W8-2 GPU pass perf; W8-3 wire mesh cache; W8-4 shader/pipeline cache. Plus W7-1..W7-4.
- TODO next: interior veiling (sky ambient/haze in enclosed rooms), light intensity UX (default point light visible, units doc/slider), window size request, visual realism review round (look at PNGs), GI colour bleed visual check, upscaler visual check
- 2026-11-04 08:20 WET: 5-hour API limit killed all 8 agents (reset 08:10). Tree verified: build clean, ctest 25/25. All 8 RESUMED via SendMessage: W7-1, W7-2, W7-3, W7-4, W8-1, W8-2, W8-3, W8-4. If limit hits again, resume with SendMessage after the stated reset time.
- W7-4 DONE: docs/Changelog-2026-11.md, FeatureGuide.md, Architecture/QuickStart/HeadlessCli/README refreshed (verified vs code). TODO after W7-1..3 log: append their results to the changelog. TODO: editor UI (Inspector) for ReflectionProbe + PlanarReflection components
- W7-2 DONE: Review3Tests 112 checks, ~25 bugs fixed (AI repeat cast/NaN/reload OOB, audio WAV+NaN, anim bad parents/short poses OOB, portal caps/escaping, state atomic fromJson, storylet reentrancy, observation dangling ptr, params NaN). Not covered by ASan yet.
- CHECK (in-flight owners): WorldTests raycast distance 300 failures (W7-3 raycast rewrite in progress?), Review2Tests Nav walkableCount()==0 (W7-1 in progress)
- TODO W9-1: run build-san over all tests incl. Review/Review2/Review3 (own lock /tmp/lit-build-san.lock), fix findings; also ASan run of LitShot scenes if possible
- W7-1 DONE: Review2Tests 603 checks, ~25 bugs (nav config/NaN/cellOf UB, crowd wrong-agent repath, bvh OOB, asset cache OOM, save forged counts/async throw/tmp collisions, streaming worker exceptions). BUILD currently fails at configure: src/Editor/CMakeLists.txt lists missing Inspector/InspectorReflection.cpp (W9-2 will create it); re-verify full build+ctest when W9-2 reports
- RUNNING: W7-3, W8-1..4, W9-1 (sanitizers), W9-2 (editor UI/quality presets)
- TODO: atomic SaveSystem::load (all-or-nothing, rollback on partial failure), NavMesh thread-safety doc/ensureLinks, StreamingSystem::prime spin bound
- CHECK-IN 2: full build clean (Editor InspectorReflection exists now), ctest all pass except StreamingTests which flaked once under machine load (passes 3/3 alone; wall-clock hitch/update thresholds at tests/StreamingTests* lines ~140-142). TODO: make streaming timing assertions load-robust (simulated clock or counters, not wall-clock)
- 7 agents RUNNING: W8-1, W8-2, W8-3, W8-4, W7-3, W9-1, W9-2
- W7-3 DONE: raycast miss 451/2222 ms -> 0.05 ms per 1000 (clip to cell bounds; easy case), double-precision ray-sphere fix; WorldSpatialFarTests; 29/29 ctest. TODO W10: hierarchical/two-level grid for in-bounds long rays (up to 1M occupied cells scan), in-bounds baseline numbers sparse 21-30 ms/1000
- W9-2 DONE: Look > Quality panel (Low/Med/High/Ultra presets), quality.* palette cmds, view.reflectionVolumes, inspectors for ReflectionProbe/PlanarReflection/IkChain/Walker, light unit labels + presets, viewport probe/plane overlay, EditorQualityTests 73 checks; ctest 29/29; UI not clicked through. TODO (Render, after W8-1): ReflectionProbe resolution field + explicit bake API + inspector 'bake now'
- RUNNING: W8-1, W8-2, W8-3, W8-4, W9-1
- W9-1 DONE: ASan/UBSan/LSan clean over all 28 tests (0 reports); valgrind origin-tracking clean on Determinism/Edge/IK/Validator/Review3/Portal; StreamingTests only fails on wall-clock limit under ASan/load. Editor target not built in build-san (concurrent module edits; re-verify later)
- W10-1 RUNNING: atomic SaveSystem::load, load-robust streaming timing tests, NavMesh ensureLinks thread-safety (+TSan if feasible), StreamingSystem::prime bound
- RUNNING: W8-1, W8-2, W8-3, W8-4, W10-1
- W8-4 DONE: shader SPIR-V cache .litcache/shaders (cold 2143 ms -> warm ~400 ms window-to-buffers), parallel prefetch, ShaderCacheTests 28; W8-3 DONE: Engine::loadMesh via asset cache, uploadProcessedMesh, preloadMeshes (cold 767 ms -> warm 15.5 ms single 180k-tri); W10-1 DONE: atomic SaveSystem::apply/load w/ rollback, streaming work-unit assertions, Nav ensureLinks mutex, prime timeout
- !!! CRITICAL REGRESSION (found 09:09 WET): LitShot showcase renders fully BLACK, Diligent barrier errors (UNDEFINED vs UNORDERED_ACCESS) on Cloud Noise, Environment Capture, Scene Color Pyramid, SSR Hi-Z. 56 renders were clean earlier. Alerted W8-1 (ac494627d55b1f616) and W8-2 (a99f89e9dfb2dad0b) to bisect/fix first. Suspects: W8-1, W8-2, W8-3 (uploadProcessedMesh), W8-4 (parallel shader prefetch). Must be fixed + verified by reading PNG before anything else ships.
- W8-2 BLOCKED/DONE (no code kept): black frame NOT from W8-2 (its edit reverted). Evidence: /tmp/litshot-w8-2 (08:28 build) and /tmp/litshot-w8-4 (pre-change) render fine; binaries built after 08:30 fail even with LIT_NO_SHADER_CACHE=1; first error generateCloudNoise frame 1. Prime suspect W8-4 threaded PrefetchShaders/BuildSurface threads; W8-4 resumed to bisect; W8-1 told to verify its own parts.
- W8-2 findings to land AFTER render is fixed: environment capture/prefilter/SH rebuilt EVERY frame while clouds animate (Sky+IBL 1.8-2.4 ms vs 0.21 ms with clouds off): rebuild only after cloud travel > ~7.5 units (~0.25 s) => saves ~1.6 ms/frame; colorBleed ~1.2 ms, ssgi ~0.7 ms in GTAO; SSR label 3.3-7.5 ms (W8-1). Baseline per pass table in docs/OvernightLog.md W8-2.
- /tmp hygiene: LitShot copies are 625 MB each, tmpfs 7.5 GB; keep at most 2 reference copies.
- CHECK-IN 09:20 WET: build clean, ctest 29/29, BUT showcase still BLACK (37.8 KB, 10 Diligent barrier errors on frame 1 starting at Cloud Noise; startup/pipelines/buffers/mesh upload log fine). Only W8-4 (a0056411d8cc135f2) still RUNNING, bisecting threaded PrefetchShaders/BuildSurface. Known-good reference binaries: /tmp/litshot-w8-2, /tmp/litshot-w8-4 (08:28 builds, render fine with current resources => regression is in code changed after 08:28: Render (W8-1/3/4), Engine mesh loading (W8-3), possibly Save/Streaming/Nav/World changes)
- W8-1 DONE (partial, UNVERIFIED visually): SSR/Lighting label swap fixed (old 'SSR' 5.6-7 ms was Lighting pass; reflections disabled already free), WS reflections linear voxel sampling + bisection refine + edge fade, SSR half-res trace + depth-aware upsample; no screenshots/timings
- NEXT if W8-4 does not find it: launch dedicated bisect agent that diffs file mtimes after 08:28, uses RenderDoc layer or Diligent debug to find skipped CommitShaderResources, tests W8-3 changes (uploadProcessedMesh, preloadMeshes) next
- (older line) RUNNING: W3-1 sanitizers, W4-5 world/portal perf, W4-7 AI/audio/anim perf, W4-8 GPU perf
- NEXT when slots free: startup B8 (shader cache, parallel import; needs Render free), Delta capture copies (Save), nav path-search speed, bench re-baseline, ASan uninitialised-memory scan, final full build+ctest, summary for owner

Wave 4 original list
- CPU perf (B6, B9), GPU perf (B7), startup (B8)
- Lighting/shadows/GTAO spatial quality (C10), upscaler + governor (C13)
- Editor profiler + hot reload + validator (D14, D15, D17)
- Nav (E19), replay determinism (E20)

Refill rule: when fewer than 3 items RUNNING/TODO, add new items from benchmark/profile findings, bug hunts, test gaps, docs gaps.
- 2026-11-04 W9-1 DONE: cloud-driven environment rebuild throttled (refresh after 7.5 units of cloud travel); default LightComponent intensity 1e6 lm (Bulb preset, visible in daylight). Interior veiling PARTIAL: lighting.comp skyVisibility ceiling march darkens enclosed ambient modestly; floor still reflects sky via reflection probes (needs sky-visibility for probe/SSR miss fallback). Window-size request not fixed (tiling WM overrides size, LitShot PNG dims vary).
- 2026-11-04 W9-2 DONE: reflection_composite.comp now applies skyVisibility (shared include/sky_visibility.glsl) to sky and probe fallback specular, extra roughness falloff (skyOpen^2 for rough). Showcase change is subtle (mean pixel diff 0.33, frame has no outdoor surface to compare); GTAO colour bleed on/off compared: bleed adds a faint warm tint (mean diff 0.4/255, max 34 at sphere undersides), no artifacts. Remaining veil in showcase is the grey floor/wall haze from ambient, not sky reflection.
- 2026-11-04 HAZE DONE: dominant cause = ambient SH irradiance (kD*irradiance) leaking into enclosed interiors; screen-space ceiling march could not see ceilings outside the depth buffer. Fixed with a world-space sky-visibility volume (sky_volume.comp, 64^3 R8, rebuilt only when the reflection voxel grid changes) sampled by skyVisibility() in lighting.comp and reflection_composite.comp. Fog, aerial, GTAO, GI, bloom, FXAA, tonemap black point ruled out (each toggle <= 1.5 percent mean luma change, black level 0-1). LitShot gained --camera X,Y,Z,YAW,PITCH and an outdoor apron in the showcase.

- W10-1 DONE: probe resolution + explicit bake API + Inspector "Bake now"; nav reachable-only bench recorded (30.6 ms per 100 paths, vs 37.3 with unreachable goals). TODO: GPU eyeball of a scene that actually contains a ReflectionProbe, re-record docs/bench-baseline.json on a quiet machine, hierarchical nav graph.
- RAYCAST-HIER DONE: world raycast coarse 8^3 occupancy skip (sparse max100 -30%, clustered -8%, dense unchanged); see OvernightLog
- DONE: probes LitShot scene (`--testscene probes`, `--no-probe`, `--bake-frame N`) GPU-verified; fixed probe bake (voxel trace step cap, sky attenuation on probe radiance). DONE: ik.spider undoable via History::spawnRebuildable + IKTests spawn/undo/redo. DONE: ik.chain undoable via spawnIkChain (target rebuilt, chain attached/detached) + IKTests testChainUndoRedo.
