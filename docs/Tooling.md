# Tooling

## Headless mode and the simulation loop

`Engine::tick(dt)` is the simulation: input sampling, systems, fixed-step physics, scripts, timers and events. It never touches the renderer. `Engine::update()` and `present()` draw; headless runs skip them.

```cpp
Engine engine;
engine.openHeadless();
engine.seed(42);
engine.runHeadless(600, 1.0f / 60.0f, [&](uint64_t tick) { engine.setInput(state); });
std::string dump = engine.dumpStateText();
```

`runHeadless(ticks, dt, beforeTick)` calls `tick(dt)` `ticks` times (`dt` 0 uses the fixed step). With a replay loaded and `ticks` 0 it runs to the end of the recording. Fixed updates run at `setFixedTimestep` (default 1/60) from an accumulator, so identical `dt` sequences give identical results.

`build/.../src/Headless/Headless` wraps this for CI:

```
Headless [--scene f.litscene] [--ticks n] [--dt s] [--seed n] [--params f.json] [--record f] [--replay f] [--hash-interval n] [--verify-replay f] [--dump f.json] [--perf f.csv] [--testscene name] [--metrics f.json] [--list-scenes]
```

It prints `ticks N entities M hash H`; two runs with equal inputs print equal hashes. A replay file supplies the seed. Meshes need a renderer, so headless scenes should be meshless or use mesh ids only.

## Seeded randomness

`World::random()` is a `RandomService`: named PCG32 streams (`random().stream("spawn")`) derived from one master seed with `seed(n)`. Streams are independent, so adding a consumer does not shift the others. Use these everywhere instead of `std::random_device`. Engine physics and the world layer contain no randomness today; the service is there for systems that add it.

## Input recording and replay

`InputState` (keys 0..511 as GLFW codes, mouse buttons 0..7, mouse position, per-tick mouse delta and scroll) is sampled once at the start of every `tick`. Systems read it through `engine.simInput()` (`held`, `pressed`, `released`) or `engine.input()` (`InputActions` is bound to it), never `InputManager` directly, so recordings reproduce exactly.

```cpp
engine.startRecording("run.replay");
engine.stopRecording();
engine.startReplay("run.replay");
```

While replaying, each tick takes its input and its `dt` from the file. Seed the engine with `replaySeed()` before building the scene so random streams match; the file stores the seed that was active at `startRecording`.

File format `litreplay 1` (text, one event per line, changes only):

```
litreplay 1
seed <u64>
ticks <count>
@<tick> [dt=<float>] [keys=<code,code,...|->] [buttons=<bitmask>] [mouse=<x>,<y>] [delta=<x>,<y>] [scroll=<x>,<y>]
end
```

- Floats use the shortest round-trip form, so replays are bit exact.
- `dt`, `keys`, `buttons` and `mouse` persist until changed; `delta` and `scroll` apply only to the tick of their line (absent means zero).
- Tick 0 should carry `dt` (it defaults to 0). `keys=-` means no keys held.
- Lines starting with `#` and blank lines are ignored. A file without `end`, with unknown fields or with events past `ticks` is rejected.

The Editor autopilot offers `record <path>`, `record off` and `replay <path>`.

## Capture and autopilot

The existing autopilot (`LIT_AUTOPILOT=script.lit`, see `docs/Editor.md`) gained:

| Verb | Effect |
| --- | --- |
| `shot <path>` | save one named frame (existing) |
| `capture <prefix> <count> [every]` | save `<prefix>_0000.png`, `<prefix>_0001.png`, ... every `every` frames, `count` images, then continue |
| `dump <path>` | write the state dump JSON |
| `perf <csv>` / `perf off` | start or stop the performance log |
| `record <path>` / `record off`, `replay <path>` | input recording and replay |
| `param <name> <value>` | set a parameter bus target |
| `expect param:<name> <value>` | assert a parameter (3 decimals) |
| `mask <entity name> [0\|1]` | set or clear the render mask flag on every entity with that name |
| `testscene <name>` | replace the scene with a test scene (below) |
| `overlay <tab>` | show the debug overlay on tab 1 to 5 (0 hides it) |

## State dump and assertions

`engine.dumpState()` returns a `Json`; `dumpStateText(pretty)` and `writeStateDump(path)` serialize it. Shape:

```
{ "world": { "time": {frame, elapsed, scale, paused}, "count": n,
             "entities": [ {index, generation, name, tag, layer, visible, parent, children, mesh,
                            position, rotation(xyzw), scale, worldPosition,
                            components: {Name: "text"}, scripts: {Name: "text"}} ] },
  "random": { "seed": hex, "streams": {name: hex state} },
  "params": { name: value } }
```

Entities are ordered by slot index. `dumpWorld(world, DumpOptions)` dumps only the world. Test helpers in `Engine.Json`:

- `jsonFind(doc, "world.entities[name=ball0].position[1]")`: dotted keys, `[index]`, and `[field=value]` selectors on arrays of objects.
- `expectNumber`, `expectString`, `expectBool`, `expectSize`, `expectVec`: return an empty string on success or a message, e.g. `CHECK_OK(expectVec(doc, "world.entities[name=a].position", {1, 2, 3}))`.
- `jsonDiff(a, b, epsilon)`: path of the first difference, empty when equal.
- `hashText` / `hashHex`: 64-bit FNV-1a of a dump for cheap equality.

## Performance logging

`engine.enablePerfLog("perf.csv")` records one row per tick; `disablePerfLog()` (or engine shutdown) writes the file. Columns: `frame, cpu_ms` (wall time between ticks), `sim_ms` (tick duration), `gpu_ms` (total), then one `gpu:<pass>` column per GPU pass (zero when a pass did not run; all zero headless).

`scripts/perf_check.py perf.csv thresholds.txt [--skip n] [--baseline old.csv --tolerance pct]` exits 1 when any limit is exceeded. Threshold lines are `<column> <min|mean|median|p95|p99|max> <limit>`; `#` starts a comment. With `--baseline`, median and p95 of every shared column may not exceed the baseline by more than the tolerance (default 10 percent). Use `--skip` to drop warmup frames. With portals, `portal:<view>` columns and their sum `portal_gpu_ms` hold the portal view GPU time, which `gpu_ms` does not include.

`scripts/lag_check.sh [outdir]` renders each scene for `FRAMES` frames (default 460) with `LitShot --perf` and prints `scripts/lag_report.py`: frame time p50, p99 and max, the number of hitches (frames over both 1.5 times the median and the median plus 4 ms) and, for the worst ones, the passes that grew.

## Frame pacing

`Engine::setFramePacing(true)` (Sandbox and Editor; off under `LIT_AUTOPILOT`) paces `run()` and the Editor loop to a rate the machine holds with headroom: the interval is 1.15 times the 90th percentile of recent frame work (the longer of CPU time and whole-frame GPU time), never shorter than the monitor refresh interval and never longer than 1/30 s. The first 30 frames after pacing is enabled are ignored, so load spikes do not lower the rate. Running flat out starved the compositor of GPU time and heated the GPU into throttling, which showed as bursts of long frames. Input is polled after the pacing sleep, so the sleep adds no input latency. `LIT_MAX_FPS=<n>` sets the upper rate, `LIT_MAX_FPS=0` turns pacing off. `LitShot` runs unpaced unless it is given `--paced`.

### Power saving

`Engine::setPowerSaving(true)` (Sandbox and Editor; off under `LIT_AUTOPILOT` and `LIT_FIXED_DT`, never on in `LitShot`) lowers the frame rate when nobody would see the difference. Waiting is done in `glfwWaitEventsTimeout`, so a key, button, cursor, scroll, focus or expose event ends the wait at once and holds the full paced rate for the following activity window. The first frame after a lowered rate advances time by at most 1/60 s.

| State | Default | Variable |
| --- | --- | --- |
| Editor focused, nothing changing | 10 fps | `LIT_IDLE_FPS` |
| Unfocused, nothing changing | 5 fps | `LIT_UNFOCUSED_IDLE_FPS` |
| Unfocused, game or play mode running | 30 fps | `LIT_UNFOCUSED_FPS` |
| Minimized, invisible or zero sized | 4 fps | `LIT_HIDDEN_FPS` |
| On battery, upper rate in every state | off | `LIT_BATTERY_MAX_FPS` |
| Activity window | 2 s | `LIT_ACTIVITY_SECONDS` |

A rate of 0 removes that limit, and `LIT_POWER_SAVING=0` turns the whole policy off. The Editor counts play mode, animated stress objects, toasts, animated time of day, held keys and buttons, history changes, camera movement, streaming and shader reloads as activity. Other animation in edit mode, such as cloud drift, advances at the idle rate. Compositors without a minimized state (Hyprland) report a window on another workspace only as unfocused. `LIT_FRAME_LOG=1` prints presented frames per second to stderr.


## Parameter bus

`engine.params()` (`Engine.Params`) holds named floats. Gameplay writes targets with `set(name, value)`; each tick the `params` system moves values toward their targets with exponential smoothing (`value += (target - value) * (1 - exp(-dt / smooth))`) using unscaled time, then evaluates mappings. Read with `get(name)` on the CPU or `litParam(slot)` in shaders.

Definitions live in a JSON file loaded with `params().loadFile(path)` and are hot reloaded: the file is polled every 16 ticks and a changed file is re-parsed; on any schema error the previous definitions stay active and the error is logged. Current and target values survive a reload.

```json
{
  "params": {
    "drive": {"default": 0, "min": 0, "max": 1, "smooth": 0.3, "slot": 0},
    "bias": {"default": 0.5}
  },
  "mappings": {
    "vignette": {"input": "drive", "curve": [[0, 0.1], [1, 0.9]], "mode": "smooth", "smooth": 0.1, "slot": 1},
    "grade": {"input": "vignette", "curve": [[0, 0], [1, 10]], "mode": "step"}
  }
}
```

- `params`: `default`, `min`, `max`, `smooth` (seconds, 0 is instant), `slot`. Unknown keys are errors.
- `mappings`: derived outputs. `input` is any param or earlier mapping, `curve` is strictly increasing `[x, y]` points (clamped outside), `mode` is `linear` (default), `smooth` (smoothstep between points) or `step`; `smooth`, `min`, `max`, `slot` as above.
- Slots (0 to 255) index the shader block; unspecified slots are assigned in declaration order. Pin `slot` for values shaders depend on.

The engine uploads all 256 slots whenever they change to the shader buffer `EngineParams` (std430, bound by block name like any custom buffer; see `docs/Shaders.md`). Include `params.glsl` and call `litParam(slot)` from compute, draw and effect shaders.


## Attention post-processing

`RENDER_MASKED` (`World::setRenderFlags(e, RENDER_MASKED, true)`) marks an entity. The flag rides the GPU renderable word and the visible entry into the G-buffer (material word bit 15, masked off wherever the material is looked up), so the tonemap pass can see which pixels belong to masked objects with no extra geometry pass. On masked pixels it applies `maskContrast` (power around mid grey) and a moving sheen band (`maskSheen` strength, `maskSheenSpeed`, `maskSheenWidth`, `maskSheenAngle` degrees, `maskSheenColor`). Any gameplay layer can drive the flag; the Editor sets it on entities tagged `observed` while their `ObservationSystem` state is `Attended`.

The vignette has `vignette` (strength), `vignetteWidth` (distance from centre at which it reaches full strength, height normalised) and `vignetteTightness` (0 to 0.999: fraction of that width that is a soft falloff). All of these and the mask settings are ordinary `RenderSettings` keys (`set maskContrast 1.4` in autopilot).

`engine.loadPostFx("resources/data/post_fx.json")` binds render settings to the parameter bus. Each key is a render setting; the value is a number, `[r, g, b]`, or `{"base": x, "param": "bus_name"}`. A bound setting follows the bus value every tick whenever that name exists, otherwise it uses `base`. Combined with bus mappings (`resources/data/params.json`) a curve file drives the vignette and the highlight. Errors keep the previous bindings.

## Hot reload

Shaders under `resources/shaders` and custom surface files reload when `setShaderWatch(true)` (Editor and Sandbox enable it). The parameter file reloads on its own poll. `engine.watchData(path, reload)` registers any other data file: it is polled every 16 ticks (also headless) and `reload(path)` runs when the modification time changes; return false to report failure. `loadPostFx` registers its file this way. `engine.dataReloads()` counts successful reloads and `engine.watchedFiles()` lists the files.

## Debug overlay

`DebugOverlay` (`Engine.Overlay`) draws one Dear ImGui window with five tabs, all live: portal graph (cells on a ring, pair links, camera cell highlighted, portal list, per-view stats), params (table, history plot, the selected mapping curve with a live input marker), observation (counts and entities per state, focus entity, latency stats), AI decision trace (recent events with options, scores and considerations) and perf (CPU and GPU history, per-pass GPU timings, per-view portal GPU timings).

```cpp
DebugOverlay overlay;
overlay.attachObservation(&observation);
overlay.attachTrace(&runner.trace());
overlay.setVisible(true);
engine.run([&](float) { overlay.draw(engine); });
```

The Editor toggles it with F3 or `view.debugOverlay` (`LIT_OVERLAY=<tab>` opens it on start). The Sandbox calls `engine.enableImGui()`, toggles it with F3 and shows it with `LIT_OVERLAY=1`. Panels without an attached source say so.

## Test scenes

`Engine.TestScenes` (`testSceneNames()`, `buildTestScene(engine, name, kit, info[, params])`) builds named scenes for headless, Editor and LitShot runs. Every scene LitShot can render is in this registry, so each one runs in all three. Scenes that animate register `testscene.*` Update systems driven by world time and delta (the Editor, LitShot and Headless all advance them the same way; `clearTestSceneSystems` / `leaveTestScene` remove them when the scene is replaced). `TestSceneInfo` carries the start camera, near / far plane and suggested tick count; `applyTestSceneCamera` applies it. `TestSceneParams` holds the knobs (light intensity, light count, dense grid and detail, large world origin / fly speed / rebase tick); the defaults suit the Editor, LitShot fills them from its options.

| Scene | What it is | Editor | LitShot |
| --- | --- | --- | --- |
| `portal_loop` | three-room portal loop, camera looks through the AB portal | `LIT_SCENE=portal_loop`, `scene.test.portal_loop` | `--testscene portal_loop` |
| `portal_torture` | nested portals, camera walks through them | `scene.test.portal_torture` (`LIT_SCENE=portal_torture` opens the Editor's own torture scene) | `--testscene portal_torture` |
| `portal_tilt` | wall-to-floor, ceiling, 45 degree rolled and 180 degree flipped portals with frames (docs/Portals.md) | `LIT_SCENE=portal_tilt`, `scene.test.portal_tilt` | `--testscene portal_tilt` (`LIT_TILT_WALK=floor\|ceiling\|rolled\|flip` walks through one) |
| `streaming_route` | chunked world streamed along a route | `LIT_SCENE=streaming_route` | `--testscene streaming_route` |
| `crowd` | 500 nav agents on a plane | `LIT_SCENE=crowd` | `--testscene crowd` |
| `vista` | content out to 60 km, near 0.5 / far 4e5 | `LIT_SCENE=vista` | `--testscene vista` |
| `skinning` | skinned characters, cloth, morph targets, shadowed lights | `LIT_SCENE=skinning` | `--testscene skinning` |
| `terrain` | streamed terrain flythrough | `LIT_SCENE=terrain` | `--testscene terrain` |
| `showcase` | Cornell-style room, area lights, sphere roughness row (LitShot version) | `scene.showcase` and `LIT_SCENE=showcase` are the Editor's own showcase; the registry one is `testscene showcase` in an autopilot script (no `scene.test.showcase` palette entry) | `--testscene showcase [--light-intensity X]` |
| `probes` | closed colour box, chrome sphere, reflection probe | `LIT_SCENE=probes` | `--testscene probes [--no-probe] [--light-intensity X]` |
| `light_stress` | field of pillars, 256 bobbing spheres, N moving shadowed lights (default 1024, `LIT_LIGHTS` in the Editor) | `LIT_SCENE=light_stress [LIT_LIGHTS=4096]` | `--testscene light_stress [--lights N]` |
| `vsm_mixed` | portal loop + cluster rocks + 4 skinned characters + N shadowed lights | `LIT_SCENE=vsm_mixed [LIT_LIGHTS=N]` | `--testscene vsm_mixed [--lights N]` |
| `dense` | grid of 1.3M / 330k triangle rocks (cluster LOD stress, grid 24, first build takes seconds) | `LIT_SCENE=dense` | `--testscene dense [--dense-grid N] [--stats f]` |
| `largeworld` | content 500 km from the origin, floating origin on, far plane 2e5, 20 / 50 / 100 km landmarks | `LIT_SCENE=largeworld` (fly with the free camera; the origin rebases at 400 m, leaving the scene restores origin 0) | `--testscene largeworld [--origin-km X] [--fly V] [--rebase-frame N]` |
| `ik` | IK terrain, spider, quadruped and biped rigs (debug lines on) | `LIT_SCENE=ik`, `scene.test.ik` (the older `scene.ikTest` is the Editor's own build) | `--testscene ik` |
| `gallery` | courtyard with material sweeps, glass, area lights, probe | `LIT_SCENE=gallery` | `--testscene gallery` |
| `textures` | floor and six spheres with base color, normal, ORM, emissive and triplanar textures (three 1024x1024 PNGs in `resources/textures/test`); the bindless-table vs legacy-array parity scene (docs/RenderGraph.md) | `LIT_SCENE=textures` | `--testscene textures` |
| `instancing` | 1.2M GPU instances (trees, rocks, grass tufts) in 768 `InstanceBatch`es over a 2 km plane, camera flies a 300 m circle (docs/Instancing.md); `LIT_INSTANCING_DENSITY`, `LIT_INSTANCING_SPEED` | `LIT_SCENE=instancing` | `--testscene instancing [--stats f]` |
| `orb_run` | ORB RUN, a playable game: collect 8 orbs in a hub, three themed rooms and a terrain courtyard linked by portals (wall-to-floor drop, ceiling, 45 degree rolled porthole, big arch) while three IK spiders patrol and chase; 3 lives, 180 s, win and lose screens (`Engine.OrbRun`, `Engine.OrbRunRules`) | `LIT_SCENE=orb_run`, `scene.test.orb_run` (free camera is the player) | `--testscene orb_run [--camera x,y,z,yaw,pitch,roll]`; playable: `Sandbox orb_run` |

In the Editor each scene also loads from the command palette as `scene.test.<name>` (except `showcase`, above) and from autopilot scripts as `testscene <name>` / `run scene.test.<name>`. Switching scenes repeatedly is safe: the world, portals, `testscene.*` systems, floating origin and the scene's render settings (time of day, clouds, fog, reflection mode) are reset, and materials, dynamic mesh sources / instances and uploaded rock meshes are pooled per Engine, so repeated switches do not grow GPU state. LitShot sets `LIT_FIXED_DT=1/60` itself for `light_stress`, `vsm_mixed` and `largeworld` so frame N of an animated scene is repeatable without `--deterministic`.

```
Headless --testscene crowd --ticks 600 --metrics crowd.json
```

prints `metric <name> <value>` lines and writes the JSON. Headless uses mesh id 0 and no GPU, so only simulation metrics exist there. Metrics: `portal_torture` (cells, portals, crossings, view nodes and depth, frame build time), `portal_tilt` (cells, portals, crossings), `streaming_route` (chunks created, destroyed and resident, peak entities, stream tick time), `crowd` (agents, retargets, distance walked, agent step time), `orb_run` (cells, portals, orbs collected, lives, time left, spider hits), `vista` (object count, farthest object, camera near and far, forward-Z depth step in metres at 100 m to 50 km); the other scenes report at least `entities`. `ctest` runs several as `TestScene_<name>`; `ToolingTests` builds every registered scene headless, asserts on the metrics and on determinism, and switches through all of them twice on one Engine checking that nothing is left behind. The GPU path is covered by `LargeWorldRenderTests` (builds `largeworld` through `Engine.TestScenes`). In the Sandbox use `LIT_TESTSCENE=<name>`.

## Far view distance

The depth buffer is D32F with a standard (not reversed) projection, so resolution at distance d is roughly `d * d * 6e-8 / near` metres: `vista` reports it. With near 0.5 m and far 400 km, the step is about 1.2 m at 4 km and 120 m at 40 km, which is fine for terrain-scale content but would z-fight coplanar surfaces far away. Raising the near plane in vista scenes is the mitigation used; a reversed-Z conversion (clear 0, greater compare, Hi-Z max/min swap, oblique portal planes) is not done.
