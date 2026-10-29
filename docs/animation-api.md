# Animation, skeletal models and morph targets

Load a GLB, create an object, and explicitly start a clip. Objects without
playback retain the model's default pose, including default skeletal skinning and morph weights.
See [animation.js](../scripts/animation.js) for clip selection and
[skinning.js](../scripts/skinning.js) for independent character playback.

```js
const asset = loadModel('assets/samples/CesiumMan/CesiumMan.glb');
const actor = createObject(asset, 0, 0, 0, .6);
playAnimation(actor, 0, {loop: true, speed: 1});
```

## Playback API

`getAnimations(assetId)` returns an array of
`{index, name, duration, supported, unsupportedReason}`. Duration is in seconds;
`unsupportedReason` is null for supported clips. Static models return `[]`.
Names can be empty or duplicated; use an index to select unambiguously.

| Function | Behavior |
| --- | --- |
| `playAnimation(objectId, clip=0, options={})` | Select by index or name (first match), restarting with fresh options. Defaults: `{loop: true, speed: 1, time: 0, blendDuration: 0}`. |
| `pauseAnimation(objectId)` | Freeze current time; no selected clip is a no-op. |
| `resumeAnimation(objectId)` | Require a selected clip; resume without resetting time and clear `finished`. |
| `seekAnimation(objectId, seconds)` | Evaluate immediately within `[0, duration]`; preserve paused/playing mode and clear `finished`. |
| `stopAnimation(objectId)` | Clear the selected clip and restore the default pose; reset speed/loop/time defaults. |
| `getAnimationState(objectId)` | Read playback state. |

Control calls return state with `clip` (index or null), `name` (or null),
`time`, `duration`, `speed`, `loop`, `playing`, `finished`, `transition`, and `status` (`stopped`, `playing`, `paused`, or `finished`).
Speed must be finite, greater than zero, and at most 100. Starting time must be
within the clip duration. Invalid control calls throw without replacing the
current playback state.

Looping clips wrap. Nonlooping clips hold their final pose and report
`finished: true`. Zero-duration clips finish on their next advance. Seeking a
looping clip to its duration evaluates the end pose immediately; the next
advance wraps. Hidden objects still advance. Paused poses are not reevaluated
each frame. Completion is observed through state, not an event.

## Transitions between clips

```js
playAnimation(actor, 'Walk', {blendDuration: .5});
```

`blendDuration` is a finite number from 0 to 60 seconds. Zero (the default)
replaces playback immediately. A positive duration blends from the current
local pose to the selected clip over elapsed wall time, independently of clip
speed. Translations, scales and morph weights interpolate linearly; rotations
use normalized shortest-path quaternion interpolation. Hierarchy transforms,
skinning and morph deformation are applied after local pose blending.

The outgoing and incoming clips both advance using their own time, speed and
loop settings. A paused/finished outgoing clip stays frozen. Starting from a
stopped model fades from its default pose. Interrupting an unfinished fade
captures the displayed local pose as a frozen source for the next fade, avoiding
a jump without accumulating an unbounded chain of outgoing clips. Blending can
also transition between different times in the same clip.

`getAnimationState` describes the incoming clip. Its `transition` is `null`, or
`{duration, elapsed, progress, sourceClip}`; `sourceClip` is null for a frozen
source. `progress` runs from zero toward one; completion clears `transition`.
Pause/resume freezes/restarts both clocks and fade progress. Stop cancels the
fade and restores the default pose. Seek cancels the fade and immediately shows
the incoming clip at the requested time. A short nonlooping incoming clip holds
its endpoint until the fade completes, then reports finished. Invalid options
leave the current animation and pose unchanged.

Each object owns independent transition state. Picking and bounds follow the
blended geometry; collider shapes retain their construction geometry. Scene
save/restore preserves active and paused fades. This API provides a two-pose
crossfade, not additive layers or a persistent animation blend tree.
See [blending.js](../scripts/blending.js) for rigid, skeletal and morph examples:
bumper changes clips (or phases for single-clip models) using a one-second fade;
A/X pauses or resumes.

## Supported glTF animation

Node translation, rotation, and scale channels support STEP, LINEAR, and
CUBICSPLINE. Linear quaternion interpolation uses the shortest path; cubic
quaternion results are normalized. Cubic tangents scale by keyframe duration.
Unanimated nodes keep their default transforms. Matrix ancestors are supported,
but animation channels targeting a node with a matrix are unsupported.
Morph-weight channels support the same three interpolation modes.

Each instance owns its clock and pose. Materials/textures remain shared.
Animated triangles update the instance's picking BVH and surface bounds, so
selection follows the current pose. Default-pose normalization remains fixed
through playback.

## Skeletal skinning

CPU skinning supports skin joint hierarchies, inverse-bind matrices, and
multiple paired contiguous `JOINTS_n`/`WEIGHTS_n` sets. Joint indices may be
unsigned bytes or unsigned shorts. Weights may be floats or normalized unsigned
bytes/shorts and are normalized across sets. Missing inverse-bind matrices use
identity. Joint transforms include ancestors; skinning applies
`jointWorld * inverseBind` without an additional mesh-node transform.

Malformed joint indices, weights, attribute pairs, or inverse-bind matrices
reject the model. Sparse accessors are unsupported. Deformed positions and UVs
feed the current renderer; animated normal/tangent lighting and GPU skinning
are not implemented. The included CesiumMan has one unnamed clip, selected by
index zero.

## Morph targets

Morph targets deform positions using weighted vertex offsets. Node weights
override mesh defaults; omitted defaults are zero. Weights are applied before
skeletal skinning or the mesh-node transform, and need not sum to one or stay
within `[0, 1]`. Each primitive retains its own target deltas.

Use the existing playback API for authored morph-weight clips:

```js
const asset = loadModel('assets/samples/AnimatedMorphCube/AnimatedMorphCube.glb');
const object = createObject(asset, 0, 0, 0, .55);
playAnimation(object, 0, {loop: true});
```

See [morph.js](../scripts/morph.js): A/X pauses or resumes and bumper restarts.
`getAnimations`, seeking, stopping, independent instance playback and scene
save/restore work as for transform clips. Stopping restores the model's default
morph weights. Static morph models render their default shape without playback.
Direct script assignment of individual weights is not exposed yet.

POSITION deltas use float VEC3 accessors; sparse morph accessors are unsupported.
Missing POSITION deltas contribute zero. Normal/tangent morph deltas are not
used by the current renderer. Malformed target counts, defaults and weight-track
sizes reject the model. Animated geometry updates picking and bounds; physics
colliders retain their creation shape. CPU deformation retains full detail and
uses the existing per-instance upload path; this is not a GPU performance feature.
The weight-stream and default-weight behavior follow the
[glTF specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc#morph-targets).

## Scene persistence

Scene snapshots save each instance's selected clip, playback time, speed, looping,
playing/paused state, finished flag and active transition. Restore rebuilds independent skeletal or
rigid-node/morph poses and picking geometry; it does not share mutable poses between
instances. See the [animation save/restore format](scene-persistence.md#animation-playback-v3)
and [animation save example](../scripts/animation-save.js).
