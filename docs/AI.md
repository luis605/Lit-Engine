# AI, navigation, characters, audio

Headless, deterministic CPU modules. All take explicit `Pcg32` streams (`RandomService::stream("ai.<name>")`) and plain data, so replays reproduce.

## Behavior graphs (`Engine.AI`)

`BehaviorGraph::loadText/loadFile` parses JSON; `BehaviorRunner(graph, actions, rng).tick(dt)` runs it. State lives in the runner; `Blackboard` holds `Json` values (`importParams` copies `Engine.Params`). Actions are `ActionRegistry::add(name, fn(AiContext&) -> Status)`.

```json
{"name": "a", "blackboard": {"hunger": 0.2},
 "root": {"type": "selector", "children": [
   {"type": "utility", "name": "needs", "select": "best", "stickiness": 0.05, "options": [
     {"name": "eat", "considerations": [{"input": "hunger", "curve": [[0,0],[1,1]]}], "node": {"type": "action", "action": "eat"}}]}]}}
```

Node types: `sequence`, `selector` (`reactive`), `parallel` (`policy` all|any), `inverter`, `succeeder`, `failer`, `repeat` (`count`), `wait` (`seconds`), `condition` (`when`), `set` (`value`|`add`), `action`, `utility`, `fsm`.

- Conditions: `{"key","op","value"}` (`value` may be `{"key": other}`), `all`, `any`, `not`.
- `utility`: `select` best|weighted (`power`, rng), `combine` product|min|max|average|sum, `stickiness`, `minScore`, `reconsider`, per option `weight`, `gate`, considerations `{input,min,max,curve,mode}`.
- `fsm`: `initial`, `states{name:{node,transitions[{to,when,after}]}}`, `any` global transitions, targets `$success`/`$failure`.

## Decision trace

`runner.trace()` records every utility scoring (all options, considerations, scores, winner, reason), fsm transitions and action results per tick. `toJson`, `toText`, `writeFile(path, asText)`; `forTick`, `lastWinner`. Capacity and `setEnabled` bound its cost.

## Navigation (`Engine.Nav`, `Engine.Geometry`)

Custom tiled voxel navmesh (no Recast dependency): triangle soup from `collectColliderGeometry(World)` or any `TriangleSoup`, rasterized per tile into walkable spans with agent height, climb, slope and radius erosion. `NavMesh::buildFromWorld`, `setGeometry`, `rebuildAll`.

- Dynamic: `addObstacle/moveObstacle/removeObstacle` dirty only overlapping tiles; `rebuildDirty(maxTiles)` rebuilds them and bumps `version()`.
- Off-mesh links: `addLink({a,b,radius,bidirectional,cost,userData})`, `setLinkEnabled`. Use them for portals: path points carry the link id.
- Queries: `findPath` (A*, partial paths, line-of-sight smoothing), `nearestPoint`, `walkable`, `lineWalkable`.
- Agents: `NavAgent`, `navSetTarget`, `navStepAgent(s)` (arrival slowdown, separation, automatic repath when the mesh version changes, `NavLinkTraverseFn` hook for teleporting links).

## Characters (`Engine.Rig`, `Engine.Face`)

- `Skeleton`, `Pose`, `SkeletalClip`/`SkeletalLibrary`, `blendPoses`, `blendPosesMasked`, `addPose`, `subtreeMask`, `SkeletalAnimator` (crossfade, 1D blend space with phase sync, masked or additive overlay), `applyPoseToEntities(World, ...)`.
- IK: `solveTwoBone` (pole, weight), `solveFootIk` (pelvis drop, per-leg planting via a `GroundFn`), `solveLookAt` (weighted chain, angle limit).
- Face: `kArkitBlendshapes` (52 names), `BlendshapeClip` (JSON curves, `fps` frame arrays), `BlendshapePlayer`, `EyeSystem` (gaze, saccades, blinks, gaze-shift blinks; `applyTo` writes eyeLook*/eyeBlink*).

## Audio (`Engine.Audio`, `Engine.AudioTrace`)

See `docs/adr/Audio.md`. `AudioMixer` (buses, ducking, param-bound gains, spatial voices, reverb), `VoiceScheduler`, `AudioPropagation` (BVH occlusion, reflections, reverb estimate, ray budget, hop hook), Null and WAV backends.

## Tests

`build/.../tests/SystemsTests` (CHECK style, headless).
