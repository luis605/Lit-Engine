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
Headless [--scene f.litscene] [--ticks n] [--dt s] [--seed n] [--params f.json] [--record f] [--replay f] [--dump f.json] [--perf f.csv] [--testscene name] [--metrics f.json] [--list-scenes]
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

`scripts/perf_check.py perf.csv thresholds.txt [--skip n] [--baseline old.csv --tolerance pct]` exits 1 when any limit is exceeded. Threshold lines are `<column> <min|mean|median|p95|p99|max> <limit>`; `#` starts a comment. With `--baseline`, median and p95 of every shared column may not exceed the baseline by more than the tolerance (default 10 percent). Use `--skip` to drop warmup frames.

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

`Engine.TestScenes` builds named scenes for headless and visual runs: `portal_torture`, `streaming_route`, `crowd`, `vista`. Each registers `testscene.*` systems that drive the scenario and exposes metrics.

```
Headless --testscene crowd --ticks 600 --metrics crowd.json
```

prints `metric <name> <value>` lines and writes the JSON. Headless uses mesh id 0, so only simulation metrics exist there. Metrics: `portal_torture` (cells, portals, crossings, view nodes and depth, frame build time), `streaming_route` (chunks created, destroyed and resident, peak entities, stream tick time), `crowd` (agents, retargets, distance walked, agent step time), `vista` (object count, farthest object, camera near and far, forward-Z depth step in metres at 100 m to 50 km). `ctest` runs each as `TestScene_<name>`; `ToolingTests` asserts on the metrics and on determinism. In the Editor use `LIT_SCENE=<name>`, the commands `scene.test.<name>` or the autopilot verb `testscene`; in the Sandbox use `LIT_TESTSCENE=<name>`.

## Far view distance

The depth buffer is D32F with a standard (not reversed) projection, so resolution at distance d is roughly `d * d * 6e-8 / near` metres: `vista` reports it. With near 0.5 m and far 400 km, the step is about 1.2 m at 4 km and 120 m at 40 km, which is fine for terrain-scale content but would z-fight coplanar surfaces far away. Raising the near plane in vista scenes is the mitigation used; a reversed-Z conversion (clear 0, greater compare, Hi-Z max/min swap, oblique portal planes) is not done.
