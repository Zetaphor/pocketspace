# Scene save and restore

Scene containers can capture model instances, panels, animation playback, physics
bodies, joints and scene lights into versioned JSON and restore them into a new container subtree. Move or grab objects, save their arrangement,
and load it in a later run. Positions use the current scene anchor: a save does
not preserve an absolute room location across launches or recentering.

Save and restore a grabbable arrangement: [scene-save.js](../scripts/scene-save.js).

## API

- `captureSceneContainer(containerId)` returns a detached JSON-compatible
  snapshot. The ID must name an active positive container; root `0` is rejected.
- `restoreSceneContainer(snapshot)` creates a new container subtree and returns
  `{containerId, containerIds, containerMap, objectIds, objectMap, renderGroups, jointIds, jointMap,
  panelIds, panelMap, panels, lightIds, lightMap}`. `objectIds` follows snapshot
  order. `objectMap` maps source object IDs to fresh IDs, and `renderGroups` maps
  positive source groups to available positive groups. `jointIds` follows snapshot
  joint order; `jointMap` maps source joint IDs to fresh IDs. Keys are strings, as in
  ordinary JSON objects. `panelIds` and `panels` follow saved panel order;
  `panelMap` maps source panel IDs to fresh IDs. `panels` contains normal
  `{id, canvas, present}` handles ready for editing and input matching. Object
  and panel IDs occupy separate namespaces. Group `0` stays independent and
  has no map entry.
- `saveSceneContainer(containerId, name)` captures and atomically replaces the
  named JSON file in the calling script's namespace, returning its path. The
  previous file survives a failed write.
- `loadSceneContainer(name)` reads a named JSON file from the calling script's
  namespace and returns the same result as `restoreSceneContainer`. A missing,
  malformed, unsupported, or oversized file throws; it does not create an empty
  scene.
- `importSceneContainer(sourceScript, name)` reads a save from another script's
  namespace and returns the same result as `loadSceneContainer`. See
  [saves from other scripts](#saves-from-other-scripts).

Names contain 1–64 ASCII letters, digits, underscores, or hyphens. The engine adds
`.json`; names cannot contain directories.

```js
const snapshot = captureSceneContainer(tool);
const path = saveSceneContainer(tool, 'desk');
// Later, including in another engine run:
const restored = loadSceneContainer('desk');
disposeSceneContainer(tool);
tool = restored.containerId;
```

## Storage namespaces

Each script has its own save directory, so two scripts can both use `desk` without
overwriting each other. Saves survive reloads, stops and engine restarts because
the directory comes from the script's path rather than its instance ID:

```text
<storage root>/my-scene.js/desk.json
<storage root>/tools%2Fbench.js/desk.json
<storage root>/scratch/desk.json
```

The directory name is the script's canonical path relative to the scripts catalog.
Bytes other than ASCII letters, digits, `.`, `_` and `-` are percent-encoded,
including `/`. A catalog symlink shares its target's namespace. Moving the catalog
keeps its saves, and scripts with the same relative name in different catalogs share
a namespace. Renaming or moving a script inside the catalog starts an empty
namespace. Encoded names are limited to 255 bytes. Scratch instance `0` uses
`scratch`.

The storage root is `$XDG_STATE_HOME/pocketspace/scenes` when that
environment variable is an absolute path, otherwise
`$HOME/.local/state/pocketspace/scenes`. `--scene-state-dir DIRECTORY`
replaces the root; relative paths resolve against the engine's working directory.
Files are private to the user when initially created. Symlink files and symlinked
namespace directories are rejected. Save writes a unique temporary file, syncs its
contents, and renames it over the destination; it does not promise directory
durability across sudden power loss.

```sh
build/pocketspace --scene-state-dir /tmp/pocketspace-saves --launch my-scene.js
```

## Saves from other scripts

`sourceScript` is the source's catalog-relative name, such as `tools/bench.js`. It
must be a readable `.js` file in the configured catalog, but it does not need to be
running. Traversal, absolute paths, and symlinks leaving the catalog throw. Scratch
saves are not available as an import source.

Import copies the JSON document through the normal validated restore, giving the
caller fresh IDs. It never shares live objects or native handles with the source,
and it never writes to the source's namespace. A failed import leaves both scenes
and the file unchanged. Model paths in the save keep their normal working-directory
meaning. Later saves by the caller go to the caller's own namespace.

Run [scene-save.js](../scripts/scene-save.js) and
[scene-import.js](../scripts/scene-import.js) from one catalog to copy a saved
arrangement between scripts:

```sh
build/pocketspace --scripts-dir scripts --launch scene-save.js --launch scene-import.js
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
all saved loading options match. Cached assets remain scene-owned after disposal
unless explicitly released with `unloadModel` when unused.

Owned point lights preserve settings and remap object attachments.
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

Save supported body and joint state while keeping callbacks in the script: [physics-save.js](../scripts/physics-save.js).

## Format

The `format` string is `pocketspace.scene-container` and `version` is `1`. Restore
rejects any other format or version.

All fields below are required unless a table says otherwise.

`models` is an array of at most 32 entries with required fields:

| Field | Value |
| --- | --- |
| `path` | Nonempty model path string without NUL characters; no duplicate paths |
| `eyePixels` | Integer multiple of 64, from 256 to 2048 |
| `optimizeVertexCache`, `gpuInstancing`, `lod` | Booleans |
| `pbr` | Boolean |
| `lighting` | Object with three-component `direction` and `color` arrays, numeric `intensity`, `ambient` and `exposure`, the `environment` path (empty when disabled), `environmentIntensity` and `environmentRotation`; ranges match [model lighting](lighting-api.md). Cached PBR lighting must match. |

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
| `physics` | `null`, or `{settings, velocity, angularVelocity}` as described below |
| `animation` | `null`, or the playback object described below |

`interactive` records membership in the engine's interaction registry;
`autoManipulate` preserves the distinction between script-controlled grabs
and the automatic manipulation helper. These are snapshot fields, not additional
`setObjectInteraction` options. Prefer editing snapshots obtained through capture.
Required fields are validated without coercion. Unknown fields are ignored. JSON documents are limited to 16 MiB, including encoded panel pixels.

See [scene containers](scene-containers.md), [JavaScript API](javascript-api.md),
and the runnable [save/restore example](../scripts/scene-save.js).

## Physics state

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

The `joints` array holds at most 512 entries. Each entry has:

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
boundary makes capture/save throw: put both endpoints in the captured subtree or
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

## Animation playback

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

## Panels and UI pixels

The `panels` array is required, even when empty. Snapshots contain at
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

## Animation transitions

Each non-null animation requires `animation.transition`. It is null for
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

Save and restore playback state; see [blending.js](../scripts/blending.js) for creating transitions: [animation-save.js](../scripts/animation-save.js).

## Point lights

The `lights` array holds at most eight entries, and is required even when empty. Each entry stores `id`,
`position`, `color`, `intensity`, `range`, `enabled`, `objectId` and `container`.
The source container field is informational: restore assigns the new container.
All light values use the [point-light API limits](lighting-api.md#dynamic-point-lights).
IDs must be unique positive integers; `objectId` is null or an object ID inside
the same snapshot. Duplicate IDs, invalid values or external attachments reject
the restore before publication. A capture also rejects light attachments crossing
the captured boundary in either direction.

Restore returns `lightIds` in snapshot order and `lightMap` from source IDs to
fresh IDs, with object attachments remapped through `objectMap`. Eight is the total live-light limit, including lights in existing tools
and disabled lights; replacing a tool requires temporary capacity for both.
Removing an object removes all attached lights. Disposing the restored container
removes its lights through the same cleanup path.

Try capture/restore with a grabbable attached light in
[lights.js](../scripts/lights.js).

## Spotlights and light types

Every `lights` entry requires `type`: `'point'`, `'spot'` or `'directional'`. Spotlights
also require a nonzero three-component `direction`, `innerConeAngle` and
`outerConeAngle`, using the [spotlight API ranges](lighting-api.md#dynamic-spotlights).
Invalid types, vectors or cone ordering reject restoration before publication.
Both types share the existing eight-light capacity and attachment/ownership rules.
The returned `lightIds` and `lightMap` include both types in snapshot order.

Restore stages a mixed set together and preserves the current scene and
ID counters on failure.

Try mixed-type persistence in the native regression, or run the grabbable
flashlight capture/restore example: [spotlights.js](../scripts/spotlights.js).

## Scene directional lights

Directional entries
store `id`, `direction`, `color`, `intensity`, `enabled`, `container` and
`objectId` as captured by the [directional API](lighting-api.md#scene-directional-lights).
The source `container` is informational; restore assigns the new owner.
They omit `position`, `range` and cone angles; restoration rejects those fields
when supplied. The nonzero direction uses the same finite component limits as
the API. Point and spot entries retain their previous schema.

All three light types share the eight-light limit, snapshot ordering, fresh
`lightIds`/`lightMap`, cleanup ownership and attachment remapping. Validation
stages the entire set before publication; invalid fields, capacity overflow or
attachments outside the snapshot leave the active scene and ID counters intact.

Capture and restore an attached directional key and an independent fill light:
[directional-lights.js](../scripts/directional-lights.js).

## Nested container subtrees

Snapshots capture a root container and all descendants, including empty containers.
`rootContainer` identifies the source root. `containers` lists `{id, parent}`
records with unique positive IDs; the captured root has parent `0`, and every
other record must reach that root without a cycle. Object, panel and light
entries contain their direct source `container`. Resource owners must appear
in the captured hierarchy. Transforms remain scene-relative.

```js
const saved = captureSceneContainer(tool);
const restored = restoreSceneContainer(saved);
const newChild = restored.containerMap[child];
print(getSceneContainerState(newChild).parentId);
disposeSceneContainer(restored.containerId); // Includes restored descendants.
```

Restore returns `containerId` for the new root, `containerIds` in fresh-ID order,
and `containerMap` from source IDs. Resource arrays continue following snapshot
order, across the whole subtree. Joint endpoints and light attachments may cross
child boundaries inside the captured subtree, but may not cross its outer
boundary. The active-plus-restored hierarchy must fit the 256-container limit.
Validation and hierarchy staging precede publication; malformed parents, cycles,
unknown owners and native resource failures leave active IDs/resources intact.

Input callbacks and JavaScript closures remain outside snapshots.
Model assets are referenced by path/options; an explicitly unloaded asset can be
loaded again during restoration without reviving its old ID. The live asset limit
is 32, independent of previously released IDs.

Run [nested-containers.js](../scripts/nested-containers.js) or
[asset-lifetime.js](../scripts/asset-lifetime.js).

## Spotlight shadow settings

Spotlights include a `shadow` object containing `enabled`, `resolution`,
`near`, `bias`, `normalBias`, `maxTriangles` and `maxGpuMs`. The shadow texture,
GPU query, measured time and watchdog state are runtime resources and are not
serialized. Restore validates the settings and single enabled-shadow slot before
loading assets or reserving native objects. An occupied slot rejects the whole
restore without changing the active scene.

See [shadow settings and limits](lighting-api.md#spotlight-shadows) and
[shadows.js](../scripts/shadows.js) for replacement with shadow-slot recovery.

## Directional shadow settings

Directional lights include `shadow` with `enabled`, `resolution`, `bias`,
`normalBias`, `maxTriangles`, `maxGpuMs`, world-space `center`, `size` and `depth`.
They omit spotlight-only `near`. All light types share one enabled
shadow slot, validated across the existing and staged scene before native
reservations. Only logical settings are serialized, never maps or timer state.

See [coverage and limits](lighting-api.md#directional-light-shadows) and the
[directional shadow example](../scripts/directional-shadows.js).

## Point shadow settings

Point lights include `shadow` with `enabled`, `resolution`, `near`, `bias`,
`normalBias`, `maxTriangles` and `maxGpuMs`. Their per-face resolution is limited
to 256 or 512. All light types
share one enabled shadow slot, validated transactionally before native allocation.
The six-view atlas and GPU timing state are never serialized.

See [point-shadow settings](lighting-api.md#point-light-shadows) and
[point-shadows.js](../scripts/point-shadows.js).
