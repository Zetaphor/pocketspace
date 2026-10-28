# Scene containers

Containers give related objects, panels and an optionally owned input callback
one lifetime. They are flat cleanup scopes, independent of render groups and
object transforms. Existing scripts omit ownership and keep the same root-scene
lifetime. Persistence/save files and nested containers are not part of this API.

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
| `createSceneContainer()` | Return a new positive integer ID. Up to 256 containers can be active; disposed IDs are not reused within a scene. |
| `getSceneContainerState(id)` | Return `{id, disposed, objectIds, panelIds, inputCallback}`. Arrays describe current live members in scene order and are snapshots. Previously disposed IDs return empty membership and `disposed: true`. Unknown IDs throw. |
| `disposeSceneContainer(id)` | Dispose all current members. Repeating disposal of the same issued ID is harmless; unknown IDs and root ID 0 throw. Returns `undefined`. |
| `setObjectContainer(objectId, containerId)` | Transfer a live object to an active container. Use 0 to transfer it to the root scene. Returns `undefined`. |
| `setPanelContainer(panel, containerId)` | Transfer a live panel handle or panel ID; 0 selects the root. Returns `undefined`. |

`createObject(..., {container: id})` and `createPanel({container: id, ...})`
assign ownership during creation. Omitted/undefined ownership and explicit 0
select the root scene. Container IDs must be numbers containing a nonnegative
integer; strings, null, fractional values, unknown and disposed IDs throw.
Validation precedes creation/reservation. If another options getter disposes
the requested owner, creation fails rather than leaving an orphaned member.

`getSceneContainerState` and disposal require an issued positive ID, including
already disposed IDs. Resources must be live when transferred. Removing a member
individually automatically removes it from the reported membership. Container
handles are local to one script scene and reset on successful scene replacement.

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
policy. Assets remain cached until scene replacement/shutdown, avoiding dangling
references and supporting reopening tools. Consequently, closing the last tool
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
v3 schema (including v1/v2 compatibility), animation playback, physics
bodies/joints, storage paths,
fresh object/joint/group mappings, limits, and unsupported resources.
