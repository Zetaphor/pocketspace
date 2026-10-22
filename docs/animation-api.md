# Node and skeletal animation playback

The engine supports per-object glTF node translation, quaternion rotation, and
scale animation with STEP, LINEAR, and CUBICSPLINE interpolation. Playback is
opt-in: loading an animated model still starts in its static bind/default pose.
Skeletal clips use CPU linear blend skinning through the same API. Morph-target
assets remain unsupported; clips report support status and unsupported requests throw. There is one selected clip per object,
with no cross-fade, clip blending, or animation event stream.

## Example

```js
const asset = loadModel('assets/samples/InterpolationTest/InterpolationTest.glb');
const object = createObject(asset, 0, 0, 0, .65);
print(JSON.stringify(getAnimations(asset)));
playAnimation(object, 'Linear Rotation', {loop: true, speed: 1});

pauseAnimation(object);       // freeze the current pose
seekAnimation(object, 1);     // seconds from the clip's timeline origin
resumeAnimation(object);     // continue from the current time
const state = getAnimationState(object);
stopAnimation(object);        // clear the selected clip; restore bind pose
```

A second object created from the same asset has an independent playback clock
and pose. Its material textures remain shared with the first object. Object
position, rotation, scale, and grab manipulation remain outer transforms applied
to the animated model; playing a clip does not overwrite those object settings.

## API

| Function | Contract |
| --- | --- |
| `getAnimations(assetId)` | Return an array of `{index, name, duration, supported, unsupportedReason}`. Indices are zero-based and stable for that asset. Duration is seconds; names may be empty or duplicated. Unsupported reason is null for supported clips. Static assets return an empty array. |
| `playAnimation(objectId, clip = 0, options = {})` | Select a clip by exact name or integer index and restart playback. Options are `loop` (boolean, default true), `speed` (finite number in `(0,100]`, default 1), and `time` (finite seconds within `[0,duration]`, default 0). Returns the new state. |
| `pauseAnimation(objectId)` | Stop advancement while preserving the selected clip, time, and pose. On an object with no selected clip, this is a no-op. Returns state. |
| `resumeAnimation(objectId)` | Resume at the current time. Requires a selected clip. It clears the finished flag; resuming at a non-looping endpoint finishes again on the next tick. Returns state. |
| `seekAnimation(objectId, seconds)` | Evaluate the selected clip at the exact supplied time in `[0,duration]`. Preserves playing/paused state and clears finished. Requires a selected clip. Returns state. |
| `stopAnimation(objectId)` | Clear the clip and restore original geometry. Time resets to 0, speed to 1, loop to true, playing/finished to false. Returns state. |
| `getAnimationState(objectId)` | Return `{clip, name, time, duration, speed, loop, playing, finished, status}`. `clip` is an index or null; `name` is the source name or null. Status is `stopped`, `playing`, `paused`, or `finished`. |

For duplicate names, select by index; string lookup chooses the first matching
name. A call to `playAnimation` always restarts with fresh default options unless
you provide them again. To continue an existing playback, use `resumeAnimation`.
Invalid clips, unsupported clips, invalid options, and out-of-range seek times
throw without replacing the current playback. Missing/removed object IDs throw.
State snapshots are ordinary JS objects; modifying them does not change playback.

When non-looping playback reaches the end, it holds the final pose, sets
`finished: true`, and stops advancement. Stopping explicitly instead restores the
bind pose. Looping wraps at duration, including back to time 0 at exact boundaries.
Seeking to duration shows the endpoint immediately; a playing loop wraps on its
next tick. Zero-duration clips finish on their first advancement. Reverse speed
and automatic playback on load are not implemented.

## Skeletal skinning

```js
const asset = loadModel('assets/samples/CesiumMan/CesiumMan.glb');
const character = createObject(asset, 0, 0, 0, .6);
playAnimation(character, 0, {loop: true});
```

CesiumMan has one unnamed two-second clip and 19 joints; select it by index 0.
All play/pause/resume/seek/stop and independent-instance behavior is identical to
node animation. No separate bone-control API is required to play an imported
skeletal clip. `getAnimations()` now reports CesiumMan's clip as supported.

The importer retains each skin's joint-node indices and inverse-bind matrices.
Missing inverse-bind matrices default to identity. For each pose, it evaluates
the full node hierarchy, including ancestors that are not themselves joints,
and builds `jointWorld * inverseBind` palettes in the skin's joint order.
Vertex positions are weighted sums of those transformed positions. These results
are already in asset world coordinates, so the skinned mesh-node transform is
not applied again. Rigid primitives still use their own node-world transforms.
This follows the [glTF skinning specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins).

Paired, contiguous `JOINTS_n` / `WEIGHTS_n` sets starting at 0 are supported,
including more than four influences per vertex. Joint accessors use unsigned
byte/short indices. Weights use float or normalized unsigned byte/short values;
all sets are normalized together for each vertex. Missing pairs, invalid joint
indices, negative/nonfinite weights, zero total weight, and malformed inverse-bind
matrices fail model loading. Sparse accessors remain unsupported by the existing
cgltf accessor-read path used here; failures are explicit rather than silently
ignoring influences. The current renderer deforms positions and retains UVs;
normal/tangent deformation for a future lit renderer is outside this implementation.

Default-pose geometry is also skinned during import, including rigs without any
animation clips. Normalization uses that default pose once and remains fixed
while playing. `stopAnimation()` restores that default skinned pose rather than
leaving the last deformed frame. Per-object skin palettes, vertices, dynamic
buffers, refitted BVH, and radius bounds remain independent; material textures
stay shared. This is CPU skinning, with work proportional to vertex/influence
count; GPU skinning and larger-rig performance are later work.

## Time, geometry, and picking

Playback advances from elapsed monotonic wall time once per native loop, including
for invisible objects, independently of the existing fixed `update(dt)` argument.
Paused instances do not repeatedly evaluate/upload geometry. Hidden objects keep
their clocks running so they show the current pose when made visible again.
Reloaded scenes get new playback state from their script; removed objects release
their pose and GPU buffers. Failed reloads retain the original instances.

The loader retains source node transforms, parent order, channels, and local
vertices for animated assets. Animation evaluates local TRS values, propagates
parent transforms, then applies the original asset center/normalization. It never
recenters each frame, so translation motion and changes in scale are preserved.
Matrix-only ancestor nodes work; a channel targeting a matrix-only node is marked
unsupported. Unanimated channels/nodes retain their default values.

LINEAR rotation uses shortest-path quaternion interpolation. CUBICSPLINE uses
Hermite tangents scaled by the keyframe interval and normalizes quaternion results.
Key times clamp outside each channel's key range. These rules follow the
[Khronos glTF animation specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#animations).

Each animated instance maintains CPU vertex geometry and dynamic GPU vertex
buffers. The asset's materials/textures stay shared. Picking uses those evaluated
vertices with the existing BVH split topology refitted to the pose. Overlay width
expands with the pose radius, avoiding the original fixed default-pose extent.
The basic asset path and its shared immutable BVH remain in use for static objects.

This initial implementation costs vertex transformation, BVH refit, and vertex
upload per advancing animated instance. It is intended as a correct starting point
for the small test fixture; larger animated scenes have not been profiled. It does
not provide GPU skinning or constant-cost animation independent of mesh size.

## Tests and headset scene

The selected [Khronos fixture](animation-test-assets.md) has nine two-second clips,
one for each TRS property and interpolation combination. Run:

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Werror -I src tests/animation-math.cpp -o /tmp/animation-math
/tmp/animation-math
python3 tests/picking-regression.py
python3 tests/grab-buttons-regression.py
```

On Deckard:

```sh
cmake -S . -B build
cmake --build build --target sf-overlay-engine animation-regression -j2
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/animation-regression
./run-overlay-engine.sh tests/animation-api-smoke.js
./run-overlay-engine.sh scripts/animation-test.js
```

The explicit native regression target retains assertions in Release and is excluded
from normal builds. Math tests cover analytic interpolation results, quaternion
signs/normalization, hierarchy order/cycles, clamp behavior, and playback time.
Native tests compare the evaluated bind pose with the existing cgltf geometry,
exercise nine clips at six times each, compare animated BVH and triangle hits over
5,400 rays, and test independent instances plus the actual QuickJS API.
The live smoke tests non-looping completion and animated-object removal.

The headset scene starts `Linear Rotation`. Bumper selects the next of nine clips;
A/X pauses/resumes; B/Y restarts. Grip or trigger can grab the model. Each fixture
clip moves one test part while its other parts retain their default pose. Check
translation/rotation/scale, jumps versus smooth interpolation, pause/resume/restart,
and selection while the part moves. Then test different clips/times on independent
instances and reload/removal. Record wearer observations separately from logs.

## Recorded verification

Deckard Release build and the math/native/API checks above passed. The existing
static picking and grab-button regressions also passed, as did the native
QuickJS lifecycle regression after the loader/binding changes.

- `/tmp/animation-regression.log`: bind compatibility, all nine clips, animated
  picking comparisons, instance isolation, input rejection, API and CPU cleanup.
- `/tmp/animation-api-runtime.log`: `ANIMATION_API_PASS`,
  `ANIMATION_ENDPOINT_PASS`, `ANIMATION_REMOVE_PASS`, and clean bounded shutdown;
  no JS load/update errors in the final run.
- `/tmp/animation-visual-startup.log`: looping rotation state and about 89.3 FPS
  with one animated fixture and the diagnostic text panel; `gl_error=0`.
- `/tmp/lifecycle-after-animation.log`: loader rollback/context cleanup passed.

These automated runs are stopped. The subsequent wearer run on `b53c9d6` used
`scripts/animation-test.js` and cycled all nine clips. The wearer reported “They
all work.” The log confirms clip changes and about 89.9 FPS with `gl_error=0`:
`/tmp/animation-wearer-b53c9d6.log`. The wearer test is stopped too.

This establishes initial headset acceptance of the nine node-animation clips.
Pause/resume/restart and animated grabbing were offered in the instructions but
were not separately reported as tested; neither independent-instance visual
behavior nor large-scene performance is established by that response. Skeletal playback was added subsequently as described above; morph playback,
blending, and larger animated workloads remain future work.

## Skinning verification

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Werror -I src tests/skinning-math.cpp -o /tmp/skinning-math
/tmp/skinning-math
```

On Deckard, after building `animation-regression`:

```sh
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 python3 tests/skinning-loader-regression.py ./build/animation-regression
./run-overlay-engine.sh scripts/skinning-test.js
```

The math test verifies joint-index remapping, inverse-bind cancellation and
multiplication order, ancestor transforms, weighted blending across sets, and
rotation. The generated GLB fixtures test normalized packed weights in two sets,
identity inverse binds when omitted, skinning without clips, a mesh transform
that must not be applied twice, and six malformed-rig cases. The native regression
also evaluates CesiumMan at four times, checks motion/bounds and independent
poses, and compares BVH/triangle picking over 1,600 additional rays. The original
nine-clip API/interpolation tests continue to run.

The headset scene displays two instances with different speeds/start times.
Each hand controls its matching character: A/X pauses/resumes, B/Y restarts,
and bumper toggles stop/play. Grip or trigger grabs either character. Verify
limbs bend without tearing/exploding, pause/seek/stop poses remain sensible,
characters remain independent, and moving limbs remain selectable.

Initial Deckard Release and generated-fixture/native tests passed. The bounded
two-character run reported 89.3 FPS at 900 frames, independent playback states,
and `gl_error=0`. Logs: `/tmp/skinning-regression.log` and
`/tmp/skinning-runtime.log`. The run is stopped. Headset acceptance of skinning
subsequently passed on `312dafc`: the wearer ran the two-character scene and
reported “All works” after the limb deformation, independent controls, stop/start,
and grab instructions. The log reported about 89.5–89.8 FPS with `gl_error=0`:
`/tmp/skinning-wearer-312dafc.log`. The scene is stopped. This is initial headset
acceptance of CesiumMan and these controls; other rigs, larger scenes, and
individual quantitative deformation/selection errors remain unmeasured.
