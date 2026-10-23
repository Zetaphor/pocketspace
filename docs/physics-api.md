# Rigid-body physics

Physics is opt-in per object. Objects without a body retain their existing
scripted transforms and grabbing behavior. The engine uses
[Jolt Physics](https://github.com/jrouwe/JoltPhysics), pinned to version 5.6.0
(revision `e77f175595e64cb44218cc9d9d56fc365ad0e36a`). Simulation runs on the CPU
with a single-threaded job system. No GPU compute backend is enabled.

```js
const asset = loadModel('assets/samples/Box/Box.glb');
const box = createObject(asset, 0, .3, 0, .2);
setObjectPhysics(box, {type: 'dynamic', mass: .25});
enableObjectInteraction(box, {grabButton: 'both'});
```

Run [physics.js](../scripts/physics.js) for three grabbable boxes above a static
platform. Pick up, stack, or throw them with grip or trigger. Either bumper
returns unheld boxes to their starting area. The retained panel shows the number
of touching object pairs and the latest contact's closing speed. There is no implicit floor or
collision with the physical room; create static bodies for scene boundaries.

## Body configuration

`setObjectPhysics(objectId, options = {})` creates or replaces a body and returns
its state. Omitted options preserve existing settings, or use the defaults below
for a new body. Replacement resets velocity and sleep state. An invalid
configuration throws while preserving the previous body.

| Option | Default | Supported values |
| --- | --- | --- |
| `type` | `'dynamic'` | `'static'`, `'dynamic'`, `'kinematic'`. |
| `shape` | `'box'` | `'box'`, `'sphere'`, or `'convex'`. |
| `halfExtents` | `null` | Box half-sizes `{x,y,z}` in meters; each 0.001–100. `null` uses model bounds. |
| `radius` | `null` | Sphere radius in meters, 0.001–100. `null` uses model bounds. |
| `offset` | `null` | Collider center `{x,y,z}` in object-local meters; each component within ±100. `null` uses the model-bounds center. |
| `mass` | `1` | Kilograms, 0.001–10000; used by dynamic bodies. |
| `friction` | `0.5` | 0–1. |
| `restitution` | `0` | Bounciness, 0–1. |
| `linearDamping` | `0.05` | 0–1. |
| `angularDamping` | `0.05` | 0–1. |
| `gravityFactor` | `1` | 0–10. |

Without custom dimensions, box colliders follow the model's normalized
default-pose bounds multiplied by object scale; thin dimensions have a
one-millimeter minimum half-extent. Automatic spheres use the largest bounds
half-extent as their radius. A box's full size is twice its half-extents.

Explicit dimensions use physical meters and are independent of visual model
scale. `halfExtents` applies only to boxes, and `radius` only to spheres; supplying
a non-null size for the wrong shape throws. Changing `shape` clears the previous
shape's custom size. Omitted options preserve existing settings; set a size or
`offset` to `null` to return that field to automatic model bounds.

An explicit `offset` places the collider center relative to the object origin;
it replaces the automatic center rather than adding to it. It uses local +X
right, +Y up, and +Z forward at zero rotation, and rotates with the object using
the existing model rotation convention. Offsets stay fixed in meters when the
visible model is scaled. For example:

```js
setObjectPhysics(box, {
  halfExtents: {x: .15, y: .05, z: .15},
  offset: {x: 0, y: -.1, z: 0}
});
```

Run [colliders.js](../scripts/colliders.js) for a thin gray floor, blue/green
rotated walls, a red box, and a yellow sphere with explicit collider dimensions.
Grip or trigger grabs the box and ball; either bumper resets unheld bodies.

Colliders follow object rotation but do not deform with node or skeletal
animation. Physics collision shapes are separate from the exact visible mesh
triangles used for pointer selection.

Static bodies remain fixed unless explicitly moved. Dynamic bodies respond to
gravity, contacts, and impulses. Kinematic bodies follow script or hand targets
and can push dynamic bodies; gravity and contact forces do not move them.
Static/kinematic pairs do not provide a movable physical response.

## Convex model colliders

```js
const avocado = createObject(loadModel('assets/Avocado.glb'), 0, .3, 0, .2);
setObjectPhysics(avocado, {shape: 'convex', mass: .25});
```

Convex colliders wrap all of a model's default-pose geometry in one solid hull.
They fit irregular objects more closely than a box, but bridge holes and concave
regions. This is not triangle-mesh collision or convex decomposition. A hull has
at most 256 vertices; complex models are approximated. Empty, flat, or otherwise
invalid hulls throw while preserving the previous body.

The engine builds and caches one normalized hull per model asset in each scene.
Instances and scale changes reuse it. Hull size follows object scale (at least
0.000001 meters); explicit
`halfExtents` and `radius` are unavailable. `offset` relocates the hull's bounds
center in local meters, using the same convention as primitive colliders.
Animation and skinning do not deform the hull: it remains a rigid default-pose
shape. Static, dynamic, and kinematic bodies all support convex shapes.

The hull's computed center of mass controls inertia, rotation, and release
velocity. It can differ from its bounds center. Run
[convex.js](../scripts/convex.js) to grab, drop, and throw a green avocado and a
pale toy car on a gray platform. Either bumper resets unheld objects.

## Controls and state

| Function | Behavior |
| --- | --- |
| `getObjectPhysics(id)` | Return body state, or `null` when a live object has no body. |
| `removeObjectPhysics(id)` | Remove simulation from an object, keeping its current rendered transform. Repeated removal is harmless. |
| `setPhysicsGravity(x, y, z)` | Set scene gravity in meters/second²; default `(0, -9.81, 0)`. |
| `setObjectVelocity(id, x, y, z)` | Set linear velocity in meters/second. |
| `setObjectAngularVelocity(id, x, y, z)` | Set angular velocity in radians/second. |
| `applyObjectImpulse(id, x, y, z)` | Apply a center-of-mass impulse in kilogram-meters/second. |

Velocity and impulse functions require an unheld dynamic body. Vector components
must be finite and within ±10000. Invalid IDs/arguments throw. Linear vectors
use scene axes (+X right, +Y up, +Z forward). Angular vectors use world axes in
the same sign convention as the model's pitch, yaw, and roll rotations; they
are angular rates, not Euler angle derivatives.

Body state includes all configuration fields, `position: {x,y,z}`,
`rotation: {x,y,z}` (pitch/yaw/roll radians), `velocity: {x,y,z}`,
`angularVelocity: {x,y,z}`, `held`, `sleeping`, and resolved `collider` data. It is the current simulation
snapshot. `sleeping` means the body is inactive, including stationary static
bodies. Euler angles may wrap while representing the same orientation.

Top-level `halfExtents`, `radius`, and `offset` report the requested values, or
`null` for automatic sizing/centering. `collider` contains the actual physical
`halfExtents` (box or convex bounds), `radius` (sphere only), and `offset`.
It also reports object-local `centerOfMass` and convex `vertexCount` (null for
primitives). The inactive size field is `null`. Resolved values are in meters after automatic model scaling and
minimum thickness are applied.

`position` remains the object origin. Linear velocity and impulses refer to the
body's center of mass, which can differ from that origin due to an offset or
an asymmetric convex hull. Throwing
accounts for center-of-mass motion caused by rotation as well as translation.

The existing position/rotation setters teleport static and dynamic bodies while
retaining velocity. On kinematic bodies they set the next simulation target.
`setObjectScale` rebuilds the collider and preserves dynamic velocity. Automatic
sizes/centers follow the new model scale; explicit sizes/offsets stay unchanged.
Visibility affects drawing/selection; a hidden body remains in the simulation.
Collision event delivery is opt-in as described below.

## Collision events

`setPhysicsCollisionEvents(enabled, options = {})` controls contact events for
the current scene. `enabled` must be a boolean; optional `stay` must also be a
boolean and defaults to `false`. Events are disabled by default. Enabling wakes
existing nonstatic bodies so their contacts can be observed on the next step.
Calling it again while enabled changes the stay setting without restarting pairs.

```js
setPhysicsCollisionEvents(true); // start/end; add {stay: true} for continued contact
onInputEvent(event => {
  if (event.type === 'physics.collisionStart') {
    print(`${event.objectId} hit ${event.otherObjectId} at ${event.relativeSpeed} m/s`);
  }
});
```

Both `onInputEvent` and `pollInputEvents` receive the same collision schema:

| Field | Meaning |
| --- | --- |
| `type` | `physics.collisionStart`, `physics.collisionStay`, or `physics.collisionEnd`. |
| `objectId`, `otherObjectId` | Object IDs in ascending order; one event per pair, not one for each object or contact point. |
| `point` | Representative contact midpoint `{x,y,z}` in scene meters. |
| `normal` | Unit `{x,y,z}` pointing from `objectId` toward `otherObjectId`, in scene axes. |
| `penetration` | Nonnegative contact penetration in meters. |
| `relativeSpeed` | Nonnegative pre-solve closing speed at the representative point, in m/s; includes angular motion. It is not an impulse or damage value. |

Multiple manifolds for a pair are combined: the deepest observed manifold
supplies the point/normal, and the maximum observed closing speed is reported.
Contacts include the solver's speculative near contacts, so a start does not
require exact visual mesh overlap. Collision shapes remain rigid box, sphere, or convex proxies.

A start is emitted on the first observed contact. If requested, stay is emitted
once per subsequent fixed physics step, including sleeping pairs. Sleeping
stays retain the last geometry and report zero closing speed. Catch-up can
produce multiple events per rendered frame. An end reports the last known
contact data when a pair separates, either collider is removed/replaced, or a
changed pose is explicitly teleported. An end can reference an object already
removed from the scene; do not assume both IDs still exist when handling it.

The native solver only records data. Events enter the ordinary independent
callback/polling queues after physics advances and are delivered through the
host loop, never from inside the solver. Treat them as snapshots: a preceding
callback can change/remove the referenced objects. Raw controller disconnection
and reconnect do not discard pending collision events.

Disabling clears tracked contacts and pending collision events without emitting
ends. A callback batch already being delivered remains a snapshot of that batch.
Enabling again establishes new starts on the next observed contacts. Successful
scene reload clears event state; a failed candidate cannot change the active
scene's subscription or queues. Module and delivery queues are bounded at 4096,
evicting the oldest 1024 entries on overflow. Enable stay only when needed and
drain polling regularly if your script uses it.

## Grabbing, timing, and lifecycle

Native grabbing temporarily makes dynamic bodies kinematic. They follow the hand
and can push other dynamic bodies. A normal release restores dynamics and uses
roughly the last 100 ms of held motion for linear/angular throwing velocity,
capped at 20 m/s and 30 rad/s. Held objects can intersect static geometry;
contacts resolve after release. No grab spring or hand collision body is added.

Forced cancellation, controller disconnection, raw-input loss, or tracking loss
does not generate a throw. A released body still responds to gravity. Raw-button
release retains the existing five-frame debounce.

Simulation uses actual elapsed time with fixed 1/90-second steps, at most eight
catch-up steps per frame; excess elapsed time is discarded after stalls.
Resting dynamic bodies can sleep. Existing JavaScript `update(dt)` retains its
fixed argument. Physics advances after script updates and before rendering.
The runtime profile log includes `physics_ms`, covering synchronization and
simulation, alongside picking/render/GPU wait timing.

Each scene owns its physics world. Object removal removes its body. Successful
reload destroys the old world; a failed candidate leaves the active world alone.
Scenes without physics allocate no world. Personal gravity settings reset on
scene replacement.

## Building

CMake 3.20 or newer downloads the pinned, SHA-256-verified Jolt archive on first
configuration. For offline builds, pass
`-DFETCHCONTENT_SOURCE_DIR_JOLT=/absolute/path/to/JoltPhysics` with that revision.
The bundled [license](../third_party/Jolt-LICENSE) covers the dependency.
