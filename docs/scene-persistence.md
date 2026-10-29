# Scene save and restore

Scene containers can capture model instances, panels, animation playback, physics
bodies and joints into versioned JSON and restore them into a new container. Move or grab objects, save their arrangement,
and load it in a later run. Positions use the current scene anchor: a save does
not preserve an absolute room location across launches or recentering.

## API

- `captureSceneContainer(containerId)` returns a detached JSON-compatible v7
  snapshot. The ID must name an active positive container; root `0` is rejected.
- `restoreSceneContainer(snapshot)` creates a new container and returns
  `{containerId, objectIds, objectMap, renderGroups, jointIds, jointMap,
  panelIds, panelMap, panels}`. `objectIds` follows snapshot
  order. `objectMap` maps source object IDs to fresh IDs, and `renderGroups` maps
  positive source groups to available positive groups. `jointIds` follows snapshot
  joint order; `jointMap` maps source joint IDs to fresh IDs. Keys are strings, as in
  ordinary JSON objects. `panelIds` and `panels` follow saved panel order;
  `panelMap` maps source panel IDs to fresh IDs. `panels` contains normal
  `{id, canvas, present}` handles ready for editing and input matching. Object
  and panel IDs occupy separate namespaces. Group `0` stays independent and
  has no map entry.
- `saveSceneContainer(containerId, name)` captures and atomically replaces the
  named JSON file, returning its path. The previous file survives a failed write.
- `loadSceneContainer(name)` reads a named JSON file and returns the same result
  as `restoreSceneContainer`. A missing, malformed, unsupported, or oversized
  file throws; it does not create an empty scene.

Names contain 1–64 ASCII letters, digits, underscores, or hyphens. The engine adds
`.json`; names cannot contain directories. By default, files live in
`$XDG_STATE_HOME/sf-overlay-engine/scenes` when that environment variable is an
absolute path, otherwise `$HOME/.local/state/sf-overlay-engine/scenes`.
`--scene-state-dir DIRECTORY` overrides the complete storage directory; relative
paths resolve against the engine's working directory. Files are private to the
user when initially created. Symlink files are rejected on load. Save writes a
unique temporary file, syncs its contents, and renames it over the destination;
it does not promise directory durability across sudden power loss.

```js
const snapshot = captureSceneContainer(tool);
const path = saveSceneContainer(tool, 'desk');
// Later, including in another engine run:
const restored = loadSceneContainer('desk');
disposeSceneContainer(tool);
tool = restored.containerId;
```

Restoring does not replace or dispose the original container. When replacing a
tool, load successfully before disposing the old one. Both must fit within the
normal object, container, asset, and overlay limits during that overlap.
Attach callbacks to the restored container and use its fresh IDs. Old IDs and
panel handles remain associated with the old scene.

## Included state and limitations

Snapshots save exact model paths, eye resolution, vertex-cache, instancing and LOD
options, PBR choice and per-asset lighting; position, Euler rotation, scale and visibility; logical render-group
membership; and all current object interaction flags, including grip/trigger
selection and hand rotation axes. Model paths retain their normal working-directory
semantics; asset files are referenced, not embedded. Keep them available when
loading on another device. Exact cached paths reuse the shared asset only when
all saved loading options match. Cached assets remain scene-owned after disposal.

Owned panels preserve their retained RGBA canvas and layout. Animated model
instances and empty containers are supported.

Callbacks, functions, script variables, widget/business state, hover/grab state,
and tracking state are not serialized. Scripts recreate
their own behavior after loading. Scene-wide render-group auto-split policies are
not saved; reapply them using `renderGroups` if needed. Runtime tiles and chosen
LOD levels are recomputed. Optional PBR LOD buffers are admitted under the
current shared budget; generated buffers and reservations are not serialized.
Insufficient optional budget preserves restored objects at finer/full detail. Fresh groups avoid accidentally merging a restored
tool with existing scene surfaces or inheriting their auto-split policies.

Restore validates the complete document, stages missing CPU model assets, and
reserves required live overlays before committing. Validation, asset loading,
overlay reservation, and native physics construction failures leave scene
contents, callbacks, cached assets, and ID counters unchanged. JSON conversion can invoke supplied script getters
or `toJSON`; their own side effects are ordinary script actions and cannot be
rolled back. File saves are external side effects and survive a later failed
script reload, like calibration writes.

## Format and compatibility

The `format` string is `sf-overlay-engine.scene-container`. Pocketspace retains
this identifier and its legacy default save directory for compatibility with
existing snapshots and named saves.
New captures use `version: 7`. Existing v1 static-instance, v2 physics, v3
animation and v4 panel documents remain readable. V1 has no physics fields or joints; a v1 document
containing non-null physics or a nonempty joints array is rejected. V1 and v2
have no animation field; non-null animation requires v3. V1/v2 saves restore
models with stopped playback. V1–v3 have no panels; a nonempty `panels` array
requires v4. V1–v5 models restore with PBR disabled and default lighting. V6 preserves
direct PBR lighting; environment selection/intensity/rotation require v7.

`models` is an array of at most 32 entries with required fields:

| Field | Value |
| --- | --- |
| `path` | Nonempty model path string without NUL characters; no duplicate paths |
| `eyePixels` | Integer multiple of 64, from 256 to 2048 |
| `optimizeVertexCache`, `gpuInstancing`, `lod` | Booleans |
| `pbr` | Boolean; required in v6. |
| `lighting` | V6 object with three-component `direction` and `color` arrays, plus numeric `intensity`, `ambient`, and `exposure`; ranges match [model lighting](lighting-api.md). V7 also requires the `environment` path (empty when disabled), `environmentIntensity` and `environmentRotation`. Cached PBR lighting must match. |

`objects` is an array of at most 256 entries with required fields:

| Field | Value |
| --- | --- |
| `id` | Unique positive source object ID less than `2147483647` |
| `model` | Zero-based index into `models` |
| `renderGroup` | Integer from 0 to 256 |
| `x`, `y`, `z` | Finite float-range scene-anchor coordinates, metres |
| `pitch`, `yaw`, `roll` | Finite float-range angles, radians |
| `scale` | Greater than 0 and at most 2 |
| `visible`, `interactive`, `laserPassThrough`, `grabbable`, `directGrab`, `autoManipulate` | Booleans |
| `laserYaw`, `laserPitchWithStickClick`, `depthAdjust`, `handPitch`, `handYaw`, `handRoll` | Booleans |
| `grabButton` | `"trigger"`, `"grip"`, or `"both"` |
| `physics` (v2+) | `null`, or `{settings, velocity, angularVelocity}` as described below |
| `animation` (v3+) | `null`, or the playback object described below |

`interactive` records membership in the engine's interaction registry;
`autoManipulate` preserves the distinction between legacy script-controlled grabs
and the automatic manipulation helper. These are snapshot fields, not additional
`setObjectInteraction` options. Prefer editing snapshots obtained through capture.
Required fields are validated without coercion. Unknown fields are ignored;
unknown format versions are rejected. JSON documents are limited to 16 MiB, including encoded panel pixels.

See [scene containers](scene-containers.md), [JavaScript API](javascript-api.md),
and the runnable [save/restore example](../scripts/scene-save.js).

## Physics state (v2 and later)

Each object's `physics.settings` contains the complete authored body configuration:
`type` (`static`, `dynamic`, `kinematic`), `shape` (`box`, `sphere`, `convex`, `mesh`),
`continuous`, `mass`, `friction`, `restitution`, `linearDamping`, `angularDamping`,
`gravityFactor`, and nullable `halfExtents`, `offset`, `radius`. Vectors are
`{x,y,z}`; numeric fields must be finite and satisfy the existing
[physics API bounds](physics-api.md). Convex and mesh colliders rebuild from the
referenced model geometry at the saved scale. Mesh bodies remain static-only.
Auto collider dimensions remain automatic, rather than becoming authored values.

The object transform comes from the current physics body pose. Dynamic linear
`velocity` and `angularVelocity` resume on restore. Static and kinematic bodies
start without motion; scripts drive new kinematic targets. Bodies restore awake
and unheld, including a body saved while grabbed. Controller ownership, throw
history, sleep timers, accumulated solver impulses and contact queues are not
saved. This is a portable scene snapshot, not deterministic solver replay.

V2 and later also require a `joints` array (at most 512). Each entry has:

| Field | Value |
| --- | --- |
| `id` | Unique positive source joint ID less than `2147483647` |
| `objectA`, `objectB` | Different saved object IDs, both with physics bodies |
| `settings` | Complete joint settings listed below |
| `referenceA`, `referenceB` | Original body construction poses, each `{position:{x,y,z},pitch,yaw,roll}` |
| `motor` | Complete motor settings listed below |

Joint settings include `type` (`fixed`, `hinge`, `distance`), nullable `anchor`,
`anchorA`, `anchorB`, numeric `axis` vector, `minAngle`, `maxAngle`, `minDistance`,
nullable `maxDistance`, `breakForce`, `breakTorque`, `spring`, and boolean
`collideConnected`. A spring is `{frequency,damping}`. Limits and break/spring
values follow the physics API. Fields for other joint types retain their native
defaults. A hinge motor includes `mode` (`off`, `position`, `velocity`),
`targetAngle`, `targetVelocity`, `maxTorque`, `frequency`, and `damping`. Other
joint types capture a default inactive motor.

Construction reference poses preserve the joint's original local attachment
frames, hinge zero angle, and fixed-joint reference orientation after objects
move or rotate. Keep these fields intact when editing a captured snapshot.
Restored joints receive fresh IDs; use `jointMap` to reconnect script behavior.
Already broken or removed joints are absent. A joint crossing the container
boundary makes capture/save throw: put both endpoints in the same container or
remove the connection before saving. External root bodies are never implicitly
copied or reattached.

Gravity, worker policy and collision-event subscription are scene-wide settings
and are not saved. Restored bodies use the current world's settings, or normal
defaults if this is its first physics content. The root floor can remain outside
the saved container as long as it is not connected by a joint.

The [physics save example](../scripts/physics-save.js) preserves a hinge motor,
a spring, and a throwable box; bumper saves and A/X restores them. Simulation
continues immediately after restore, so moving objects will leave their saved
positions again according to their saved velocities and current gravity.

## Animation playback (v3)

Each object's `animation` is `null` for stopped/default-pose instances. Otherwise
it contains the complete selected playback state:

| Field | Value |
| --- | --- |
| `clip` | Nonnegative integer clip index in the referenced model |
| `name`, `duration` | Exact clip name and duration from the model, for validation |
| `time` | Finite seconds from 0 through `duration` |
| `speed` | Finite playback multiplier greater than 0 and at most 100 |
| `loop`, `playing`, `finished` | Booleans matching the selected playback state |

Clip index, name and duration must match a supported clip in the loaded model;
a missing/unsupported clip or changed metadata makes restore throw. Asset files
are still external references: matching metadata does not verify identical model
contents. Keep the saved assets available and compatible.

Restore evaluates a separate CPU pose for each animated instance before committing
any scene changes. Skeletal and rigid-node animation, deformed geometry, bounds
and picking resume through the existing animation path. Poses and GPU buffers
are rebuilt from the model and playback values; raw vertex arrays, skeletons and
GPU resources are not embedded in JSON. Invalid playback or pose-construction
failure leaves the current scene intact.

Playing clips advance from the saved time once the engine resumes its frame loop.
Paused clips stay at the saved pose. Finished clips retain their final pose and
finished status; stopped instances remain at their default pose without a
per-instance animation allocation. `finished` requires non-playing playback at
the clip duration, with looping disabled (a zero-duration clip can finish while
looping is enabled). A selected clip may also be paused at its endpoint without
being marked finished, as allowed by `seekAnimation`.

Animated objects can also have physics. Collider construction follows the
[existing rigid collider rules](physics-api.md): saved animation poses do not
deform collision shapes. Playback flags and dynamic-body motion restore
independently. Morph poses and transitions use the normal animation path; collider shapes do
not deform with them.

The [animation save example](../scripts/animation-save.js) uses independent
skeletal and rigid-node playback. Bumper saves, A/X restores, and B/Y toggles
playing/paused state; interaction remains available after restoring.

## Panels and UI pixels (v4)

V4 requires a `panels` array, including when it is empty. Snapshots contain at
most 32 panels and at most 64 MiB of decoded RGBA panel pixels in total. Pixel
and physical dimensions retain their normal creation limits. The overall 16 MiB
JSON limit also applies: highly detailed large images can exceed the file limit
and make capture/save throw. A failed save leaves the previous file intact.

Each panel entry contains:

| Field | Value |
| --- | --- |
| `id` | Unique positive source panel ID less than `2147483647` |
| `widthPixels`, `heightPixels` | Integer pixel dimensions from 1 to 4096 |
| `widthMeters` | Finite physical width greater than 0 and at most 10 |
| `heightMeters` | Finite positive physical height matching the pixel aspect ratio within float precision |
| `position` | Finite float-range `{x,y,z}` |
| `mode` | `world`, `viewerFacing`, `headLocked`, or `arbitrary` |
| `rotation` | Nine finite float-range numbers, row-major 3×3 |
| `visible`, `interactive` | Booleans |
| `fontPath` | Stored font path string, possibly empty |
| `fontPixels` | Integer legacy text size from 1 to 4096 |
| `pixels` | `{encoding, data}` as described below |

The pixels represent the latest retained canvas, including pending edits, with
all RGBA bytes preserved. Text, shapes, images and transparent pixels do not
need to be redrawn to restore their appearance. Image/font source files are not
needed to display the saved pixels. Future text draws use the saved font path
and therefore still need that font installed or a new `panelLoadFont` choice.
Callbacks and application variables remain script-owned; reconnect them using
the returned handles or `panelMap`. For example:

```js
const loaded = loadSceneContainer('tool');
const button = loaded.panels[0];
onInputEvent(event => {
  if (event.type === 'panel.pointerDown' && event.objectId === button.id) {
    button.canvas.rect(0, 0, 100, 40, 0, 120, 200, 255, true);
    button.present();
  }
});
```

`pixels.encoding` is `rgba8-base64` (row-major raw RGBA bytes) or
`rgba8-rle-base64` (pixel runs). RLE packets are eight bytes: an unsigned 32-bit
little-endian positive pixel count followed by four RGBA bytes. Counts must
expand to exactly `widthPixels * heightPixels` pixels. Base64 uses the standard
alphabet and canonical padding. Capture chooses RLE only when smaller than raw
pixels; decoding rejects malformed encodings, lengths and oversized runs before
committing. The decoded budget applies independently of compression.

Restored panels get independent pixel storage, fresh native overlays and new
textures on the normal next upload. Visibility, pointer input scaling, placement
mode, physical size and arbitrary rotation retain their existing panel behavior.
Live overlay reservation failures roll back the restore, including already
reserved object and panel overlays. Original panels, callbacks and ID counters
remain intact. Normal deferred removal still detaches a panel before freeing
its texture. Texture upload and compositor submission follow the usual frame
loop, as they do for newly created panels.

The [panel save example](../scripts/panel-save.js) uses bumper to save, B/Y to
change the pixels and placement, and A/X to restore. Clicking the restored panel
shows that its new handle still supports pointer callbacks and canvas edits.

## Animation transitions (v5)

V5 requires `animation.transition` for each non-null animation. It is null for
single-clip playback, or an object with `duration` (greater than zero, at most
60 seconds), `elapsed` (at least zero and less than duration), `source` and
`frozen`. The outer animation describes the incoming clip. An active fade
cannot have `finished: true`, even if its incoming clip already reached its end.

A live outgoing clip is stored in `source` with the same clip metadata and
playback fields as ordinary animation; `frozen` must be null. For default-pose
or interrupted-fade sources, `source` is null and `frozen` contains one local
pose per model node, each with `translation` (3 floats), `rotation` (4 floats,
nonzero quaternion), `scale` (3 floats) and `weights` (one float per morph target).
All numbers must be finite. Node counts and weight counts must match the loaded
asset; parent links and static matrix transforms come from that asset. Frozen
poses are limited to 65,536 nodes and the normal 16 MiB encoded snapshot bound.

Restore validates outgoing clip metadata too and rebuilds independent blended
geometry before committing. Both clocks, fade progress, and pause state resume
from the save. Vertex arrays and GPU buffers remain external to the snapshot.
V1–v4 saves restore without a transition; a non-null transition in those older
versions is rejected rather than silently discarded. Existing v3 playback and
v4 panel requirements continue to apply.
