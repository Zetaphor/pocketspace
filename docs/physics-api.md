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
returns unheld boxes to their starting area. There is no implicit floor or
collision with the physical room; create static bodies for scene boundaries.

## Body configuration

`setObjectPhysics(objectId, options = {})` creates or replaces a body and returns
its state. Omitted options preserve existing settings, or use the defaults below
for a new body. Replacement resets velocity and sleep state. An invalid
configuration throws while preserving the previous body.

| Option | Default | Supported values |
| --- | --- | --- |
| `type` | `'dynamic'` | `'static'`, `'dynamic'`, `'kinematic'`. |
| `shape` | `'box'` | `'box'` or `'sphere'`. |
| `mass` | `1` | Kilograms, 0.001–10000; used by dynamic bodies. |
| `friction` | `0.5` | 0–1. |
| `restitution` | `0` | Bounciness, 0–1. |
| `linearDamping` | `0.05` | 0–1. |
| `angularDamping` | `0.05` | 0–1. |
| `gravityFactor` | `1` | 0–10. |

Box colliders follow the model's normalized default-pose bounds, including their
local center, multiplied by object scale. Thin dimensions have a one-millimeter
minimum half-extent. A sphere uses the largest bounds half-extent as its radius;
it is a spherical approximation of the model. Colliders follow object rotation.
They do not deform with node or skeletal animation. Physics collision shapes
are separate from the exact triangles used for pointer selection.

Static bodies remain fixed unless explicitly moved. Dynamic bodies respond to
gravity, contacts, and impulses. Kinematic bodies follow script or hand targets
and can push dynamic bodies; gravity and contact forces do not move them.
Static/kinematic pairs do not provide a movable physical response.

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
`angularVelocity: {x,y,z}`, `held`, and `sleeping`. It is the current simulation
snapshot. `sleeping` means the body is inactive, including stationary static
bodies. Euler angles may wrap while representing the same orientation.

The existing position/rotation setters teleport static and dynamic bodies while
retaining velocity. On kinematic bodies they set the next simulation target.
`setObjectScale` rebuilds the collider and preserves dynamic velocity.
Visibility affects drawing/selection; a hidden body remains in the simulation.
There are no collision callbacks in this release.

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
