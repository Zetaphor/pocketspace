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
