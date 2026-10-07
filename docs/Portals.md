# Portals

`Engine.Portal` is a game-agnostic cell and portal graph with recursive portal rendering. Cells and portals are data. Loops and impossible topology are allowed.

## Model

- A cell is a named AABB with parameters: gravity, tint, `sun` and `ambient` lighting scale, and free scalars. All cells share one global coordinate frame, so cell geometry is ordinary world content placed apart from other cells.
- A portal pair is two oriented quads (`pose`: right, up, normal, position; `half` size). Each end carries `transform`, a rigid map from its cell space to the partner's: `T = poseB * twist * flip180(up) * inverse(poseA)`. The normal faces the cell the viewer stands in. Scale is fixed at 1.
- Impossible topology comes from the transforms, not from placement. Three rooms in a loop where the last pair turns 90 degrees return the walker to a different place and heading (`PortalTests`, W-1).
- World data is JSON (`PortalSystem::loadJson` / `parseJson` / `toJson`): `settings`, `cells[]` (`name`, `min`, `max`, `gravity`, `tint`, `sun`, `ambient`, `scalars`), `portals[]` (`name`, `a`/`b` with `cell`, `position`, `rotation` yaw/pitch/roll degrees, `size` half extents, optional `twist` degrees).

## Rendering

`PortalSystem::buildFrame(camera, viewportHeight)` produces a `PortalFrame`: a BFS tree of view nodes. Node 0 is the camera. Each node owns:

- a virtual camera (`T * cameraWorld` chained through the portals),
- an oblique projection that replaces the near plane with the destination portal plane (Lengyel, reversed-free `ZO` form), so geometry behind the exit is cut. It falls back to the plain projection when the virtual eye is within 1 cm of that plane,
- six cull planes: four through the virtual eye and the destination quad edges, the exit plane, and far. This is the frustum clipped to the portal quad,
- a stable `key` (portal path) used to keep per-view GPU history,
- `surfaces`: visible portals of the node's cell (front facing or within the near-plane reach, inside the node frustum, optional caller occlusion predicate) with the child node index, or `-1` for the degraded case.

Budget: `maxDepth` (default 3) and `maxViews` (default 16, hard cap 24). Candidates are sorted by projected size so the largest portals get views first. Portals beyond the budget, or smaller than `minPixels`, render as a flat cell tint (graceful degradation). Cells behind portals that are out of frustum, back facing, or reported blocked are never visited.

`Renderer::renderPortalNode` renders nodes children first. Each view reuses the full pipeline:

1. per-view visibility bits and per-view Hi-Z history (two Hi-Z textures per view key, bits copied in and out of the shared buffer), two-phase cull with the clipped planes, G-buffer draw, Hi-Z, retest draw,
2. AO, lighting and clouds-off shading with the view's cell `sun`/`ambient` scale; cascaded shadows and SSR/SSGI are off in portal views (shadow cascades belong to the main camera),
3. children composited, transparency, then the HDR result is copied into a per-view texture.

The parent view composites each portal with `portal_composite.frag`: a per-pixel ray/quad test that samples the child texture and writes the plane depth, so objects in front of the portal still occlude it. The ray test covers rays that start behind the plane, which is what makes the camera crossing seamless: near-plane pieces that pass the plane already show the destination.

Per-view cost is exposed through `Engine::portalStats()` (`label`, `depth`, `drawn` object count, GPU milliseconds). Draw counts fall when a portal leaves the frustum.

## Camera crossing

`PortalSystem::step(camera)` switches cell when the near-plane centre crosses a portal quad (segment test, so it is tunnelling safe) and applies the transform to the camera. The image is identical on both sides of the switch because the destination was already being rendered from the same virtual camera. `Camera` is yaw/pitch, so only portals whose transform keeps up vertical can be crossed; rolled portals are view only.

## Straddling objects and queries

- `track(world, entity)` registers an object. `updateObjects` moves it through a portal when its centre crosses, and while its bounding sphere overlaps a portal it keeps a hidden-or-visible twin entity at `T * world` in the destination cell (W-4). Twins are excluded from queries.
- `raycast` and `overlapSphere` run on the world spatial grid and continue through portals, applying the transform (`PortalRayHit::crossings`). The grid tests bounding spheres, so thin occluders are conservative.

## Cell parameters

`sampleParams(cell, position)` blends the cell's parameters toward each neighbour behind a portal within `blendDistance`: weight 0.5 at the plane, 0 at the distance. Gravity is rotated by the portal transform, so the sampled vector is continuous across a crossing in the camera's frame (W-5). Weights are clamped to a total of 1.

## Test scenes

`buildPortalScene(portals, world, kit, PortalTestScene::Loop | Torture)` creates lit rooms with distinct colours, glowing portal frames and a tracked probe sphere.

- `Loop`: cells A, B, C, a 90 degree twist on the C to A link. Walk east three times.
- `Torture`: self-facing hall (infinite regress), a rolled pair, and a free-standing window into a second room that looks back into the hall.

Editor: `LIT_SCENE=portal_loop` or `portal_torture`, or commands `scene.portalLoop` / `scene.portalTorture` (autopilot: `run scene.portalLoop`). `scripts/portal_shot.txt` is a screenshot script.

## Tests

`build/tests/PortalTests` (headless, CHECK macro): transforms and loop, oblique clip, frame recursion and budget, crossing continuity, raycast/overlap through portals, straddling twins, parameter blending, JSON round trip and rejection.

## Not done

Shadow cascades and SSR inside portal views, per-cell shadow maps, streaming and world deltas (W-6, W-7), audio/navigation links, portal-aware physics sweeps.
