# Scene save and restore

Scene containers can capture static model instances into versioned JSON and
restore them into a new container. Move or grab objects, save their arrangement,
and load it in a later run. Positions use the current scene anchor: a save does
not preserve an absolute room location across launches or recentering.

## API

- `captureSceneContainer(containerId)` returns a detached JSON-compatible v1
  snapshot. The ID must name an active positive container; root `0` is rejected.
- `restoreSceneContainer(snapshot)` creates a new container and returns
  `{containerId, objectIds, objectMap, renderGroups}`. `objectIds` follows snapshot
  order. `objectMap` maps source object IDs to fresh IDs, and `renderGroups` maps
  positive source groups to available positive groups. Keys are strings, as in
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

V1 saves exact model paths, eye resolution, vertex-cache, instancing and LOD
options; position, Euler rotation, scale and visibility; logical render-group
membership; and all current object interaction flags, including grip/trigger
selection and hand rotation axes. Model paths retain their normal working-directory
semantics; asset files are referenced, not embedded. Keep them available when
loading on another device. Exact cached paths reuse the shared asset only when
all saved loading options match. Cached assets remain scene-owned after disposal.

An owned panel, physics body, or animated/deformed object pose causes capture
and save to throw. This prevents an incomplete save from silently losing those
resources. Static instances of animation-capable assets are supported when they
have no per-instance animation pose. An empty container is supported.

Callbacks, functions, script variables, panel UI, physics/joints, animation
playback, hover/grab state, and tracking state are not serialized. Scripts recreate
their own behavior after loading. Scene-wide render-group auto-split policies are
not saved; reapply them using `renderGroups` if needed. Runtime tiles and chosen
LOD levels are recomputed. Fresh groups avoid accidentally merging a restored
tool with existing scene surfaces or inheriting their auto-split policies.

Restore validates the complete document, stages missing CPU model assets, and
reserves required live overlays before committing. Validation, asset loading,
and overlay reservation failures leave scene contents, callbacks, cached assets,
and ID counters unchanged. JSON conversion can invoke supplied script getters
or `toJSON`; their own side effects are ordinary script actions and cannot be
rolled back. File saves are external side effects and survive a later failed
script reload, like calibration writes.

## V1 format

The `format` string is `sf-overlay-engine.scene-container`; `version` is `1`.
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

`interactive` records membership in the engine's interaction registry;
`autoManipulate` preserves the distinction between legacy script-controlled grabs
and the automatic manipulation helper. These are snapshot fields, not additional
`setObjectInteraction` options. Prefer editing snapshots obtained through capture.
Required fields are validated without coercion. Unknown fields are ignored;
unknown format versions are rejected. JSON documents are limited to 1 MiB.

See [scene containers](scene-containers.md), [JavaScript API](javascript-api.md),
and the runnable [save/restore example](../scripts/scene-save.js).
