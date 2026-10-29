# Rendering comparison experiment

See the [test index](index.md) for a brief description of every test and helper.

This experiment leaves the published examples and engine defaults unchanged.
Run from the repository root on the VR runtime host:

```sh
./run-overlay-engine.sh tests/scenes/overlay-comparison.js
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

Use `--engine /absolute/path/to/sf-overlay-engine` to test an isolated build.
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

## Shared scene-depth prototype

`shared-depth-prototype` is an offscreen experiment, excluded from normal builds.
It reuses the actual GLES shader/pass harness to compare per-group occluder
rendering with one opaque/masked scene prepass and read-only depth for group
colors. It opens no OpenVR session and leaves no headset demo running.

```sh
cmake --build build --target shared-depth-prototype render-occlusion-regression -j2
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/shared-depth-prototype --bench
```

Omit `--bench` for pixel comparisons only. Checks cover stereo, lateral views,
opaque/masked/blended/hidden geometry, checker-mask holes, ToyCar and Avocado,
coincident surfaces, and mismatched projection grids. Expected rejection cases
must exhibit pixel differences; these are experimental findings, not crashes.
The benchmark alternates order across three rounds, uses 48 bodies plus a floor,
five groups, two eyes, a fixed common 768-pixel grid, bounded warmup/measurement,
and `glFinish` per frame. Baseline occluders use bounds culling and scissors.

This is a matching-grid feasibility test, not an engine FPS comparison. It reuses
one offscreen color target, omits compositor submission, physics, triple buffers,
and dynamic group layouts. Timing includes CPU submission and completed GPU work.
See [results and adoption requirements](results/shared-depth-prototype.md).


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
  python3 tests/compare-auto-split.py --engine build/sf-overlay-engine \
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

## GPU synchronization investigation

The excluded `gpu-sync-experiment` target builds the real engine with test-only
profiling. Production builds keep `glFinish`, triple buffering, and existing
compositor ownership behavior. No script API or production setting selects the
experimental wait method.

```sh
cmake --build build --target gpu-sync-experiment gpu-sync-pixels -j2
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/gpu-sync-pixels
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-gpu-sync.py --rounds 2 --output /tmp/gpu-sync-comparison
```

The runner compares `glFinish` with a fence inserted after rendering and waited
on the CPU **before** any overlay texture submission. It never submits unfinished
textures, skips a required completion wait, pipelines an extra frame, or changes
buffer reuse. Fence creation/wait failure falls back to `glFinish` and invalidates
the benchmark case. The fence is deleted after its completion wait. An initial
flush flag submits the pending GL commands; subsequent waits repeat only if the timeout expires. The default 100-millisecond
timeout returns immediately when signaled; it does not deliberately sleep that
long. `--fence-wait-ms 1` reproduces the frequent-wakeup comparison.

`SF_GPU_SYNC_MODE=fence` selects the alternative only in the experiment executable;
its default is `finish`. Optional `GL_EXT_disjoint_timer_query` queries enclose the
object render loop. Results are read only after availability is reported; disjoint
results are discarded and unavailable GPU timings remain `null`. Profiling also
records CPU enqueue time, completion-wait wall time, render-thread CPU time inside
that wait, and overlay submission time. GPU query duration is not exclusive GPU
occupancy and can include scheduling effects; it is not interchangeable with the
CPU wait duration. Instrumentation itself can affect timings.

The runner reuses the fixed overlay-comparison geometry, alternates order across
boxes/models and rounds, rejects GL/fence errors and wrong body counts, discards
warmup, and saves logs and summaries. It defaults to 48 bodies, grouped rendering,
12-second runs, and five seconds of warmup. `--renderer single` tests one common
scene surface instead. Keep the headset awake and head steady during comparisons.
`gpu-sync-pixels` compares finished opaque/masked/blended output exactly between
both completion methods, including stereo and lateral viewpoints.

See [synchronization contract review and measurements](results/gpu-sync.md).


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
  python3 tests/compare-regroup.py --engine build/sf-overlay-engine \
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


`tests/regroup-lifecycle.py --engine build/sf-overlay-engine --output /tmp/regroup-lifecycle.log`
adds a bounded native lifecycle run: split/rebalance/merge, hide/show, partial/full
body removal, and live script replacement from boxes to models. It checks that
reload commits, removal leaves only the floor, structural operations occurred,
and SIGTERM stops the process normally.
See [regrouping measurements and validation](results/regroup.md).

## Render pass attribution

The excluded `gpu-sync-experiment` target also supports
`SF_GPU_PROFILE_PASSES=1`. This replaces its whole-render GPU query with separate
elapsed GPU queries around the occluder, opaque, and blended draw callbacks in
one sampled frame per second. Other frames run without pass queries. Production
builds compile out the hooks and environment option.

```sh
cmake --build build --target gpu-sync-experiment -j2
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/profile-render-passes.py --output /tmp/render-pass-profile
```

The bounded runner alternates whole-render and per-pass timing runs across two
rounds of fixed/adaptive 128 mixed models and fixed 128 Avocados, with mesh cache
optimization enabled. It retains logs, checks object counts and shutdown buffer
accounting, rejects unavailable/disjoint GPU timings and reported GL errors, and
records whether paired storage estimates match. Keep the headset awake and the
view steady. Use `--case`, `--rounds`, and `--seconds` to select bounded workloads.

`RENDER_PASS_SAMPLE` arrays are ordered occluder, opaque, blended. GPU times sum
the elapsed queries for each category; CPU values measure render-thread CPU time
inside the same callbacks, including culling and driver calls. Draw/triangle
counts describe submitted primitives, not visible fragments or hardware vertex
invocations. Timings exclude framebuffer clears, surface setup, layer state
changes outside the callbacks, and compositor work. GPU elapsed time can include
scheduling effects. These sampled sums are attribution evidence, not a replacement
for whole-render timing or a guarantee of the savings from removing a pass.
Compare matching-storage pairs to assess instrumentation overhead before drawing
throughput conclusions; differing dynamic allocations invalidate that comparison.

## Opaque shader specialization experiment

`opaque-shader-pixels` compares the general shader with specialized opaque color
and texture-free opaque depth shaders. It checks 216 byte-identical views across
stereo, rotations, group membership, Box/Avocado/ToyCar, material factors, sheen,
masked holes, and blended materials. Masked/blended parts retain the general shader.

```sh
cmake --build build --target opaque-shader-pixels gpu-sync-experiment -j2
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/opaque-shader-pixels
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-opaque-shaders.py --output /tmp/opaque-shader-comparison
```

The excluded `gpu-sync-experiment` accepts `SF_GPU_OPAQUE_SHADER=1` for native
opaque-only benchmarks; it rejects scenes containing any masked/blended asset
with exit status 13. The runner alternates baseline and specialized modes over
two rounds, reverses case order, enables mesh cache optimization, retains logs,
checks GPU timings/errors and object counts, and rejects pairs with differing
buffer estimates. Both use the unchanged vertex shader, geometry, sampling,
alpha convention, and producer completion wait. No production setting or API
enables these shaders. See the [results and adoption decision](results/opaque-shaders.md).

## Primitive occluder visibility experiment

`primitive-visibility-pixels` compares the normal renderer with conservative
material-primitive rejection for nonmember occluders. It covers stereo, group
membership, material alpha modes, near-plane views, and spatially separated
parts, and requires that some draws actually be rejected. CPU checks exercise
invalid data, deforming-asset fallback, and cache reset.

```sh
cmake --build build --target primitive-visibility-pixels gpu-sync-experiment -j2
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/primitive-visibility-pixels
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-primitive-visibility.py --output /tmp/primitive-visibility
```

Only the excluded `gpu-sync-experiment` accepts
`SF_GPU_PRIMITIVE_VISIBILITY=1`. The candidate caches normalized static primitive
bounds, refreshes world/stereo bounds after scene mutations, and adds padded
angular/depth rejection after the existing object-level occlusion check. Single
primitive, animated, skinned, morphing, invalid, and near-plane cases retain the
baseline path. Reload clears cached asset bounds. Visible member/color passes
are unchanged; this does not remove geometry inside a visible model.

The runner alternates modes and case order across two rounds, disables opaque
shader specialization and pass-query sampling, enables mesh cache optimization,
and rejects invalid timing or mismatched buffer estimates. It retains GPU/FPS
metrics and skipped draws/triangles per stereo frame, plus bounds preparation
wall time. Whole-render GPU timing starts after preparation; FPS includes its
cost. See [measurements and adoption decision](results/primitive-visibility.md).

## Spatial occluder cluster experiment

`cluster-visibility-pixels` checks original depth draws against full and culled
spatial-cluster draws. Color draws keep their original buffers and ordering.
The test checks 216 views in each mode, including stereo, material alpha modes,
near-plane views, and dense separated islands within one material primitive.
Uploaded cluster indices are read back and checked for exact preservation of
oriented triangles and duplicates.

```sh
cmake --build build --target cluster-visibility-pixels gpu-sync-experiment -j2
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/cluster-visibility-pixels
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-cluster-visibility.py --output /tmp/cluster-visibility
```

The excluded `gpu-sync-experiment` accepts `SF_GPU_CLUSTER_MODE=original`,
`full`, or `cull`. Every selected mode creates the same extra index buffers,
leaving source vertex/color buffers unchanged. `original` draws existing depth
indices, `full` draws reordered cluster indices in one range per primitive, and
`cull` rejects padded off-tile/deeper clusters and merges contiguous surviving
ranges. Comparing all three separates index-layout effects from rejection costs.
Clusters contain at most 4,096 triangles, partitioned along centroid bounds.
Animated/skinned/morph assets and per-instance deformations retain the original
path. Invalid/near-plane projections retain draws. Reload releases cached buffers.

The bounded runner alternates/reverses modes and cases over two rounds, enables
mesh cache optimization, disables other experiments, rejects invalid GPU timings,
and checks matching buffer estimates and experimental index bytes across each
three-mode set. Statistics report eligible/skipped triangles, submitted ranges,
and preparation wall time. Preparation precedes the whole-render GPU timer; FPS
includes its cost. Experimental index bytes are additional to the normal engine
GPU allocation estimate and are reported separately. No production option or
script API enables cluster rendering.
The cluster runner defaults to fixed mixed models, controlled two-group mixed
models, and fixed Avocados. The two-group case is also selectable through
`tests/fast-motion.py --case 128-models-two-groups`; it keeps stable membership
and moves both groups rapidly through overlapping regions without automatic
regrouping. See the [results and adoption decision](results/cluster-visibility.md).

## Ordered GPU instancing

The production API defaults to ordered instancing with `loadModel(path, eyePixels)`;
pass `{gpuInstancing: false}` to opt out. Explicit `{gpuInstancing: true}` remains supported;
see the [API contract](../docs/javascript-api.md#ordered-gpu-instancing).
`instancing-pixels` uses the actual renderer and batching implementation to
compare original and instanced pixels, including material modes, coplanar
ownership, rotated anchors, indexed/nonindexed geometry, deformation fallback,
256-instance batches, asset-order barriers, attribute state, and buffer cleanup.
`lifecycle-script-regression` runs `instancing-api-smoke.js` for option validation
and cache behavior without a graphics context.

```sh
cmake --build build --target instancing-pixels lifecycle-script-regression gpu-sync-experiment -j2
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/instancing-pixels
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-instancing.py --case 128-avocados-fixed --output /tmp/instancing
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/instancing-lifecycle.py --engine build/sf-overlay-engine --output /tmp/instancing-reload
```

Only the excluded profiling executable accepts `SF_GPU_INSTANCING=0|1`, forcing
original or instanced submission for opted-in assets. It prewarms the shader and
buffer in both modes to match owned storage. The runner disables other geometry
experiments and checks timings/errors, counts, buffer estimates, and actual
saved draws in eligible scenes. Default cases include fixed Avocados, two-group
Avocados, and a mixed-model fallback control. Rejected allocation pairs retain
logs and are not throughput evidence. The `gpuInstancing` summary field is the
API request; `instancing` is the benchmark's forced path selection.

`tests/fast-motion.py --instancing` selects the API option for a normal production
run; `--case 128-avocados-two-groups` selects the controlled two-group workload.
The lifecycle runner uses one production process for four ten-second phases,
alternating sixteen instanced models and empty scenes. It checks three committed
reloads, stable counts, and zero owned GPU allocation estimates in empty phases.
No persistent demo is left running. See [recorded measurements](results/instancing.md).

## Multi-part instancing experiment

The excluded `gpu-sync-experiment` binary accepts
`SF_GPU_MULTIPART_INSTANCING=1` with `SF_GPU_INSTANCING=0|1` to compare original
versus combined multi-material indexed draws. Both paths retain identical
combined geometry storage, including the additional per-vertex material slot,
so timing comparisons reject mismatched allocations. Production does not expose
this flag or batch multi-part assets.

The prototype preserves primitive and object order, original textures and index
order, and supports up to eight opaque/masked material primitives. It uses
explicit texture gradients outside material selection. Blended, animated,
skinned, morph and CPU-deformed geometry retains original draws. Original GPU
buffers remain allocated alongside the test-only combined copy. This is an
investigation, not a memory optimization.

```sh
cmake --build build --target multipart-instancing-pixels gpu-sync-experiment -j2
DISPLAY=:0 EGL_PLATFORM=x11 ./build/multipart-instancing-pixels
python3 tests/compare-instancing.py --multipart --case 32-cars-fixed \
  --case 128-models-fixed --rounds 2 --seconds 16 --output /tmp/sf-multipart
```

`32-cars-fixed` is an optional repeated ToyCar workload; the existing mixed case
interleaves ToyCar and Avocado to measure isolated combined draws without sorting.
The pixel target also verifies that reversing coplanar material order changes
the reference image, then checks the prototype against the original order.

## Bounded-error model LOD experiment

`lod-pixels` and the excluded `gpu-sync-experiment` are compiled with
`SF_MODEL_LOD_EXPERIMENT`; the production engine is not. `SF_GPU_MODEL_LOD=0.5`
or `0.25` selects a triangle target during GPU upload. Missing/other values retain
full geometry. This is a fixed-detail investigation, not a production LOD API or
a distance-based selector.

The prototype uses meshoptimizer's attribute-aware simplifier with UV weights
of 0.1, locked topological borders, and a relative combined error limit of 0.001.
It may stop above the requested triangle count. Only indexed opaque primitives in static assets are eligible; masked/blended
primitives and animated, skinned and morph assets keep full geometry. CPU vertices, picking, colliders and normalization stay unchanged.
The same selected index stream renders both eyes and all color/depth passes.

Original vertex/index allocation capacities remain allocated in both comparison
paths. Reduced index streams are padded for storage but drawn with their actual
counts. GPU estimates therefore still describe the full capacities; these runs
isolate rendering work and do not claim memory savings. Vertex-cache optimization
is enabled in both paths. Other shader/geometry experiments and GPU instancing
must be off; `compare-lod.py` selects these controls automatically.

```sh
cmake --build build --target lod-pixels gpu-sync-experiment -j2
DISPLAY=:0 EGL_PLATFORM=x11 ./build/lod-pixels /tmp/sf-lod-images
python3 tests/compare-lod.py --ratio 0.5 --rounds 2 --seconds 16 \
  --output /tmp/sf-lod-comparison
```

The pixel target writes original/reduced PPMs and per-view `LOD_PIXELS` metrics.
Unlike the exact rendering regressions, this is lossy geometry: pixel differences
are measured, not asserted equal. It validates CPU-mesh preservation, guarded
fallbacks, GL errors and metadata cleanup. `LOD_UPLOAD` records achieved triangle
counts, the simplifier's error metric and one-time generation cost. The metric
is not a guaranteed pixel displacement or a substitute for headset visual review.

## Production automatic model LOD

Normal `loadModel` defaults to `lod: true`; see the
[API contract](../docs/javascript-api.md#automatic-model-lod). `auto-lod-regression`
checks size/distance hysteresis, off-axis bounds, near restoration, full CPU
geometry, exact pixels against expanded selected-index references, instancing
at every level, mixed-level barriers and actual buffer deletion. The lifecycle
API harness runs `lod-api-smoke.js` for strict boolean parsing and cache rules.

```sh
cmake --build build --target auto-lod-regression sf-overlay-engine -j2
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
cmake --build build --target scene-container-regression sf-overlay-engine -j2
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
cmake --build build --target morph-regression -j2
python3 tests/morph-loader-regression.py ./build/morph-regression
```

The generator supplies five-target STEP/LINEAR/CUBICSPLINE clips, node/mesh/zero
weights, combined skinning, and malformed models. The native harness checks
positions, animated picking/bounds, independent poses, the bundled Khronos cube,
real JavaScript controls, and restored morph poses. No VR session is needed.
Run `./run-overlay-engine.sh scripts/morph.js` for headset visual acceptance.

## Raw controller subscription recovery

```sh
cmake --build build --target lifecycle-input-regression -j2
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
cmake --build build --target animation-blend-regression -j2
./build/animation-blend-regression
```

Checks local translation/scale/quaternion/morph blending, both live clocks,
default and interrupted sources, endpoints, pause/resume/seek/stop, 900 blended
BVH/reference picking comparisons, and independent rigid/skin/morph geometry.
The real JavaScript fixture covers invalid options, live/frozen v5 snapshot
roundtrips, malformed transition saves, v4 compatibility and example controls.
No VR session is needed. `scripts/blending.js` is the headset example; left to
right it shows node animation, skeletal animation and morph animation. Bumper
crossfades to the next clip (or a different phase for single-clip assets) over
one second; A/X pauses/resumes all three. Press bumper during a fade to test a
continuous interrupted transition. Grip or trigger grabs each model.

## Animated-model performance profiling

```sh
cmake -S . -B build
cmake --build build --target animation-profile-experiment animation-profile-regression -j2
# Workstation EGL check (choose the EGL platform appropriate to your host):
EGL_PLATFORM=surfaceless ./build/animation-profile-regression
node tests/animation-profile-scene.cjs
python3 tests/animation-profile-analysis-test.py
```

On an awake Steam Frame, after stopping any other engine demo:

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/animation-profile-experiment tests/scenes/animation-profile.js > /tmp/animation-profile.log 2>&1
```

Keep the headset active and looking at the models. The six 20-second stages use
8, 16 and 32 CesiumMan instances, paired animated/paused, with staggered clip
phases, 768 eye pixels and one logical render group. There is no physics or
manual grabbing in this workload. One HUD panel reports process FPS, CPU and
RSS. B/Y aborts and removes the actors. Completion also removes the actors;
stop the process after the completion marker. Change the scene's `MODEL` constant
to `InterpolationTest` or `AnimatedMorphCube` for those paths; record that change.

```sh
python3 tests/analyze-animation-profile.py /tmp/animation-profile.log > /tmp/animation-profile.json
```

The profiler is compiled only into excluded targets with `SF_ANIMATION_PROFILE`.
Normal engine builds contain no animation phase timers. Each JSON log window
covers 120 frames, resetting when object/playing counts or script generation
change. The analyzer discards complete windows starting in each stage's first
five seconds, rejects incomplete/aborted or mismatched runs, and weights means
by sampled frames. `maxWindowP95Ms` is the largest window p95, not a combined p95.

CPU phases are pose/channel evaluation (including hierarchy, blend and skin
palettes), fused per-vertex deformation/normalization/bounds, BVH refit, and
dynamic VBO upload submission. Initial pose geometry allocation is excluded
from phase timing; first-stage windows are warmup. Do not split the production
vertex loop just to time bounds separately: that would change memory traffic.
Counters report evaluated objects, expanded vertices, dirty poses uploaded,
and submitted bytes per frame. Clean paused poses do not upload again.

`uploadSubmitMs` measures CPU time in GL upload calls, including any driver
blocking, and is not GPU DMA duration. `renderSubmitMs` is CPU render submission;
`gpuWaitMs` is CPU time waiting for existing GPU completion, not a GPU timestamp.
`workMs` excludes the loop's final pacing sleep; logged FPS includes pacing.
Compositor/head tracking, presentation, and sampling conditions still affect
end-to-end results. Compare equivalent layouts and retain picking, bounds,
stereo color/depth coherence, and independent instance poses in any GPU redesign.

## GPU skinning prototype

GPU skinning is automatic for eligible models in the normal engine. The
excluded comparison executable retains CPU/GPU profiling switches; these are
not public script options. Build comparison and regression targets explicitly:

```sh
cmake -S . -B build
cmake --build build --target gpu-skinning-experiment gpu-skinning-regression -j2
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
joint boxes for selection. CPU reference and GPU modes use the same conservative
bounds policy in the comparison executable so they request comparable surface
layouts and sampling density. Normal builds use conservative bounds for GPU
skins and exact deformed bounds for CPU fallbacks.

On an active Frame with no other demo running, use the same binary and scene
for both cases, stopping the process between runs:

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  SF_GPU_SKINNING=0 ./build/gpu-skinning-experiment tests/scenes/gpu-skinning-profile.js > /tmp/skin-cpu.log 2>&1
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  SF_GPU_SKINNING=1 ./build/gpu-skinning-experiment tests/scenes/gpu-skinning-profile.js > /tmp/skin-gpu.log 2>&1
```

Each run has 20 seconds animated then 20 seconds paused, all with 32 independent
CesiumMan poses, 768 eye pixels, one render group and one HUD. Keep the view and
controller pointing comparable; candidate picking contributes real CPU work.
B/Y aborts. After `ANIMATION_PROFILE_DONE`, stop the process. Analyze each log
with `tests/analyze-animation-profile.py` as for the animation profiling scene.
Record surface allocation estimates and compare pixels before adopting a result;
GPU palette/VBO memory savings are separate from compositor surface density.

The regression is compiled against the production GPU path (no experimental
mode definition). CPU-reference animation regressions explicitly select the
retained CPU implementation. The regression captures GPU-transformed vertices and compares them with the CPU
reference, checks containment, both-eye images, 21,000 ray comparisons, clean
uploads/cache reuse, live and frozen transitions, saved poses, and CPU fallbacks.
The pixel check allows at most 32 pixels with channel differences over 2/255 in
each 256x256 image; report actual differences rather than describing that bound
as exact equality. The CPU production regressions must also pass after changes
to the shared pose code. No GPU readback is used in normal experiment rendering
or picking; transform feedback readback is confined to the correctness test.


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


For manual grabbing acceptance, run the experimental executable with
`SF_GPU_SKINNING=1` and `tests/scenes/gpu-skinning-interaction.js`. Point at models
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

Run `./build/sf-overlay-engine tests/scenes/gpu-skinning-production.js` on Frame
with the normal display/library environment. No GPU experiment variable is used.
Twelve instances mix Fox, CesiumMan, RiggedFigure, RiggedSimple, rigid-node
InterpolationTest and AnimatedMorphCube. Grab with either hand using grip or
trigger; A/X pauses/resumes, bumper captures/restores the container in place,
and B/Y clears it. Cleanup also runs after five minutes. Check pose continuity,
selection after restore, and coherent overlap while moving your head. The HUD
shows live FPS/CPU; this manual scene is not a matched performance benchmark.
