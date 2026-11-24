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

## GPU

There is no `--gpu` mode: the laws drive the simulation headless, where the renderer is not initialised. Rendering laws (portal
view parity by pixels, render graph invariants under fuzzed settings) would need an `EngineSim` that opens a window and reads back
images; that is future work. Render settings are still fuzzed through `RenderSettings` (render.bent).

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
| `snapshot_restore_identity`, `snapshot_then_simulate` | `p = []`, `q = [step(n: 1)]` | `World::captureSnapshot` / `restoreSnapshot` do not include `TimeState`: after a restore the frame counter and elapsed time keep their post-snapshot values, so the world hash (and anything timed) differs. Entity content does round-trip (`snapshot_then_simulate_content` holds). |
| `no_tunnelling_thick_wall` | sphere r = 0.05 at 75 m/s (1.25 m per tick) into a 2 m static wall | No continuous collision detection: a body that lands past the wall's centre in one tick is pushed out of the far side. `no_tunnelling_half_thickness` (speed below half the thickness per tick) holds. |
| `render_setting_rejects_nonfinite` | `render(key: "timeOfDay", value: "nan")` | `setRenderSetting` accepts nan / inf for float settings (the parser does not reject non-finite values; only some keys are clamped). |
| `no_body_escapes_arena` | 15 lattice bodies, gravity (0.78, -16.8, -3), a 0.75 m box at 15 m/s downward | Deep transient penetration under load: at tick 70 the box's centre is 0.25 m below the floor surface (1 m inside a 2 m static wall). Discrete solver, 2 iterations. |
| `nasty_programs_keep_world_consistent` (second case) | keepWorld reparent under a parent of scale 7.6e-10 x 1.5e-6 x 0.5 | `World::validate` reports a stale world cache for an ill-conditioned hierarchy: cached and recomputed matrices differ by rounding beyond the check's tolerance (a validator false positive, not real staleness). |
| `nasty_programs_survive_persistence` | one entity with `scale: vec3(inf, 0, 0)`, `saveLoad()` | `World::saveScene` writes non-finite local matrices (here `-nan`) that `loadScene` rejects, so the saved file cannot be loaded back. |

### Not bugs (laws corrected, documented in the law files)

* Save / load changes `hashWorldState`: `World::clear` bumps generations on purpose so pre-load handles never alias.
* Engine settings (gravity) are not part of a world snapshot.
* Rebase round trips lose sub-ulp offsets (1 mm at 32 km) and colliding bodies amplify 1-ulp shift differences chaotically.
* JSON dump replaces invalid UTF-8 with U+FFFD (nlohmann replace handler).
* Hidden entities are not in the spatial index, so queries skip them.
* A nav path requested from inside an obstacle starts at the requested point; walkability is per cell, so path points may lie up
  to one cell inside an obstacle footprint.
* A ray shorter than the distance to the target misses it (law bug, fixed).
