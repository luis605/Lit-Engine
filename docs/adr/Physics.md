# ADR: physics backend (PH-1)

Status: proposed, pending user approval.

## Candidates

| | Jolt | PhysX | Bullet | Custom (`Engine.Physics`) |
|---|---|---|---|---|
| Concave static, convex/compound dynamic | yes | yes | yes | spheres, boxes, planes only |
| Character controller | yes (CharacterVirtual) | yes | weak | own (`Engine.Character`) |
| Determinism | cross-platform deterministic option | no | no | deterministic, single thread |
| Threading | job system | yes | limited | none yet |
| License | MIT | BSD-3 | zlib | own |
| Win/Linux/macOS | yes | yes (heavy build) | yes | yes, no dependency |
| Maintenance | active | active, large | slow | ours |

## Decision

Keep and extend the custom solver for now. Jolt remains the recommended backend for concave and compound dynamics (PH-4).

- Jolt is not installed here and the repo vendors no third-party physics. Vendoring it is a network and CI change that cannot be validated on this machine, so PH-1 "builds on all three platforms" cannot be shown.
- Everything the P0 requirements need (portal-aware queries, ghost twins, kinematic controller, gravity fields, camera collision, impact feedback) is a layer above the collider set. A backend does not provide portal logic; Jolt would still need the same layer.
- The layer is written against three primitives only: `physicsRaycast`, `physicsSweepSphere`, `physicsOverlapCapsule` (plus `stepPhysics`). Replacing the backend means reimplementing those.

## Consequences

- No concave or compound dynamics until Jolt is vendored (PH-4 blocked).
- Box colliders are axis aligned. Slopes use `PlaneCollider`.
- Swept spheres use inflated boxes (exact on faces, conservative on edges and corners).
