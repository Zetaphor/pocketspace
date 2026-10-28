# Scene save and restore

Scene containers can capture model instances, physics bodies and joints into
versioned JSON and restore them into a new container. Move or grab objects, save their arrangement,
and load it in a later run. Positions use the current scene anchor: a save does
not preserve an absolute room location across launches or recentering.

## API

- `captureSceneContainer(containerId)` returns a detached JSON-compatible v2
  snapshot. The ID must name an active positive container; root `0` is rejected.
- `restoreSceneContainer(snapshot)` creates a new container and returns
  `{containerId, objectIds, objectMap, renderGroups, jointIds, jointMap}`. `objectIds` follows snapshot
  order. `objectMap` maps source object IDs to fresh IDs, and `renderGroups` maps
  positive source groups to available positive groups. `jointIds` follows snapshot
  joint order; `jointMap` maps source joint IDs to fresh IDs. Keys are strings, as in
  ordinary JSON objects. Group `0` stays independent and has no map entry.
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
options; position, Euler rotation, scale and visibility; logical render-group
membership; and all current object interaction flags, including grip/trigger
selection and hand rotation axes. Model paths retain their normal working-directory
semantics; asset files are referenced, not embedded. Keep them available when
loading on another device. Exact cached paths reuse the shared asset only when
all saved loading options match. Cached assets remain scene-owned after disposal.

An owned panel or animated/deformed object pose causes capture
and save to throw. This prevents an incomplete save from silently losing those
resources. Static instances of animation-capable assets are supported when they
have no per-instance animation pose. An empty container is supported.

Callbacks, functions, script variables, panel UI, animation
playback, hover/grab state, and tracking state are not serialized. Scripts recreate
their own behavior after loading. Scene-wide render-group auto-split policies are
not saved; reapply them using `renderGroups` if needed. Runtime tiles and chosen
LOD levels are recomputed. Fresh groups avoid accidentally merging a restored
tool with existing scene surfaces or inheriting their auto-split policies.

Restore validates the complete document, stages missing CPU model assets, and
reserves required live overlays before committing. Validation, asset loading,
overlay reservation, and native physics construction failures leave scene
contents, callbacks, cached assets, and ID counters unchanged. JSON conversion can invoke supplied script getters
or `toJSON`; their own side effects are ordinary script actions and cannot be
rolled back. File saves are external side effects and survive a later failed
script reload, like calibration writes.

## Format and compatibility

The `format` string is `sf-overlay-engine.scene-container`.
New captures use `version: 2`; existing `version: 1` static-instance documents
remain readable. V1 has no physics fields or joints; a v1 document containing
non-null physics or a nonempty joints array is rejected.

`models` is an array of at most 32 entries with required fields:

| Field | Value |
| --- | --- |
| `path` | Nonempty model path string without NUL characters; no duplicate paths |
| `eyePixels` | Integer multiple of 64, from 256 to 2048 |
| `optimizeVertexCache`, `gpuInstancing`, `lod` | Booleans |

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
| `physics` (v2) | `null`, or `{settings, velocity, angularVelocity}` as described below |

`interactive` records membership in the engine's interaction registry;
`autoManipulate` preserves the distinction between legacy script-controlled grabs
and the automatic manipulation helper. These are snapshot fields, not additional
`setObjectInteraction` options. Prefer editing snapshots obtained through capture.
Required fields are validated without coercion. Unknown fields are ignored;
unknown format versions are rejected. JSON documents are limited to 1 MiB.

See [scene containers](scene-containers.md), [JavaScript API](javascript-api.md),
and the runnable [save/restore example](../scripts/scene-save.js).

## Physics state (v2)

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

V2 also requires a `joints` array (at most 512). Each entry has:

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
