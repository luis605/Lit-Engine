# Physics and character

Backend choice: `docs/adr/Physics.md`.

## Colliders and queries (`Engine.Physics`)

Colliders: `SphereCollider`, `BoxCollider` (axis aligned), `CapsuleCollider` (Y axis, `radius`, `halfHeight` of the cylinder), `CompoundCollider` (extra sphere/box/capsule parts with offsets, added to the entity's own collider), `ConvexHullCollider` (`makeConvexHull(points)`, up to 128 points; follows the entity rotation and scale), and static surfaces `PlaneCollider`, `HeightfieldCollider`, `TriMeshCollider` (`makeTriangleMesh(vertices, indices)`, own BVH, two sided).

Queries: `physicsRaycast`, `physicsSweepSphere`, `physicsSweepCapsule` (sphere sweeps along the axis), `physicsSweepBox` (conservative stepping on `physicsOverlapBox` plus bisection), `physicsOverlapCapsule`, `physicsOverlapBox`, each with an optional skip filter. `Collision` events carry `closingSpeed`. Hull sweeps use planes offset by the radius (corners are slightly conservative).

Static surfaces take the full entity matrix: heightfields and triangle meshes may be rotated and scaled (rays exact for any scale; sphere/capsule queries use the geometric mean of the scale for distances). Mesh contacts: sphere and capsule by closest point (capsule also handles segments crossing a triangle), box and hull by SAT against each triangle (edge axes must win by 5 percent to avoid seam bumps).

## Dynamics (`stepPhysics`)

Lite solver: positional correction plus velocity impulses, `solverIterations` passes. Bodies translate only; they keep their orientation (hull parts use the entity rotation as is). A body with a single `SphereCollider` spins: `RigidBody::angularVelocity` is integrated into the entity rotation.

- Pairs: any part against any part (sphere, box, capsule, hull); dynamic compound and hull bodies collide with each other, with primitives, planes, heightfields and static triangle meshes.
- Friction: Coulomb, `RigidBody::friction` (dynamic) and `staticFriction` (stick), mixed with `sqrt(a * b)`; surfaces use their `friction` (negative = 0.5). Stick also cancels the slip integrated this step so bodies do not creep on slopes. Spheres exchange slip for spin (solid sphere inertia), so a sliding ball turns into a rolling one (5/7 of the initial speed).
- Rolling resistance: `RigidBody::rollingResistance` (torque = coefficient * radius * normal impulse) on spinning spheres.
- Sleeping uses the larger of linear speed and surface spin speed.

## Portal layer (`Engine.PortalPhysics`)

- `portalSweep` / `portalRaycast` / `portalOverlap` continue through portals applying the transform (`crossings`, final `point`, `direction`, `cell`).
- `PortalCast::transform` is the product of the portals crossed (`transformVector`), so callers can carry velocities through a sweep. `portalOverlapCapsule` tests a segment on both sides of a portal plane.
- `installPortalPhysics` rotates a tracked body's velocity and angular velocity when it crosses (any shape; the crossing is segment based, so fast bodies do not tunnel).
- `stepPortalPhysics` = `stepPhysics`, `updateObjects`, then ghost twin collision: a straddling body's twin pushes the real body out of geometry on the far side (boxes use the inscribed sphere).
- Engine uses it for the `physics` system whenever portals are active.

## Character (`Engine.Character`)

`Engine::enableCharacter(settings, rigSettings)` adds the `character` system (WASD, shift, space, mouse look when captured, V toggles view, T toggles capture). Headless use: construct `CharacterController`, call `setInput`, `look`, `update(dt)`.

- Capsule, substepped, depenetration against colliders, step up (`stepHeight`), ground snap, slope limit, jump.
- Gravity from `PortalSystem::sampleParams` (per cell, blended across portal planes); `up` follows the field (`upResponse`), the view frame follows `up` keeping the view direction and its leftover roll decays over `uprightSeconds` (0.3 s; 0 = up and view snap to gravity-up at once). The eye and the third person boom hang off `up`.
- Crossing: the first person near-plane point (third person: eye) is tested against portal openings each substep, for any rigid transform (rolled, pitched, wall-to-floor, flipped). Position, velocity, up and the view frame take the portal transform (the look angles stay, so the view is continuous); camera cell switches in the same step. Capsule also collides with the far-side geometry through the portal (contacts behind the exit plane are ignored).
- Camera via `CameraRig`: first or third person, runtime toggle keeps position. Third person sweeps a sphere back through portals; if it crosses, the camera is placed and oriented by the transformed trace.
- Impacts (PH-5): a dynamic body hitting the character collider above `impactMinSpeed` gives a clamped velocity shove, a decaying camera kick, and `setImpactHook`.

## Limits

Characters are not blocked by dynamic bodies. Dynamic boxes, capsules and compounds are not rotated by collisions (only lone spheres spin). Triangle meshes are static only and two sided (a body can be pushed through a thin shell by a deep tunnel). Hull, compound and mesh colliders are not serialized (data is shared); `CapsuleCollider`, `CompoundCollider`, friction fields and angular velocity are. Capsule against mesh, heightfield and hull uses closest-point iteration or sample spheres, not exact swept capsules.

## Tests

`PhysicsShapesTests`: mesh raycast/sweep/overlap with rotation and scale, dynamic bodies on meshes, slope friction, compound and hull bodies, capsule colliders, Coulomb friction and rolling, rotated and scaled heightfields, portal sweeps with transform, capsule overlap through portals and shape-independent velocity/spin carry. `build/tests/PhysicsTests`: crossings at 4 angles x 3 speeds in both views with camera continuity, thrown body through a portal with twin and far-side collision, sweep/raycast/overlap through a portal, steps, slopes, jump, gravity fields and blending, rig toggle and collision, impact.
