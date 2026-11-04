# Running tests and comparisons

The multi-script offscreen regression renders two independent scenes whose
objects both use local ID `1`, then destroys one renderer and confirms the other
still produces the same pixels. It repeats the allocation and cleanup cycle
three times without opening SteamVR or capturing input:

```sh
cmake --build build --target multi-script-render-regression -j1
EGL_PLATFORM=surfaceless ./build/multi-script-render-regression
```

The shared controller target comparator is a headless test of distance,
stable instance ties, and invalid hit rejection:

```sh
cmake --build build --target input-arbitration-regression -j1
./build/input-arbitration-regression
```

## Running regressions

`python3 tools/run-regressions.py` builds and runs every regression declared in
`CMakeLists.txt` and `cmake/*.cmake` in `build-test`, a separate tree that compiles
regression sources at `-O0` with assertions on. `physics-performance-regression`
is a measurement and runs by name. The shared test engine objects stay
optimized, and so does `pbr-index-production-regression`, whose exact-pixel
comparison depends on the fused multiply-add instructions optimized ARM64 builds
use. Pass target names to run only those. Logs go to `build-test/regression-logs`.
Measurements and profiling runs belong in the optimized `build` tree.

Engine regressions share one copy of the engine compiled with test hooks
(`SF_TEST_HOOKS`): fault injection, counters and reference shaders that production
builds do not contain. The GLES regressions also share one compiled offscreen
renderer (`tests/render_harness.cpp`), and the fast-tree regressions reuse one
precompiled `src/render_host.h`. When `ccache` is on `PATH`, CMake uses it, so a
clean rebuild of an existing tree mostly reads cached objects.

It uses one compile job and runs one regression at a time, because regressions share
the GPU and memory with the SteamVR compositor. Do not run them concurrently on the
headset; doing so destabilized a live session.

## Multi-script headset validation

For interactive two-script acceptance on Deckard, run the paired fixture while
the headset is active:

```sh
./run-pocketspace.sh --scripts-dir tests/scenes \
  --launch multi-script-interaction-near.js \
  --launch multi-script-interaction-far.js
```

The boxes overlap above the blue NEAR and orange FAR panels. The blue panel is
closer where the panels overlap. Aim at the overlap and click; only blue should
respond, and no box should be grabbed. Trigger and grip both grab a box. Grab the
boxes with both hands at once and move them independently.
The engine log names the script that received each click or grab.
Turn one controller off and back on while the other remains connected; the
connected hand should keep working and the reconnected hand should resume grabs.
To check reload isolation, an operator can schedule a reload from another Deckard
shell while the headset user holds both boxes:

```sh
sleep 12; touch tests/scenes/multi-script-interaction-near.js
```

Only blue should reset; orange should stay held and movable. After releasing,
both boxes should be grabbable again. `touch` changes only the file modification
time and must be run from the repository root. The operator should give the user
the countdown before starting it.
Stop the engine with Ctrl-C after the check and verify that it exited.

See the [test index](index.md) for a brief description of every test and helper.

This experiment leaves the published examples and engine defaults unchanged.
Run from the repository root on the VR runtime host:

```sh
./run-pocketspace.sh --scripts-dir tests/scenes --launch overlay-comparison.js
```

The HUD names the current renderer, geometry, body count, FPS, CPU, process RAM,
and estimated GPU allocations. Grip or trigger grabs individual bodies.

| Control | Action |
| --- | --- |
| Bumper | Cycle 16, 48, 96, 160, and 240 bodies. |
| A/X | Switch colored boxes and alternating Khronos ToyCar/Avocado models. |
| B/Y | Switch single and grouped rendering. |

Every switch rebuilds the same deterministic layout at the selected count.
Bodies use the same position and 0.14-meter scale in each renderer; boxes use box colliders
and detailed models use cached convex hulls. Fallen or distant bodies are removed
and the HUD reports the remaining count. The floor participates in the shared
projection plane in both modes.

Single mode puts all 3D geometry, including the floor, into one render group.
It renders one stereo texture and displays one visible scene overlay. The hidden
staging handle, statistics panel, and controller lasers are additional handles.
Grouped mode uses four spatial body groups plus a floor group. Both paths use
the engine's current density policy, 4096-pixel ceiling, alpha convention, and
staged presentation. Compare head motion, floor occlusion, close-up model detail,
and independent grabbing/hand rotation before increasing counts. Neither path bypasses the
resolution ceiling; inspect runtime warnings when objects spread apart. The
default compact layout uses 0.16-meter spacing, a 1.5-meter floor, and a
0.8-meter forward offset. `--layout wide` in automated runs uses 0.2-meter
spacing, a 1.8-meter floor, and no forward offset. Compare renderers within a
layout; changing layouts changes visibility and sampling demand.

## Bounded automated runs

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-overlays.py --counts 48 96 --seconds 12 --warmup 5 \
  --output /tmp/overlay-comparison
```

Use `--engine /absolute/path/to/pocketspace` to test an isolated build.
The runner refuses to start if another engine process is running. It tests both
renderers with boxes and detailed models, retaining the same asset cache,
positions, scales, HUD, and object count. Kinematic bodies, hidden lasers, and
disabled interaction keep geometry fixed; interactive headset runs use dynamic
bodies. Keep the headset awake and your head steady during measurements.

The runner terminates each scene with SIGTERM, preserves per-case logs and
`summary.json`, discards warmup samples, verifies object counts, and reports
median FPS, CPU, RSS, GPU allocation estimates, maximum allocated eye pixels,
and groups that hit the resolution ceiling. A faster capped run is not a
quality-matched win against an uncapped run. GPU estimates are owned
render resources, not measured dedicated VRAM. Pair order alternates by geometry.
Use `--geometries models --reverse-order` to repeat just the model pairs in
the opposite order. These short runs show trends, not a hardware-wide throughput
guarantee. Repeat close results and compare visually before selecting a rendering mode.

`examples-regression` also checks the experiment's switches, 96-body scene
replacement, model colliders, and fallen-body cleanup using real bindings.

See the [recorded comparison](results/overlay-comparison.md) for measured results
and resolution-limit caveats.

## Automatic render group subdivision

Build `render-group-split-regression`, `panel-submission-regression`, and
`render-occlusion-regression` for CPU/API planning checks, simulated presentation
failures, and actual GLES occlusion coverage. Failures exit without core dumps.
`examples-regression` also exercises the adaptive scene's controls and bindings.

`tests/scenes/auto-split.js` starts two islands of colored boxes in one opted-in
logical group, plus a floor in a separate group. Grip/trigger grabs individuals;
bumper moves the existing IDs between wide islands and a compact grid, A/X
rebuilds boxes/ToyCar/Avocado, and B/Y
clears the bodies. Check head-motion edges, floor occlusion, and the allocation
HUD as tiles split, merge, or rebalance. Geometry switches rebuild memberships from one tile. Runtime
logs report logical group IDs, new tile counts, and planned allocation savings.

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-auto-split.py --engine build/pocketspace \
  --output /tmp/auto-split-comparison
```

This bounded runner compares fixed kinematic geometry with subdivision disabled
and enabled, alternating pair order for boxes/models. Both variants preload the
same assets, use identical positions, floor, interaction flags, and panel, and
retain member sampling density. It verifies object counts and split decisions,
discards warmup samples, and records median FPS/CPU/RSS/GPU allocation estimates.
Keep the headset awake and head steady for performance comparisons. No persistent
demo remains after the runner finishes; headset visual acceptance is separate.

See [native subdivision results](results/auto-split.md) for measurements and
validation limits.

## Render tile merging and rebalancing

`setRenderGroupAutoSplit` keeps split-only defaults. The grouped stress example
and adaptive test scene explicitly enable the new `merge` and `rebalance` flags.
`render-group-split-regression` covers both planners, strict option validation,
normalized allocation savings, material/hidden/near-plane exclusions, and proposal
stability using a simulated clock. `panel-submission-regression` injects failures
into either output and checks that both fallback tiles remain available, including
culled outputs, merge retry, and snapshot transfer on removal. GLES checks compare
combined output before/after runtime tile reassignment and merging on a matching
grid at stereo/lateral viewpoints.

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-regroup.py --engine build/pocketspace \
  --output /tmp/regroup-comparison
```

The bounded native runner compares split-only against split/merge/rebalance for
boxes and actual ToyCar/Avocado instances. Kinematic comparison runs ignore live
controller callbacks, disable grabbing, and hide lasers so inputs cannot rebuild
or move the measured objects. Each run holds 16 kinematic bodies plus
a floor and keeps the same IDs while moving through four eight-second phases:
wide islands, interleaved tile membership across islands, a compact grid, and
wide islands again. It rejects wrong IDs/counts, GL/submission errors, incorrect
settled tile counts, and missing expected structural operations. Samples begin
four seconds into each phase to allow handoff/shrink/statistics settling. Pair
order alternates between geometries. It records FPS, CPU, RSS, GPU estimates,
structural events, body tile counts, and resolution-cap warnings. Keep the headset
awake and head steady; results depend on projected demand and are scene-specific.
A valid refusal to merge can require adjusting the fixture, not relaxing the
allocation threshold. No persistent demo is left running. Visual headset
acceptance is separate from these bounded checks.


`tests/regroup-lifecycle.py --engine build/pocketspace --output /tmp/regroup-lifecycle.log`
adds a bounded native lifecycle run: split/rebalance/merge, hide/show, partial/full
body removal, and live script replacement from boxes to models. It checks that
reload commits, removal leaves only the floor, structural operations occurred,
and SIGTERM stops the process normally.
See [regrouping measurements and validation](results/regroup.md).

## Ordered GPU instancing

The production API defaults to ordered instancing with `loadModel(path, eyePixels)`;
pass `{gpuInstancing: false}` to opt out. Explicit `{gpuInstancing: true}` remains supported;
see the [API contract](../docs/javascript-api.md#ordered-gpu-instancing).
`instancing-regression` uses the actual renderer and batching implementation to
compare original and instanced pixels, including material modes, coplanar
ownership, rotated anchors, indexed/nonindexed geometry, deformation fallback,
256-instance batches, asset-order barriers, attribute state, and buffer cleanup.
`lifecycle-script-regression` runs `instancing-api-smoke.js` for option validation
and cache behavior without a graphics context.

```sh
cmake --build build --target instancing-regression lifecycle-script-regression -j1
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/instancing-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/instancing-lifecycle.py --engine build/pocketspace --output /tmp/instancing-reload
```

`tests/fast-motion.py --instancing` selects the API option for a normal production
run; `--case 128-avocados-two-groups` selects the controlled two-group workload.
The lifecycle runner uses one production process for four ten-second phases,
alternating sixteen instanced models and empty scenes. It checks three committed
reloads, stable counts, and zero owned GPU allocation estimates in empty phases.
No persistent demo is left running. See [recorded measurements](results/instancing.md).

## Production automatic model LOD

Normal `loadModel` defaults to `lod: true`; see the
[API contract](../docs/javascript-api.md#automatic-model-lod). `auto-lod-regression`
checks size/distance hysteresis, off-axis bounds, near restoration, full CPU
geometry, exact pixels against expanded selected-index references, instancing
at every level, mixed-level barriers and actual buffer deletion. The lifecycle
API harness runs `lod-api-smoke.js` for strict boolean parsing and cache rules.

```sh
cmake --build build --target auto-lod-regression pocketspace -j1
DISPLAY=:0 EGL_PLATFORM=x11 ./build/auto-lod-regression
python3 tests/instancing-lifecycle.py --lod --output /tmp/auto-lod-reload
python3 tests/compare-auto-lod.py --output /tmp/auto-lod-performance
```

`--lod` selects ToyCar in the reload fixture so generated index buffers must be
released in both empty phases. `compare-auto-lod.py` alternates the production
default and explicit opt-out, checking that grouped allocations match and GPU
estimate differences equal the generated shared index-buffer bytes. It records
actual FPS, not the fixed-detail GPU-query measurements. `fast-motion.py --no-lod` selects explicit opt-out. Other isolated optimization comparison runners
use that opt-out to keep LOD from confounding their measurements; the fixed-detail
LOD harness still applies its test override. No interactive demo is left running.

## Scene ownership containers

`scene-container-regression` exercises real bindings for container ownership,
transfers to other containers/root, strict IDs and throwing/reentrant getters,
the active-container limit, idempotent disposal, attached physics/joint cleanup,
shared surfaces, pointer/hover cleanup, self-disposing callbacks and failed-reload
callback ownership. Individual removals and repeated container/scene lifetimes
use the same cleanup paths. The fixture is `scene-container-api-smoke.js`.

```sh
cmake --build build --target scene-container-regression pocketspace -j1
./build/scene-container-regression
python3 tests/container-lifecycle.py --output /tmp/scene-container-lifecycle
```

The bounded production runner opens/closes a tool three times without reloading
its script. A root object survives in the same render group; tool panels and
physics bodies/joints must disappear while root resources remain usable.
Per-phase state/count assertions, clean native logs and final grouped-budget
release are required. Cached model GPU memory deliberately remains allocated
between tool lifetimes; this is not an asset-unload or allocator/RSS test.
See the [public API](../docs/scene-containers.md) and final
[example](../scripts/containers.js).

## Morph target regression

Build `morph-regression` with the normal engine dependencies, then run:

```sh
cmake --build build --target morph-regression -j1
python3 tests/morph-loader-regression.py ./build/morph-regression
```

The generator supplies five-target STEP/LINEAR/CUBICSPLINE clips, node/mesh/zero
weights, combined skinning, and malformed models. The native harness checks
positions, animated picking/bounds, independent poses, the bundled Khronos cube,
real JavaScript controls, and restored morph poses. No VR session is needed.
Run `./run-pocketspace.sh --scripts-dir scripts --launch morph.js` for headset visual acceptance.

## Raw controller subscription recovery

```sh
cmake --build build --target lifecycle-input-regression -j1
./build/lifecycle-input-regression
```

This local socket harness keeps the old connection alive while replacing the
controller identity, and simulates a replacement subscription with no initial
state. It checks that a new connection restores button callbacks, preserves
physics events, and avoids retrying valid idle streams or absent controllers.
No VR session is needed. In the headset, use `scripts/morph.js` to verify A/X
pause/resume after controller reconnection; do not hold an object during this
check because connection recovery cancels existing holds.

## Animation transition regression

```sh
cmake --build build --target animation-blend-regression -j1
./build/animation-blend-regression
```

Checks local translation/scale/quaternion/morph blending, both live clocks,
default and interrupted sources, endpoints, pause/resume/seek/stop, 900 blended
BVH/reference picking comparisons, and independent rigid/skin/morph geometry.
The real JavaScript fixture covers invalid options, live/frozen snapshot
roundtrips, malformed transition saves and example controls.
No VR session is needed. `scripts/blending.js` is the headset example; left to
right it shows node animation, skeletal animation and morph animation. Bumper
crossfades to the next clip (or a different phase for single-clip assets) over
one second; A/X pauses/resumes all three. Press bumper during a fade to test a
continuous interrupted transition. Grip or trigger grabs each model.

## Animation profile regression

```sh
cmake --build build --target animation-profile-regression -j1
EGL_PLATFORM=surfaceless ./build/animation-profile-regression
```

The regression checks real GLES animation phase counters, upload byte totals,
clean-pose upload skips and exact cached rigid GPU buffers over all rigid clips.
Its counters are test hooks (`SF_TEST_HOOKS`) compiled only into regression
targets; normal engine builds contain no animation phase timers.

## GPU skinning

GPU skinning is automatic for eligible models in the normal engine. Build the
regression explicitly:

```sh
cmake -S . -B build
cmake --build build --target gpu-skinning-regression -j1
EGL_PLATFORM=surfaceless python3 tests/skinning-loader-regression.py ./build/gpu-skinning-regression
```

The GPU path supports assets whose primitives are all skinned, with one
four-lane influence set, at most 1,024 joints per skin, and no morph targets.
Other supported assets keep their normal CPU deformation. Static default poses
keep the existing path. Shared indexed vertices include local position, UV,
joints and weights in the deduplication key. Per-instance RGBA32F textures hold
joint palettes; skinning runs in the common color/depth vertex shader.

Conservative bounds union transformed local boxes for vertices influenced by
each joint. A ray missing those bounds does no CPU deformation. Candidate rays
build the exact picking mesh/BVH once per pose, shared by both hands; a new pose
invalidates that cache. This preserves triangle picking, rather than substituting
joint boxes for selection. Normal builds use conservative bounds for GPU skins
and exact deformed bounds for CPU fallbacks.

The regression is compiled against the production GPU path (no experimental
mode definition). CPU-reference animation regressions explicitly select the
retained CPU implementation. The regression captures GPU-transformed vertices and compares them with the CPU
reference, checks containment, both-eye images, 21,000 ray comparisons, clean
uploads/cache reuse, live and frozen transitions, saved poses, and CPU fallbacks.
The pixel check allows at most 32 pixels with channel differences over 2/255 in
each 256x256 image; report actual differences rather than describing that bound
as exact equality. The CPU production regressions must also pass after changes
to the shared pose code. No GPU readback is used in normal rendering or picking; transform feedback readback is confined to the correctness test.


The GPU regression also generates a six-part synthetic asset with two distinct
skin palettes, opaque/masked/blended materials, and an alpha-cutout checker.
Individual-part images prevent hidden/coplanar parts from passing untested.
It checks pose texture destruction and shared buffer deletion over repeated
pose creation/removal. This fixture broadens coverage without claiming visual
acceptance of another real-world model.

For a deterministic interaction cost comparison, append `--profile-picking`:

```sh
EGL_PLATFORM=surfaceless python3 tests/skinning-loader-regression.py   build/native/gpu-skinning-regression --profile-picking
```

On Frame use `DISPLAY=:0 EGL_PLATFORM=x11` and its normal SteamVR library path.
This measures CPU animation evaluation plus the real `meshHit` path for 32
independent animated instances and two synthetic controller rays. Cases force
0, 1, 4 or 32 conservative-bound candidates; both hands share each candidate
cache. It also checks paused-cache reuse. Each case discards 20 warmup iterations
and measures 120 iterations. `SKIN_PICK_PROFILE` reports mean/p95 CPU work,
deformation/bounds, BVH time and processed vertices. Assertions verify the exact
candidate deformation count. No uploads, drawing, compositor pacing or physics
are timed, so these durations are not headset FPS or end-to-end grabbing results.


For manual grabbing acceptance, run
`./build/pocketspace --scripts-dir tests/scenes --launch gpu-skinning-interaction.js`. Point at models
with both hands, grab with grip or trigger, and move/rotate a model while it
animates. A/X pauses/resumes all actors; repeat grabs while paused. B/Y removes
all actors and hides lasers; the same cleanup runs after five minutes. Stop the
engine process after collecting feedback. Watch for selection misses, pose
jumps, stale geometry after release, and color/depth disagreement when models
overlap. This check uses the 32-actor workload and reports live FPS/CPU on its HUD.


The GPU matrix includes six real-model clips across CesiumMan, Fox (all three
clips), RiggedFigure and RiggedSimple, plus one generated multi-part clip. For each clip it samples
0%, 12.5%, 50%, 87.5% and 100% of its duration; the middle samples exercise live
cross-clip sources where available and frozen-pose transitions. It compares every
expanded CPU vertex with captured indexed GPU vertices, exact ray hits, bounds,
and stereo images, then repeatedly destroys instance resources. See the
[pinned asset inventory](results/gpu-skinning/khronos-models.json) and
[recorded model checks](results/gpu-skinning.md#additional-khronos-models).


Production GPU skinning also runs 30 mixed-scene reload cycles through
[gpu-skinning-mixed-api.js](gpu-skinning-mixed-api.js): three eligible skin models,
a rigid clip, morph clip and static box. Each cycle checks failed-reload rollback,
live/frozen snapshot restoration, independent poses, restored CPU fallbacks,
container disposal and deletion of old palette textures after reload/close.
On hosts whose default font is outside the engine search paths, set `SF_FONT`
to an installed TTF when running panel persistence checks.


### Normal-engine mixed backend acceptance

Run `./build/pocketspace --scripts-dir tests/scenes --launch gpu-skinning-production.js` on Frame
with the normal display/library environment.
Twelve instances mix Fox, CesiumMan, RiggedFigure, RiggedSimple, rigid-node
InterpolationTest and AnimatedMorphCube. Grab with either hand using grip or
trigger; A/X pauses/resumes, bumper captures/restores the container in place,
and B/Y clears it. Cleanup also runs after five minutes. Check pose continuity,
selection after restore, and coherent overlap while moving your head. The HUD
shows live FPS/CPU; this manual scene is not a matched performance benchmark.

## Adjacent rigid primitive batching

Build `rigid-batch-regression` explicitly. It compares 864 exact stereo views
between individual and batched draws, CPU/GPU/morph fallbacks, material
barriers, clip changes, mapped combined-stream ranges and buffer cleanup. The
draw counters it reads are test hooks compiled only into regression targets.

Batching only combines adjacent, complete-triangle CPU rigid parts with
identical factors/sheen/alpha/cutoff/double-sided state and no source texture
(the engine's immutable white texture). Skins, morphs, blended parts, textured
parts and static assets retain their existing paths. No sorting crosses a part,
material or object boundary.

Eligible poses own one combined expanded VBO; per-part VBOs are absent.
Changed primitive ranges upload once. Unmerged/textured parts still draw their
correct ranges. Teardown releases the stream once and GPU allocation estimates
count it once. CPU geometry/BVH/bounds are unchanged. The nine untextured cubes
in InterpolationTest can merge into one draw; its textured plane stays separate.

The mixed GPU lifecycle regression also checks independent restored rigid streams,
absence of per-part VBOs, exact allocation size, and buffer deletion through 30
successful/failed reload and live/frozen snapshot cycles. The rigid phase test
maps individual stream ranges across all nine rigid clips and preserves pending
updates through repeated pose evaluations.

The normal-engine mixed acceptance scene selects Linear Scale for its rigid
actors. It checks GPU skins, morph fallbacks and adjacent rigid batches together;
bumper save/restore rebuilds independently owned streams. Use the existing
[normal-engine instructions](#normal-engine-mixed-backend-acceptance).

## PBR materials

Build `pbr-regression` explicitly. Run from the repository root with a working
GLES context; no OpenVR session or controller interaction is required:

```sh
cmake -S . -B build
cmake --build build -j1 --target pbr-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/pbr-regression
```

Set `SF_PBR_IMAGES=/tmp/pbr-images` to write bounded first-pose PPM previews.

The fixture set includes two high-triangle sphere grids. Keep them in offscreen
checks rather than assuming they are suitable for a live headset gallery. For
visual acceptance, run `tests/scenes/pbr-materials.js`; it does not capture pointer
input. Inspect normal-map orientation, mirrored geometry, uniform unlit colors
and increasing emissive brightness. `scripts/pbr.js` separately enables grabbing
and rotation on ToyCar. Automated rendering checks are distinct from headset
visual acceptance and performance measurements.

`culling-regression` is an additional excluded target. It checks front/back
visibility and instancing under the shared culling policy. `--benchmark` adds
ABBA comparisons of culling off/on/on/off: 240 boxes and 32 ToyCars, full detail,
ordered instancing, one fixed 768×768 offscreen view, alternating eyes, fixed
camera/object transforms. Each stage warms 30 frames and measures 60. It checks
GPU disjoint/error flags and reports GPU, enqueue and synchronized frame times.
It opens no OpenVR session, creates no overlays and captures no pointer input.
Use an awake/worn headset for meaningful native performance measurements; these
are offscreen rendering costs, not compositor FPS or sustained thermal results.

## Baked environments

For the external-image conversion workflow, install
`tools/requirements-environment.txt` in a virtual environment, then run
`python3 tests/environment-convert-test.py` using that environment. It creates
small temporary HDR, EXR and PNG panoramas, checks linear radiance and sRGB
conversion, bakes a custom environment, and rejects HDR mislabeled as sRGB.
It does not require a headset or download images.

Run `python3 tests/environment-bake-test.py` on a host with NumPy. The original
studio resource can be regenerated with `tools/generate-environment.py`; runtime
and tests do not need NumPy to load an existing bake.

Build the excluded `environment-regression` target after reconfiguring CMake.
Run it from the repository root with the same GLES environment as `pbr-regression`.
Set `SF_ENV_IMAGES=/tmp/environment-images` for four bounded first-view PPMs.
`--benchmark` adds one/sixteen ToyCar comparisons with a 768×768 offscreen view,
full-detail PBR geometry, fixed transforms/camera and alternating eyes. Each
stage warms 30 frames and measures 120 in off/on/on/off order, with the same
already-uploaded environment storage in every stage. GPU-disjoint and GL-error
checks must pass. These measurements compare the incremental lighting feature;
IBL changes appearance and the measurements are not compositor FPS. No OpenVR
session, overlays or pointer capture are used. Keep the headset awake and worn.

For visual acceptance, `tests/scenes/pbr-materials.js` now selects the baked
studio environment. Compare tangent-test highlights and horizon orientation,
negative-scale check marks/spheres, matching encoding-test columns, invariant
unlit colors and increasing emissive brightness. It remains non-interactive.

## Surface-aware PBR LOD checks

Reconfigure CMake and build the excluded `pbr-lod-regression` target. Run it from the repository root with the normal GLES
runtime environment. It creates no OpenVR session, overlays or pointer input.
`SF_PBR_LOD_IMAGES=/tmp/pbr-lod-images` saves bounded full/reduced PPM previews;
`--benchmark` adds fixed-level full/reduced/reduced/full comparisons for one and
sixteen full-detail PBR ToyCars with baked environment lighting, alternating eyes
and one 768×768 offscreen target. Each stage warms 30 and measures 120 frames.
GPU-disjoint and GL errors fail the comparison. Keep the headset awake and worn;
these are offscreen costs, not compositor FPS.

The simplifier includes all position/UV/surface attributes, locks seams and
primitive borders, and retains full CPU meshes. Reduced streams remain expanded
on the GPU. The regression checks exact source shader-input tuples, exact
reduced-stream routing across stereo/depth/overlap passes, lossy image differences,
material/deformation/opt-out guards and resource release. It additionally checks
32 MiB budget admission/exhaustion, arithmetic overflow, partial upload failure,
full-detail fallback, pressure during restore and failed reload preservation.
Failure injection is a test hook compiled only into regression targets. See
[the production results](results/pbr-lod-production.md) for budgeted native cost
and the [prototype results](results/pbr-lod.md) for quality and memory tradeoffs.

For the interactive headset check, run:

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/pocketspace --scripts-dir tests/scenes --launch pbr-lod.js
```

Approach and move away from the car and normal-map fixture. Grip or trigger grabs
must restore full detail; rotate and release them, then inspect silhouettes,
reflections, normal-map orientation, overlaps and transitions while moving your
head. The normal engine uses the existing automatic selection and hysteresis, with one object choice
for both eyes and all passes. Unsupported or ineffective reductions retain full
detail. Production enables the same generation by default under `lod: true`,
with the shared payload budget. Only regression targets recognize
`SF_PBR_LOD=0` to force a full-detail baseline; the production engine ignores it.

## PBR depth checks

Reconfigure CMake and build the excluded `pbr-depth-regression` target. Run from
the repository root with the normal GLES environment:

```sh
cmake -S . -B build
cmake --build build -j1 --target pbr-depth-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/pbr-depth-regression
```

The default run compares 1,248 views exactly and checks independent opaque/mask/
blend coverage, program/attribute state and cleanup. `--benchmark` measures
4/16-car external-depth and color-only controls at 768 pixels per eye, full
detail, fixed lighting/camera and identical draw/triangle counts. Each original/
optimized/optimized/original stage warms up 30 frames and measures 120. Keep the
headset awake and worn; these offscreen checks create no overlays or input capture.
See [results and limitations](results/pbr-depth.md).

For interactive inspection, run the normal engine with `tests/scenes/pbr-depth.js`.
Grip or trigger grabs/rotates the car and alpha-test model against a blue backdrop;
check overlaps, cutout holes and head movement for flicker or missing parts.
Production enables the cheaper depth pass automatically.

## Production PBR indexing checks

Reconfigure CMake and build `pbr-index-production-regression` explicitly. Run from
the repository root with the normal GLES environment. It uses the production
opt-in policy. It compares both eyes at all three LOD choices, color/external
depth, original blended materials and animated fallbacks. It checks reconstructed
full shader inputs, CPU preservation, shared snapshots, cache conflicts, rejected
reload preservation, buffer cleanup, a full LOD budget and partial allocation
rollback. Failure injection is a test hook compiled only into regression targets.

```sh
cmake -S . -B build
cmake --build build -j1 --target pbr-index-production-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/pbr-index-production-regression
```

`--benchmark` adds matched 16-car comparisons with the opt-in policy off/on at
LOD 0/1/2 for color-only and external-depth workloads. The studio environment,
768-square target, fixed camera and triangle counts stay matched. Each sequence
runs off/on/on/off, with 30 warmup and 120 measured frames per stage. Shader
programs and assets are initialized first; every frame checks actual draw/triangle
counts, GPU disjoint and GL errors. Keep the headset awake and worn. This is
an offscreen production-path measurement, not compositor FPS.

Run `tests/scenes/pbr-index.js` with the normal engine for interactive inspection.
Grip/trigger grabs and rotates a car, normal-map fixture and alpha-test model;
approach and move away to check LOD, then grab to restore full detail. Check
reflections, cutouts and overlaps against the blue backdrop. The public
`scripts/pbr.js` example also enables the existing `optimizeVertexCache` option.
See [production results](results/pbr-index-production.md).

## Cache-indexed reduced PBR LOD

`pbr-index-production-regression` also reconstructs compact reduced streams and
compares every oriented triangle and full shader-input tuple against the expanded
reduced control, at identical simplification error/count. Its image comparisons
cover all three detail choices and both eyes in color and external-depth passes.
It verifies rollback after a reduced index upload as well as full-detail failures.
Regression targets include the LOD test hooks; `SF_PBR_LOD_INDEXED=0` forces expanded
reduced streams for its matched control only. This override is absent from the
normal engine. The cache option remains opt-in.

In `--benchmark`, LOD 0 compares the full-detail cache policy off/on. LOD 1/2 keep
full-detail cache indexing enabled in both assets and compare expanded versus
cache-indexed reduced streams, isolating reduced packing. Raw samples record
`cache` and `indexedLod` separately. Source triangles, simplification/error policy,
lighting, projection, target dimensions and actual draw/triangle counts stay
matched. See [measurements and headset acceptance](results/pbr-lod-index.md).
The normal-engine `tests/scenes/pbr-index.js` exercises both full and reduced paths.

## Broader PBR cache validation

The default `pbr-index-production-regression` now covers nineteen imported models:
the existing car/normal/mirror/alpha/encoding and animation fixtures plus Lantern,
both metal/rough sphere grids, unlit/emissive samples, Box, Avocado, RiggedSimple,
RiggedFigure, CesiumMan and InterpolationTest. The two high-triangle sphere grids
use full detail in both variants so optional-budget admission cannot change their
compared geometry; other eligible fixtures also exercise reduced LODs.

A generated coplanar two-color grid intentionally demonstrates equal-depth
ownership differences for opaque/masked materials in both eyes. Its exact
oriented triangle/shader-input multiset and coverage must remain unchanged while
color ownership changes. This is an expected compatibility limit, not an image
comparison failure. It provides concrete evidence for retaining the cache opt-in.

`--benchmark-models` follows the correctness checks with full-detail color and
external-depth comparisons: sixteen Avocados, sixteen Lanterns, one textured
metal/rough sphere grid and one untextured sphere grid. It preloads both assets
and shader programs, keeps LOD disabled and quality matched, records actual
triangle/draw counts and geometry bytes, and uses off/on/on/off stages with 30
warmup plus 120 measured frames. Run with the headset awake and worn. No overlay
or input capture is created. See [results](results/pbr-cache-materials.md).

## Dynamic point lights

Build and run the real GLES and script regression from the repository root:

```sh
cmake -S . -B build
cmake --build build -j1 --target point-light-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/point-light-regression
```

The test opens no OpenVR session and captures no controller input. It checks both
eyes, direct-light intensity/color, range cutoff, disabled and unlit invariance,
external-depth silhouettes, local-offset transforms, option getter mutations,
capacity, attachment/container cleanup, failed reloads and snapshot restoration.
Failures exit with a diagnostic instead of deliberately generating a core dump.

With the headset awake and worn, add `--profile` for zero/one/four/eight-light
measurements in forward and reverse order. Each case uses a 768-square
target with either one Box or 16 ToyCars, 60 warmup and 180 measured frames,
alternating eyes. GPU timer queries
reject disjoint measurements; wall time includes rendering and GPU completion.
This is a bounded light-loop comparison, not a many-model throughput guarantee.

Run `./run-pocketspace.sh --scripts-dir scripts --launch lights.js` for interactive acceptance. Grab the
small marker with grip or trigger and move it around the car. Check that the lit
area follows it while moving your head; bumper cycles colors, A/X toggles light,
and B/Y captures then restores the lit tool. Verify fresh attachments after
restoring. Stop the demo immediately after the user verifies it.

## Spotlights

Build `spotlight-regression` explicitly after reconfiguring CMake. Run from the
repository root with the same EGL/SteamVR library environment as the point-light
regression. It opens no OpenVR session and captures no pointer input:

```sh
cmake -S . -B build
cmake --build build -j1 --target spotlight-regression point-light-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/spotlight-regression
```

Checks cover narrow/wide/reversed cones in both eyes, normalization, equal-angle
edges, full-hemisphere point-light equivalence, unlit/depth invariance, attachment
rotation, type-safe APIs, cone validation, getter mutations, mixed capacity,
named-file restoration and invalid mixed-snapshot rollback.

With the headset awake and worn, run `point-light-regression --profile-spots` for
matched point/spotlight timings. It retains the Box/16-car workload and measured
frame counts described above. Broad spotlight cones (inner 1.2, outer 1.4 radians)
fully illuminate the targets to avoid crediting a narrower illuminated workload
as a cheaper BRDF. Each type uses the forward/reverse count sequence independently.

Run `./run-pocketspace.sh --scripts-dir scripts --launch spotlights.js` for interactive acceptance. Grab
the blue handle with grip/trigger and rotate it to aim across the car. Bumper
switches width, A/X toggles, and B/Y captures then restores. Check head motion,
cone falloff and restored attachment behavior. Stop the demo immediately after
verification.

## Light shading work comparison

Build `light-work-regression` explicitly after reconfiguring CMake. It compares
production per-object light lists with the same shader receiving all scene
lights. It opens no overlays and captures no input:

```sh
cmake -S . -B build
cmake --build build -j1 --target light-work-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/light-work-regression
```

Pixel checks use detailed cars plus generated-tangent and surface-frame fixtures,
point/spot/mixed lights, both eyes, head movement, alpha modes and unlit materials.
Boundary, invalid-bound fallback and deformed-pose bounds are checked separately.
Alpha must match exactly and RGB may differ by at most one byte per channel.
Add `--profile` only with an active, worn headset for ABBA comparisons on 16
full-detail cars at 768 pixels. Each configuration has 30 warmup and 90 measured
frames per stage, alternating eyes, with identical geometry, view and lighting.
ABBA stages interleave within each frame to limit clock/power drift. Range-list
timings toggle selection on the same renderer/program/buffers; separate reference
and candidate renderers are used for pixel checks.
Light counts are 0, 1, 4 and 8. Broad-range and spatially local lights are
compared separately; GPU timer disjoint results reject the run.
The measured wall time includes submission and GPU completion, not just CPU work.

## Spotlight cone selection

Build `spotlight-cone-regression` explicitly after reconfiguring CMake. It uses
real GLES without opening an OpenVR session or capturing input:

```sh
cmake -S . -B build
cmake --build build -j1 --target spotlight-cone-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/spotlight-cone-regression
```

CPU checks cover cone sides, tangency, apex overlap, behind-light spheres,
hemispheres and invalid axes, plus 288,000 deterministic points sampled in/on
spheres. A sphere containing a lit point must never be rejected. Rendering checks
compare range-only and range/cone lists across detailed cars, tangent/surface
fixtures, alpha modes, both eyes, head movement and attached-light rotation.
Alpha must match exactly; RGB differences are bounded to one byte per channel.

With the headset awake and worn, add `--profile` for the fixed 16-car, 768-square
workload. Broad, narrow, mixed and backward-pointing lights use 0/1/4/8 counts.
ABBA reference/candidate stages interleave within each frame on the same renderer,
shader and buffers, with 30 warmup and 90 measured frames per stage. GPU disjoint
queries reject the run. Wall time includes submission and GPU completion. The
reference retains range filtering and disables only cone filtering. No lighting,
geometry or sample density changes between modes.

## Combined light-filtering headset acceptance

Run the normal engine with `tests/scenes/light-filtering.js`. The scene keeps four
lights enabled and nonzero, so the automatic filtering policy is eligible. The
car and normal-map target plus all four light markers accept grip/trigger grabs.
Blue and yellow boxes aim blue/white spotlights; the yellow sphere and green box
carry warm/green point lights. Close grabs follow hand rotation.

Move each target sideways through cone edges and farther from the markers through
range boundaries, then sweep the spot handles across it. Move your head throughout.
Look for abrupt lighting pops, missing highlights or flicker, rather than the
expected smooth physical falloff. A/X changes range (.65/1.2 meters), B/Y changes
cone width, and bumper resets positions. Changing those settings intentionally
changes illumination; check continuity while moving with a fixed setting. Release
held objects before resetting. The HUD is non-interactive; only this interactive
scene's model selection uses pointer input. Stop the demo after user verification.

## PBR geometry and pixel cost diagnostic

Build the excluded `pbr-cost-profile` target after reconfiguring CMake:

```sh
cmake -S . -B build
cmake --build build -j1 --target pbr-cost-profile
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/pbr-cost-profile
```

Run with the headset awake and worn for normal power. This offscreen GLES harness
opens no OpenVR overlays and captures no input. It renders 32 PBR ToyCars with
zero/eight mixed lights, fixed full/reduced geometry and 384/768/2048-square viewports.
All cases retain the same backing framebuffer and asset allocations, shader,
projection, and deterministic moving-pose/eye sequence. Case order reverses each
frame; each case has 30 warmup and 90 measured frames, alternating eyes. Repeat
whole runs to check consistency. GPU disjoint/GL errors fail the run; switching
back to the baseline must reproduce its pixels exactly.

Reduced resolution and geometry intentionally change quality for attribution.
They are not optimization candidates or equivalent-quality throughput results.
Viewport sensitivity includes pixel coverage, textures, shading and raster work;
LOD sensitivity includes vertex/index work and changes in rasterization/coverage.
Neither proves a pure hardware bottleneck or predicts additive savings. GPU query
time excludes compositor work; wall time includes `glFinish`. No pixel readback
occurs inside measured frames. Production defaults and shaders remain unchanged.

## Interactive PBR variant validation

Run `./run-pocketspace.sh --scripts-dir tests/scenes --launch pbr-variants.js` after the automated
variant checks pass. The normal engine exercises the prepared policy. It shows a car,
normal-map fixture and alpha-mode model on independent surfaces against blue,
with four grabbable handles carrying eight point/spot lights. Grip/trigger moves
models or handles; A/X cycles eight/four/zero lights; B/Y toggles the HDR
environment; bumper resets poses (release held objects first).

Check head movement, approach/distance and LOD, rotation, overlaps, cutout holes,
reflections, and stable rendering after light-count/environment changes. Lighting
changes intentionally between settings. Look for stale state, flicker, missing
parts or incorrectly filled cutouts. Shader variants are prepared at startup/successful reload; state changes use
ready programs or general fallback without compiling in the render path. The
16-slot cache can fall back to general PBR while switching states. Stop the demo
immediately after user verification and confirm its process exited.


## Spotlight shadows

Build `spot-shadow-regression` explicitly after reconfiguring CMake, then run it
from the repo root with `DISPLAY=:0 EGL_PLATFORM=x11` and the native SteamVR
library path. It uses real GLES rendering and API fixtures without creating
OpenVR overlays. `--benchmark` adds bounded one/four-car comparisons at
256/512/1024 map pixels and 768 pixels per eye. Keep the headset awake and worn
for performance measurements; no pointer input is captured.

`./run-pocketspace.sh --scripts-dir scripts --launch shadows.js` is interactive: grab the blue light
handle or yellow box, toggle shadows with A/X, cycle resolution with bumper,
and capture/restore with B/Y. Check ground contact, head motion, moving light and
caster, and the status/time HUD. Stop the demo after visual verification.

## Directional shadows

Run `spot-shadow-regression --directional` in the same native GLES environment
as the spotlight checks. `--directional-benchmark` adds bounded paired one/four-car
comparisons at 256/512/1024 map pixels, using a fixed 2-meter square and 4-meter
depth. Runs are offscreen and capture no input. Keep the headset awake and worn
for timing. The shared API suite includes `directional-shadow-api-smoke.js`.

Run `./run-pocketspace.sh --scripts-dir scripts --launch directional-shadows.js` for interaction. Move
the yellow caster and rotate the blue handle; its translation does not move
world-space coverage. A/X toggles shadows, bumper cycles resolution, B/Y saves
then restores. Check stable head motion, parallel shadows and active HUD status;
stop after user verification.

## Point shadows

Run `spot-shadow-regression --point` with the native GLES environment.
`--point-benchmark` uses bounded paired one-car stages at 256/512 pixels per
face with no overlays or pointer input. Keep the headset active for profiling.
Run `./run-pocketspace.sh --scripts-dir scripts --launch point-shadows.js` for interaction; grab the lamp
and yellow caster, use bumper for resolution, A/X for shadows and B/Y to save
then restore. Inspect floor/cube self-shadowing and transitions across face edges.
Stop the demo after visual verification.
