# Inverse kinematics and procedural walking

Module `Engine.IK` (src/Engine/IK). Solvers live in `Engine.IK.Solvers` and are pure math with fixed-size scratch (no per-frame allocation). The `walkers` and `ik` systems run in `Phase::Update` right after `animation`, so IK overrides animated poses before transforms are consumed by rendering.

## A spider in ten lines

```cpp
import Engine.IK;

World& world = engine.world();
SpiderOptions options;
options.legs = 8;
options.legLength = 1.2f;
options.bodyMesh = sphereMesh;
options.boneMesh = cubeMesh;
const EntityHandle spider = makeSpider(world, glm::vec3(0.0f), options);
setWalkerTarget(world, spider, glm::vec3(0.0f, 0.0f, -8.0f));
setWalkerInput(world, spider, glm::vec2(0.0f, 1.0f));
engine.run();
```

`setWalkerInput` is WASD-like (x right, y forward, mapped through `Walker::inputRight/inputForward`; point them at the camera axes for camera-relative control). It overrides the move target while non-zero. One-line recipe: `makeWalkerFromRecipe(world, pos, "quadruped length=1.0 speed=1.2")` (kinds: spider, hexapod, quadruped, biped; keys: legs, segments, length, radius, bodylength, speed, stride, step, thickness).

## Components

- `IkChain` (on the root joint): `joints` root..tip, `target` entity or `targetPoint`, optional `pole`/`polePoint`, `weight`, `iterations`, `algorithm` (Auto = analytic two-bone for 3 joints without hinges, else FABRIK; or TwoBone, Fabrik, Ccd) and per-bone `limits` (`hinge` with min/max radians around the bend axis, or `cone`). Solved only when target, pole, root, tip or weight changed (dirty skip; `updateIk` returns solved/skipped counts). Create with `createIkChain(world, joints, target)`.
- `Walker` (on the body): `legs` (IkChain entity plus foot target marker), `stride`, `stepHeight`, `maxSpeed`, `turnRate`, smoothing rates, `maxTiltDegrees`, `ground` (optional `bool(position, height&, normal&)`; default raycasts `BoxCollider`/`PlaneCollider`/`SphereCollider` via `physicsRaycast` below the foot).
- Gait: legs are grouped so same-side neighbours and the mirrored leg never swing together; a leg steps when its planted foot is more than `stride` from its home, lands slightly ahead of home, follows a sine arc, and swing time scales with speed. Planted feet are fixed in world space (no sliding). The body slows when a planted foot nears full leg extension, and height/tilt follow a plane fitted through the feet (steps and slopes), smoothed by exponential damping.
- Both components are serialized (`IkChain`, `Walker`).

## Existing hierarchies

`makeWalkerFromHierarchy(world, body)` treats children of `body` whose name starts with `leg` as hips and follows the first non-visual child path to the tip (tag `ik.visual` marks visuals to skip). `detectLegs` returns the joint paths.

## JSON rigs

```json
{"name":"s","legs":8,"segments":2,"legLength":1.2,"walker":{"maxSpeed":2.0},
 "chains":[{"joints":["a","b","c"],"target":"goal","pole":[0,2,0],"algorithm":"fabrik","weight":1,
            "iterations":12,"limits":[{"hinge":true,"min":-90,"max":0}]}]}
```
`loadRigJson(world, json, position)` builds a spider when `legs` is present, or converts the entity named by `hierarchy` (`pattern` selects the leg prefix); `chains` attach extra chains by entity name. `rigToJson(world, root)` exports the walker tuning and all chains below `root`.

## Editor commands

`ik.spider` (spawn a walking spider), `ik.chain` (chain from selection: several joints, or one root with its first-child path; creates a draggable target), `ik.walker` (selected hierarchy with `leg*` children becomes a walker), `ik.debug` (toggle drawing of bones, targets, poles, planted/swinging feet and stride rings through `Engine::debugLine`). Spawned rigs are not undoable yet.

## Solver API

`IkChainState` holds up to `kMaxIkJoints` points, bone lengths and limits. `solveFabrik`, `solveCcd`, `solveTwoBoneAnalytic`, `solveIk` take a target and `IkSettings` (iterations, tolerance, pole, base direction). Unreachable targets yield a straight, fully extended chain pointing at the target.
