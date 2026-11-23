# Portals

`Engine.Portal` is a game-agnostic cell and portal graph with recursive portal rendering. Cells and portals are data. Loops and impossible topology are allowed.

## Model

- A cell is a named AABB with parameters: gravity, tint, `sun` and `ambient` lighting scale, and free scalars. All cells share one global coordinate frame, so cell geometry is ordinary world content placed apart from other cells.
- A portal pair is two oriented quads (`pose`: right, up, normal, position; `half` size). Each end carries `transform`, a rigid map from its cell space to the partner's: `T = poseB * twist * flip180(up) * inverse(poseA)`. The normal faces the cell the viewer stands in. Scale is fixed at 1.
- Impossible topology comes from the transforms, not from placement. Three rooms in a loop where the last pair turns 90 degrees return the walker to a different place and heading (`PortalTests`, W-1).
- World data is JSON (`PortalSystem::loadJson` / `parseJson` / `toJson`): `settings` (`maxDepth`, `maxViews`, `blendDistance`, `minPixels`, `occlusion`, `tintLighting`, `lightTransport`), `cells[]` (`name`, `min`, `max`, `gravity`, `tint`, `sun`, `ambient`, `scalars`), `portals[]` (`name`, `a`/`b` with `cell`, `position`, `rotation` yaw/pitch/roll degrees, `size` half extents, optional `twist` degrees).

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
2. shadows: sunless (indoor) cells skip cascades entirely. Sunlit views share one set of sun cascades per frame where they can (see "Shared sun cascades"); a view the shared set does not cover refits and renders its own. Local lights use the shared virtual shadow map atlas: pages are requested from each view's depth and cached across views and frames,
3. local lights are clustered per view (`light_cull.comp`, cluster AABBs built from the x/y rows of the projection, so the oblique depth row does not distort them) and filtered by cell: a view gathers only lights of its own cell (including the portal images of neighbouring cells' lights, see "Light through portals") and cell-less lights (`SceneUniforms::cellParams.x`, per-light cell ids from `Engine` via `Renderer::setLightCells`; only cells that own a portal filter, portal-less grid cells never do). VSM page requests walk the same lists, so other cells' lights request no pages,
4. AO, GI / colour bleed, lighting (cell `sun`/`ambient` scale and colour grade, no clouds), SSR, probes, planar and world-space reflections, in the same stage order as the main view (see below),
5. children composited, transparency, then the HDR result is copied into a per-view texture.

The parent view composites each portal with `portal_composite.frag`: a per-pixel ray/quad test that samples the child texture and writes the plane depth, so objects in front of the portal still occlude it. The ray test covers rays that start behind the plane, which is what makes the camera crossing seamless: near-plane pieces that pass the plane already show the destination.

### Screen-space effects and the portal plane

The portal quad is never geometry for screen-space effects. Each view keeps a copy of its depth and G-buffer (base colour, normal/roughness, material) after its own portals were resolved. Before the parent shades, `portal_resolve.frag` splices the child's depth (reprojected into the parent projection) and G-buffer (normals rotated into the parent's virtual space) into the parent's depth and G-buffer inside each portal quad, except where parent geometry is in front of the plane. Order per view (`Renderer::shadeScene`, `shadePortalView`):

1. resolve: depth + G-buffer of the room behind each portal,
2. GTAO and lighting (stages 0 and 1): AO horizons, contact shadows and sky visibility march over the room behind the portal; the portal pixels themselves are shaded but overwritten later,
3. colour-only composite (`Portal Composite Color PSO`, no depth write): the child-lit image goes into HDR so the colour pyramid has the right colours,
4. stage 2: colour pyramid, screen-space GI / colour bleed, SSR, planar and world reflections. Rays and gathers hit the child room, never the plane,
5. final composite (plane depth written): overwrites any change stage 2 made on portal pixels, so they keep the child-lit colour exactly, and gives transparency the plane as occluder,
6. after transparency the spliced depth is copied back (`portalEffectsDepth`), so god rays, the mask flag in tonemapping, custom post effects and later passes also see the room behind the portal.

Degraded portals (flat tint, no child view) have no child data and keep the old behaviour.

### Light through portals

A lamp in cell B lights cell A through an open portal. Every frame `Engine` (`portalLights` in `Engine.cpp`) asks `PortalSystem::lightImages(cell, position, range)` for each collected light of a portal-owning cell: for every portal end of that cell the light is in front of and whose quad its range sphere touches, the light gets an image in the partner cell, `T * pose` with the end's transform (position, spot direction / tube axis), same colour, range and type. Images are appended after the collected lights (up to `kMaxPortalLights` = 1024), carry the receiving cell as their cell (so the cell filter hands them to every view of that cell: the main view, and portal views of the same cell at any depth) and the receiving end's quad as their aperture (`LightAperture`, `Renderer::setLightApertures`).

- Aperture: stored in the light buffer at `kMaxLights + (index - first)` (`LIGHT_APERTURE_BASE` in `lights.glsl`, first portal light in `cellParams.y`). `lightAperture()` returns the transmission of the segment receiver to light through the quad: 0 unless the receiver is on the cell side of the plane and the light behind it, then a 3 cm soft edge at the quad border. Shading (`evaluateLights`, toon lights) and VSM page requests use it, so receivers that do not see the light through the opening are neither lit nor request pages.
- Shadows: images are ordinary VSM lights. Their faces carry the portal plane as a fifth clip distance (`VsmFace.clipPlane`, `vsm_caster.vert`) and `vsm_cull.comp` drops casters entirely behind it, so only casters on the receiving side shadow the image (the wall around the opening does, geometry that sits behind the portal plane in the receiving cell's space does not).
- The portal quad itself is never a receiver or occluder for these lights: the resolve splices the room behind each quad into the parent's G-buffer, and those spliced pixels lie behind the plane (transmission 0), then the composite overwrites them anyway.
- One hop within the depth budget: images exist when `maxDepth >= 1` and `settings.lightTransport` (JSON `lightTransport`, default true); an image never produces further images (two hops would need two apertures).
- Self-linked cells (torture hall) get images of their own lamps through the other end, which is what the portal shows.

### Shared sun cascades

The sun direction is the same in every cell (one world frame, one directional light), so cascades are view-independent apart from their fit. `Renderer::beginSharedCascades` keeps the main camera's fit of the frame. A sunlit portal view whose visible region (frustum up to its own shadow distance, clipped to its cell box, `PortalViewNode::cellMin/cellMax`) lies inside the last shared cascade samples the shared set (`Lighting::useCascades`, `cellParams.z` = view shadow distance switches `sunShadow` to "finest cascade that contains the point"): the first such view renders it, later ones (and the main view) skip the Sun Cascades pass and mark no cascade casters. A sunlit view outside the shared set refits and renders its own set, which evicts the shared one (the next view that needs it renders it again). `PortalViewStats::sunCascades / sharedCascades` (and `LitShot --stats`, `portalView` lines) report which views rendered or reused cascades.

### Cell lighting grade

The cell `tint` also grades the lighting of every view of the cell: sun illuminance and ambient (SH irradiance, prefiltered environment, reflection fallbacks; `SceneUniforms::cellTint`) are multiplied by `mix(1, tint / max(tint), tintLighting)`. `settings.tintLighting` (JSON, default 0.35) sets the strength, a cell scalar `tintLighting` overrides it per cell; a grey tint grades nothing. Local lights are not graded. Degraded portals keep showing the flat tint.

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

LitShot: `--testscene portal_loop` (static view from room A through AB), `portal_torture` (walks), `vsm_mixed` (loop rooms plus characters, cluster rocks and many lights; `LIT_MIXED_SUN=1` makes every cell sunlit). Editor: `LIT_SCENE=portal_loop` or `portal_torture`, or commands `scene.portalLoop` / `scene.portalTorture` (autopilot: `run scene.portalLoop`). `scripts/portal_shot.txt` is a screenshot script.

## Tests

`build/tests/PortalTests` (headless, CHECK macro): transforms and loop, oblique clip, frame recursion and budget, crossing continuity, raycast/overlap through portals, straddling twins, parameter blending, JSON round trip and rejection.

## Not done

Light transport beyond one hop and occluders on the source side of a portal (an image is not shadowed by geometry between the lamp and the portal in its own cell), shared cascades are fitted to the main camera only (a sunlit view far from it renders its own set), VSM face budget (lights get faces main-frustum-first, then by distance, so portal views can miss shadows when the face budget is exhausted), cell `tint` only colours degraded portals (no lighting tint), portal-aware physics sweeps. Streaming, world deltas, navigation links and audio hops: see `docs/Streaming.md`.

Depth is reversed Z: the oblique near-plane clip of child views sets the clip depth row to `w - scale * plane` (`obliqueProjection`), depth comparisons in the portal shaders are inverted (larger = nearer) and a sky/empty child pixel writes 0.
Floating origin: per-view visibility bits and Hi-Z history are view-space data and survive a rebase (camera, cells and portals shift together).
