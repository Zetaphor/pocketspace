# Scene containers

Containers give related objects, panels, lights and an optionally owned input
callback one cleanup lifetime. Containers can own child containers. They do not
transform their children or change render groups; positions remain scene-relative.
Root ownership (`0`) lasts until scene replacement unless explicitly removed.
[Scene persistence](scene-persistence.md) captures a complete container subtree.

```js
const model = loadModel('assets/samples/Box/Box.glb');
const tool = createSceneContainer();
const box = createObject(model, 0, 0, 0, .15, {container: tool, renderGroup: 1});
const panel = createPanel({widthPixels: 256, heightPixels: 64, container: tool});
enableObjectInteraction(box, {grabButton: 'both'});
// Later: remove the tool's objects, panels, physics and attached joints together.
disposeSceneContainer(tool);
```

## API

| Function | Behavior |
| --- | --- |
| `createSceneContainer(options = {})` | Optional `{parent: id}` attaches the new container to an active parent; omitted/undefined parent or `0` selects root. Return a new positive integer ID. Up to 256 containers can be active; disposed IDs are not reused within a scene. |
| `getSceneContainerState(id)` | Return `{id, disposed, parentId, childIds, objectIds, panelIds, lightIds, inputCallback}`. Arrays describe direct current live members in scene order and are snapshots. Previously disposed IDs return empty membership and `disposed: true`. Unknown IDs throw. |
| `disposeSceneContainer(id)` | Dispose the container, all descendants and their current members. Repeating disposal of the same issued ID is harmless; unknown IDs and root ID 0 throw. Returns `undefined`. |
| `setSceneContainerParent(id, parentId)` | Reparent a live container; `0` detaches it to root. Self-parenting and ancestor cycles throw without changing ownership. |
| `setObjectContainer(objectId, containerId)` | Transfer a live object to an active container. Use 0 to transfer it to the root scene. Returns `undefined`. |
| `setPanelContainer(panel, containerId)` | Transfer a live panel handle or panel ID; 0 selects the root. Returns `undefined`. |

`createObject(..., {container: id})` and `createPanel({container: id, ...})`
assign ownership during creation. Omitted/undefined ownership and explicit 0
select the root scene. Container IDs must be numbers containing a nonnegative
integer; strings, null, fractional values, unknown and disposed IDs throw.
Validation precedes creation/reservation. If another options getter disposes
the requested owner, creation fails rather than leaving an orphaned member.

`getSceneContainerState` and disposal require an issued positive ID, including
already disposed IDs. Resources must be live when transferred. Disposed IDs report `parentId: 0` and an empty `childIds` array. Child IDs are
listed in creation-ID order; resource arrays describe direct ownership, not the
whole subtree. Removing a member
individually automatically removes it from the reported membership. Container
handles are local to one script scene and reset on successful scene replacement.

Transfer live objects and panels, then dispose either owner: [container-transfer.js](../scripts/container-transfer.js).

## Nested ownership

```js
const tool = createSceneContainer();
const module = createSceneContainer({parent: tool});
setSceneContainerParent(module, 0); // It now survives disposal of tool.
setSceneContainerParent(module, tool);
disposeSceneContainer(tool); // Both IDs and all their members are disposed.
```

Parent links control cleanup only. Reparenting preserves transforms, render
groups, direct resource ownership and callbacks. Each active container counts
against the shared 256-container limit, regardless of depth. Descendant callbacks
are cleared when their ancestor is disposed. Resource transfers out of a subtree
survive its disposal, unless another ordinary cleanup rule applies (for example,
a light attached to an object being removed). Disposing a child leaves its
parent and siblings intact. Closing an already disposed subtree is harmless.

Capture/save includes every descendant and preserves direct ownership in v11 and later.
Restore returns `containerId` for the restored root, all fresh `containerIds` and
`containerMap` from source IDs. Older snapshots restore as one flat container.
Root `0` cannot be disposed, reparented or captured. JavaScript variables and
closures are not nested resources and are not serialized.

Run [nested-containers.js](../scripts/nested-containers.js) for reparenting,
recursive disposal and subtree restore.

## Input callbacks

`onInputEvent(callback, {container: id})` assigns the existing single input
callback to a container. Omitting options or using `{container: 0}` gives it
root lifetime. Registration still replaces the previous callback; containers do
not add multiple subscriptions or restore previously replaced callbacks when
a container closes. Ownership controls lifetime, not event filtering: the
callback still receives scene-wide events. Invalid ownership leaves the previous callback
installed. Register again with a different owner to transfer callback lifetime.

Disposal clears the callback only if that container still owns it. A callback
may dispose its own container safely; the current call finishes and subsequent
events have no callback to invoke unless a replacement has been registered.
The independent `pollInputEvents()` queue remains available to the root script.
Disposal does not erase JavaScript variables, closures or the global `update()`
function; scripts must stop using their removed resource handles.

Keep a tool-owned callback safe across closing and reopening: [containers.js](../scripts/containers.js).

## Light ownership

`createPointLight({container: id})` and `setPointLight(light, {container: id})`
assign or transfer light ownership. `createSpotLight` and `setSpotLight` use the
same `container` option, as do `createDirectionalLight` and
`setDirectionalLight`. Closing a container removes all three light types.
Object attachments do not imply container ownership: removing the attached object
also removes the light, but transferring an object does not transfer its lights.
See [dynamic point lights](lighting-api.md#dynamic-point-lights) for offset and
snapshot-boundary rules. Complete tools: [lights.js](../scripts/lights.js),
[spotlights.js](../scripts/spotlights.js) and
[directional-lights.js](../scripts/directional-lights.js).

## Cleanup and shared resources

Owned object removal uses the same path as `removeObject`: physics bodies,
attached joints, animation poses, interaction registration and hover/pointer
state are removed. Held-object cancellation follows the normal manipulation
cleanup path. A joint connecting an owned object to an outside object is also
removed; the outside object survives. No joint-break event is fabricated.

Owned panel removal uses normal deferred panel retirement: it immediately leaves
the live scene, and native overlays detach before textures are destroyed. Shared
render surfaces remain while another member uses them, even when that member
belongs to another container or the root. A last-member removal releases the
surface through normal cleanup.

Model assets, textures, full/LOD meshes and render-group policies are scene-wide
resources. Closing a container does not unload assets or reset shared rendering
policy. Assets remain cached until explicitly released with `unloadModel(assetId)` or
until scene replacement/shutdown, supporting reopening tools. Consequently, closing the last tool
does not imply zero reported GPU allocation. The existing scene limits on assets,
objects and overlays continue to apply.

Failed script reloads preserve the active containers, callback ownership and
resources. Successful replacement discards the entire old scene, including all
containers. See [scene lifecycle](lifecycle.md) and the
[containers.js example](../scripts/containers.js).

## Scene persistence

Use `captureSceneContainer(id)` / `restoreSceneContainer(snapshot)` for in-memory
JSON snapshots and `saveSceneContainer(id, name)` / `loadSceneContainer(name)` for
named files. See [scene save and restore](scene-persistence.md) for the complete
v12 schema (including v1–v11 compatibility), panels and pixels, animation
playback, physics bodies/joints, storage paths,
fresh object/joint/group mappings, limits, and unsupported resources.

Capture and restore a tool without a save file: [scene-snapshot.js](../scripts/scene-snapshot.js).

## Releasing unused model assets

```js
const asset = loadModel('assets/samples/Box/Box.glb');
const tool = createSceneContainer();
createObject(asset, 0, 0, 0, .15, {container: tool});
disposeSceneContainer(tool);
unloadModel(asset); // Throws if any live object still references the asset.
```

`unloadModel(assetId)` explicitly releases an unused model's CPU mesh/animation/
picking data, model GPU buffers/textures, optional LOD budget and cached physics
hull/mesh geometry. Shared lighting environments survive while another asset
references them. Hidden and animated objects count as live users; closing one
container does not release a model still used elsewhere. Unload never removes
objects for you. Unknown or already unloaded IDs throw.

Asset IDs are not reused or renumbered. Loading a released path produces a fresh
ID and permits new load options. Up to 32 models may be loaded simultaneously;
released IDs leave small metadata slots until scene replacement, so repeated
load/release does not retain model payload but is not zero-overhead. General
shader caches, reusable rendering buffers and allocator/driver caches may remain;
CPU RSS and total GPU estimates need not immediately drop to zero.

Snapshots retain model paths/options rather than asset handles or buffers.
Restoring a saved subtree can reload a released path; conflicting options for a
currently loaded path still throw. Failed restore/reload leaves the active scene
intact. Run [asset-lifetime.js](../scripts/asset-lifetime.js) to close/release,
reopen and restore a nested model tool.
