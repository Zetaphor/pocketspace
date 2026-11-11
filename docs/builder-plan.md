# Thing builder: plan

Status: draft for review. Nothing is built yet.

## Goal

A builder demo, `demos/builder.js`, for making 3D objects in VR from simple
shapes, in the spirit of Anyland's creation tools. Everything you build is a
**Thing**: a named set of **parts**, each a parametric shape with its own
position, rotation, per-axis size, color and finish. You pull shapes out of a
palette, move, rotate and stretch them with your hands, paint them with a color
pen, and finish the Thing, which bakes its parts into one model. A finished
Thing can be placed around you, and reopened later to edit it or to make a
spin-off copy.

Pocketspace runs beside other VR apps, so the builder takes controller input
only in an explicit **build mode**. Outside build mode, Things are scenery: the
laser passes through them and the builder ignores buttons.

## Phases

1. **Engine additions** (below): per-axis object scale, shape assets, object
   material overrides, snapshot support for both, and text input.
2. **Builder version 1**: build mode, shape palette, direct and laser
   manipulation, two-hand stretch, snapping, clone and delete, undo/redo, color
   pen and finishes, naming, baking, saving, and reopening Things for editing or
   as a spin-off.
3. **Thing properties**: per-Thing flags mapped to physics and interaction.
4. **States and rules**: per-part states with tweening, and "when … then …"
   rules.
5. **Inventory board**: a personal board of saved Things to spawn copies from.

Phases 3 to 5 are outlined at the end and get their own detailed plans before
they start.

**Not planned:** vertex tweezers, sky/sun/fog/ambient orbs, terrain, avatar
attachments via a mirror, climbing, liquids, video and slideshow screens,
creator and usage statistics, and sharing between users. Most of these need a
world or an avatar that Pocketspace doesn't own as an overlay engine.

## Engine additions

Each addition ships with a reference entry, a regression, and coverage in the
smallest relevant example, as the [demos plan](demos-plan.md) did. Anything else
that comes up during implementation is added to this list before it is built.

### 1. Per-axis object scale

Two-hand stretch needs independent X/Y/Z sizes. Object scale becomes a
three-component value in the object's local axes; it is the only representation
of scale, and uniform scale is the case where all three are equal.

- `setObjectScale(id, x, y = x, z = x)` sets the scale; each component is greater
  than zero and at most 2 meters. `getObjectTransform(id).scale` returns
  `{x, y, z}`. `createObject`'s `scale` argument accepts a number or `{x, y, z}`.
- Transform order is scale, then rotation, then translation, so a stretched part
  keeps stretching along its own axes when rotated.
- Every draw path (unlit, PBR, skinned, instanced and shadow) scales positions
  before rotating them. Only PBR shades with normals: they use the inverse of the
  scale, tangents use the scale, and both are normalized per vertex.
- Picking and grabbing transform the ray into object space. Bounds, crop,
  render-group layout, LOD distance and the near-hand grab threshold use the
  largest axis where they need a single size.
- Physics: box, convex and mesh colliders take the per-axis scale through Jolt's
  scaled shapes. An automatic sphere collider on a non-uniformly scaled object
  throws, because Jolt can't scale a sphere into an ellipsoid; an explicit radius
  is in meters and stays allowed. Grabbed and simulated
  bodies keep their current release behavior.
- Snapshots store `scale` as `{x, y, z}`. Saves that predate this change are not
  read; the snapshot version is 2.
- Every example, demo and test that reads `scale` from `getObjectTransform`
  changes in the same work.

### 2. Shape assets

`createShapeAsset(parts, options = {})` returns an asset ID for a model generated
in the engine, with no file. `parts` is an array of
`{shape, position: [x, y, z], rotation: [pitch, yaw, roll], scale: [x, y, z],
color: [r, g, b], finish}`, in meters, radians and linear 0–1 color, all in one
local frame. A single part is a one-element array.

- **Shapes:** `cube`, `sphere`, `cylinder`, `cone`, `wedge` (a triangular prism),
  `pyramid` (square base), `torus` and `arch` (a half ring with a square
  cross-section). Each fills the unit box from −0.5 to 0.5 before its scale, so
  a part's scale is its size in meters. The torus and arch have fixed
  proportions; stretching changes their outer size, not their thickness.
  Generators live in a new `src/shape_mesh.h`, with flat normals on flat faces
  and smooth normals on curved ones, at fixed tessellation chosen by
  profiling.
- **Finishes:** `matte`, `glossy`, `metal` and `glow`, as fixed PBR
  metallic/roughness/emissive values. Shape assets always use PBR lighting and
  accept the `loadModel` options that apply to generated geometry (`lod`,
  `gpuInstancing`, `optimizeVertexCache`).
- Parts with the same color and finish share one material primitive, so a
  baked Thing draws once per distinct material rather than once per part. At
  most 64 distinct materials and 256 parts per asset.
- The result goes through the same path as an imported model: centered and
  normalized to unit maximum extent, with CPU triangles for picking, bounds and
  colliders, a picking BVH, and GPU upload. Building it is synchronous; a
  256-part Thing has to fit within a frame budget measured on the headset.
- Shape assets count toward the 32-asset scene limit and are released with
  `unloadModel`. Identical part lists are not deduplicated.

### 3. Object material overrides

While building, each part is an object on one of eight shared one-part shape
assets, colored per object, so a Thing in progress costs no extra assets.

- `setObjectMaterial(id, {color, finish})` overrides the base color factor and
  finish of every material primitive of that object; `setObjectMaterial(id,
  null)` removes the override. It works on any PBR object, not only shapes.
  `getObjectMaterial(id)` returns the override or `null`.
- Ordered GPU instancing either carries the override in its per-instance stream
  or places overridden objects in their own batches. The choice follows a
  measurement on the headset with 200 colored parts, recorded under
  `tests/results/`.
- Snapshots store the override.

### 4. Snapshots of shape assets

`saveSceneContainer` and `captureSceneContainer` store a shape asset by its
part list instead of a path, and restore regenerates it. The snapshot format
table in [scene persistence](scene-persistence.md) gains the `shapes` asset
form. Restore validates every part before creating anything, so a bad shape
list leaves the active scene intact.

### 5. Text input

`requestTextInput(options)` opens a text entry for the calling script:
`{prompt, text = '', maxLength = 64}`. Input events report `text.change` with
the current text, then `text.done` or `text.cancel`. Only one request is open
across all scripts; a new request from another script cancels the open one, and
stopping the script closes it.

The backend is SteamVR's keyboard, opened with
`IVROverlay::ShowKeyboardForOverlay` for the requesting script's panel (or, with
no panel given, a small engine overlay) in minimal mode, which sends each key as
it is pressed. The engine polls that overlay's event queue while a request is
open; it polls no overlay events today. In minimal mode SteamVR keeps no text
of its own, so the engine holds the text, applies the initial `text`, and
handles backspace and arrow keys.

A spike on Steam Frame showed that an overlay app can open the keyboard, that
keys, Done and Close arrive on the overlay's queue, and that a physical keyboard
does not reach it: physical keys go to the focused desktop app. Physical
keyboard input is not supported.

## Builder version 1

### Build mode and the wrist ring

- A small ring follows the left controller's wrist. Touching it with the right
  controller's tip opens or closes the main panel. The ring glows (the `glow`
  finish) while build mode is on. Touch detection uses controller positions, so
  the ring needs no laser and no button.
- The main panel has: **Build mode** on/off, **New Thing**, **Things** (the
  saved list), **Snap angle** (off, 15°, 45°, 90°), **Snap grid** (off, 1, 2.5,
  5 cm), **Undo**, **Redo**, **Name** and **Done**.
- Outside build mode, the builder's panels other than the main panel are hidden,
  every Thing is `laserPassThrough` and not grabbable, and the builder ignores
  raw buttons. Lasers are shown only while build mode is on and a hand aims at a
  part or panel.

### Shape palette and spawning

- In build mode, a tray of eight miniature shapes hovers by the left hand.
  Holding the trigger with the right tip on a miniature, then pulling away,
  spawns a full-size part (10 cm) that stays in the hand until release.
- New parts take the pen's current color and finish.

### Manipulating parts

The builder moves parts itself instead of using the engine's grab, so snapping
and two-hand stretch apply to every move.

| Control | Action |
| --- | --- |
| Trigger with the tip inside a part | Hold it: it follows the hand's position and rotation. |
| Trigger on a part along the laser | Hold it at a distance; the stick moves it nearer and farther. |
| Both triggers on the same part | Stretch: hand separation along each of the part's axes scales that axis; the midpoint moves it. |
| A/X while holding a part | Clone: a copy stays at the part's position from before the move. |
| Grip with the tip inside a part, or on it along the laser | Delete the part. |
| B/Y | Undo; with the stick clicked, redo. |

- **Stretch:** at the start, the builder records the hands' separation in the
  part's local axes. An axis whose share of that separation is under 25% keeps
  its size; each other axis scales by the ratio of its current to its starting
  separation. Grabbing near a corner stretches all three axes; along an edge,
  one.
- **Snapping:** rotation snaps to the selected angle on each axis of the Thing's
  frame, and position snaps to the grid in that frame. Both apply on release
  and while moving.
- **Touch tests** use each shape's exact solid in its local frame, so a tip in
  the hole of a torus is outside it.
- **Undo/redo:** spawn, move, stretch, paint, clone and delete are recorded with
  before and after values, up to 200 steps per editing session. New Thing,
  Done and opening a Thing clear the history.
- A Thing in progress can have at most 200 parts, which leaves object slots for
  the palette, the pen, the ring and placed Things.

### Color pen and finishes

- A pen (a generated model) sits in a holder on the palette tray; pick it up with
  the trigger. Its nib shows the current color and finish.
- A palette panel by the left hand has a hue bar, a saturation/lightness field
  and the four finish swatches, drawn once and kept. Touching the panel with the
  nib, or pointing the laser at it and pulling the trigger, picks a color or a
  finish.
- Touching a part with the nib paints it. Painting is an undo step.

### Naming, baking and saving Things

- **Name** opens text input with the current name. New Things are called
  "Thing 1", "Thing 2" and so on until renamed.
- **Done** bakes the parts: the builder computes the parts' bounds, passes the
  part list relative to their center to `createShapeAsset`, creates one object
  at the center with the Thing's extent as its scale, and removes the part
  objects. Baking a Thing with no parts does nothing.
- Each Thing is saved with `saveScriptData` as `{id, name, parts, derivedFrom}`
  under its own name, within the 64 KiB saved-data limit (about 500 parts, more
  than a Thing in progress can hold). A saved index lists the Things.
- The placed Things are saved with `saveSceneContainer`, which stores their
  shape assets by part list, and restored when the builder starts.

### Reopening Things

- **Things** lists the saved Things. Choosing one shows **Place**, **Edit**,
  **Spin-off** and **Delete**.
- **Edit** replaces the baked object with its parts, in place, for editing. Done
  bakes it again under the same Thing ID; each placed copy is updated.
- **Spin-off** opens a copy of the parts as a new Thing with `derivedFrom` set,
  leaving the original unchanged.
- **Delete** asks for a second press, as the dashboard's Reset does, and removes
  the saved Thing and its placed copies.
- In build mode, pointing the laser at a placed Thing and pressing the trigger
  holds it to move it; grip deletes that copy.

### Rendering and limits

- The parts of the Thing in progress share one render group; placed Things
  share another. Whether to split them follows a headset measurement with a
  200-part Thing and ten placed Things.
- The 32-asset limit allows the eight part shapes, the pen and the tray, and up
  to about 20 distinct placed Things. Placing a further copy of the same Thing
  reuses its asset. Raising the limit for shape assets is a separate decision,
  made only if this limit gets in the way.

### Assets

- The pen, its holder and the palette tray base are generated by
  `tools/generate-demo-models.py` into `assets/demos/`, listed in
  `assets/demos/README.md`. The miniature shapes on the tray are shape assets.

## Later phases

These are outlines only. Each gets a detailed plan, with its own questions,
before work starts.

### Phase 3: Thing properties

A properties panel per Thing, mapped onto the existing physics and interaction
APIs: **Holdable** (grabbable outside build mode), **Stays held until grip** (a
press-to-toggle hold), **Uncollidable**, **Invisible** (hidden outside build
mode), and the physics materials **Bouncy**, **Slidy** and **Sticky**. Colliders
are derived from the parts: one box, convex or mesh collider per part, combined
in the Thing's body. Properties are stored with the Thing and its placed copies.

### Phase 4: states and rules

Each part gets numbered states holding its position, rotation, scale, color and
finish. Changing states tweens between them over a duration. Rules follow
"when [event] then [action]": for example `when touched then become 2 in 1s`, or
`when starts then become next in 1s` for loops. Events come from hand touch,
holding, the trigger, and other Things; actions change state, emit a copy of
another Thing with velocity, or send a bus message. Rules are built from tokens
on a panel, with text input for values. Sound actions wait for an engine audio
API, which doesn't exist yet.

### Phase 5: inventory board

Reaching behind the shoulder and pulling the trigger brings out a board of saved
Things as miniatures. Dropping a Thing on the board saves it there; pulling a
miniature off spawns a full-size copy.

## Tests and documentation

| Addition | Tests | Documentation |
| --- | --- | --- |
| Per-axis scale | Transform round trip; rotated non-uniform picking and grabbing; normals in each draw path against reference images; crop and LOD sizes; box, convex and mesh colliders; sphere rejection; snapshot round trip; invalid values | JavaScript API reference, physics API |
| Shape assets | Each shape's bounds, normals and triangle counts; part and material limits; normalization; picking BVH hits; unload and asset limit; invalid part lists; build time at 256 parts | JavaScript API reference, architecture asset section |
| Material overrides | Rendered color and finish; removal; instanced and non-instanced paths; invalid IDs and values | Lighting API |
| Shape-asset snapshots | Save, restore and capture; validation failure leaves the scene intact; container subtrees | Scene persistence format |
| Text input | Open, change, done, cancel; cancellation by another script and by stopping; the chosen backend through `SF_TEST_HOOKS` fault injection | JavaScript API reference, input architecture |
| Builder | `examples-regression` loads it and drives build mode, spawning, stretch, snapping, clone, delete, undo/redo, painting, naming, baking, saving, reopening and spin-off | `docs/demos.md`: controls, saved state and limits |

`tests/index.md` lists every new test, and `tools/lint-docs.py` covers the new
demo and assets.

## Verification

- Focused regressions while building each piece, one job at a time; the full
  regression set, docs lint and site build before each commit.
- Headset measurements for the shape-asset build time, the instancing choice for
  material overrides, and render grouping, recorded under `tests/results/`.
- Headset acceptance by the user: building with the hands feels direct, stretch
  and snapping do what you expect, painting is reliable, and frame rate holds
  with a 200-part Thing beside the other demos. The builder is stopped and
  checked afterwards.

## Order

1. Text input spike: done, see [steamvr-keyboard.md](../tests/results/steamvr-keyboard.md).
2. Per-axis object scale: done.
3. Shape assets, then material overrides, then shape-asset snapshots.
4. Text input on the SteamVR keyboard.
5. Builder version 1, in commits for build mode and palette, manipulation and
   snapping, undo/redo, color, then naming, baking, saving and reopening.
6. Phases 3 to 5, each after its own plan.

## Decisions

- Confirmed: per-axis scale replaces uniform scale everywhere; there is no
  separate stretch value.
- Confirmed: the controls in [Manipulating parts](#manipulating-parts).
- Confirmed: SteamVR's keyboard in minimal mode, with no physical keyboard
  support.
