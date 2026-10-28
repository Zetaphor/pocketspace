# Animation and skeletal models

Load a GLB, create an object, and explicitly start a clip. Objects without
playback retain the model's default pose, including default skeletal skinning.
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
| `playAnimation(objectId, clip=0, options={})` | Select by index or name (first match), restarting with fresh options. Defaults: `{loop: true, speed: 1, time: 0}`. |
| `pauseAnimation(objectId)` | Freeze current time; no selected clip is a no-op. |
| `resumeAnimation(objectId)` | Require a selected clip; resume without resetting time and clear `finished`. |
| `seekAnimation(objectId, seconds)` | Evaluate immediately within `[0, duration]`; preserve paused/playing mode and clear `finished`. |
| `stopAnimation(objectId)` | Clear the selected clip and restore the default pose; reset speed/loop/time defaults. |
| `getAnimationState(objectId)` | Read playback state. |

Control calls return state with `clip` (index or null), `name` (or null),
`time`, `duration`, `speed`, `loop`, `playing`, `finished`, and `status` (`stopped`, `playing`, `paused`, or `finished`).
Speed must be finite, greater than zero, and at most 100. Starting time must be
within the clip duration. Invalid control calls throw without replacing the
current playback state.

Looping clips wrap. Nonlooping clips hold their final pose and report
`finished: true`. Zero-duration clips finish on their next advance. Seeking a
looping clip to its duration evaluates the end pose immediately; the next
advance wraps. Hidden objects still advance. Paused poses are not reevaluated
each frame. Completion is observed through state, not an event.

## Supported glTF animation

Node translation, rotation, and scale channels support STEP, LINEAR, and
CUBICSPLINE. Linear quaternion interpolation uses the shortest path; cubic
quaternion results are normalized. Cubic tangents scale by keyframe duration.
Unanimated nodes keep their default transforms. Matrix ancestors are supported,
but animation channels targeting a node with a matrix are unsupported.
Morph-weight channels and clip blending are not implemented.

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

## Scene persistence

Scene snapshots save each instance's selected clip, playback time, speed, looping,
playing/paused state and finished flag. Restore rebuilds independent skeletal or
rigid-node poses and picking geometry; it does not share mutable poses between
instances. See the [animation save/restore format](scene-persistence.md#animation-playback-v3)
and [animation save example](../scripts/animation-save.js).
