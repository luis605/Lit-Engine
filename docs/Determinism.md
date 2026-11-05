# Determinism contract

The simulation is a pure function of: the initial state (scene, params, seed), the per-tick `dt`, and the per-tick input. Wall-clock time, thread scheduling, pointer values and container hash order must never influence simulation state.

## World-state hash

`hashWorldState(world, HashSources)` in `Engine.WorldHash` (`src/Engine/Tools/WorldHash.*`) returns a 64-bit FNV-1a digest of:

- frame counter and live entity count;
- every live entity in index order: index, generation, name, tag, layer, mesh, visibility, parent, child count, local position/scale/rotation, component text from the registered serializers, script payloads;
- the `RandomService` seed and every named stream (state and increment);
- optionally the `StateStore`, `StoryletEngine` and `ParamBus` contents, the `NavMesh::hash()`, and caller supplied `extra` hashes (AI `BehaviorRunner::stateHash()`, audio voice schedule, rig pose, nav agents).

Floats that go through `StateHasher::addFloat` are quantised to `kHashQuantum` (1e-4); non finite values hash to a sentinel; quaternions are sign canonicalised. Component and script text from serializers is hashed verbatim, so it is stable across runs and builds but not guaranteed across compilers with different float formatting or FMA contraction.

## Replay checkpoints

A replay file may carry `hashinterval N` and `hash <tick> <hex>` lines. `tick` is the number of ticks already simulated (0 is the initial state). `ReplayVerifier` compares checkpoints.

Headless CLI:

    Headless --scene s.litscene --ticks 600 --record run.replay --hash-interval 30
    Headless --scene s.litscene --verify-replay run.replay

`--verify-replay` replays the input, recomputes every checkpoint, prints `MISMATCH tick ...` for each divergence and exits 1 on any mismatch or missing checkpoint.

## Rules for simulation code

- Randomness only from `world.random().stream(name)` (Pcg32). No `rand`, `random_device`, time seeds.
- Never iterate `std::unordered_map/set` where order affects state or output; use `std::map`, a sorted key vector, or sort results by entity index.
- Parallel work (`Jobs::parallelFor`) must write disjoint data and merge in a fixed order. The parallel spatial refresh partitions by cell and processes each partition in entity order, so bucket contents do not depend on thread timing.
- Float reductions run in a fixed order; no parallel sum without fixed chunking.
- Initialise all members. Use `dt` from the tick, never the clock.

## Fixed nondeterminism sources

- `World::runScripts` iterated an `unordered_map` of scripts: script update order depended on hash order and insertion history (and differed after a save restore). Now sorted by entity index.
- `World::overlapSphere` and `World::findByTag` returned entities in bucket order, which depends on insertion/removal history. Results are now sorted by entity index.

## Tests

`tests/DeterminismTests.cpp` runs a scripted simulation (physics, behaviour tree, nav agent, storylets, voice scheduler, two bone IK, params) repeatedly and checks identical hashes every 30 ticks; saves at tick 150 and restores into three fresh worlds with different seeds, requiring identical hashes afterwards.
