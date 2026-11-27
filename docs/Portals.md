# Portals

`Engine.Portal` is a game-agnostic cell and portal graph with recursive portal rendering. Cells and portals are data. Loops and impossible topology are allowed.

## Model

- A cell is a named AABB with parameters: gravity, tint, `sun` and `ambient` lighting scale, and free scalars. All cells share one global coordinate frame, so cell geometry is ordinary world content placed apart from other cells.
- A portal pair is two oriented quads (`pose`: right, up, normal, position; `half` size). Each end carries `transform`, a rigid map from its cell space to the partner's: `T = poseB * twist * flip180(up) * inverse(poseA)`. The normal faces the cell the viewer stands in. Scale is fixed at 1.
- Impossible topology comes from the transforms, not from placement. Three rooms in a loop where the last pair turns 90 degrees return the walker to a different place and heading (`PortalTests`, W-1).
- Any rigid pose works: walls, floors and ceilings (pitch +-90), rolled and flipped (roll 180) ends. Crossing, rendering, physics and twins all use the full transform.
- World data is JSON (`PortalSystem::loadJson` / `parseJson` / `toJson`): `settings` (`maxDepth`, `maxViews`, `blendDistance`, `minPixels`, `occlusion`, `tintLighting`, `lightTransport`, `uprightCamera`, `uprightSeconds`, `spill` scale of every pair's spill), `cells[]` (`name`, `min`, `max`, `gravity`, `tint`, `sun`, `ambient`, `scalars`), `portals[]` (`name`, `a`/`b` with `cell`, `position`, `rotation` yaw/pitch/roll degrees, `size` half extents, optional `twist` degrees, and the pair's look: `shape`, `cornerRadius`, `frame`, `edge`, `spill`, see "Frames and edges"). `toJson` writes the authored rotation, so pitched and rolled ends round trip exactly.

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

The parent view composites each portal with `portal_composite.frag`: a per-pixel ray / shape test (`include/portal_shape.glsl`) that samples the child texture and writes the plane depth, so objects in front of the portal still occlude it. The ray test covers rays that start behind the plane, which is what makes the camera crossing seamless: near-plane pieces that pass the plane already show the destination. Nothing in the portal path assumes an upright view or portal: rays come from the inverse view, the oblique near plane and the cull planes come from the destination quad, cluster AABBs use only the projection's x / y rows, normals are spliced with the full parent / child rotation, the VSM clip plane and light apertures are the quad's plane, and the sky of a child view is drawn with the child's (possibly rolled) virtual camera, so it is exactly what the camera sees after crossing.

### Screen-space effects and the portal plane

The portal quad is never geometry for screen-space effects. Each view keeps a copy of its depth and G-buffer (base colour, normal/roughness, material) after its own portals were resolved. Before the parent shades, `portal_resolve.frag` splices the child's depth (reprojected into the parent projection) and G-buffer (normals rotated into the parent's virtual space) into the parent's depth and G-buffer inside each portal quad, except where parent geometry is in front of the plane. Order per view (`Renderer::shadeScene`, `shadePortalView`):

1. resolve: depth + G-buffer of the room behind each portal,
2. GTAO and lighting (stages 0 and 1): AO horizons, contact shadows and sky visibility march over the room behind the portal; the portal pixels themselves are shaded but overwritten later,
3. colour-only composite (`Portal Composite Color PSO`, no depth write): the child-lit image goes into HDR so the colour pyramid has the right colours,
4. stage 2: colour pyramid, screen-space GI / colour bleed, SSR, planar and world reflections. Rays and gathers hit the child room, never the plane,
5. final composite (plane depth written): overwrites any change stage 2 made on portal pixels, so they keep the child-lit colour exactly, and gives transparency the plane as occluder,
6. after transparency the spliced depth is copied back (`portalEffectsDepth`), so god rays, the mask flag in tonemapping, custom post effects and later passes also see the room behind the portal.

Degraded portals (flat tint, no child view) have no child data and keep the old behaviour.

The resolve splices only pixels the composite covers fully (`portalCoverage` in `portal_shape.glsl`, shared by both shaders): the anti-aliased / dissolved rim band keeps the parent's own depth and G-buffer (the frame reveal that continues behind the contour), is lit as the frame, and the composite blends the child over it. Edge effects (refraction, chromatic split, rim glow) only change the composite's colour, which the final composite writes after stage 2, so no screen-space effect ever sees them or the quad.

### Frames and edges

A portal is a hole with a frame, not a decal (`PortalLook` per pair, both ends):

- `shape`: `rect` (default), `ellipse` or `rounded` (`cornerRadius` metres). The quad is the shape's bounding box. Crossing (`crossing()` in `PortalInternal.hpp`, so camera, characters, tracked objects, ray casts and sweeps), the composite and the resolve all use the shape; light apertures and the masked-view stencil keep the conservative quad.
- `frame` (`PortalFrameStyle`, `false` to disable): `buildPortalFrames(portals, world, kit)` creates two entities per end from `buildPortalFrameMesh` (meshes pooled by `portalFrameKey`): the body (flat front ring between the opening and a rounded rectangle `width` outside the quad, inner and outer bevels of `bevel`, outer side) and the reveal (the inner tube, emissive when `emissive` / `emissiveIntensity` are set, else the body material: `color`, `metalness`, `roughness`). The frame sits `depth` in front of the plane, so the opening is a short tunnel and the far side starts at the plane (parallax over the frame thickness), and reaches `back` behind it, so the opening contour always has frame behind it: the feathered edge blends into the reveal, never into whatever lies behind the quad (no halo). Frames are ordinary geometry: they cast and receive shadows, get AO where they meet walls and floors (contact), and are seen through other portals. Without a `mesh` callback in the kit (headless tests) four oriented cube bars stand in. JSON-loaded worlds call `buildPortalFrames` after `loadJson`.
- `edge` (`PortalEdgeStyle`, composite only, spatial, no history): `feather` pixels of analytic anti-aliasing (the distance one pixel right and down, no derivatives), `rim` glow (exposed HDR units, `rimColor`, within `rimWidth` metres of the contour, plus `fresnel` at grazing angles), `distortion` pixels of refraction and `chroma` pixels of chromatic split in the rim band (spatial value noise, slowly moved by time), optional `dissolve` (0..1) of the rim band into the frame.
- `spill` (default 1): the far room's light leaks into the near cell. `Engine` adds per open end a soft proxy spot just behind the opening, aimed into the end's cell, coloured and scaled by `PortalSystem::spillIrradiance` (irradiance at the partner quad from the partner cell's collected lights, cosine to its normal, range window, plus that cell's sun) times a fixed reflectance and the opening area (`settings.spill` scales all of them; `LIT_PORTAL_SPILL=<scale>` sets it for `portal_tilt`). It uses the aperture mechanism of the light images (cell = the end's cell, aperture = its quad, VSM clip plane = its plane), so it only lights and shadows what is in front of the opening, frames included. A CPU estimate keeps it deterministic and free of readbacks; it does not see emissive surfaces or GI of the far room.

The test scenes give each pair a metal frame with a glowing reveal and rim in the pair colour (`accentLook` in `PortalSceneInternal.hpp`); their wall openings sit 3 cm above the floor so the reveal never shares the floor's plane.

### Light through portals

A lamp in cell B lights cell A through an open portal. Every frame `Engine` (`portalLights` in `Engine.cpp`) asks `PortalSystem::lightImages(cell, position, range)` for each collected light of a portal-owning cell: for every portal end of that cell the light is in front of and whose quad its range sphere touches, the light gets an image in the partner cell, `T * pose` with the end's transform (position, spot direction / tube axis), same colour, range and type. Images are appended after the collected lights (up to `kMaxPortalLights` = 1024), carry the receiving cell as their cell (so the cell filter hands them to every view of that cell: the main view, and portal views of the same cell at any depth) and the receiving end's quad as their aperture (`LightAperture`, `Renderer::setLightApertures`).

- Aperture: stored in the light buffer at `kMaxLights + (index - first)` (`LIGHT_APERTURE_BASE` in `lights.glsl`, first portal light in `cellParams.y`). `lightAperture()` returns the transmission of the segment receiver to light through the quad: 0 unless the receiver is on the cell side of the plane and the light behind it, then a 3 cm soft edge at the quad border. Shading (`evaluateLights`, toon lights) and VSM page requests use it, so receivers that do not see the light through the opening are neither lit nor request pages.
- Shadows: images are ordinary VSM lights. Their faces carry the portal plane as an extra clip distance (`VsmFace.clipPlane`, `vsm_caster.vert`) and `vsm_cull.comp` drops casters entirely behind it, so only casters on the receiving side shadow the image (the wall around the opening does, geometry that sits behind the portal plane in the receiving cell's space does not).
- The portal quad itself is never a receiver or occluder for these lights: the resolve splices the room behind each quad into the parent's G-buffer, and those spliced pixels lie behind the plane (transmission 0), then the composite overwrites them anyway.
- One hop within the depth budget: images exist when `maxDepth >= 1` and `settings.lightTransport` (JSON `lightTransport`, default true); an image never produces further images (two hops would need two apertures).
- Self-linked cells (torture hall) get images of their own lamps through the other end, which is what the portal shows.

### Shared sun cascades

The sun direction is the same in every cell (one world frame, one directional light), so cascades are view-independent apart from their fit. `Renderer::beginSharedCascades` keeps the main camera's fit of the frame. A sunlit portal view whose visible region (frustum up to its own shadow distance, clipped to its cell box, `PortalViewNode::cellMin/cellMax`) lies inside the last shared cascade samples the shared set (`Lighting::useCascades`, `cellParams.z` = view shadow distance switches `sunShadow` to "finest cascade that contains the point"): the first such view renders it, later ones (and the main view) skip the Sun Cascades pass and mark no cascade casters. A sunlit view outside the shared set refits and renders its own set, which evicts the shared one (the next view that needs it renders it again). `PortalViewStats::sunCascades / sharedCascades` (and `LitShot --stats`, `portalView` lines) report which views rendered or reused cascades.

### Cell lighting grade

The cell `tint` also grades the lighting of every view of the cell: sun illuminance and ambient (SH irradiance, prefiltered environment, reflection fallbacks; `SceneUniforms::cellTint`) are multiplied by `mix(1, tint / max(tint), tintLighting)`. `settings.tintLighting` (JSON, default 0.35) sets the strength, a cell scalar `tintLighting` overrides it per cell; a grey tint grades nothing. Local lights are not graded. Degraded portals keep showing the flat tint.

Per-view cost is exposed through `Engine::portalStats()` (`label`, `depth`, `drawn` object count, GPU milliseconds). Draw counts fall when a portal leaves the frustum.

## Camera crossing

`PortalSystem::step(camera)` switches cell when the near-plane centre crosses a portal opening (segment test, so it is tunnelling safe) and applies the transform to the camera (`Camera::transformBy`). The image is identical on both sides of the switch because the destination was already being rendered from the same virtual camera: the view matrix after the swap is `view * inverse(T)`, the child node's view (`PortalTests`, tilted crossings, to 1e-4).

`Camera` has a full orientation (`ViewOrientation` in `Engine.camera`): a reference frame (rotation; its +Y is the up that mouse yaw and UP / DOWN movement use, identity = world Y) and yaw / pitch / roll relative to it. A crossing rotates the frame and keeps the angles, so any rigid transform (rolled, pitched, floor / ceiling, 180 degree flips) is exact; a frame that is still upright after the rotation is folded back into identity and yaw, so upright portals keep the classic yaw / pitch state. `getFront / getUp / getRight / getRotation`, `setRotation` (decomposed against the frame), `setOrientation(yaw, pitch, roll)`, `setFrame`, `rotateBy` are the API; view, frustum, culling, shadows, observation and the portal frame builder all work from the view matrix.

Upright relax: after a tilted crossing the free camera (Engine, when no character controller is enabled and `settings.uprightCamera`) calls `relaxFrame(cellUp(cell), dt, uprightSeconds)`: the frame is turned to the cell's gravity-up keeping the view exactly (the difference becomes roll, and pitch beyond +-89 degrees when looking straight up / down), then roll and the excess pitch go to 0 with a critically damped motion (starts and ends smoothly, no velocity jump) that settles in about `uprightSeconds` (default 0.3 s; 0 = at once). The character controller does the same with its own view frame (`CharacterSettings::uprightSeconds`, docs/Physics.md). Mouse pitch beyond the limit may only move back toward it, so the relax never pops. Saves (`Save`: `roll`, `frame`) and scene text (camera line tail `roll qw qx qy qz`) store roll and frame only when present; older files load upright.

## Straddling objects and queries

- `track(world, entity)` registers an object. `updateObjects` moves it through a portal when its centre crosses, and while its bounding sphere overlaps a portal it keeps a hidden-or-visible twin entity at `T * world` in the destination cell (W-4). Twins are excluded from queries.
- `raycast` and `overlapSphere` run on the world spatial grid and continue through portals, applying the transform (`PortalRayHit::crossings`). The grid tests bounding spheres, so thin occluders are conservative.

## Cell parameters

`sampleParams(cell, position)` blends the cell's parameters toward each neighbour behind a portal within `blendDistance`: weight 0.5 at the plane, 0 at the distance. Gravity is rotated by the portal transform, so the sampled vector is continuous across a crossing in the camera's frame (W-5). Weights are clamped to a total of 1.

## Test scenes

`buildPortalScene(portals, world, kit, PortalTestScene::Loop | Torture)` creates lit rooms with distinct colours, glowing portal frames and a tracked probe sphere.

- `Loop`: cells A, B, C, a 90 degree twist on the C to A link. Walk east three times.
- `Torture`: self-facing hall (infinite regress), a 90 degree twisted pair, and a free-standing window into a second room that looks back into the hall.
- `Tilt` (`portal_tilt`): hub H and rooms F, C, R, U, all surface portals (plane 1 cm in front of the wall / floor / ceiling, no hole): `floor` (H east wall to F floor, 90 degrees: walk in, come out of the floor going up), `ceiling` (H ceiling to C floor, ellipse), `rolled` (H north wall to R wall rolled 45 degrees), `flip` (H west wall to U wall rolled 180 degrees). Floor arrows point +X in every room, a tracked probe straddles the floor portal.

LitShot: `--testscene portal_loop` (static view from room A through AB), `portal_torture` (walks), `portal_tilt` (static; `LIT_TILT_WALK=floor|ceiling|rolled|flip` starts 2.5 m before that portal and walks through it at 1.5 m/s along the view, the free camera relaxes upright on the other side), `vsm_mixed` (loop rooms plus characters, cluster rocks and many lights; `LIT_MIXED_SUN=1` makes every cell sunlit). `--camera x,y,z,yaw,pitch[,roll]` takes a roll. Editor: `LIT_SCENE=portal_loop`, `portal_torture` or `portal_tilt`, or commands `scene.portalLoop` / `scene.portalTorture` / `scene.test.portal_tilt` (autopilot: `run scene.portalLoop`). `scripts/portal_shot.txt` is a screenshot script.

Screenshots (`media/portals/`, LitShot `--deterministic`): `before_portal_loop.png` / `before_portal_torture.png` (960x540, before this work: flat glowing bars, axis-aligned boxes around the turned `Back` portal) and `after_*` (generated frames, shapes, rims); `tilt_overview.png` and `tilt_<portal>_from_<room>.png` (960x540, each tilted portal from both sides); `tilt_walk_<portal>.png` (480x270 frames 95, 97, 98, 100, 103, 108 of `LIT_TILT_WALK=<portal>`: before, at and after the crossing, then the upright relax). Frame-to-frame mean luminance difference around the crossings (0..255): floor and rolled / flip before the relax under 1.6 (normal motion 0.4-1.1), the rolled / flip relax then rotates the image on purpose.

## Tests

`build/tests/PortalTests` (headless, CHECK macro): transforms and loop, oblique clip, frame recursion and budget, crossing continuity, raycast/overlap through portals, straddling twins, parameter blending, JSON round trip and rejection, camera roll / frame basics, crossings through every `portal_tilt` portal and back from three offsets with and without roll (view after the swap = child view before, 1e-4; upright relax keeps the view, then ends upright), immediate upright after a 180 degree flip, roll / frame persistence in scene text and saves (and legacy files upright), shapes, look JSON, frame meshes and spill. `PhysicsTests`: falling through a floor portal out of a wall (up follows the transform, relaxes in 0.3 s or at once, view continuity) and a body thrown through a rolled portal.

## Not done

The light spill is a CPU estimate from the far cell's local lights and sun (no emissive surfaces, no far-room GI, no readback of the child image). Light apertures and the masked-view stencil use the quad, not the shape. Light transport beyond one hop and occluders on the source side of a portal (an image is not shadowed by geometry between the lamp and the portal in its own cell), shared cascades are fitted to the main camera only (a sunlit view far from it renders its own set), VSM face budget (lights get faces main-frustum-first, then by distance, so portal views can miss shadows when the face budget is exhausted), cell `tint` only colours degraded portals (no lighting tint), portal-aware physics sweeps. Streaming, world deltas, navigation links and audio hops: see `docs/Streaming.md`.

Depth is reversed Z: the oblique near-plane clip of child views sets the clip depth row to `w - scale * plane` (`obliqueProjection`), depth comparisons in the portal shaders are inverted (larger = nearer) and a sky/empty child pixel writes 0.
Floating origin: per-view visibility bits and Hi-Z history are view-space data and survive a rebase (camera, cells and portals shift together).
