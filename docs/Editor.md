# Editor

`build-release/src/Editor/Editor`, run from `build-release/` so the relative `resources/` paths resolve. The window opens on the stress scene (`LIT_SCENE=showcase` opens the PBR demo instead). Everything the editor can do is a command: it has a name in the Space palette, most have a shortcut, and the status bar always shows the keys that matter right now.

## Getting around

| Input | Action |
|---|---|
| RMB (hold) + WASD, E/Q | fly; Shift is 4x faster, Ctrl 4x slower, the wheel changes speed while flying |
| Alt+LMB (or Alt+MMB) drag | orbit around the selection, or the surface under the cursor when nothing is selected |
| MMB drag | pan |
| Wheel | dolly toward whatever is under the cursor; Shift triples the step |
| F / Shift+F or Home | frame the selection / frame everything |
| Numpad 1, 3, 7 (Ctrl for the opposite side) | front, right, top views, orthographic; orbiting (Alt+drag) out of an axis view returns to perspective, the wheel zooms the ortho view |
| Numpad 5 | toggle orthographic / perspective (an "Orthographic" label shows in the top right) |
| Axis widget (top right) | click a face to snap the view |
| Ctrl+Alt+1..9 / Alt+1..9 | store / recall a camera view |
| Ctrl+Shift+G | toggle the ground grid |

## Selecting

Click selects the one entity under the cursor (lights and cameras by their viewport icon), Shift+click adds, Ctrl+click toggles; box selection starts only once the mouse has moved more than a few pixels while held. `[` and `]` walk siblings, Shift+`[` goes to the parent, Shift+`]` to the first child, Ctrl+`]` selects all children. Ctrl+L selects everything with the same mesh, Ctrl+Shift+L the same material, Ctrl+F finds by name, Ctrl+Shift+F filters the outliner. Ctrl+1..9 stores the selection in a slot, 1..9 recalls it and Shift+1..9 recalls and frames it. Ctrl+A selects the first 100 000 entities (with a toast if the scene is larger).

## Transforming

Press G (move), R (rotate) or S (scale) with the mouse anywhere; the object follows the mouse until Enter/click applies or Esc/RMB cancels. While transforming:

- X, Y, Z lock to that world axis; the same key again switches to the local axis, a third time frees it. Shift+X/Y/Z locks to the plane that excludes the axis.
- Type a number or an expression (`2.5`, `-90`, `2*pi`) for an exact value; Backspace edits it.
- Ctrl inverts snapping (Shift+Tab toggles it permanently); the status bar shows the current step.

Q cycles the on-screen handles (move, rotate, scale, off) for mouse-only work, and backtick toggles local/world space. Arrow keys nudge the selection by the snap step (Shift for 10x). Alt+G/R/S reset position/rotation/scale, T moves the selection to the 3D cursor, End drops it to the surface below.

## Creating and editing

Shift+A opens the add menu at the cursor: shapes and meshes, an empty, point, spot and sun lights, a camera; each item has a one-letter key. New objects land on the surface under the mouse; when the mouse has not been over the viewport (or that point is off screen) they land where the view center ray meets the scene or the ground, else 10 m in front of the camera. Shift+D duplicates and immediately grabs (as in Blender), Ctrl+D duplicates in place, Delete deletes, Ctrl+C/X/V copy/cut/paste at the cursor, F2 renames, Ctrl+G groups under a new parent, P parents the selection to the active entity and Alt+P clears the parent. M assigns an existing material, Shift+M gives the selection its own copy. H hides and shows, Alt+H reveals every hidden entity at any depth (one undo step). Every edit is undoable (Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y); Ctrl+H shows the history timeline, where clicking a step jumps to it.

## Panels

- **Outliner** (O): the hierarchy with filtering (`name`, `tag:x`, `mesh:cube`, `layer:2`, `hidden`), drag-and-drop parenting, inline visibility toggles and lazy rows, so millions of entities stay responsive.
- **Inspector** (I): a floating card next to the selection with transform, render, material (all PBR lobes: clearcoat, sheen, anisotropy, subsurface, transmission, textures) and component fields. The card tries positions around the object and takes the one that covers neither the object nor nearby visible objects and lights and stays clear of the other panels; when nothing fits it docks at the side with a leader line to the object. It scrolls when it is taller than about half the window; tag, layers and components start collapsed. Lights and cameras show only transform and their component (no mesh, material or shader). The Pin button or Ctrl+Shift+I docks it to the right edge; dragging it also pins it.
- **World** (Shift+W): shown in place of the inspector when nothing is selected: sky mode, sun and time of day, sky brightness, clouds, ambient (IBL) intensity, exposure, tonemapper, fog and haze, and the main effect toggles. Edits are undoable like every render-setting change. Its close button hides it until something is selected again.
- **Look** (L, docked on the right below the stats): sky and time of day, sky brightness, exposure, tonemapper and grading, bloom, GTAO, SSR, shadows, fog, debug views (Alt+L cycles them) and a per-pass GPU timing table.
- **Stats** (Ctrl+Shift+T): frame and GPU time, entity, light and selection counts, camera position and projection, under the axis widget; the World card, Look panel and inspector stack below it.
- Tab (zen) hides every panel.

## The palette and the radial menu

Tap Space for the palette: fuzzy search over every command (`dup` finds Duplicate), `>` limits to commands, `@` searches entities by name or `#index`, `=` turns it into a calculator (`=2*pi*3`, Enter copies the result) and `?` shows tips. Ctrl+P opens it too. Hold Space for a radial menu at the cursor and release over an action: with a selection it offers Move, Rotate, Scale, Duplicate, Delete, Focus, Add and Hide; with nothing selected, Add, Select all, Paste, Frame all, World, Ortho, Reveal all and Look. Ctrl+Shift+D toggles the animated time of day and Ctrl+Left/Right move the sun 30 minutes earlier/later. Lights draw as clickable icons in the viewport (bulb, spot cone, sun) and cameras as a camera glyph; selecting one shows its range, cone or frustum. F1 shows the full key map, generated from the command registry so it is always current.

## Shaders and effects

`shader.new` (palette: "New surface shader") takes a name or a path. A bare name (`rusty` or `rusty.surface`) creates `resources/shaders/surfaces/rusty.surface` in the source tree; a path (`tests/editor/mine.surface`) opens that file, or creates it from the template if it does not exist. Either way it assigns it to the selection and hot-reloads it on every save; see [Shaders.md](Shaders.md) for the API. Each custom compute job, draw and post effect has a `Toggle ...` palette command. F9 reloads every shader. Compile errors show up as error toasts (file and line of your source) and in the log; the previous pipeline keeps running, and a surface that never compiled draws with the standard surface until it does.

## Play mode

F5 snapshots the scene and runs scripts, physics and animation; F5 again restores the snapshot. F6 pauses, F7 steps one frame. A colored frame around the viewport shows the state.

## Files

Ctrl+S saves `resources/scene.litscene` (Ctrl+Shift+S asks for a path), Ctrl+O opens, Ctrl+N clears. Scenes include materials, render settings, surface shaders and custom passes, so reopening a level restores its look, not just its entities. The editor autosaves every 60 s by default (`LIT_AUTOSAVE_SECONDS` changes it, 0 disables it); a save only fires after 3 s without input, and only when the scene has unsaved changes. After a crash it offers to recover the autosave.

## Profiling

F10 toggles the CPU profiler overlay, F11 writes a Chrome trace to `resources/trace.json`, and the Look panel lists GPU time per pass. `LIT_PROFILE=1` enables the CPU profiler from launch. The log prints `[startup]` lines with the time spent in each initialization phase.

## Environment variables

| Variable | Effect |
|---|---|
| `LIT_SCENE=showcase` | open the PBR showcase instead of the stress scene |
| `LIT_OBJECTS`, `LIT_MOVING`, `LIT_SEED` | stress scene size (default 8 000 000), GPU-animated count (default 1 500 000), random seed |
| `LIT_CAM_POS=x,y,z`, `LIT_CAM_PITCH`, `LIT_CAM_YAW` | start camera |
| `LIT_FORCE_LOD`, `LIT_LOD_BIAS` | force a LOD level or scale LOD selection |
| `LIT_AUTOPILOT=script.lit` | run a scripted session (below) |
| `LIT_AUTOSAVE_SECONDS` | autosave interval (default 60, 0 disables) |
| `LIT_PROFILE` | start with the CPU profiler on |
| `LIT_NO_SHADER_CACHE` | compile every shader from source |

## Autopilot

Autopilot scripts drive the real editor through the same input paths as a user, one step per line (`#` starts a comment):

| Verb | Meaning |
|---|---|
| `key <Key> [ctrl] [shift] [alt]`, `down <Key>`, `up <Key>` | press and release, hold, release a key |
| `text <string>` | type characters |
| `move <x> <y>`, `click <button> [x y]`, `press <button> [ctrl] [shift] [alt]`, `release <button>`, `wheel <delta>` | mouse; coordinates accept `%` of the window |
| `run <command.id>` | run a command by name |
| `set <setting> <value...>` | set any `RenderSettings` field by name (`shadowCascades 3`, `gradientZenith 0.2 0.3 0.7`); `time`, `ev`, `compensation`, `debug`, `cascades` are short aliases |
| `material <roughness\|metallic\|opacity\|clearcoat> <value>` | edit the active entity's material |
| `pass <name> <0\|1>` | disable or enable a custom pass |
| `save <path>`, `load <path>` | save or open a scene |
| `expect <what> <value...>` | check `selection`, `entities`, `undo`, `mode`, `playing`, `name`, `x`/`y`/`z`, `rx`/`ry`/`rz`, `material`, `visible`, `parent`, `palette`, `zen`, `dirty`, `prompt`, `passes`, `handles` (0 move, 1 rotate, 2 scale, 3 off), `world`, `look`, `ortho`, `pie` (labels of the open radial menu, or `closed`), `inspector` (sections shown, e.g. `header,transform,components`), `roughness:<entity name>`, `shader:<surface name>` (`ok`, `fallback`, `failed`, `missing`), `effect:<effect name>` (`hdr`, `ldr`, `invalid`, `missing`), `toasts:error` (error toasts on screen) or `setting:<field>`; the value may contain spaces; failures are counted and logged |
| `log <text>` | write a line to the log, handy as a marker when profiling |
| `measure <frames> [label] [budget ms]` | log median, p95 and max frame time over the next frames; with a budget, a median above it fails the run |
| `shot <path>` | save a screenshot of the next frame |
| `wait <seconds>`, `frames <n>` | wait |
| `quit` | exit with the check results |

`tests/editor/smoke.lit` covers selection, modal transforms, undo, palette, help and zen mode; `tests/editor/scene_roundtrip.lit` saves and reopens a scene and checks that materials, render settings and pass state come back; `tests/editor/layout.lit` covers the world card, orthographic views, the context radial menu and the light inspector; `tests/editor/unsaved.lit` the unsaved-changes prompt and undo of render settings; `tests/editor/click.lit` checks that a click in the stress scene selects exactly one entity, that new objects land in front of the camera and that `shader.new` creates its file; `tests/editor/perf.lit` measures idle, hover, single-click selection, grab of that one entity and palette frame times on the stress scene.
