# Observation, state, storylets, save, camera rig

All modules are game agnostic and headless; none are wired into `Engine::tick`, so add them as systems or call them from the game loop. Tests: `tests/NarrativeTests.cpp`.

## Engine.Observation

`ObservationSystem(world, config)` tracks entities (`track`/`untrack`) and gives each `Unobserved`, `Seen` or `Attended`. `ObservationChanged{entity, from, to, time}` is emitted on `world.events()`.

```cpp
obs.setView(observationViewFromCamera(world.camera(), aspect));
obs.update(dt);
obs.state(e); obs.attended(); obs.focusEntity();
```

- Focus is a cone (`focusConeDegrees`) around the camera forward in both camera modes. `setIgnore(e)` removes the player body from occlusion.
- GPU path: the renderer reads back a low resolution object-ID grid (value `entity.index + 1`, 0 empty) and calls `submitGpuIds(frameIssued, issuedTime, w, h, ids)`, stamping with `obs.frame()` and `obs.time()`. A sample older than `maxLatencySeconds` or `maxLatencyFrames` is rejected and counted. The renderer pass that produces the grid is not implemented yet.
- CPU fallback (used headless, with no fresh sample): view frustum test, then a line-of-sight ray through `World::raycast` against bounding spheres, then the cone test. Deterministic.
- Hysteresis: a state change needs the new raw level to hold for `seenEnter`, `attendEnter` (up) or `seenExit`, `attendExit` (down); the cone widens by `focusExitScale` while attended.
- Latency (last, mean, max, frames, rejected) is in `stats()` and logged every `logIntervalSeconds`; rejected readbacks log a warning.

## Engine.State

`StateStore(bus)` holds typed variables: `range` (min, max, default, clamped), `flag`, `counter` (integer, clamped), `set` (string members). `StateChanged{name, kind, oldValue, newValue, member, added}` fires only on a real change.

```json
{ "vars": { "bond": {"type": "range", "min": 0, "max": 1, "default": 0.25},
            "capacity": {"type": "counter", "min": 1, "max": 6, "default": 3},
            "met": {"type": "flag"}, "habits": {"type": "set"} } }
```

`loadSchema` validates the whole file (unknown keys, bad ranges, default outside range) and keeps the old schema on error. API: `get`, `set`, `add`, `setFlag`, `addMember`, `removeMember`, `hasMember`, `toJson`, `fromJson`.

## Engine.Storylet

`StoryletEngine(state, random, bus)` loads storylets (`loadText`, `loadFile`) with the schema below; references to unknown variables are load errors.

```json
{ "storylets": [ {
  "id": "coffee",
  "when": {"all": ["bond >= 0.2", {"location_tag": "home"}, {"not_played": "coffee"}]},
  "priority": 40, "cooldown_seconds": 1200, "once": false, "delivery": "walking_talk",
  "lines": [{"speaker": "a", "text_id": "coffee_a", "voice_id": "coffee_a", "seconds": 2}],
  "effects": ["bond += 0.05", {"remember": "habit.coffee"}] } ] }
```

- Conditions: `all`, `any`, `not`, arrays (all), `"var op number"` (`>= <= > < == !=`), `"flag"`, `"!flag"`, `{"set": "member"}` (or list, any), `played`, `not_played`.
- Effects: `"var = x"`, `+=`, `-=`, `{"remember": key}` (adds to the `memory` set), `add_member`, `remove_member` (`{"set":..., "member":...}`). Effects and the played record apply when the last line ends.
- The highest priority eligible storylet plays; equal priority is broken with the `storylet` RNG stream. A higher priority storylet interrupts a playing one (`interruptible`, default true).
- `interrupt(reason)` ends the current line cleanly (`StoryletLineEnded.interrupted`); the storylet resumes at that line after a 1 s retry delay. `cooldown_seconds` counts from completion; `once` blocks replay.
- Events: `StoryletStarted`, `StoryletLineStarted`, `StoryletLineEnded`, `StoryletInterrupted`, `StoryletCompleted`. No dialogue UI is involved; consumers play `text_id` and `voice_id`.
- `update(dt)` uses sim time only, so replays are exact.

## Engine.Save

`SaveSystem({world, state, storylets, params, observation})` writes one JSON file: `{format, version, checksum, body}`. Body holds the world snapshot, time, camera, RNG seed and every stream state, state variables, storylet history (including the playing line), params (values and targets), observation timers, and registered `addSection(name, save, load)` blobs for game data.

- `save(path, label)` writes `path.tmp`, fsyncs, copies the old file to `path.bak`, then renames: a crash never leaves a partial slot.
- `load(path)` verifies format, version and checksum before touching anything; newer versions are rejected, older ones go through `addMigration(from, fn)`.
- Autosave: `configureAutosave(dir, interval, slots)`, `autosave(dt)` each tick, `loadLatestAutosave()` falls back to older slots when the newest is corrupt.
- Saving mid-run and loading into a fresh process continues bit-identically (tested over 600 ticks).

## Engine.CameraRig

`CameraRig(world, settings)` drives `world.camera()` from a target entity: first person at `target + eyeHeight`, third person `thirdDistance` behind along the camera forward (optional `shoulderOffset`). `toggle()` keeps the eye position; with no target it keeps the free camera position. Third person raycasts from the eye backwards (`World::raycast`, ignoring the target hierarchy; replaceable with `setCollisionQuery`), stops `collisionRadius` before a hit and never goes below `minDistance`. Orientation stays on `Camera` (`look()` clamps pitch), so attention uses the same forward in both modes. Call `update()` after moving the target.
