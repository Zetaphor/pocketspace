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
| `shape` | `'box'` | `'box'`, `'sphere'`, `'convex'`, or `'mesh'` (static only). |
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

## Static mesh colliders

```js
const platform = createObject(loadModel('assets/physics/RingRamp.glb'), 0, -.2, 0, 1);
setObjectPhysics(platform, {type: 'static', shape: 'mesh'});
```

Mesh colliders use the model's default-pose triangles, preserving concave regions,
openings, and slopes. Set `type: 'static'` explicitly when creating a new mesh
body; dynamic and kinematic mesh bodies are rejected. Use box, sphere, or convex
colliders for moving bodies. Static mesh pairs do not collide with one another.

The engine welds shared vertices, removes duplicate/degenerate triangles, and
caches a collision tree per model asset in each scene. A mesh with no valid
triangles throws while preserving the previous body. Instances and scale changes
reuse the tree. Scale must be at least 0.000001 meters. `halfExtents` and `radius`
are unavailable; `offset` relocates the bounds center in local meters.

Triangles collide on their front side, determined by counterclockwise winding.
Material `doubleSided` affects rendering, not physics. Give walls and platforms
closed geometry when both sides should collide. Meshes remain rigid: animation,
skinning, alpha masking, and blended transparency do not change the collider.
`setObjectPosition` and rotation setters can reposition static meshes, but physics
forces do not move them.

Run [mesh-colliders.js](../scripts/mesh-colliders.js) for a blue platform with a
central opening and a raised rear ramp, above a gray catch floor. Grab the yellow
ball or red cube with grip or trigger. Drop them through the opening or onto the
ramp; either bumper resets unheld objects.

## Joints

`createPhysicsJoint(objectA, objectB, options = {})` returns a scene-local joint
ID. Both objects must already have physics bodies, must differ, and at least one
must be dynamic. Joints connect bodies; they do not create visible ropes or hinges.

```js
const hinge = createPhysicsJoint(frame, door, {
  type: 'hinge',
  anchor: {x: -.5, y: 0, z: 0},
  axis: {x: 0, y: 1, z: 0},
  minAngle: -1.2,
  maxAngle: 1.2
});
```

| Option | Default | Meaning |
| --- | --- | --- |
| `type` | `'fixed'` | `'fixed'`, `'hinge'`, or `'distance'`. |
| `anchor` | Midpoint of object origins | Fixed/hinge attachment point in scene meters, +Z forward. |
| `axis` | `{x:0,y:1,z:0}` | Hinge rotation axis in the same axes/signs as angular velocity; normalized internally. Must be finite and nonzero. |
| `minAngle` | `-Math.PI` | Hinge lower limit in radians, between `-Math.PI` and `0`. |
| `maxAngle` | `Math.PI` | Hinge upper limit in radians, between `0` and `Math.PI`. |
| `anchorA`, `anchorB` | Respective object origins | Distance attachment points in scene meters. |
| `minDistance` | `0` | Minimum anchor separation in meters, 0–10000. |
| `maxDistance` | Initial anchor separation | Maximum separation in meters, at least `minDistance`, at most 10000. |
| `collideConnected` | `false` | Allow contacts between the two connected bodies. |
| `spring` | `null` | Distance-only `{frequency, damping}`; softens the distance limits. See below. |
| `breakForce` | `null` | Break above this resultant force, 0.001–1000000000 newtons; all joint types. |
| `breakTorque` | `null` | Break above this resultant torque, 0.001–1000000000 newton-meters; fixed/hinge only. |

A fixed joint preserves the initial relative position and orientation. A hinge
keeps its anchor points together and allows rotation only around its axis; its
initial relative orientation defines angle zero. Its limits must include zero;
use a fixed joint when both limits would be zero. A distance joint limits anchor
separation while allowing free rotation. Set equal distance limits for a rigid
link, or use zero minimum for a tether. Anchors and hinge axes are supplied in
world/scene space at creation, then remain attached to their bodies. Options
for the wrong joint type throw. There are at most 512 joints per scene.

`getPhysicsJoint(id)` returns `null` for an absent joint, otherwise:

- `id`, `objectA`, `objectB`, `type`, and `collideConnected`.
- `anchorA` and `anchorB`: current attachment positions in scene meters.
- `distance`: current anchor separation in meters.
- `spring`: resolved `{frequency,damping}` for a spring distance joint, otherwise `null`.
- `breakForce` and `breakTorque`: configured thresholds, or `null` when disabled.
- Hinge `axis`, `angle`, `minAngle`, and `maxAngle`; these are `null` for other types.
- Distance `minDistance` and `maxDistance`; these are `null` for other types.

`removePhysicsJoint(id)` removes a joint; removing an absent ID is harmless.
Removing either object or its physics body automatically removes its joints.
Reloading a scene releases all joints. To replace body settings or scale a
connected object, remove its joints first and recreate them afterward. Rejected
body replacement or scale changes leave the body, visual scale, and joints intact.
Transform setters still teleport bodies; reposition both ends or recreate joints
when resetting a connected assembly.

Connected bodies suppress mutual contacts by default. If multiple joints disable
contacts on the same pair, contacts stay disabled until all those joints are
removed. Other objects still collide normally. Jointed dynamic objects remain
dynamic while held, with a bounded drive toward the hand target so the solver
can enforce the connection. They can resist hand motion at a hinge or distance
limit. `getObjectPhysics(id).jointed` reports whether any joints are attached.
Joints use an iterative solver, so small positional/angle errors under load are
expected. Hinge motors and distance springs are configurable as described below.
Runtime joint-limit/spring/threshold editing is not exposed; remove and recreate
a joint to change its anchors or limits.

Run [joints.js](../scripts/joints.js) for a blue hinged panel, a tethered yellow
ball, and a red box welded to an avocado. Grip or trigger grabs the moving
objects. Either bumper resets the assemblies when nothing is held.

## Breakable joints

Add `breakForce` or `breakTorque` when creating a joint. Omitted/null thresholds
leave that failure mode disabled; exceeding either configured threshold breaks
the connection. A distance joint supports force breaking only, including when
configured as a spring. Fixed and hinge joints also support torque breaking.

```js
const link = createPhysicsJoint(anchor, weight, {
  type: 'distance', minDistance: .3, maxDistance: .3, breakForce: 10
});
onInputEvent(event => {
  if (event.type === 'physics.jointBreak' && event.jointId === link) {
    print('Connection released');
  }
});
```

Loads are estimated from the solver's accumulated velocity impulses divided by
the fixed 1/90-second step. Force is the resultant linear load; torque is the
resultant angular load, including a hinge's motor and angle-limit contributions.
These are solver estimates, not geometric strain or material-strength simulation.
Breaking tests run after each completed step, using a strict greater-than check;
there is no fatigue accumulation. A joint can oppose motion during its final step
before it is removed. Existing collision filtering and motor sleeping rules are
restored through the normal joint-removal path. Held objects remain held and can
move freely once their last joint breaks.

`physics.jointBreak` is delivered once per automatically broken joint through
both `onInputEvent` and `pollInputEvents`. It carries `jointId`, `objectId`
(the creation-time object A), `otherObjectId` (object B), `force` in newtons, and
`torque` in newton-meters. It has no collision `point`/`normal` fields. Delivery is
independent of `setPhysicsCollisionEvents`; controller input loss does not clear
pending break events. Each delivery queue retains at most 4096 events, dropping
older events in batches when full.

The joint is already absent when the event is delivered, so `getPhysicsJoint`
returns `null`. Explicit removal, body/object deletion, and scene reload do not
emit break events. Reload discards pending events. Create a new joint to reconnect
objects or change a threshold; IDs are not reused within a scene.

Run [breakable-joints.js](../scripts/breakable-joints.js) for a suspended blue box
with a breakable fixed connection and a yellow ball with a breakable tether.
Grab and pull to release their connections, then drop them onto the floor. Either
bumper reconnects and resets both when nothing is held.

## Distance springs

Add `spring` when creating a distance joint to make its limits elastic:

```js
const suspension = createPhysicsJoint(anchor, weight, {
  type: 'distance', minDistance: .3, maxDistance: .3,
  spring: {frequency: 2, damping: .15}
});
```

Equal minimum/maximum distances define the unloaded resting length. The spring
pushes apart compressed anchors and pulls together stretched anchors, while
allowing free rotation. With different limits, the region between them is slack;
restoring force acts only outside the range. A spring softens those limits, so
anchors can move beyond them under load. Gravity stretches a hanging spring's
equilibrium distance beyond its resting length.

`spring: null` or omission retains the existing hard distance behavior. A spring
object defaults to `frequency: 2` hertz and `damping: 0.5`. Frequency must be
0.01–30 hertz and damping ratio 0–10; both must be finite numbers. Higher
frequency means a stiffer response. Damping below one allows more bouncing;
one critically damps the spring's axial response. Coupled bodies and swinging
motion can affect the overall settling behavior. Non-null spring settings on
fixed/hinge joints throw.

`getPhysicsJoint(id).spring` reports the resolved settings. Springs use normal
body sleeping, collision, grabbing, cleanup, and reload behavior. Their anchors
are invisible, as with other joints. Remove and recreate the joint to change its
resting length or spring settings. Spring forces are not capped by hinge motor
`maxTorque`; a spring distance joint has no motor.

Run [springs.js](../scripts/springs.js) for a bouncy yellow ball and a more heavily
damped blue box suspended from separate anchors. Grip or trigger grabs either
weight. Pull down, release, and compare how they settle. Either bumper resets
both weights when nothing is held.

## Hinge motors

`setPhysicsJointMotor(id, options)` configures an existing hinge motor and returns
the updated joint state. Omitted fields preserve their previous values. Other
joint types and invalid settings throw without changing the motor.

```js
setPhysicsJointMotor(hinge, {
  mode: 'position', targetAngle: .9,
  maxTorque: .15, frequency: 3, damping: 1
});
// A partial update retains torque and response settings.
setPhysicsJointMotor(hinge, {targetAngle: 0});
setPhysicsJointMotor(hinge, {mode: 'velocity', targetVelocity: .8});
setPhysicsJointMotor(hinge, {mode: 'off'});
```

| Option | Default | Meaning |
| --- | --- | --- |
| `mode` | `'off'` | `'off'`, `'position'`, or `'velocity'`. |
| `targetAngle` | `0` | Position target in radians, within the hinge's angle limits. |
| `targetVelocity` | `0` | Velocity target in radians/second, −30 to 30; positive increases the reported hinge angle before it wraps. |
| `maxTorque` | `1` | Maximum motor torque in either direction, 0–10000 newton-meters. Zero supplies no motor torque. |
| `frequency` | `2` | Position response frequency, 0.01–30 hertz. Higher values produce a stiffer response. |
| `damping` | `1` | Position damping ratio, 0–10. One is critical damping for the motor's angular response; body geometry and coupled joints affect the full assembly. |

Position mode drives toward the target angle using a damped spring. Velocity
mode drives toward the target speed. Both obey the torque cap and hinge limits;
loads, collisions, and hand motion can prevent reaching the target. Frequency
and damping are retained in velocity/off mode but only affect position mode.
Switching off removes motor drive; it does not brake or reset current velocity.
The normal body damping still applies.

`getPhysicsJoint(id).motor` reports the resolved configuration above for hinges,
including when off. It is `null` for fixed/distance joints. Enabled motors with
nonzero torque keep their connected moving bodies awake. Changing settings wakes
the assembly; switching off, setting zero torque, or removing the last powered
joint restores normal sleeping. Motor settings follow the joint's lifetime and
are discarded on removal/reload. Motors remain active during grabbing.

Run [motors.js](../scripts/motors.js) for a blue panel driven to an angle and a
green panel driven at a constant speed. Either bumper toggles the blue panel
between closed and open; A or X toggles power for both motors. Grip or trigger
can grab either panel to oppose the motor, then release it to resume motion.

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
`halfExtents` (box, convex, or mesh bounds), `radius` (sphere only), and `offset`.
It also reports object-local `centerOfMass`, convex `vertexCount`, and mesh
`triangleCount`; counts are `null` for other shapes. Meshes have no physical mass
center; their `centerOfMass` is the shape coordinate origin after scaling/offset. The inactive size field is `null`. Resolved values are in meters after automatic model scaling and
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
require exact visual mesh overlap. Collision shapes remain rigid box, sphere, convex, or static mesh shapes.

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
