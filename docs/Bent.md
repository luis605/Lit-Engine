# Bent: laws about the engine, and a tool that tries to break them

Bent is a small pure language for declaring **laws**: properties that must hold for every input. The `bent` tool generates
thousands of random inputs per law (scene programs, vectors, documents, ...), runs them in parallel against fresh headless
engines, and when a law breaks it **shrinks** the input to a minimal counterexample, prints it as a runnable `.bent` program and
writes it to `tests/bent/regressions/`.

The ideas come from the Bend language: every value is immutable, and every case is an independent immutable program, so cases run
on all cores with no locking at all. Bent is its own language (lexer, parser, evaluator written from scratch, no dependencies);
it does not use Bend itself.

```
law save_load_preserves_content {
    forall (p: program)
    => after(p) { contentHash() } == after(p ++ [saveLoad()]) { contentHash() }
}
```

`program` is a generator of lists of actions (spawn, move, destroy, step, ...). `after(p) { e }` runs the scene program `p` on a
fresh headless `Engine` and evaluates `e` against the resulting world.

* Sources: `src/Engine/Bent/` (language core `BentLang`: `Value`, `Lexer`, `Parser`, `Interp`, `Gen`, `Builtins`, `Check`;
  engine bridge `BentEngine`: `EngineHost.cpp`, `EngineObserve.cpp`), CLI `src/Bent/main.cpp`.
* Laws: `tests/bent/laws/*.bent`, shared generators `tests/bent/lib/prelude.bent`, regressions `tests/bent/regressions/`.
* Tests: `BentTests` (language unit tests), `BentLaws` (`bent suite`: every law with a fixed seed and a small budget, plus every
  regression).

## Running

```
bent check tests/bent/laws/physics.bent                 # 100 cases per law, seed 1, all cores
bent check FILE --cases 5000 --seed 7 --jobs 8 --law no_tunnelling_thick_wall
bent check FILE --time 60                               # as many cases as fit in 60 s (split over the laws)
bent check FILE --json > report.json                    # machine readable (human output goes to stderr)
bent check FILE --isolate                               # one forked process per case: survives crashes and hangs
bent replay FILE --law NAME --seed 7 --case 1234        # re-run one random case, explained
bent shrink FILE --law NAME --seed 7 --case 1234        # shrink that case and write the regression
bent list FILE | bent list --actions | bent list --observations | bent list --builtins
bent suite --laws tests/bent/laws --regressions tests/bent/regressions --cases 25 --seed 1
bent eval --expr 'sample(listOf(int(0, 9), 3, 3))'
```

`check` runs the laws declared in FILE itself (`--law` selects any loaded law, including ones from includes) and every `example`
loaded. Output per law: `PASS` / `FAIL` / `ERROR` (an exception or crash) / `GAVE-UP` (every case was discarded by a precondition).
Exit status 0 when everything holds, 1 when a law fails, 2 for usage or language errors. Engine log output is silenced unless
`--verbose`.

Options: `--cases N` (default 100), `--seed S` (default 1), `--time SEC` (total budget, cases unlimited unless `--cases`),
`--jobs J` (default: hardware threads), `--no-shrink`, `--shrink-limit N` (attempts, default 4000), `--shrink-time SEC`
(default 180), `--case-timeout SEC` (default 120, enforced by killing the child with `--isolate`), `--regressions DIR`
(default `tests/bent/regressions` when it exists), `--no-regressions`, `--json-out FILE`.

### Determinism and parallelism

Case `k` of law `L` under seed `S` is a pure function of `(S, L, k)`: its choice tape is seeded from `mix(S, hash(L), k)`, and its
size parameter (list lengths grow over the first 40 cases) depends only on `k`. Every `after` block builds a brand new `Engine`
(`openHeadless`, seed 1, fixed step 1/60), so a case shares nothing with any other. Workers pull case indices from an atomic
counter; the reported failure is always the *smallest* failing case index, so the result is identical for any `--jobs` (the
`BentTests` suite checks serial and parallel runs agree, and the determinism laws check equal hashes across concurrently running
engines). The engine was verified safe to instantiate many times per process: an `Engine` owns its `World`, job system, portals,
streaming and physics settings; the only shared mutable state is the stdout logger (thread safe) and GLFW (not touched headless).
`--isolate` switches to a process pool (fork per case, the parent stays single threaded) for crash and hang isolation; in thread
mode a crash prints the law, case and seed and suggests `--isolate`.

## Language reference

### Lexical

Comments start with `#` or `//` (to end of line) or are `/* ... */`. Identifiers `[A-Za-z_][A-Za-z0-9_]*`. Integers `42`,
`0x2a` (64 bit, overflow is an error), floats `1.5`, `1e-3`, `nan`, `inf`. Strings `"..."` with `\n \t \r \0 \" \\ \xHH
\u{1F600}`. Every error carries `file:line:col`.

### Values

All values are immutable: `unit`, `bool`, `int` (64 bit), `float` (double), `string`, `vec3`, `quat` (w, x, y, z), `list`,
`record` (fields sorted by name), `action` (a scene step: name plus named arguments), `entity` (a reference to the k-th entity
the program spawned), functions and generators. `==` is structural (`1 == 1.0` is true, `nan == nan` is false).
`a ~= b` is approximate equality (default tolerance 1e-6, absolute or relative); `a ~= b within 0.01` sets it. It works on numbers,
vectors, quaternions (sign canonical: `q ~= -q`), lists and records recursively.

### Declarations

```
include "../lib/prelude.bent"          # relative to this file, loaded once
let arena = 10.0                       # global constant (evaluated once, in order)
gen body = spawn(pos: smallVec)        # same as let; documents that the value is a generator
fn sq(x) = x * x                       # function (hoisted: may be used before its declaration, may recurse)
law name { forall (x: GEN, y: GEN2) where PRECONDITION => PROPERTY }
example name { x = VALUE, y = VALUE }              # a concrete case of law `name` (regressions)
example name known "reason" { x = VALUE }          # expected to fail until the bug is fixed
```

In a law, later generators may use earlier variables (`forall (n: int(1, 5), xs: listOf(int(0, 9), n, n))`). A false `where`
discards the case. `forall (...)` and `where` are optional: `law l { 1 + 1 == 2 }`.

### Expressions

Precedence, lowest first: `==>` (implication, right associative), `or`, `and`, `not`, comparisons (`== != < <= > >= ~=`), `++`
(list / string concatenation), `+ -`, `* / %`, unary `-`, postfix (call `f(a, name: b)`, field `.x`, index `[i]`, negative from the
end).

```
let x = 1 in x + 1
if c then a else b
fn(x, y) => x + y                      # lambda (closures capture their environment)
[1, 2, 3]    {a: 1, "odd key": 2}      # list and record literals
forall e in entities(): alive(e)       # quantifiers over lists (also: exists)
after(prog) { expr }                   # run prog on a fresh engine, evaluate expr on the final world
always after(prog) { expr }            # expr holds on the initial world, after every action and after every tick
always within N after(prog) { expr }   # ... and for N more ticks after the program
eventually within N after(prog) { expr }   # expr holds at the end of prog or within N further ticks
```

Records: `r.field`, `r["field"]`, `with(r, field: v)`, `keys`, `values`, `has`, `get(r, k, default)`, `fromPairs([[k, v]])`.
Vectors: `v.x`, `+ - * /` with scalars, `dot`, `cross`, `length`, `distance`, `normalize`, `lerp`; quaternions: `quat(w, x, y, z)`,
`axisAngle(axis, degrees)`, `q * q`, `q * v`, `rotate(q, v)`. Actions: `a.name`, `a.argname` (unit when not given).

### Builtin functions

Lists: `len map filter fold all any count head last tail take drop reverse concat repeat zip enumerate contains indexOf sort sortBy
dedup allDistinct flatten min max sum range`. Numbers: `abs sqrt floor ceil round sin cos exp log pow atan2 radians clamp isNaN
isFinite toInt toFloat`. Strings: `str join substr chars`. Misc: `kindOf hash error trace(label, value) approx actionName`.
`trace` adds a note shown with a counterexample. `bent list --builtins` prints them all.

### Generators

| Generator | Values | Shrinks toward |
| --- | --- | --- |
| `int(lo, hi)` | integers in range; edge values (lo, hi, near 0) often | 0, or the bound nearest 0 |
| `float(lo, hi)` | finite doubles; lo, hi, small integers and halves often | 0.0, or the bound nearest 0 |
| `nasty(g)` | g, or (20%) one of 0, -0, denormals, FLT_MIN, 1e30, FLT_MAX, inf, -inf, NaN, 1e300, FLT_EPSILON, 2^24 + 1 | the plain value |
| `bool()` | false / true | false |
| `just(v)`, `elements(list)` | constant, one of the list | first element |
| `oneof(a, b, ...)` | one alternative (generators or values) | first alternative |
| `frequency([[w, g], ...])` | weighted choice | first entry |
| `listOf(g, min, max)` | lists of min..max elements | shorter lists, simpler elements |
| `unitVec3()`, `unitQuat()` | unit vectors (simplest: +Y), unit quaternions (identity, multiples of 45 degrees, any) | +Y, identity |
| `text(min, max)`, `ident(min, max)` | strings incl. quotes, backslash, newline, NUL, DEL, multi-byte UTF-8 and an invalid byte; identifiers | "a..." |
| `subseq(list)` | a sub-sequence | empty |
| `map(g, f)`, `filter(g, p)`, `bind(g, f)`, `delay(fn() => g)` | transformed / constrained / dependent / recursive generators | via g |
| `sample(g)` | draws a value inside a property (rarely needed) | |

**Lifting.** Builtin constructors applied to generators produce generators: `vec3(float(0, 1), 0, 2)`, `[int(0, 9), bool()]`,
`{a: text(0, 3)}` and every action (`spawn(pos: smallVec, size: float(0.1, 2))`) are generators of vectors, lists, records and
actions. User functions are not lifted; use `map` / `bind`.

## Scene programs (actions)

A scene program is a list of action values (nested lists are flattened). It means nothing until `after`, `always` or
`eventually` interprets it against a fresh headless Engine. Entities are referenced by **spawn index**: the k-th `spawn`, `plane`
or `light` of the program is `entity(k)`; an integer `target` is taken modulo the number spawned so far (so every random program
is meaningful) and `-1` is the most recent. References follow entities across `saveLoad` (by slot order), `saveRoundtrip`,
`restoreText` and `compact` remaps; destroyed entities keep their (dead) reference.

Bridge conventions: physics components are registered for serialization (`registerPhysicsComponents`) so world hashes and saves
include rigid bodies; mesh ids have synthetic bounds (headless has no GPU meshes): mesh `m` is a sphere of radius `0.5 + 0.5 * (m %
4)` at the entity origin, `-1` is no mesh. Fixed step 1/60 s. The camera starts at (0, 2, 10).

`bent list --actions` prints the full table. Summary:

| Action | Parameters |
| --- | --- |
| `spawn` | `shape` (sphere, box, capsule, hull, compound, none), `pos`, `size` (radius or vec3 half extents), `height`, `rot`, `scale`, `dynamic`, `mass`, `restitution`, `friction`, `vel`, `gravityScale`, `mesh`, `material`, `name`, `tag`, `layer`, `parent`, `sleeping`, `track` (portal ghosting), `light` |
| `plane`, `light` | static plane collider; point / spot / directional light entity |
| `move`, `teleport`, `setRotation`, `setScale`, `setName`, `setTag`, `setLayer`, `setVisible`, `destroy`, `reparent` | entity edits (`target`) |
| `setVelocity`, `impulse`, `force(target, f, ticks)`, `wake`, `gravity(g)`, `solver(iterations, sleepVelocity, sleepTime)` | physics |
| `step(n)` | n fixed ticks |
| `camera(pos, yaw, pitch, roll)`, `cameraWorld(pos)`, `cameraWalk(delta, steps)` | camera; cameraWalk moves in steps, crossing portals (portal transform applied to the remaining walk) |
| `portalPair(a, aEuler, b, bEuler, half, twist)` | portal pair with arbitrary rigid ends (yaw pitch roll) |
| `saveLoad`, `saveRoundtrip`, `snapshot(slot)`, `restore(slot)`, `snapshotText(slot)`, `restoreText(slot)`, `compact`, `clear` | persistence |
| `rebase(delta)`, `floatingOrigin(threshold, snap)` | floating origin |
| `streaming(cell, nx, nz, load, keep, perCell)` | deterministic spatial streaming of a grid of cells with `perCell` entities each |
| `terrain(tileSize, cells, maxLevel, view, amplitude)` | deterministic terrain streaming over a sine height field |
| `input(keys, mouse, delta, buttons)`, `flyCamera()` | simulated input (GLFW key codes) and the fly camera system |
| `render(key, value)` | `setRenderSetting` |
| `deltaTrack(cell)`, `deltaCapture(cell)`, `deltaReload()`, `deltaApply(cell)`, `deltaRelease(cell)` | delta store |
| `navObstacle(center, half)`, `navBuild(half, cell, radius)` | nav mesh on a floor with box obstacles |
| `validate()`, `seed(n)`, `timeScale(s)`, `pause(paused)` | misc |

## Observations

Observations read the current world (the innermost `after` / `always` / `eventually`); using one outside is an error. They are
ordinary functions (`map(entities(), position)` works). `bent list --observations` prints them all:

* entities: `entities() spawned() entityCount() liveCount() alive(e) handle(e) name(e) tag(e) parent(e) children(e) components(e)`
* transforms: `position(e) worldPosition(e)` (absolute, double: origin + world translation) `rotation(e) scale(e) bounds(e) aabb(e)`
* physics: `velocity(e) isDynamic(e) sleeping(e) energy() kinetic() maxSpeed() impulses() physicsRaycast(from, to)`
* whole world: `hasNaN() worldHash()` (engine `hashWorldState`) `contentHash()` (slot independent) `relativeHash()` (everything but
  positions) `worldErrors()` (`World::validate`) `validatorIssues() validatorWarnings() validatorErrors()`
* queries: `raycast(from, to) overlap(center, radius)`
* camera and portals: `cameraView()` ({pos, local, front, up, right, cell}) `cameraPos() portalFrame(i) portalCount()
  portalCrossings() portalViewError() portalPingPongs()`
* large world: `origin() time() frame() ticks()`
* streaming: `resident(cell) residentCells() cellCount() cellBox(cell) cellDistance(cell) loadRadius() keepRadius() streamStats()
  terrainStats()`
* nav: `navPath(from, to) navWalkable(p) navObstacles() navRadius() navCell()`
* misc: `renderSetting(key) renderSettingNames() deltaJson() deltaCount() inputHeld(key)`

World-free host functions: `jsonDump(v) jsonPretty(v) jsonParse(text)` (`{ok, value, error}`, through `Engine.Json`) and
`replayMatches(prog)` (records input while running prog, replays the recording into a second engine with input actions removed,
compares world hash and camera).

## Writing a law

1. Pick the invariant and the smallest world that exhibits it. Prefer **metamorphic** laws (two programs that must agree:
   with and without a save/load, rebase, extra no-op, split step) and **reference** laws (engine result versus a brute force or
   analytic computation written in Bent, e.g. `raycast` versus `bruteRay`, free fall versus the integrator formula).
2. Write generators that make every random case meaningful: bound speeds below what the property assumes, place bodies on a
   lattice when overlap would break the premise, use `where` only for rare rejections (too many discards end in `GAVE-UP`).
3. Use `~= within` with a tolerance derived from float precision, not a magic number that hides drift.
4. Run with a few thousand cases: `bent check FILE --law NAME --cases 5000`. A failure prints the minimal program, the notes of
   the failed comparisons (`a == b is false`, which `forall` element failed, which tick an `always` failed at) and the regression
   path.

Example (tests/bent/laws/physics.bent):

```
law no_tunnelling_thick_wall {
    forall (r: float(0.05, 1), speed: float(1, 110), y: float(-0.9, 0.9), z: float(-0.9, 0.9), restitution: float(0, 1))
    where speed / 60 < 2.0
    => always after([spawn(shape: "box", pos: vec3(10, 0, 0), size: vec3(1, 5, 5), dynamic: false, mesh: -1),
                     gravity(g: vec3(0, 0, 0)),
                     spawn(shape: "sphere", pos: vec3(0, y, z), size: r, vel: vec3(speed, 0, 0), restitution: restitution),
                     step(n: 120)]) {
        len(spawned()) < 2 or worldPosition(entity(1)).x < 9.0
    }
}
```

## How shrinking works

Generators never call a random number generator: they draw bounded integer **choices** from a tape. A fresh case fills the tape
from the seeded PRNG; replaying a recorded tape reproduces the case exactly (missing choices read as 0, oversized ones are clamped,
so any edited tape is still a valid case). Every generator maps choice 0 to its simplest value (0, the empty list, the first
alternative, `false`, identity rotation, the plain value of `nasty`), and list elements are recorded as spans `[continue flag,
element choices]`. Shrinking is therefore generic ("integrated shrinking", as in Hypothesis) and works through `map`, `filter`,
`bind`, lifting and dependent `forall` variables:

1. try the empty tape (everything simplest);
2. delete whole list elements (actions are removed from scene programs this way), last first;
3. zero whole spans (simplify a sub-value, e.g. an action's arguments);
4. delete blocks of 8, 4, 2, 1 choices;
5. minimise each choice: try 0, then smaller shifted values, then binary search between the accepted value and the next larger.

A candidate is accepted when it is shorter or lexicographically smaller (shortlex) and still fails the same way (a false
property stays false; an error stays an error of the same kind, e.g. a crash stays a crash). Candidates are evaluated in parallel
batches and the first accepted one in candidate order wins, so shrinking is as deterministic as the search. Passes repeat until
nothing improves or the attempt / time budget runs out. Finally the minimal tape is replayed in explain mode: its bound values are
printed with the canonical printer (`print(v)` parses back to an equal value), and the regression file is written:

```
# Bent regression: shrunk counterexample for law `...` (tests/bent/laws/....bent)
# Found by: bent check ... (seed 1, case 37)
# Failure: property is false
#   tests/bent/laws/...: 0.0 == 1.0 is false
include "../laws/physics.bent"

example the_law_name {
  p = [spawn(...), step(n: 3)]
}
```

`bent check` on a regression file (or `bent suite`) runs its example. To record a known, unfixed bug, change `example name {` to
`example name known "short reason" {`: the suite then expects it to fail and reports `FIXED?` when it starts passing.

## Adding actions and observations

Both live in `src/Engine/Bent/`:

* Action: add an `ActionSpec{name, params, doc}` to `engineActionSpecs()` (EngineHost.cpp) and a branch in `EngineSim::apply`
  reading arguments with `ArgReader` (`a.vec("pos", default)`, `a.f`, `a.integer`, `a.flag`, `a.str`, `a.quat`). Call `onTick()`
  after every tick you simulate (that is what `always` hooks into) and stop when it returns false. Register created entities
  with `addRef` so programs can address them; remap references when slots change.
* Observation: add an `ObservationSpec` to `engineObservationSpecs()` (EngineObserve.cpp) and a branch in `EngineSim::observe`.
  Observations must not change the world (laws assume evaluating them is pure); convert with `fromVec`, `fromDVec`, `fromQuat`,
  `Value::record`, and return entities with `refOf(handle)`.
* Pure helpers that need no world: `program.native(name, fn)` in `EngineHost::addBuiltins`.
* A different host (another subsystem, a fake for tests): implement `bent::Host` and `bent::Sim` (see the counter host in
  `tests/BentTests.cpp`).

## GPU mode

`bent check FILE --gpu` / `bent suite --gpu --laws tests/bent/laws/gpu` enable the `rxGpuShot` natives (src/Engine/Bent/Ext_Gpu.cpp). A law
asks for a frame with `rxGpuShot({scene, settings: [[key, value]], env: [["LIT_...", v]], camera: [x,y,z,yaw,pitch,roll], width, height,
frames, run})`; the frame is rendered by LitShot (the Engine rendering path of src/Shot, `--deterministic`, 320x180 by default) in a child
process, one at a time (shared GPU), retried on failure, and the result is a record of frame statistics (mean / std luminance, black and white
fractions, gradient energy, blobScore, image hash, log error and warning counts, bindless on, upload MB per frame). `rxImageDiff(a, b)`
compares two shots (diffFrac, meanAbs, 16x9 block luminance difference). Results are cached per spec; add `run: n` for an independent render.
Persistent out-of-device-memory discards the case (the device is shared) instead of failing it. Without `--gpu` laws under a `gpu/` directory
are skipped; with it and no LitShot / device the exit status is 77. ctest: `BentGpuLaws` (SKIP_RETURN_CODE 77); the default `BentLaws` never
runs them. Laws: tests/bent/laws/gpu/*.bent (frame health, clean log, determinism, upload bound, sky, graph / bindless / render-scale /
resolution / cluster-LOD equivalence, exposure monotonicity, debug views, hostile settings, feature switches, portal blobs). Thresholds there
were set from LitShot output but could not be calibrated on a busy device; treat first GPU failures as calibration candidates.
Build LitShot first (`ninja LitShot`): the laws use build-release/src/Shot/LitShot (or `LITSHOT=...`).

## Render, settings and tooling laws (ext_render_*.bent, lib/render.bent)

Natives `rx*` in src/Engine/Bent/Ext_Render.cpp drive real engine code with no GPU:
* `ext_render_settings` (18): write / dump / reload round trips, nan / inf / junk rejection, clamp table, enum range, int saturation.
* `ext_render_governor` (13): ladder monotone, single steps, hysteresis, cooldown, no oscillation under constant load, recovery, nan samples.
* `ext_render_projection` (13): reversed-Z endpoints and formula, pinhole xy, frustum containment, orthographic, invariances.
* `ext_render_material` (7): packMaterial clamps, identity, textures, NaN.
* `ext_render_cluster` (14): meshlet DAG validity, error monotone, bounds, determinism, crack-free cuts, hierarchy == flat cut, soup fuzzing.
* `ext_render_graph` (11): culling vs a reference, ordering, aliasing lifetimes, pool reuse and leaks (fake backend).
* `ext_render_slots` (7): texture slot allocator vs a model.
* `ext_render_params` (12): ParamBus loader totality, limits, convergence, curves.
* `ext_render_scene` (11): scene text corruption, validator stability, frame explainer.
Fixed: setRenderSetting accepted nan / inf (and UB float->int, out-of-range enums); QualityGovernor froze for good on a NaN frame time;
packMaterial passed NaN through std::clamp; explainFrame sorted NaN weights; step response curves returned the previous value at a knot.
Gaps: light packing, cascade fitting, Hi-Z, oblique clip, surface parser and the editor palette / autopilot are not reachable headless
(Diligent / Editor-only code); only the GPU laws touch them indirectly.

## Findings

First campaign (2026-11-24): 77 laws in 12 files; seed 1 with 3,000 cases per law, seeds 2 and 3 with 10,000 cases per law, on
6-7 threads, plus a fork-per-case (`--isolate`) run of the robustness laws and targeted reruns: about 1.6 million cases in total
(a failing law stops at its first failing case and shrinks it). Every counterexample below is the shrinker's output.

### Fixed (each has a passing regression in tests/bent/regressions)

| Law | Minimal counterexample | Cause and fix |
| --- | --- | --- |
| `streaming_evicts_beyond_keep` | `streaming(cell: 20, nx: 2, nz: 2, load: 20, keep: 20)`, camera at (40.5, 0, 0): cell 0 stays resident 20.5 m away | `PortalSystem::addCell` pins the camera cell to 0 and only `PortalSystem::step` (portals only) moves it, so Engine's streaming system treated grid cell 0 as the camera cell forever: it was always loaded and never evicted. `Engine::enableStreaming` now locates the camera with `cellAt` when there are no portals (EngineFeatures.cpp). |
| `save_load_then_simulate` | one dynamic `spawn(shape: "hull")`, `saveLoad()`, `step(n: 1)` | `ConvexHullCollider` had no serializer: save / load, text snapshots and the save system silently dropped hull colliders. Serializer added (vertices, rebuilt with `makeConvexHull`, Physics.cpp). |
| `degenerate_scale_stays_finite` | two zero-scale entities, `reparent(target: 0, parent: 1, keepWorld: true)` | `World::setParent(keepWorld)` multiplied by `inverse(parentWorld)` of a singular matrix: NaN local matrix. Now keeps the previous local matrix when the inverse is not finite (World.cpp). |
| `nasty_programs_keep_world_consistent` | parent scale 1e30 with an unnormalised rotation, child scale 1e-45 | `World::validate` reported "stale world cache" because cached and recomputed matrices overflowed to inf differently. Non-finite entries are now skipped (WorldValidate.cpp). |

### Known (regressions marked `known`; `BentLaws` expects them to fail)

| Law | Minimal counterexample | Issue |
| --- | --- | --- |
| `render_setting_rejects_nonfinite` | `render(key: "timeOfDay", value: "nan")` | `setRenderSetting` accepts nan / inf for float settings (the parser does not reject non-finite values; only some keys are clamped). |
| `no_body_escapes_arena` | 15 lattice bodies, gravity (0.78, -16.8, -3), a 0.75 m box at 15 m/s downward | Deep transient penetration under load: at tick 70 the box's centre is 0.25 m below the floor surface (1 m inside a 2 m static wall). Discrete solver, 2 iterations. |

### Not bugs (laws corrected, documented in the law files)

* Save / load changes `hashWorldState`: `World::clear` bumps generations on purpose so pre-load handles never alias.
* Engine settings (gravity) are not part of a world snapshot.
* Rebase round trips lose sub-ulp offsets (1 mm at 32 km) and colliding bodies amplify 1-ulp shift differences chaotically.
* JSON dump replaces invalid UTF-8 with U+FFFD (nlohmann replace handler).
* Hidden entities are not in the spatial index, so queries skip them.
* A nav path requested from inside an obstacle starts at the requested point; walkability is per cell, so path points may lie up
  to one cell inside an obstacle footprint.
* A ray shorter than the distance to the target misses it (law bug, fixed).

## Physics, character, nav, AI, animation, IK and audio laws (ext_phys_*)

Laws in `tests/bent/laws/ext_phys_{queries,dynamics,character,portal,nav,ai,anim,ik,audio}.bent`, helpers in `tests/bent/lib/ext_phys.bent`.
They use pure builtins (`src/Engine/Bent/Ext_Physics.cpp`, `Ext_Sim.cpp`): every call builds its own standalone world or subsystem object,
so cases stay independent and deterministic. `pxRun(scene)` (bodies of every collider kind, steps, gravity, layers matrix, kicks, portals,
`roundtrip` save/load), `pxCast(scene, query)` (ray, sphere / capsule / box sweeps, overlaps, portal ray and sweep), `pxChar` (character
controller script), `pxPortalFrames`, `navEval` / `navCrowd`, `aiRun`, `skelSample` / `skelBlend` / `skelModel` / `skelAnimate` /
`skelTwoBone`, `skinCheck` / `skinNormalize`, `faceNames` / `faceMorph` / `faceClipSample` / `faceCombine`, `ikSolve`, `audioRender` /
`audioTrace` / `audioSched`. `bent list --builtins` prints them.

* Queries: ray / sweep / overlap against signed-distance references (sphere tracing), brute-force triangles for meshes and rotated, scaled
  height fields, oriented boxes for hulls; skip equals removal; translation and order invariance; sweep never later than the ray.
* Dynamics: determinism, finiteness, momentum and energy of elastic collisions, bounce height, friction bounds, stacking, layers, plane /
  mesh / terrain resting, orientation policy, static bodies, contact events, sleeping, no tunnelling below the speed bound, save/load.
* Character: no penetration, floor, speed bound, jump apex, determinism. Portal: velocity and spin carry, portal rays and sweeps equal
  the unfolded world, missed openings.
* Nav: path validity, endpoints, A* cost monotone under obstacles, symmetry, incremental rebuild equals fresh build, links, crowd overlap.
* AI: behaviour trees against a reference interpreter, determinism, seed use, blackboard writes, utility trace scores, FSM tables.
* Animation: clip sampling, looping, blends, masks, forward kinematics, crossfade, two bone IK, skinning against an independent LBS,
  morph linearity, bounds, weight repair, ARKit mapping. IK: FABRIK / CCD / analytic, limits, degenerate input. Audio: gain bounds, mute,
  linear bus gain, attenuation, ducking, propagation, hops, voice scheduler.

Findings (fixed): `capsuleBox` (alternating projection, 4 rounds) missed overlaps up to ~0.15 m deep; `TriMeshCollider` and
`HeightfieldCollider` were not serialized (silently dropped by save / load); `normalizeSkinWeights` left all-zero weights when the weight sum
overflowed.
Findings (fixed): static friction coefficient capped at 4x dynamic (`px_low_friction_box_coasts`); FABRIK / CCD unfold a chain folded on the target line (`ik_target_on_first_bone_line_is_reached`); the `px_portal_*` mismatches were law bugs (the helpers ignored the pair twist, and rays could be blocked by the real collider). Known: ORCA agents can overlap 0.25 m or more (`nav_crowd_overlap_is_small`).
By design: box sweeps use square-cornered inflated boxes (conservative); heightfield normals always face up; partial paths are
direction dependent; ORCA is a soft constraint.

## World and core laws (tests/bent/laws/ext_world_*.bent, helpers tests/bent/lib/ext_world.bent)

206 laws (204 hold, 2 are `known` regressions). Bridge additions live in `src/Engine/Bent/Ext_World.cpp` (new actions such as
`setWorldPosition lookAtTarget rotateBy addComponent prefabCapture timerAfter busSubscribe jobWorkers rngDraw sceneMutate spawnBatch`,
observations such as `worldMatrix worldMatrixRef frustumQuery frustumBrute subtreeHash timerLog busLog entityEvents`, and world-free natives
`rngSeq rngService textTableRun jobsRun asyncRun paramRun curveEval replayRoundtrip utf8Encode gizmo*`).
`cover("label", cond)` records that a case exercised a branch; `bent check --coverage` prints the hit rates and a law whose label is never hit
over 20+ cases is reported `VACUOUS` (a failure), so guarded laws cannot silently test nothing.

| File | Laws about |
| --- | --- |
| ext_world_entities | handle generations, dead handle reads, destroy = exact subtree, slot reuse, clear/compact, link symmetry, acyclic reparenting, keepWorld matrices, entity events, batch create / destroy |
| ext_world_transforms | world matrix cache vs recomputation, parent composition, rotation/scale read-back and normalisation, negative / zero / extreme scale, setWorld*, lookAt, rotateBy, bulk setPositions |
| ext_world_meta | tag / name / layer indexes vs scan, names and tags through persistence, sparse-set component pools, prefab capture / instantiate |
| ext_world_spatial | overlap / raycast / frustum vs brute force across index rebuilds (compact, load, restore, rebase, workers), screen rays, physics ray casts vs analytic sphere / box / hull |
| ext_world_persist | snapshot / restore, scene text fixpoints, rebase + save, damaged and old-version scene files |
| ext_world_hash | worldHash ignores no-ops and changes with every kind of edit; stateDump exposes hashed changes |
| ext_world_json | depth limit 512, NUL, strict grammar and number grammar, unicode escapes, duplicate keys, jsonDiff / jsonFind |
| ext_world_services | PCG32 (reference answers, uniformity, streams), TextTable model, JobSystem / AsyncJobs, ParamBus and curves, gizmo math, replay files |
| ext_world_time | timers (after / every / cancel / owner / pause / scale), event bus model, pause and time scale |
| ext_world_origin | toWorld / toLocal, repeated rebases, queries after rebase, floating origin snap grid |
| ext_world_determinism | dumps, worker counts (incl. parallel spatial refresh and scene parse), replay with timers and streams |

### Findings (world / core campaign)

Fixed: `WorldSnapshot` ignored the floating origin (a restore after the origin moved shifted every entity: the editor's play / stop did this) now records
and re-applies it; scene load accepted a `hidden` render flag inconsistent with the visible field (World::validate failed after loading a damaged file);
bridge `restoreText` left references dead and aliased reused slots.
Fixed: `\n` in names and tags is escaped in scene text (old files still load); `Pcg32::rangeInt(INT_MIN, INT_MAX)` covers the full range.
By design (documented in the laws): child order is not preserved by a load; rotation / scale no-op writes move the last float bit; screen ray origin
is on the near plane; 1e400 is a JSON parse error.
Gaps: capsule ray casts (axis convention unconfirmed), scripts, delta store, rare large-scale suites (use --cases 5000).

## Portal, camera, streaming, terrain, instancing and save laws (`ext_portal_*.bent`)

Natives (pure functions, each builds its own PortalSystem / Camera / ResidencyManager / StreamingSystem / World / SaveSystem; sources
`src/Engine/Bent/Ext_Portal*.cpp`, helpers `tests/bent/lib/ext_portal.bent`): `xm*` matrices (lists of 16), `xp*` portals, `xc*` camera
orientation, `xr*` residency and worker pool, `xs*` streaming system, `xt*` terrain, `xi*` instancing, `xd*` deltas, `xv*` save system.

| File | Laws | Covers |
| --- | --- | --- |
| `ext_portal_transform` | 19 | pair transforms are mutual inverses, rigid, map end frames, twist is a rotation about the far normal, crossing round trip retraces, rings of n pairs compose to identity, view matrix identical after the swap for upright / tilted / rolled / floor / ceiling / half-turn pairs, real `step` crossings |
| `ext_portal_camera` | 20 | ViewOrientation: orthonormal basis, roll about front, rotate / inverse / many rotations (no drift), canonical fold, decompose round trip, relax (keeps view, converges, no overshoot, pitch excess, at once, no-op), transformBy, setRotation |
| `ext_portal_frame` | 15 | buildFrame: root is the camera, hard cap 24 views, soft maxViews / maxDepth for small portals, tree + unique keys, node view = transform chain, oblique clip plane, masked frusta, determinism, blocked / behind / empty cases |
| `ext_portal_query` | 14 | raycast vs brute force (both ends), overlap, straddling twins, cell parameter blending (convex, far, continuous), light images (brute force, mirrored image, switches, one hop) |
| `ext_portal_json` | 15 | JSON round trip fixpoint, acceptance oracle, truncations, garbage, wrong types, shape containment / nesting / Lipschitz / crossing, frame mesh validity and bounds |
| `ext_portal_stream` | 26 | residency score (monotone, bias, at rest formula, velocity), byte budget and eviction order, commit queue budget, worker pool priority + cancellation, spatial streaming (radii, determinism across workers, prime, hysteresis, cancellation accounting, frame upload budget, byte budget, LOD bands), BFS graph distances |
| `ext_portal_terrain` | 23 | height function, tile samples / edges / collider, tile selection (disjoint, cover, 2:1 balance, finest under camera, translation invariance), scatter (tiling independence, keys, determinism), splat, streamer visibility |
| `ext_portal_instancing` | 18 | InstanceBatch model-based edits, versions and dirty ranges, pack / unpack, hash, LOD monotone, pool allocator (best fit, no overlap, reuse, clear), spatial cells, CPU cull (cell = flat, distance, behind camera, LOD) |
| `ext_portal_save` | 9 | delta rebuild (also in another origin), idempotence, removed sets, save resume equals straight run, damaged files rejected, failed apply rolls back, atomic files and backups |

Findings of this campaign: **fixed** `PortalFrameMesh.cpp` `outerContour` used the unclamped bevel, so a bevel wider than the frame pulled
the outer ring into the opening corner (regression example in `ext_portal_json.bent`). **Fixed**: `portal_params_continuous_beside_the_opening` and `cam_relax_stays_finished`. **By design, documented in the laws**:
`byteBudget 0` is unlimited; byte-budget enforcement lags the wanted set by up to two updates; the terrain streamer settles over about four
updates after a move; twisted rectangular pairs let an object enter A outside B's frame; ceiling portal one-frame GPU difference and one-hop
light are not headless-observable. Gaps: save-version migrations (only version 1 exists), GPU portal parity, observation GPU ids.

## Memory safety

One check, `memory_safe`, asks whether any generated program can make the engine touch memory it does not own. Neither half below is
a proof: "no loop can overflow, ever" is undecidable for C++ in general. What you get is a dynamic search plus a static pattern scan.

**Dynamic (`bent memsafe`, law `memory_safe` in `tests/bent/laws/memsafe.bent`).** The whole law suite, with its fuzz generators (including
the hostile `nastyProgram` ones), runs in a sanitizer build of `bent` (`-DLIT_SANITIZE=address,undefined`, which also enables
`-fsanitize=bounds`, signed overflow and null checks, plus `_GLIBCXX_ASSERTIONS` / `_LIBCPP_HARDENING_MODE_EXTENSIVE` on the Engine
target). `memsafe` forces `--isolate`, so every case is its own process: an ASan/UBSan abort or assertion kills one case, its sanitizer
log (`log_path`, first 14 lines) becomes that case's failure message, and the run prints a `MEMSAFE` line with the exact
`bent replay FILE --law L --seed S --case K --isolate` command. The verdict line is `memory_safe: ok|FAIL`; ordinary law violations are
reported but are not memory-safety hits. MSan is not used (needs an instrumented libstdc++ and every dependency). `memsafe` exits 77
when the binary is not sanitizer-instrumented.

    cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLIT_SANITIZE=address,undefined
    flock /tmp/claude-1001/lit-build.lock ninja -C build-san -j2 bent      # heavy: full sanitized engine
    build-san/src/Bent/bent memsafe --laws tests/bent/laws --cases 10 --seed 1 --jobs 2
    (cd build-san && ctest -R BentMemSafe)     # skips with 77 if build-san/src/Bent/bent is missing; never builds it

It finds only what the generated programs reach. A bug behind an action nobody generates, or needing a size the generators never draw, is
invisible. Raise `--cases`, change `--seed`, or add generators/laws to widen the search.

**Static (`scripts/memsafe_scan.sh`, ctest `MemSafeStatic`).** Runs `clang++ --analyze` (core, cplusplus, security, unix, deadcode),
clang warnings (`-Warray-bounds -Wstringop-overflow -Wsign-compare -Wconversion -Wdangling -Wuninitialized ...`) and, when installed,
clang-tidy (bugprone-*, cert array/overflow checks, cppcoreguidelines-pro-bounds-*) over `src/Engine`. Findings are normalised to sorted
`file:check` lines (no line numbers, one per occurrence) and compared with `scripts/memsafe_baseline.txt`; only NEW findings fail (ratchet).
Clang cannot read GCC's module BMIs, so translation units that `import` a project module cannot be analysed: only the non-module ones are
scanned and the rest are listed in `$MEMSAFE_OUT/skipped.txt`. Exit 77 without clang++. A clean scan means "no known bad patterns in the
scanned files", nothing more.

    scripts/memsafe_scan.sh                    # fail on new findings
    scripts/memsafe_scan.sh --list             # current findings by check
    scripts/memsafe_scan.sh --update-baseline  # accept the current findings (commit the baseline diff, review it first)
