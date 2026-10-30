# Rendering comparison experiment

See the [test index](index.md) for a brief description of every test and helper.

This experiment leaves the published examples and engine defaults unchanged.
Run from the repository root on the VR runtime host:

```sh
./run-pocketspace.sh tests/scenes/overlay-comparison.js
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
rejects paired storage and geometry mismatches. Actual triangle/LOD counts must
remain stable and equal, with complete stereo member geometry. Automatic LOD is
enabled; use `--no-lod` to isolate passes when movement crosses LOD thresholds.
Add `--instancing` to include production ordered instancing (the historical profile omitted it).
The runner clears inherited `SF_GPU_*` experiment options before each run and
explicitly enables the shipped prepared-PBR policy in the excluded engine.
It records bounded program-cache accounting in addition to buffer allocations.
Keep the headset awake and the view steady. Use `--case`, `--rounds`, and `--seconds` to select bounded workloads.

For a dense moving-car PBR profile, add `--case 32-cars-fixed --no-lod
--instancing --pbr-lights 0` (keep these options on one command line). Repeat
with `--pbr-lights 4` and `--pbr-lights 8`, using separate output directories.
The explicit zero-light option still enables PBR; omitting it retains the
historical unlit workload. Mixed lights alternate points and spots, with fixed
positions, two-meter ranges and .3/.6-radian spot half-angles. Geometry, material
settings and trajectories stay the same across counts; illumination changes
intentionally. Full detail isolates lighting cost from moving LOD thresholds.
These runs create grouped overlays but enable no interaction or pointer capture,
and terminate after the configured duration. Compare allocations and geometry
across light counts as well as within instrumentation pairs; live head motion
and time-based trajectories remain sources of variation.

For a smaller fixed/adaptive detailed workload, use `--case 32-cars-fixed
--case 32-cars-adaptive --no-lod --instancing --pbr-lights 8` on one command line.
This preserves full geometry while exposing repeated PBR occluder cost. Keep
instrumentation comparisons within a grouping mode; layouts can allocate different
sampling densities. Grouped peak mismatch rejects a pair even when median owned
storage matches. See [the prepared-PBR refresh](results/pbr-pass-refresh.md).

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
  python3 tests/instancing-lifecycle.py --engine build/pocketspace --output /tmp/instancing-reload
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
cmake --build build --target auto-lod-regression pocketspace -j2
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
cmake --build build --target scene-container-regression pocketspace -j2
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
Run `./run-pocketspace.sh scripts/morph.js` for headset visual acceptance.

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

Run `./build/pocketspace tests/scenes/gpu-skinning-production.js` on Frame
with the normal display/library environment. No GPU experiment variable is used.
Twelve instances mix Fox, CesiumMan, RiggedFigure, RiggedSimple, rigid-node
InterpolationTest and AnimatedMorphCube. Grab with either hand using grip or
trigger; A/X pauses/resumes, bumper captures/restores the container in place,
and B/Y clears it. Cleanup also runs after five minutes. Check pose continuity,
selection after restore, and coherent overlap while moving your head. The HUD
shows live FPS/CPU; this manual scene is not a matched performance benchmark.

## Rigid-node and morph profiling

`tests/scenes/rigid-morph-profile.js` uses the current animation backend with
phase timers in `animation-profile-experiment`. It runs eight 20-second stages:
64 animated/paused and 256 paused/animated InterpolationTest instances, then
64 paused/animated and 256 animated/paused AnimatedMorphCube instances. Every
actor has an independent phase; each workload uses one render group and 768
reference eye pixels, no physics, and one HUD. Actors use a centered 8x8 or
16x16 grid. The larger grid uses smaller objects to keep the overall view bounded.

Use Frame's display/library environment and run:

```sh
cmake --build build --target animation-profile-experiment -j2
./build/animation-profile-experiment tests/scenes/rigid-morph-profile.js > /tmp/rigid-morph.log 2>&1
python3 tests/analyze-animation-profile.py /tmp/rigid-morph.log
```

Keep looking at the models for about three minutes. B/Y aborts and clears actors;
normal completion clears them and prints `ANIMATION_PROFILE_DONE`. Stop the
process afterward. Compare animation phases with paused phases at the same count;
record allocation estimates and any group density caps. Poses/projected bounds
still differ, so paired FPS alone does not isolate animation CPU cost. These
bundled models are low-triangle feature samples, not proxies for high-detail
morph characters. Use the phase measurements to decide what warrants further
investigation. Scene transitions, actor counts, playback and abort cleanup are
checked by `node tests/animation-profile-scene.cjs`.

## Rigid primitive update comparison

The excluded `rigid-pose-experiment` target compares full CPU primitive updates
with transform-cached rigid updates. Normal builds enable rigid caching by
default; only the excluded target exposes the environment comparison switch. Only rigid, non-morph primitives can skip work;
exact world-matrix equality includes parent animation. Cached per-part bounds and
radius preserve global bounds; BVH refitting skips only when no part changed.
Pending upload dirtiness survives repeated evaluations before rendering.

Build `rigid-pose-experiment` and `animation-profile-regression` explicitly. The
native profile regression compares cached/full vertices, bounds, BVH nodes and
actual uploaded VBO contents over every InterpolationTest clip and frozen fades.
Existing animation/blend/GPU lifecycle regressions also enable the cache in tests.

On Frame with its display/library environment, run the same executable/scene
in both modes, stopping each after `ANIMATION_PROFILE_DONE`:

```sh
SF_RIGID_POSE_CACHE=0 ./build/rigid-pose-experiment tests/scenes/rigid-pose-profile.js > /tmp/rigid-full.log 2>&1
SF_RIGID_POSE_CACHE=1 ./build/rigid-pose-experiment tests/scenes/rigid-pose-profile.js > /tmp/rigid-cache.log 2>&1
```

Each mode runs 256 actors animated then paused for 20 seconds each. It selects
**Linear Scale** (clip 1), so improvements do not depend on STEP animation holding
constant values. One group, 768 reference eye pixels, same grid/size/materials;
B/Y aborts. Use the existing animation analyzer, compare allocation estimates,
and reject FPS attribution if surface allocations differ. The mode environment
switch is compiled only into this excluded target. No draw ordering changes.

## Draw uniform submission comparison

Build excluded targets `draw-submit-experiment` and `draw-uniform-pixels`.
`EGL_PLATFORM=surfaceless ./build/draw-uniform-pixels` compares 288 exact stereo
images across static, rigid, morph and GPU-skinned models, opaque/masked/blended
materials, depth passes, instancing switches and external uniform writes between
passes. Pass-local caches must always start invalid; they never cache textures
or retain state across frames/program replacement. No draw reordering/batching
or geometry/shader changes are made. Object uniform writes can be omitted when
that object has no primitive in the current pass.

Using Frame's normal display/library environment, run the same binary/scene with
`SF_DRAW_UNIFORM_CACHE=0` and then `1`:

```sh
SF_DRAW_UNIFORM_CACHE=0 ./build/draw-submit-experiment tests/scenes/rigid-pose-profile.js > /tmp/draw-full.log 2>&1
SF_DRAW_UNIFORM_CACHE=1 ./build/draw-submit-experiment tests/scenes/rigid-pose-profile.js > /tmp/draw-cache.log 2>&1
```

Each mode takes 40 seconds (256 Linear Scale actors, animated then paused).
Keep the view steady; B/Y aborts. Stop after the completion marker. Use the
animation analyzer; compare allocation estimates before attributing FPS changes.
`DRAW_UNIFORMS` periodically reports issued/skipped object/material uniform
calls for one frame. The toggle and counters are excluded from normal builds;
normal rendering retains direct full uniform writes. The
[completed comparison](results/draw-uniforms.md) did not justify adoption.

## Vertex array submission comparison

Build excluded targets `vertex-array-experiment` and `vertex-array-pixels`.
`EGL_PLATFORM=surfaceless ./build/vertex-array-pixels` compares 864 exact stereo
views covering opaque/masked/blended materials, static/rigid/morph/GPU skin,
instancing, LOD levels, resource deletion and return to default VAO 0. Both
comparison modes allocate the same VAOs during asset/CPU-pose upload. GPU skin
retains its existing draw path. Asset/pose teardown deletes owned VAOs before
buffers; no global GL-name cache survives deletion, reload or context changes.

On Frame with its normal display/library environment, compare:

```sh
SF_VERTEX_ARRAY_CACHE=0 ./build/vertex-array-experiment tests/scenes/rigid-pose-profile.js > /tmp/vao-full.log 2>&1
SF_VERTEX_ARRAY_CACHE=1 ./build/vertex-array-experiment tests/scenes/rigid-pose-profile.js > /tmp/vao-cache.log 2>&1
```

Each mode runs 256 Linear Scale actors for 20 seconds animated and 20 paused.
Keep looking at the models; B/Y aborts. Stop after the completion marker and use
`tests/analyze-animation-profile.py`. Require matching surface allocations and
unchanged pixels before attributing a throughput change. VAOs retain vertex
attribute/index bindings; no geometry buffers, draw order, shader, material or
quality setting changes. Driver VAO metadata is outside the engine's buffer
allocation estimate. Normal builds compile out this experiment and its switch.


The [completed vertex-array comparison](results/vertex-arrays.md) passed exact
images but did not establish a throughput improvement. It remains excluded;
normal engine vertex setup is unchanged.

## Adjacent rigid primitive batching

Build excluded targets `rigid-batch-experiment` and `rigid-batch-pixels`. The
native image check compares 864 exact stereo views, CPU/GPU/morph fallbacks,
material barriers, clip changes, mapped combined-stream ranges and buffer cleanup.
The prototype only combines adjacent, complete-triangle CPU rigid parts with
identical factors/sheen/alpha/cutoff/double-sided state and no source texture
(the engine's immutable white texture). Skins, morphs, blended parts, textured
parts and static assets retain their existing paths. No sorting crosses a part,
material or object boundary.

Eligible poses now own one combined expanded VBO; per-part VBOs are absent.
Changed primitive ranges upload once. Both comparison modes use this same stream:
reference draws individual ranges, batching combines eligible adjacent ranges.
Unmerged/textured parts still draw their correct ranges. Teardown releases the
stream once and GPU allocation estimates count it once. CPU geometry/BVH/bounds
are unchanged. The historical prototype report used duplicate storage/uploads;
that arrangement has been removed.

On Frame with its normal display/library environment:

```sh
SF_RIGID_BATCH=0 ./build/rigid-batch-experiment tests/scenes/rigid-pose-profile.js > /tmp/batch-full.log 2>&1
SF_RIGID_BATCH=1 ./build/rigid-batch-experiment tests/scenes/rigid-pose-profile.js > /tmp/batch-on.log 2>&1
```

Each mode runs 256 Linear Scale actors, 20 seconds animated then 20 paused.
Keep the view steady; B/Y aborts. Stop after completion and use the animation
analyzer. `RIGID_BATCH_DRAWS` reports CPU-pose draws and merged source parts for
one frame. Require matching allocations and pixels before attributing a gain.
The nine untextured cubes in InterpolationTest can merge into one draw; its
textured plane stays separate. The single-stream implementation and adjacent rigid grouping are enabled in
normal builds. Environment toggles and
draw counters exist only in the excluded comparison executable. The pixel target
uses production code with test counters, without the experiment definition.
This is independent of the rejected multi-material shader experiment.


The mixed GPU lifecycle regression also checks independent restored rigid streams,
absence of per-part VBOs, exact allocation size, and buffer deletion through 30
successful/failed reload and live/frozen snapshot cycles. The rigid phase test
maps individual stream ranges across all nine rigid clips and preserves pending
updates through repeated pose evaluations.


The normal-engine mixed acceptance scene now selects Linear Scale for its rigid
actors. It checks GPU skins, morph fallbacks and adjacent rigid batches together;
bumper save/restore rebuilds independently owned streams. Use the existing
[normal-engine instructions](#normal-engine-mixed-backend-acceptance).

## Static vertex specialization experiment

The excluded `gpu-sync-experiment` accepts `SF_GPU_STATIC_VERTEX=1` to replace
`skinEnabled ? skinPoint() : p` with `p` in the ordinary model vertex program.
The compiler can then remove unused skin attributes, uniforms, and palette
sampling. Projection, geometry, material shaders, and the instancing program
remain unchanged. This is an experiment for non-skinned workloads, not a
production backend policy; the experimental mode rejects skinned assets.

Build `static-vertex-pixels` and run it with the Frame display/library environment.
Its 216 byte-identical images cover Box, Avocado, ToyCar, both eyes, three head
positions, opaque/masked/blended state, checker-mask holes and external occlusion.
It verifies that the linked specialized shader has no skin-enable/palette uniform.

```sh
cmake --build build --target static-vertex-pixels gpu-sync-experiment -j2
./build/static-vertex-pixels
python3 tests/compare-static-vertex.py --output /tmp/static-vertex-comparison \
  --case 128-models-fixed --case 32-cars-fixed
```

The comparison uses automatic LOD, mesh cache optimization, and ordered instancing.
It reverses pair/case order in round two, rejects GPU errors/disjoint samples and
storage mismatches, retains logs, and verifies normal process/budget cleanup.
Keep the headset awake and view steady. GPU timer values include scheduling and
are separate from FPS and compositor latency. See the
[dense-scene investigation](results/dense-model-current.md) for results.

## Mixed ordinary/GPU vertex routing investigation

`ordinary-program-regression` checks the integrated two-program route with real
GLES rendering and transactional script/scene lifecycle. It covers GPU skinning,
CPU skinning fallback, rigid and morph animation, static indexed/LOD geometry,
actual instanced batches between ordinary/GPU draws, alpha modes and external
occlusion. It verifies exact images, restored GL state, failed reload rollback,
rendering restored scenes, and owned/borrowed program cleanup.

```sh
cmake --build build --target ordinary-program-regression gpu-sync-experiment -j2
./build/ordinary-program-regression
python3 tests/compare-static-vertex.py --mixed-routing --output /tmp/ordinary-routing
```

Only `gpu-sync-experiment` accepts `SF_GPU_ORDINARY_VERTEX=1`. It chooses an ordinary
primary shader and routes GPU-skinned poses to the general program, restoring
view/material/attribute state around changes and instancing. The helper and switch
are experimental; normal builds keep the general shader. Do not combine routing
with other shader/visibility experiments. The `--production-routing` spelling is
an alias describing the integration form, not default-on adoption. See the
[completed integration results](results/ordinary-program-routing.md): correctness
passes but native performance does not establish a consistent gain.

For an optional interactive check, run the excluded executable with that switch
and [ordinary-program-mixed.js](scenes/ordinary-program-mixed.js). Grip/trigger
grabs, A/X pause/resume, bumper save/restore and B/Y finish cover 18 mixed actors
in two groups. This interactive scene enables grabbing intentionally; timed
`fast-motion.py` workloads have non-interactive model surfaces and no HUD panels.

## Geometry-aware grouping comparison

The excluded `gpu-sync-experiment` now prints `RENDER_WORK_SAMPLE` once per
profiling window. Arrays are ordered occluder, opaque, blended. `drawsPerFrame`
and `trianglesPerFrame` count actual submissions in every rendered frame,
including indexed/LOD draws, instanced multiplicity, GPU skins and merged rigid
ranges. These are submitted triangles, not visible fragments or vertex shader
invocations. `sceneTrianglesPerFrame` counts selected geometry once per visible
object, independently of repeated surface/eye draws. `lodObjectsPerFrame` counts
visible objects at selected levels 0, 1 and 2, including objects without reduced
indices at level 0. `sceneFrames` must equal rendered `frames`.

`SF_GPU_GROUP_POLICY=occluders` tests a tie-break between split/rebalance candidates
with the same minimum allocated pixel area. The predictor uses both eyes' cached
bounds and selected opaque/masked triangle counts. It never increases the minimum
area, changes density/caps, or changes membership/rendering rules outside choosing
an equal-cost partition. Production keeps the existing planner.

```sh
cmake --build build --target group-occluder-regression gpu-sync-experiment -j2
./build/group-occluder-regression
python3 tests/render-work-analysis-test.py
python3 tests/compare-group-policy.py --no-lod --case 128-models-adaptive \
  --seconds 20 --output /tmp/group-policy
```

The native planner regression compares 256 deterministic layouts and checks
complete stable membership, unchanged minimum allocation, conservative fallbacks
and predicted occluder work. The Python analysis rejects malformed counters,
frame/count mismatches, changing/mismatched scene triangles or LOD distributions,
and incomplete/mismatched member geometry even when allocation estimates match.
Draw batching and occluder duplication may intentionally differ; selected member
triangle geometry must match. The runner also
requires matching allocation estimates and grouped peaks, reverses per-case pair
order and clears inherited experiment overrides. Full-detail `--no-lod` isolates
grouping from moving LOD thresholds; it does not measure automatic LOD throughput.
Timed overlays are non-interactive and have no HUD or visible controller beams.
See the [group policy investigation](results/group-occluder-policy.md).

`validate-render-work.py --output /tmp/render-work-validation.log` runs the
non-interactive [render-work-validation.js](scenes/render-work-validation.js)
fixture through the excluded executable and checks exact draw/triangle totals
for instanced Boxes, GPU-skinned Foxes and merged rigid ranges. Each stage lasts
four seconds; normal completion clears actors and the runner stops the process.
It verifies frame/LOD coherence, clean GL status and zero grouped budget charges
at shutdown. No interactive headset feedback is needed for these counting checks.

## PBR materials

Build `pbr-regression` explicitly. Run from the repository root with a working
GLES context; no OpenVR session or controller interaction is required:

```sh
cmake -S . -B build
cmake --build build -j2 --target pbr-regression
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

## Static PBR indexing experiment

Reconfigure CMake and build the excluded `pbr-index-regression` target. Run it
from the repository root with the same native GLES environment as
`pbr-regression`. No OpenVR session, overlay or controller input is created.
It compares expanded geometry, plain full-input indexing, and indexing with
opaque/mask vertex-cache/fetch ordering explicitly enabled in mode 2. The earlier
recorded run left the policy off; new samples record the actual flag. Uploaded
ToyCar streams must demonstrate changed triangle order in opaque/mask modes and
preserved order in blend mode. Alpha modes are assigned before upload; changing
an opaque upload to blend afterward would incorrectly test reordered transparency.
Both member-color and external-depth views are compared. Identity includes all 14 position/UV/surface floats;
retained CPU geometry stays expanded. Animated, skinned and morph assets retain
the expanded upload. Both 16-bit and 32-bit index boundaries are checked.

`--benchmark` compares one/sixteen full-detail ToyCars with environment lighting,
a fixed 768×768 offscreen target and alternating eyes. Each variant uses an
expanded/indexed/indexed/expanded sequence, with 30 warmup and 120 measured frames
per stage. Geometry, shaders, lighting and quality stay matched; GPU disjoint and
GL errors fail the run. The reported storage is geometry VBOs plus index buffers,
excluding textures, environment storage and retained CPU arrays. Every measured
frame asserts matching color draw/triangle counts and zero external-depth work. Keep the headset
awake and worn. Results are offscreen costs, not compositor FPS.

The excluded `pbr-index-demo` executable is the normal engine compiled with the
test-only path. `SF_PBR_INDEXED=0` retains expanded uploads, `1` enables indexing
with triangle order retained, and `2`, when `optimizeVertexCache` is enabled, additionally orders static non-blend
triangles for the vertex cache and vertex fetch. Use the same environment as the
normal engine and pass a script such as `scripts/pbr.js`. This switch is not a
public API and is absent from the production executable.

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
Failure injection is compiled only into this excluded test target. See
[the production results](results/pbr-lod-production.md) for budgeted native cost
and the [prototype results](results/pbr-lod.md) for quality and memory tradeoffs.

For the interactive headset check, run:

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/pocketspace tests/scenes/pbr-lod.js
```

Approach and move away from the car and normal-map fixture. Grip or trigger grabs
must restore full detail; rotate and release them, then inspect silhouettes,
reflections, normal-map orientation, overlaps and transitions while moving your
head. The excluded `pbr-lod-demo` target additionally logs detail transitions with
triangle counts and held state. The normal engine uses the existing automatic selection and hysteresis, with one object choice
for both eyes and all passes. Unsupported or ineffective reductions retain full
detail. Production enables the same generation by default under `lod: true`,
with the shared payload budget. Only the excluded diagnostic targets recognize
`SF_PBR_LOD=0` to force a full-detail baseline; the production engine ignores it.

## PBR depth checks

Reconfigure CMake and build the excluded `pbr-depth-regression` target. Run from
the repository root with the normal GLES environment:

```sh
cmake -S . -B build
cmake --build build -j2 --target pbr-depth-regression
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
Production enables the cheaper depth pass automatically. Only the excluded
`pbr-depth-demo` diagnostic executable accepts `SF_PBR_DEPTH=0/1` for comparisons.

## Production PBR indexing checks

Reconfigure CMake and build `pbr-index-production-regression` explicitly. Run from
the repository root with the normal GLES environment. Unlike the earlier
comparison target, this target uses the production opt-in policy and ignores
`SF_PBR_INDEXED`. It compares both eyes at all three LOD choices, color/external
depth, original blended materials and animated fallbacks. It checks reconstructed
full shader inputs, CPU preservation, shared snapshots, cache conflicts, rejected
reload preservation, buffer cleanup, a full LOD budget and partial allocation
rollback. Failure injection is compiled only into this excluded check.

```sh
cmake -S . -B build
cmake --build build -j2 --target pbr-index-production-regression
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
The target includes excluded LOD diagnostics; `SF_PBR_LOD_INDEXED=0` forces expanded
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
cmake --build build -j2 --target point-light-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/point-light-regression
```

The test opens no OpenVR session and captures no controller input. It checks both
eyes, direct-light intensity/color, range cutoff, disabled and unlit invariance,
external-depth silhouettes, local-offset transforms, option getter mutations,
capacity, attachment/container cleanup, failed reloads and v8/legacy restoration.
Failures exit with a diagnostic instead of deliberately generating a core dump.

With the headset awake and worn, add `--profile` for zero/one/four/eight-light
measurements in forward and reverse order. Each case uses a 768-square
target with either one Box or 16 ToyCars, 60 warmup and 180 measured frames,
alternating eyes. GPU timer queries
reject disjoint measurements; wall time includes rendering and GPU completion.
This is a bounded light-loop comparison, not a many-model throughput guarantee.

Run `./run-pocketspace.sh scripts/lights.js` for interactive acceptance. Grab the
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
cmake --build build -j2 --target spotlight-regression point-light-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/spotlight-regression
```

Checks cover narrow/wide/reversed cones in both eyes, normalization, equal-angle
edges, full-hemisphere point-light equivalence, unlit/depth invariance, attachment
rotation, type-safe APIs, cone validation, getter mutations, mixed capacity,
named-file restoration, invalid mixed-snapshot rollback and legacy point lights.

With the headset awake and worn, run `point-light-regression --profile-spots` for
matched point/spotlight timings. It retains the Box/16-car workload and measured
frame counts described above. Broad spotlight cones (inner 1.2, outer 1.4 radians)
fully illuminate the targets to avoid crediting a narrower illuminated workload
as a cheaper BRDF. Each type uses the forward/reverse count sequence independently.

Run `./run-pocketspace.sh scripts/spotlights.js` for interactive acceptance. Grab
the blue handle with grip/trigger and rotate it to aim across the car. Bumper
switches width, A/X toggles, and B/Y captures then restores. Check head motion,
cone falloff and restored attachment behavior. Stop the demo immediately after
verification.

## Light shading work comparison

Build `light-work-regression` explicitly after reconfiguring CMake. It compares
production per-object light lists with the same shader receiving all scene
lights. It also retains the rejected shader-work experiment behind a test-only
`--profile-shader` mode. It opens no overlays and captures no input:

```sh
cmake -S . -B build
cmake --build build -j2 --target light-work-regression
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
and candidate renderers are used for pixel checks and the rejected shader mode.
Light counts are 0, 1, 4 and 8. Broad-range and spatially local lights are
compared separately; GPU timer disjoint results reject the run.
The measured wall time includes submission and GPU completion, not just CPU work.

## Spotlight cone selection

Build `spotlight-cone-regression` explicitly after reconfiguring CMake. It uses
real GLES without opening an OpenVR session or capturing input:

```sh
cmake -S . -B build
cmake --build build -j2 --target spotlight-cone-regression
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
cmake --build build -j2 --target pbr-cost-profile
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

## PBR prepared-rotation experiment

`pbr-transform-experiment` is an excluded offscreen target. Reconfigure CMake,
then build it explicitly and run with the same EGL/SteamVR library environment
as `pbr-cost-profile`. Keep the headset awake and worn for normal GPU power;
it opens no overlays and captures no input.

The candidate shares the position shader's GPU sine/cosine values with its
normal/tangent rotations, preserving Euler arithmetic. The production shader
remains the reference. An earlier CPU-prepared version failed edge-pixel checks
and is not retained as a candidate.
This option exists only under `SF_PBR_TRANSFORM_EXPERIMENT`; normal builds have
no switch. Stereo comparisons exercise full/reduced cars, normal maps, morphing
surface frames, alpha modes, overlapping/separate targets, attachments and large
angles. Coverage must match exactly and RGB may differ by at most one byte.
Correctness failure stops the experiment before performance measurements.

If pixels pass, interleaved ABBA measurements use the same renderer, buffers,
geometry, framebuffer and moving poses at 768/2048 pixels with zero/eight lights.
Each stage alternates eyes, with 30 warmup and 90 measured frames. GPU disjoint
and GL errors fail the run. Repeat runs; do not adopt based on a single case or
GPU timing alone. Wall time includes draw submission and `glFinish`.

## PBR invariant-light arithmetic experiment

Build the excluded `pbr-light-invariant-experiment` target after reconfiguring
CMake. Run with the EGL/library environment used for `pbr-cost-profile`, and an
awake, worn headset for normal GPU power. This offscreen target opens no overlays
and captures no input.

The candidate evaluates `sqrt(nv*nv*(1-a2)+a2)` once per fragment and reuses it in
directional and point/spot visibility terms. It also factors the material's
`(1-metal)*baseColor/PI` term out of per-light diffuse calculations. The latter
changes floating-point grouping and must pass the pixel gate. Only `SF_PBR_LIGHT_INVARIANT_EXPERIMENT` exposes this test
choice; normal builds retain the production shader. Pixel comparisons precede
profiling and require exact alpha plus at most one-byte RGB differences across
stereo/LOD/deformation/normal maps/alpha/attachments and zero/one/four/eight lights.

Matched ABBA timings share the renderer, buffers, shader inputs, projection and
moving poses, with full/LOD-2 geometry at 768/2048 pixels and zero/eight lights.
Each stage has 30 warmup and 90 measured frames, alternating eyes. Repeat runs;
GPU/disjoint/GL checks must pass. Wall time includes `glFinish`. Source-level
reuse may already be performed by the compiler, so correctness alone does not
justify adoption.

## Bounded PBR shader-variant experiment

Reconfigure CMake, build the excluded `pbr-variants-experiment` target explicitly,
and run with the same EGL/library environment as `pbr-cost-profile`. Keep the
headset awake and worn for normal GPU power. The offscreen target opens no
overlays and captures no input.

The candidate replaces material-feature flags, alpha mode, environment enable
and the object's filtered light count with compile-time shader constants. It
keeps the original expressions and draw order. The additional program cache has
16 slots; overflow or a remembered compile failure falls back to general PBR.
Depth passes retain the existing program. The prepared cache and source
specialization live in `src/pbr_variants.h` and `src/pbr_program.h`; production
uses ready variants automatically under the guarded policy. The test macro
exposes lazy compilation/failure injection solely for comparisons.

Stereo comparisons cover normal maps, alpha modes, morph deformation, LOD,
attached mixed lighting, environment on/off, and unlit materials at zero/one/four/
eight lights. Each normal comparison requires specialization without fallback;
separate checks exercise the slot cap, rejected compilation, exact fallback
pixels, cache reset and program deletion. Alpha must match exactly and RGB may
change by at most one byte. Failure stops timing.

Matched ABBA measurements include selection/uniform/draw overhead and use the
same assets, buffers, framebuffer, projection and moving poses. Full/LOD-2 cars
run at 768/2048 pixels with zero/eight lights. Each stage has 30 warmup and 90
measured frames, alternating eyes. The current timing path prepares the set
before measurement and asserts compile
time remains unchanged during draws. The 1024-pixel/four-light guard routes lighter
cases to general PBR. Repeat runs.
The cache reports linked-program count, compile time and program-binary lengths;
binary length is not total driver GPU/RAM storage. The 16-slot bound applies to
additional owned programs, excluding existing general/depth programs and any
driver cache. No production policy is inferred from this isolated test.

`compare-pbr-variants.py --output /tmp/pbr-variants-live` runs the same candidate
inside the excluded `gpu-sync-experiment` engine. It compares 32 moving full-detail
cars at zero/eight lights, cache/fetch optimization and ordered instancing, with
normal grouped allocation and compositor submission. Two repetitions reverse
light-count and candidate/reference order. Each run lasts 16 seconds by default;
steady GPU samples start at six seconds. It rejects GL/disjoint/fence, geometry,
allocation and shutdown-accounting failures, and records program counts, binary
lengths, total compile time and fallback draws. Inherited `SF_GPU_*` options are
cleared. Only this excluded engine reads `SF_GPU_PBR_VARIANTS=0/1`. No interaction
is enabled, and every run terminates automatically. The prepared policy compiles only at startup/successful reload; previously unseen
filtered counts use general fallback. The earlier lazy-policy reports are retained
separately and must not be confused with the prepared measurements.

### Interactive PBR variant validation

Run `./run-pocketspace.sh tests/scenes/pbr-variants.js` after the automated
variant checks pass. The normal engine now exercises the prepared policy;
`gpu-sync-experiment` with `SF_GPU_PBR_VARIANTS=1` remains an equivalent diagnostic
entry point. It shows a car,
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


`pbr-variants-lifecycle.py --output /tmp/pbr-variant-lifecycle.log` runs a bounded
native restore/removal, unknown environment/new-asset fallback, invalid reload
and successful reload check. It requires two preparation generations, unchanged
compile time after the last preparation, nonzero fallback, clean GPU status and
zero grouped charges on shutdown. Use the same EGL/library environment and an
active headset; it enables no interaction and stops automatically.

## Spotlight shadows

Build `spot-shadow-regression` explicitly after reconfiguring CMake, then run it
from the repo root with `DISPLAY=:0 EGL_PLATFORM=x11` and the native SteamVR
library path. It uses real GLES rendering and API fixtures without creating
OpenVR overlays. `--benchmark` adds bounded one/four-car comparisons at
256/512/1024 map pixels and 768 pixels per eye. Keep the headset awake and worn
for performance measurements; no pointer input is captured.

`./run-pocketspace.sh scripts/shadows.js` is interactive: grab the blue light
handle or yellow box, toggle shadows with A/X, cycle resolution with bumper,
and capture/restore with B/Y. Check ground contact, head motion, moving light and
caster, and the status/time HUD. Stop the demo after visual verification.

## Directional shadows

Run `spot-shadow-regression --directional` in the same native GLES environment
as the spotlight checks. `--directional-benchmark` adds bounded paired one/four-car
comparisons at 256/512/1024 map pixels, using a fixed 2-meter square and 4-meter
depth. Runs are offscreen and capture no input. Keep the headset awake and worn
for timing. The shared API suite includes `directional-shadow-api-smoke.js`.

Run `./run-pocketspace.sh scripts/directional-shadows.js` for interaction. Move
the yellow caster and rotate the blue handle; its translation does not move
world-space coverage. A/X toggles shadows, bumper cycles resolution, B/Y saves
then restores. Check stable head motion, parallel shadows and active HUD status;
stop after user verification.

## Point shadows

Run `spot-shadow-regression --point` with the native GLES environment.
`--point-benchmark` uses bounded paired one-car stages at 256/512 pixels per
face with no overlays or pointer input. Keep the headset active for profiling.
Run `./run-pocketspace.sh scripts/point-shadows.js` for interaction; grab the lamp
and yellow caster, use bumper for resolution, A/X for shadows and B/Y to save
then restore. Inspect floor/cube self-shadowing and transitions across face edges.
Stop the demo after visual verification.
