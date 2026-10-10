# Physics and character

Backend choice: `docs/adr/Physics.md`.

## Queries (`Engine.Physics`)

`physicsRaycast`, `physicsSweepSphere`, `physicsOverlapCapsule` over `SphereCollider`, `BoxCollider`, `PlaneCollider`, each with an optional skip filter. `Collision` events carry `closingSpeed`.

## Portal layer (`Engine.PortalPhysics`)

- `portalSweep` / `portalRaycast` / `portalOverlap` continue through portals applying the transform (`crossings`, final `point`, `direction`, `cell`).
- `installPortalPhysics` rotates a tracked body's velocity when it crosses.
- `stepPortalPhysics` = `stepPhysics`, `updateObjects`, then ghost twin collision: a straddling body's twin pushes the real body out of geometry on the far side (boxes use the inscribed sphere).
- Engine uses it for the `physics` system whenever portals are active.

## Character (`Engine.Character`)

`Engine::enableCharacter(settings, rigSettings)` adds the `character` system (WASD, shift, space, mouse look when captured, V toggles view, T toggles capture). Headless use: construct `CharacterController`, call `setInput`, `look`, `update(dt)`.

- Capsule, substepped, depenetration against colliders, step up (`stepHeight`), ground snap, slope limit, jump.
- Gravity from `PortalSystem::sampleParams` (per cell, blended across portal planes); `up` follows the field. The camera stays upright; only physics follows `up`.
- Crossing: the first person near-plane point (third person: eye) is tested against portal quads each substep. Position, velocity, up and yaw take the portal transform; camera cell switches in the same step. Capsule also collides with the far-side geometry through the portal (contacts behind the exit plane are ignored).
- Camera via `CameraRig`: first or third person, runtime toggle keeps position. Third person sweeps a sphere back through portals; if it crosses, the camera is placed and oriented by the transformed trace.
- Impacts (PH-5): a dynamic body hitting the character collider above `impactMinSpeed` gives a clamped velocity shove, a decaying camera kick, and `setImpactHook`.

## Limits

Rolled portals are not crossable (camera is yaw/pitch). Characters are not blocked by dynamic bodies. Thin or rotated geometry needs the backend change in the ADR.

## Tests

`build/tests/PhysicsTests`: crossings at 4 angles x 3 speeds in both views with camera continuity, thrown body through a portal with twin and far-side collision, sweep/raycast/overlap through a portal, steps, slopes, jump, gravity fields and blending, rig toggle and collision, impact.
