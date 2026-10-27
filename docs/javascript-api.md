# JavaScript API

Scripts execute as QuickJS globals, without Node.js/browser modules, filesystem
APIs, or timers. Define an optional `update(dt)` function at load time. The host
caches it and supplies `1 / 90` seconds at its 90 Hz target; use `Date.now()`
when actual elapsed time matters. Native animation uses elapsed wall time.
`print(...values)` writes to the engine log. Relative asset paths resolve from
the process working directory; the launcher uses the repository root.

## Engine statistics

`getEngineStats()` returns the latest cached, process-wide sample and current-scene physics timings. Process counters are
sampled roughly once per second; querying does not read `/proc` or synchronize
the GPU. Scene counts are current at the time of the query.

| Field | Meaning |
| --- | --- |
| `fps` | Measured engine-loop frames per elapsed second, including rendering and pacing; not the HMD/compositor presentation rate. |
| `cpuPercent` | Process user+system CPU time divided by elapsed time. 100% means one fully occupied core; multithreaded usage can exceed 100%. |
| `ramBytes` | Process resident RAM (`VmRSS`), in bytes. |
| `vramBytes` | Driver-reported process GPU memory total from DRM fdinfo, in bytes, or `null` when unavailable. Duplicate DRM clients are counted once. |
| `gpuAllocatedEstimateBytes` | Estimated engine GPU allocation size in bytes: allocated stereo color/depth buffers, model textures and vertex/index buffers, deformed VBOs, and panel textures. |
| `sampleId` | Increasing sample number; zero before the first sample. |
| `physicsAdvanceMs` | Wall time of the latest physics-world advance in milliseconds, including solver, contact handling, and joint breaking; excludes host pose synchronization. |
| `physicsSolverMs` | Solver wall time summed over the latest advance's fixed steps, including worker waits. |
| `physicsSteps` | Fixed steps in the latest advance (0–8). |
| `physicsActiveBodies` | Active rigid bodies after the latest advance. |
| `physicsWorkers` | Additional worker threads used by the latest advance; zero when no step ran. |
| `sampleSeconds` | Elapsed seconds covered by the latest sample; zero before sampling. |
| `objectCount`, `panelCount` | Current scene counts, including hidden instances. |

FPS, CPU, RAM, and memory estimates are `null` until sampled. Unavailable measured
counters remain `null`. The GPU estimate excludes driver/compositor overhead,
allocator padding except measured color-buffer stride, and small utility textures.
It assumes four bytes per depth pixel, counts shared depth attachments once,
and is not measured GPU residency. RAM and
GPU values can overlap on systems with shared memory; do not add them together.
Steam Frame GPU memory uses shared system RAM rather than a separate VRAM pool.

Run [stress.js](../scripts/stress.js) for colored, grabbable physics boxes and a
statistics panel including solver time, active bodies, and worker count. Either bumper adds one object; A or X adds five; B or Y clears
the spawned objects. The scene starts with four boxes and caps at 51 spawned boxes.
Spawning stops on a creation error and displays the reason on the HUD; clear or
remove objects to free resources before trying again. The engine's 256-object
limit and SteamVR's system-wide overlay budget still apply. Each model uses one
overlay, in addition to the HUD and two controller lasers.
Boxes below scene Y = −1.2 meters or more than 3 meters from the scene origin
are removed every 100 ms, reclaiming their physics body, overlay buffers, and
spawn capacity. Shared model assets remain cached for subsequent spawns.
It uses the normal 768-pixel-per-eye object surfaces. The HUD refreshes twice per
second; the underlying statistics sample once per second. When measured GPU
memory is unavailable it explicitly displays the allocation estimate instead.

## Coordinates and models

Scene coordinates are meters: +X right, +Y up, +Z forward. The fixed standing-space
anchor starts one meter ahead and 0.2 meters below the initial HMD position.
Imported geometry is centered and normalized to a unit maximum default-pose
extent. Object scale specifies that extent in meters. Animated poses can extend
beyond it. Positions and rotations below are relative to the scene anchor.

| Function | Behavior |
| --- | --- |
| `loadModel(path, eyePixels = 768)` | Load a GLB and return an asset ID. Reference resolution must be a multiple of 64 from 256 through 2048. Runtime buffers may crop transparent margins at the same model pixel density. The same exact path is cached; a conflicting explicit resolution throws. Up to 32 assets per scene. |
| `createObject(assetId, x, y, z, scale, options = {})` | Return an object ID; finite coordinates and scale greater than zero and at most 2 meters are required. Up to 256 objects. Optional `renderGroup` is an integer from 0 through 256; zero gives an independent overlay, positive IDs share a stereo surface. |
| `setObjectPosition(id, x, y, z)` | Set position. |
| `setObjectYaw(id, radians)` | Set yaw. |
| `setObjectRotation(id, pitch, yaw, roll)` | Set rotation in radians. |
| `setObjectScale(id, scale)` | Set positive scale, at most 2 meters. |
| `setObjectVisible(id, visible)` | Show/hide rendering and selection. |
| `removeObject(id)` | Remove the instance and its interaction state. |

Invalid IDs and invalid arguments throw. During live scene updates, object
creation reserves the native handles its surface needs and throws if SteamVR
cannot allocate them; a failed reservation leaves no object behind. Removal of
the last surface member releases its handles immediately so subsequent spawns
can reuse the available capacity. Assets remain loaded until the scene
is replaced. Instances share source materials/textures; object transforms and
animation playback remain independent. Render resolution trades clarity for
GPU time and memory. By default each object has its own surface. Opaque and
alpha-masked models occlude one another using per-eye geometry depth before
surface submission. Occluder drawing is limited to conservative target bounds;
near-plane cases retain the full viewport. Each surface keeps three independent
color buffers and reuses one depth attachment while rendering them. Masked holes remain transparent. Blended materials are not
opaque occluders; ordering between blended surfaces remains compositor-dependent.

Object surfaces crop centered, transparent borders using conservative bounds from
both eyes. Pixel count and physical overlay width shrink together, preserving
the reference pixel spacing and centered sample grid; `loadModel` still defaults
to a 768-pixel reference. This removes unused pixels rather than scaling the
model into fewer samples. Near-plane crossings or invalid bounds retain the full
reference surface. Buffers grow immediately when required, and shrink after 45
consecutive eligible frames, with at most two shrinks per frame. Resize keeps the
overlay handle and interaction state; the native intersection mask retains its
reference physical extent within the cropped surface. Allocation estimates count
the actual current buffers, including crops.


### Choosing an object rendering approach

Scripts choose rendering per object through the final `createObject` options.
The choice is independent of the model asset: a group may contain boxes,
detailed models, animated instances, and static scenery together.

| Approach | `renderGroup` | Visible 3D surfaces | Native handles |
| --- | --- | --- | --- |
| Independent objects (default) | Omit it or use `0` | One per visible object | One per allocated object surface. |
| Single scene surface | Give all 3D objects the same positive ID | One for the whole scene | Two for the shared surface, including hidden staging. |
| Multiple grouped surfaces | Assign different positive IDs to subsets | One per nonempty visible group | Two per allocated group, including hidden staging. |

For example, these are alternative ways to create the same content:

```js
// Independent surfaces: native rectangular input can be enabled per object.
createObject(floorAsset, 0, -.55, 0, 1.2);
createObject(boxAsset, -.2, 0, 0, .14);
createObject(carAsset,  .2, 0, 0, .14, {renderGroup: 0});
```

```js
// One visible stereo surface: include the floor and every 3D model in group 1.
createObject(floorAsset, 0, -.55, 0, 1.2, {renderGroup: 1});
createObject(boxAsset, -.2, 0, 0, .14, {renderGroup: 1});
createObject(carAsset,  .2, 0, 0, .14, {renderGroup: 1});
```

```js
// Separate groups can concentrate pixels around different parts of the scene.
createObject(floorAsset, 0, -.55, 0, 1.2, {renderGroup: 3});
createObject(boxAsset, -.2, 0, 0, .14, {renderGroup: 1});
createObject(carAsset,  .2, 0, 0, .14, {renderGroup: 2});
```

Configure physics and grabbing per instance with `setObjectPhysics` and
`enableObjectInteraction` as usual. Group IDs are scene-local integer labels
from 1 through 256, not object IDs.
Membership is fixed at creation. Changing an object's group requires removing
and recreating it; the new object has a new ID, and physics, interaction, and
animation settings must be applied again. Existing objects are never migrated
when another object uses a different group ID.

A single surface reduces repeated scene traversal and depth passes, but must
cover all its members at one sampling density. Large or widely separated groups
can allocate more GPU memory or reach the resolution ceiling. Several compact
groups may be faster. Use `getEngineStats` and visual checks for the intended
scene instead of assuming one approach always wins. Panels created with
`createPanel` and controller lasers remain separate overlays in all three modes;
"single scene surface" refers to 3D geometry, not every overlay owned by the app.

### Shared render group behavior

`createObject(asset, x, y, z, scale, {renderGroup: 1})` opts an object into a
shared stereo surface. Each group uses two overlay handles, alternating a
visible overlay and a hidden staging overlay. Transform, width, and texture are
prepared on the hidden overlay before it is shown; the previously visible
overlay is then hidden. Group membership is fixed at creation and scoped to the
scene. Physics, transforms, animation, visibility, mesh picking, and grabbing
remain per object. Native rectangular pointer state/events are unavailable for
grouped objects; use mesh hover/controller events and the interaction helper.
The shared surface passes native overlay input through to other applications.

A live creation reserves both handles when its group needs them; failure rolls
back the reservation before publishing the object. Removing the last member
releases both. Hidden members do not render; an entirely hidden group
releases its surface during synchronization and recreates it when shown again.
Both eyes share conservative group bounds and geometry depth, including occlusion
by objects outside the group. Grouped surfaces use different texture regions on
one common projection plane so late head-pose reprojection moves box silhouettes
and their depth cutouts together. Include mutually occluding geometry such as
floors in render groups too; independent surfaces use their own planes and can
reproject differently during head movement. Alpha blending retains scene-order
limitations. Object framebuffer colors use premultiplied alpha, including masked
and blended materials, and the native overlay is configured to match.

Surface dimensions grow with projected bounds and the most demanding member's
reference angular pixel density. This can use more memory when members spread
apart or approach the headset. Resolution is bounded by 4096 pixels per eye and
half the GPU's maximum renderbuffer width; reaching that ceiling logs a warning
and reduces sampling density. Keep groups compact to avoid this limit. Buffer
growth uses 25% allocation headroom rounded to 256 pixels. Shrinking requires
the requested size to stay below 60% of the allocation for 45 frames, with at
most two shrinks per frame. A resize renders and shows a replacement overlay
before detaching the previous one; it temporarily needs another overlay pair
and buffer set. Failed allocation keeps the previous surface available. Each
surface retains three color buffers and one shared depth attachment.

[grouped-stress.js](../scripts/grouped-stress.js) uses four compact groups for up to 240
colored boxes plus a floor in its own render group and a statistics panel. Bumper adds 10, A/X
adds 40, and B/Y clears. Fallen/distant bodies are removed. The original
[stress.js](../scripts/stress.js) keeps its independent surfaces and 51-box cap.


Shared GPU meshes reuse identical position/UV vertices within each material
primitive when vertex and index buffers together use less storage. Triangle
order, UV seams, and material boundaries are preserved. Indices use 16 bits when
possible, otherwise 32 bits. Picking, collision construction, and per-instance
CPU animation retain their expanded geometry; deformed instances draw from their
own dynamic vertex buffers.

GLB triangle primitives across mesh nodes, node transforms, embedded base-color
textures, material factors, alpha modes, UV transforms, and approximate sheen
are supported. [Animation](animation-api.md) covers skinning and clip limits.

## Grabbing and selection

```js
const asset = loadModel('assets/samples/Box/Box.glb');
const box = createObject(asset, 0, 0, 0, .3);
enableObjectInteraction(box, {grabButton: 'both'});
setObjectInteraction(box, {
  laserYaw: true, depthAdjust: true,
  handRotationAxes: {pitch: true, yaw: true, roll: true}
});
setLaserVisible('left', true);
setLaserVisible('right', true);
```

`setObjectInteraction(id, options)` updates only supplied fields:

| Option | Default on a new object | Meaning |
| --- | --- | --- |
| `grabbable` | `false` | Enable native grab/movement handling. |
| `grabButton` | `'trigger'` | `'trigger'`, `'grip'`, or `'both'`; either allowed button can acquire/hold. |
| `laserPassThrough` | `true` | Let native laser input pass through the surface; grabbable forces this false. |
| `directGrab` | `true` | Use near-hand holding when acquired close to the controller. |
| `laserYaw` | `false` | Distant grab: stick left/right changes yaw. |
| `depthAdjust` | `false` | Distant grab: stick up moves farther, down moves nearer. |
| `handRotationAxes` | All `false` | `{pitch, yaw, roll}` chooses hand rotation axes for direct grabs. |

Acquisition requires a current transformed triangle hit. Empty mesh gaps never
fall back to a bounding sphere. Near-hand mode is selected after that hit when
the controller-to-object-center distance is below `0.10 + scale * 0.5` meters;
there is no proximity-only pickup. Static picking uses a shared per-asset BVH;
animated instances refit their own picking geometry. Each hand can hold its own
object. A held object releases after all allowed buttons are up for five frames.

`enableObjectInteraction(id, {grabButton})` enables native grabbing with the
chosen button. Its options object recognizes `grabButton`; configure other
fields with `setObjectInteraction`. The legacy one-argument form marks the
object interactive without newly enabling automatic movement, for scripts that
handle movement themselves. `enableObjectSurface(id)` is a compatibility no-op.

`setLaserVisible(hand, visible)` controls the engine's selection beam for
`'left'` or `'right'`. Its raw-controller grabs are independent of native laser
focus. SteamVR owns panel pointer delivery.

`getPointer(id)` returns native surface state `{valid, down, x, y, hand}`;
`hand` is `'left'`, `'right'`, or `null`. Coordinates are surface-local, not
panel pixels. This is not the custom beam's grab state. `getPointer()` is usable
only when exactly one object is interactive.

## Controllers

`getController(hand)` returns `{valid, connected, x, y, z, rotation}`.
`rotation` is a row-major 3×3 matrix. Positions use scene coordinates.
`connected` can remain true during temporary pose loss; check `valid` before
using pose data. An invalid pose returns zero position/rotation values.

`getControllerInput(hand)` reads the cached raw controller stream:

| Field | Contents |
| --- | --- |
| `valid`, `hand` | Stream validity and requested hand. |
| `raw` | Component paths mapped to boolean or finite numeric values. |
| `buttons` | `{down, touched}` for trigger, grip, bumper, thumbstick, a/b/x/y, menu, system, view, and dpad_up/down/left/right. |
| `analog` | Numeric `trigger`, `grip`, and `thumbstick: {x, y}`. |
| `capacitive` | Sensor projections, including ring, pinky, thumbrest, and dpad. |

Missing convenience values are false/zero. Unsupported structured values are
invalidated. Use [getFingerTracking](tracking-api.md) when missing values and
freshness must be distinguished from real zero.

`getInput(hand)` reads optional OpenVR action bindings. It returns `valid`,
`gripClick/Touch/Value`, `triggerClick/Touch/Value`, `thumbstick: {x,y}`,
`thumbstickClick/Touch`, `bumperClick/Touch`, `systemClick/Touch`, `menuClick`,
`viewClick`, `aClick`, `bClick`, `xClick`, `yClick`, and
`dpadUp/Down/Left/RightClick`. Action availability depends on SteamVR focus and
bindings; unavailable values are false/zero.

`pulse(hand, seconds = .05, amplitude = .5)` requests action-based haptics and
returns success. Both numbers must be finite within `[0, 1]`; availability
still depends on native action bindings. All hand arguments use `'left'` or
`'right'`.

## Events

`onInputEvent(callback)` replaces the single callback. `pollInputEvents()`
drains an independent queue, so polling does not consume callback delivery.
Delivery happens in the host loop. The bounded queue evicts the oldest 1024
entries when it exceeds 4096; consumers should drain it regularly.

Events include `type`, `hand`, `control`, `objectId`, `x`, `y`, `down`, `value`,
`dx`, `dy`, `dz`, `dpitch`, `dyaw`, and `droll`; interpret fields by event type.

| Type | Meaning |
| --- | --- |
| `controller.button` | Raw boolean path in `control`, state in `down`. |
| `controller.value` | Raw numeric path in `control`, number in `value`. |
| `physics.jointBreak` | Automatic joint release with joint/object IDs and estimated force/torque; see [breakable joints](physics-api.md#breakable-joints). |
| `physics.collisionStart`, `physics.collisionStay`, `physics.collisionEnd` | Opt-in pair contacts with object IDs, point/normal, penetration, and closing speed; see [physics events](physics-api.md#collision-events). |
| `overlay.pointer` | Native surface pointer update. |
| `overlay.surfaceHoverStart`, `overlay.surfaceHover`, `overlay.surfaceHoverEnd` | Native rectangular surface hover. |
| `overlay.meshHoverStart`, `overlay.meshHoverEnd` | Actual model hit transitions. |
| `overlay.grabStart`, `overlay.grabMove`, `overlay.grabEnd` | Native object grab lifecycle. |
| `overlay.move`, `overlay.rotate` | Object transform changes with position/rotation deltas. |
| `panel.hoverStart`, `panel.hover`, `panel.hoverEnd` | Panel hover with top-left-origin pixel coordinates. |
| `panel.pointerDown`, `panel.pointerUp` | Panel button transition; `control` is `left`, `right`, or `other`. |

Raw controller events can carry the latest native surface-hover `objectId`,
or zero. That field is not a promise that the hand owns or selected that object.
Panel IDs and object IDs are separate numeric namespaces; distinguish them by
`type`. Animation does not emit completion events; inspect playback state.

## Additional APIs

- [Panels](panels.md): `createPanel`, canvas drawing, `panelLoadFont`,
  `panelSetTransform`, `panelSetRotation`, `panelSetVisible`, `removePanel`, and
  compatibility functions `panelClear`, `panelText`, `panelPresent`,
  `panelCanvasClear`, `panelCanvasText`.
- [Physics](physics-api.md): `setObjectPhysics`, `getObjectPhysics`,
  `removeObjectPhysics`, `setPhysicsGravity`, `setObjectVelocity`,
  `setObjectAngularVelocity`, `applyObjectImpulse`, `setPhysicsCollisionEvents`,
  `createPhysicsJoint`, `getPhysicsJoint`, `setPhysicsJoint`, `removePhysicsJoint`,
  `setPhysicsJointMotor`.
- [Animation](animation-api.md): `getAnimations`, `playAnimation`, `pauseAnimation`,
  `resumeAnimation`, `stopAnimation`, `seekAnimation`, `getAnimationState`.
- [Tracking](tracking-api.md): `getEyeTracking`, `getCalibratedEyeTracking`,
  `getEyeFoveationCenters`, `getFingerTracking`.
- [Calibration](eye-calibration.md): `saveEyeTrackingCalibration`,
  `getEyeTrackingCalibration`.
- [Lifecycle](lifecycle.md): reload and resource ownership.
