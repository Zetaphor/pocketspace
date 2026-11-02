# Test index

Brief descriptions of the checks, scenes, experiments, and supporting tools in this
folder. Paths are relative to `tests/`; run commands from the repository root.
See [experiment instructions](README.md) for rendering comparisons and native
runtime setup. API smoke files are fixtures for regression harnesses, rather
than independent test runners. Headset scenes require the engine and VR runtime;
performance experiments are separate from correctness and visual acceptance.

## Automated regression checks

| File | Purpose |
| --- | --- |
| [morph-regression.cpp](morph-regression.cpp) | Morph defaults, interpolation, skinning order, bounds, picking, independent poses and JavaScript playback. |
| [morph-loader-regression.py](morph-loader-regression.py) | Generate valid and malformed GLB fixtures and run morph-regression. |
| [morph-api-smoke.js](morph-api-smoke.js) | Morph clip playback and stop/reset through real bindings. |
| [animation-blend-regression.cpp](animation-blend-regression.cpp) | TRS/morph blending, live clocks, interruptions, animated picking, pause/endpoints and v5 persistence. |
| [animation-blend-api-smoke.js](animation-blend-api-smoke.js) | Actual animation transition options and malformed save fixtures for the blend harness. |
| [animation-profile-regression.cpp](animation-profile-regression.cpp) | Real GLES animation phase counters, upload byte totals, clean-pose skip and stable sampling windows. |
| [animation-profile-scene.cjs](animation-profile-scene.cjs) | Automated animated/paused stage counts, completion cleanup and abort behavior. |
| [animation-profile-analysis-test.py](animation-profile-analysis-test.py) | Warmup filtering, weighted summaries and incomplete/mixed run rejection. |
| [gpu-skinning-regression.cpp](gpu-skinning-regression.cpp) | CesiumMan, Fox (all clips), RiggedFigure, RiggedSimple and generated multi-part GPU vertex/pixel/picking checks; bounds, resource cleanup, transitions, fallback, and 30 mixed-scene reload/save/restore cycles. |
| [animation-math.cpp](animation-math.cpp) | Animation interpolation, keyframe sampling, and playback math. |
| [animation-regression.cpp](animation-regression.cpp) | Real model loading, clip playback, skin deformation, animated picking, API bindings, and cleanup. |
| [examples-regression.cpp](examples-regression.cpp) | Published examples and rendering test scenes through real bindings without VR, including snapshot remapping, ownership transfer, panel/body recreation and animation seeking. |
| [eye-calibration-math.cpp](eye-calibration-math.cpp) | Gaze correction math, head-relative transforms, and calibration bounds. |
| [eye-calibration-test.cjs](eye-calibration-test.cjs) | Calibration generator behavior with simulated gaze, jitter, invalid samples, and validation failures. |
| [gaze-accuracy-test.cjs](gaze-accuracy-test.cjs) | Gaze measurement scene behavior using a simulated clock and tracking input. |
| [gaze-analysis-test.py](gaze-analysis-test.py) | Offline gaze analyzer fitting and quality filtering against a known affine mapping. |
| [grab-buttons-regression.py](grab-buttons-regression.py) | Native grab state machine for grip, trigger, and either-button selection without VR or graphics. |
| [lifecycle-input-regression.cpp](lifecycle-input-regression.cpp) | Raw input device replacement, missing-state retry, idle streams, connection upgrades, reconnects, queues and resource cleanup using a local server. |
| [lifecycle-script-regression.cpp](lifecycle-script-regression.cpp) | Script loading, failed-reload rollback, callbacks, and scene cleanup without VR or graphics. |
| [multi-script-lifecycle-regression.cpp](multi-script-lifecycle-regression.cpp) | Two QuickJS hosts with colliding local IDs, independent callback ownership, duplicate rejection, failed reload rollback, scoped stop, and callback-error isolation without VR or graphics. |
| [panel-submission-regression.cpp](panel-submission-regression.cpp) | Cached panel writes and grouped surface handoffs, including injected presentation failures and fallback cleanup. |
| [physics-api-regression.cpp](physics-api-regression.cpp) | Physics JavaScript fixtures against real bindings, with native simulation and lifecycle checks. |
| [physics-collision-regression.cpp](physics-collision-regression.cpp) | Collision events and contact lifecycle, including interaction with input handling. |
| [physics-joint-regression.cpp](physics-joint-regression.cpp) | Native joint constraints, springs, motors, breakage, updates, and cleanup. |
| [physics-performance-regression.cpp](physics-performance-regression.cpp) | Physics optimization correctness, including worker policy and collision processing. |
| [physics-regression.cpp](physics-regression.cpp) | Rigid-body simulation, collider configuration, queries, motion, and validation. |
| [picking-regression.py](picking-regression.py) | Actual mesh picker checked against known geometry and a world-space reference without a headset. |
| [projected-bounds-regression.cpp](projected-bounds-regression.cpp) | Cached scene projection bounds compared with the original calculation. |
| [render-group-split-regression.cpp](render-group-split-regression.cpp) | Subdivision, merging, rebalancing, allocation budgets, planner stability, and API validation. |
| [render-occlusion-regression.cpp](render-occlusion-regression.cpp) | Actual GLES pixels for occlusion, materials, stereo views, grouping, and optimized mesh upload; no OpenVR session. |
| [runtime-stats-regression.cpp](runtime-stats-regression.cpp) | CPU stat parsing, sampling cadence, FPS, CPU usage, and resident memory. |
| [scene-lookup-regression.cpp](scene-lookup-regression.cpp) | Object lookup with missing IDs, erasures, vector growth, and scene replacement. |
| [skinning-loader-regression.py](skinning-loader-regression.py) | Generated valid and malformed skin GLBs passed to the real animation/loader regression. |
| [skinning-math.cpp](skinning-math.cpp) | Joint transforms, inverse binds, hierarchy, and weighted skinning math. |
| [tracking-regression.cpp](tracking-regression.cpp) | Tracking transforms, invalid/stale data, gaze policy, event queues, and disconnect handling. |

## Multi-script fixtures

| File | Purpose |
| --- | --- |
| [scenes/multi-script-left.js](scenes/multi-script-left.js), [scenes/multi-script-right.js](scenes/multi-script-right.js) | Paired noninteractive scenes with the same top-level names and local object/panel IDs for concurrent context and overlay checks. |
| [scenes/multi-script-interaction-near.js](scenes/multi-script-interaction-near.js), [scenes/multi-script-interaction-far.js](scenes/multi-script-interaction-far.js) | Paired interactive headset scenes with overlapping grabbable boxes and panels; log the owning script for clicks and grabs. |
| [scenes/multi-script-interaction-priority-far.js](scenes/multi-script-interaction-priority-far.js) | Far scene variant with model-ray priority `5` for the headset override check. |

## JavaScript API smoke fixtures

| File | Purpose |
| --- | --- |
| [animation-api-smoke.js](animation-api-smoke.js) | Animation enumeration, playback, seeking, and option validation. |
| [breakable-joint-api-smoke.js](breakable-joint-api-smoke.js) | Joint break thresholds, state, and API validation. |
| [collision-api-smoke.js](collision-api-smoke.js) | Collision event API and filtering options. |
| [convex-api-smoke.js](convex-api-smoke.js) | Convex hull collider creation, caching, state, and validation. |
| [custom-collider-api-smoke.js](custom-collider-api-smoke.js) | Explicit collider dimensions and offsets. |
| [engine-stats-api-smoke.js](engine-stats-api-smoke.js) | Statistics field availability before the first runtime sample. |
| [eye-calibration-smoke.js](eye-calibration-smoke.js) | Calibration file validation, save/load round-trip, and activation. |
| [grab-buttons-api-smoke.js](grab-buttons-api-smoke.js) | Accepted grab buttons and rejection of invalid options. |
| [instancing-api-smoke.js](instancing-api-smoke.js) | Default-on model instancing and explicit opt-out, strict boolean validation, getter failures, cache inheritance, and conflicts. |
| [joint-api-smoke.js](joint-api-smoke.js) | Joint creation, state, removal, and connected-body restrictions. |
| [joint-update-api-smoke.js](joint-update-api-smoke.js) | Updating existing joints, partial options, and clearing optional settings. |
| [mesh-collider-api-smoke.js](mesh-collider-api-smoke.js) | Static triangle mesh colliders, scaling, state, and validation. |
| [motor-api-smoke.js](motor-api-smoke.js) | Joint motor modes, targets, limits, and validation. |
| [physics-api-smoke.js](physics-api-smoke.js) | Body setup, gravity, velocity, forces, state, and invalid calls. |
| [spring-api-smoke.js](spring-api-smoke.js) | Distance-joint spring settings, defaults, removal, and validation. |

## Native runners and performance experiments

| File | Purpose |
| --- | --- |
| [compare-auto-split.py](compare-auto-split.py) | Bounded comparison of subdivision disabled/enabled on the same two-island scene. |
| [compare-gpu-sync.py](compare-gpu-sync.py) | Bounded glFinish versus CPU-waited fence comparison using the instrumented engine. |
| [compare-overlays.py](compare-overlays.py) | Single versus grouped rendering with matched box/model layouts and telemetry. |
| [compare-opaque-shaders.py](compare-opaque-shaders.py) | Matched general versus specialized opaque/depth shader timing on moving models. |
| [compare-primitive-visibility.py](compare-primitive-visibility.py) | Matched object-only versus primitive occluder rejection, with skipped-work and preparation-cost counters. |
| [compare-cluster-visibility.py](compare-cluster-visibility.py) | Compares original, full spatial-cluster, and culled spatial-cluster occluder draws with CPU/GPU and memory counters. |
| [compare-instancing.py](compare-instancing.py) | Matched draw submission, GPU timing, FPS, and saved-draw comparisons for ordered model instancing. |
| [scene-container-api-smoke.js](scene-container-api-smoke.js), [scene-container-regression.cpp](scene-container-regression.cpp) | Ownership, strict IDs/getters, transfers, physics/joints, self-disposing callbacks, rollback, shared surfaces and repeated cleanup. |
| [multi-script-render-regression.cpp](multi-script-render-regression.cpp) | Two offscreen scenes with overlapping local object IDs, distinct pixels, and independent GPU target cleanup across three cycles. |
| [input-arbitration-regression.cpp](input-arbitration-regression.cpp) | Priority, distance, stable instance-ID ties, order independence, and invalid-distance rejection for shared controller targets. |
| [engine_launch.py](engine_launch.py) | Catalog-only launch arguments shared by device test runners. |
| [container-lifecycle.py](container-lifecycle.py), [scenes/container-lifecycle.js](scenes/container-lifecycle.js) | Production container create/dispose cycles with a root survivor, shared surface, panels and physics. |
| [lod-api-smoke.js](lod-api-smoke.js) | Default-on LOD, explicit opt-out, strict booleans, getter failures and cached-path conflicts. |
| [auto-lod-regression.cpp](auto-lod-regression.cpp) | Production LOD selection/hysteresis, exact reduced-index reference pixels, mixed-level instancing and buffer cleanup. |
| [compare-auto-lod.py](compare-auto-lod.py) | Production default-on versus opt-out FPS, with grouped storage matched plus accounted LOD buffers. |
| [lod-pixels.cpp](lod-pixels.cpp), [lod-experiment.h](lod-experiment.h) | Test-only bounded-error GPU simplification, stereo image metrics, unchanged CPU meshes, fallback and cleanup. |
| [compare-lod.py](compare-lod.py) | Matched original versus simplified-model FPS/GPU timing with identical allocation capacities. |
| [multipart-instancing-pixels.cpp](multipart-instancing-pixels.cpp), [multipart-instancing-experiment.h](multipart-instancing-experiment.h) | Test-only combined multi-material geometry, exact stereo/order/material comparisons, singleton batching, and cleanup. |
| [instancing-lifecycle.py](instancing-lifecycle.py) | Production reloads between instanced and empty scenes, requiring zero owned GPU bytes in empty scenes. |
| [compare-regroup.py](compare-regroup.py) | Split-only versus merge/rebalance while the same objects move between layouts. |
| [compare-vertex-cache.py](compare-vertex-cache.py) | Alternating mesh cache optimization off/on with matched instrumentation and allocation checks. |
| [fast-motion.py](fast-motion.py) | Bounded continuously moving box/model workloads with throughput and error checks. |
| [profile-render-passes.py](profile-render-passes.py) | Pairs whole-render GPU timing with sampled occluder, opaque, and blended draw attribution on moving models, including zero/four/eight-light PBR workloads. |
| [lifecycle-runtime.py](lifecycle-runtime.py) | Runtime script reload, rollback, callbacks, object removal, overlay inventory, and shutdown. |
| [regroup-lifecycle.py](regroup-lifecycle.py) | Runtime grouping changes, visibility, removal, and scene reload lifecycle. |
| [soak-motion.py](soak-motion.py) | Repeated moving-model scene reloads with bounded duration, object counts, errors, and buffer accounting. |
| [physics-benchmark.cpp](physics-benchmark.cpp) | Physics throughput across body counts, worker settings, and continuous collision modes. |
| [render-stress-benchmark.cpp](render-stress-benchmark.cpp) | Fixed-view rendering workload for GPU/CPU cost measurement. |
| [gpu-sync-pixels.cpp](gpu-sync-pixels.cpp) | Exact finished-pixel comparison between glFinish and CPU-waited fences. |
| [opaque-shader-pixels.cpp](opaque-shader-pixels.cpp) | Exact pixel comparisons for opaque color/depth specialization, including masked and blended fallback. |
| [opaque-shader-experiment.h](opaque-shader-experiment.h) | Test-only specialized shader programs and uniform setup; excluded from production builds. |
| [primitive-visibility-pixels.cpp](primitive-visibility-pixels.cpp) | Exact occlusion pixels, actual rejection, near-plane/invalid-data fallback, and deforming-asset fallback checks. |
| [primitive-visibility-experiment.h](primitive-visibility-experiment.h) | Test-only static primitive bounds cache and conservative nonmember occluder rejection. |
| [cluster-visibility-pixels.cpp](cluster-visibility-pixels.cpp) | Exact full/culled cluster pixels, within-primitive rejection, near-plane views, material modes, and deforming fallback. |
| [cluster-visibility-experiment.h](cluster-visibility-experiment.h) | Test-only spatial triangle partitioning, verified occluder index buffers, conservative bounds, and merged draw ranges. |
| [instancing-pixels.cpp](instancing-pixels.cpp) | Exact instanced pixels, observable coplanar order, rotated anchors, material/deformation fallback, index formats, capacity, and GL cleanup. |
| [gpu-sync-experiment.h](gpu-sync-experiment.h) | Test-only GPU timing and completion-wait instrumentation used by the excluded gpu-sync-experiment target. |
| [shared-depth-prototype.cpp](shared-depth-prototype.cpp) | Offscreen feasibility and timing experiment for sharing a scene depth prepass; not an adopted renderer. |
| [memory-sampler.c](memory-sampler.c) | Test-only glibc heap and process RSS sampler preloaded into a native run. |

## Headset scenes and runner fixtures

| File | Purpose |
| --- | --- |
| [scenes/animation-test.js](scenes/animation-test.js) | Interactive clip selection and playback on a grabbable animated model. |
| [scenes/auto-split.js](scenes/auto-split.js) | Boxes/models moved between compact and wide layouts to inspect tile split/merge/rebalance behavior. |
| [scenes/fast-motion.js](scenes/fast-motion.js) | Deterministic kinematic movers crossing tile boundaries; shared by bounded motion runners. |
| [scenes/gaze-accuracy-test.js](scenes/gaze-accuracy-test.js) | Raw gaze accuracy measurements over a target grid without modifying calibration. |
| [scenes/grab-buttons-test.js](scenes/grab-buttons-test.js) | Side-by-side trigger-only, grip-only, and either-button grabbing. |
| [scenes/input-api-smoke.js](scenes/input-api-smoke.js) | Controller input shape and values reported from the live runtime. |
| [scenes/input-interaction-smoke.js](scenes/input-interaction-smoke.js) | Model interaction and input-event callback smoke check. |
| [scenes/lifecycle-test.js](scenes/lifecycle-test.js) | Held-object removal, controller-loss follow-up, respawning, and panel churn. |
| [scenes/model-selection-test.js](scenes/model-selection-test.js) | Picking through hollow geometry and checking mesh-aware selection cases. |
| [scenes/overlay-comparison.js](scenes/overlay-comparison.js) | Interactive single/grouped renderer, geometry, and object-count comparison with a statistics HUD. |
| [scenes/panel-api-smoke.js](scenes/panel-api-smoke.js) | Basic panel creation, transform, drawing aliases, and presentation. |
| [scenes/panel-smoke.js](scenes/panel-smoke.js) | Live controller values drawn on a panel. |
| [scenes/panel-test-suite.js](scenes/panel-test-suite.js) | Panel transforms, drawing, interaction, and model occlusion checks in the headset. |
| [scenes/performance-multi-model-test.js](scenes/performance-multi-model-test.js) | Four ToyCar instances sharing one asset and picking index. |
| [scenes/performance-test.js](scenes/performance-test.js) | Single grabbable ToyCar at a fixed eye resolution. |
| [scenes/skinning-test.js](scenes/skinning-test.js) | Two independently animated instances sharing one rigged asset. |
| [scenes/tracking-smoke.js](scenes/tracking-smoke.js) | Live gaze/foveation/finger API validation without panels or input capture. |
| [scenes/tracking-test.js](scenes/tracking-test.js) | Visual gaze and finger tracking diagnostics with validity reporting. |

## Diagnostic helpers and fixtures

| File | Purpose |
| --- | --- |
| [tools/analyze-gaze-accuracy.py](tools/analyze-gaze-accuracy.py) | Summarizes gaze grids and evaluates affine correction on separate validation grids. |
| [tools/generate-selection-fixture.py](tools/generate-selection-fixture.py) | Generates the hollow-frame GLB used by mesh selection checks. |
| [tools/lifecycle-probe.cpp](tools/lifecycle-probe.cpp) | Read-only OpenVR overlay inventory for lifecycle integration checks. |
| [tools/panel-ray-check.cpp](tools/panel-ray-check.cpp) | Compares panel ray math with actual runtime overlay transforms and intersections. |
| [tools/steam-frame-input.py](tools/steam-frame-input.py) | Raw Steam Frame controller input monitor through the local vrserver WebSocket. |
| [tools/tracking-probe.cpp](tools/tracking-probe.cpp) | Native OpenVR tracking capability and data probe. |
| [fixtures/tracking-probe/actions.json](fixtures/tracking-probe/actions.json) | Action manifest fixture for the tracking probe. |
| [fixtures/tracking-probe/frame_controller.json](fixtures/tracking-probe/frame_controller.json) | Controller binding fixture for the tracking probe. |
| [fixtures/tracking-probe/frame_hmd.json](fixtures/tracking-probe/frame_hmd.json) | Headset binding fixture for the tracking probe. |

## Recorded results

| File | Purpose |
| --- | --- |
| [results/auto-split.md](results/auto-split.md) | Subdivision measurements and validation limits. |
| [results/fast-motion.md](results/fast-motion.md) | Continuous-motion validation, buffer limits, and headset findings. |
| [results/gpu-sync.md](results/gpu-sync.md) | Completion synchronization contract review and measured comparisons. |
| [results/memory-reload.md](results/memory-reload.md) | Reload RSS growth attributed mainly to reusable free allocator space. |
| [results/motion-soak.md](results/motion-soak.md) | Five-minute motion/reload stability check. |
| [results/overlay-comparison.md](results/overlay-comparison.md) | Single/grouped rendering measurements and resolution caveats. |
| [results/opaque-shaders.md](results/opaque-shaders.md) | Pixel validation and native timing of shader specialization; not adopted as the default. |
| [results/primitive-visibility.md](results/primitive-visibility.md) | Primitive rejection correctness and throughput evidence; not adopted as the default. |
| [results/cluster-visibility.md](results/cluster-visibility.md) | Spatial-cluster correctness, matched three-mode timing, and rejected adaptive measurements; not adopted. |
| [results/scene-containers.md](results/scene-containers.md) | Ownership/rollback regression evidence and live production create/dispose resource cycles. |
| [results/auto-lod.md](results/auto-lod.md) | Default automatic LOD contract, production comparisons, API/rendering and reload validation. |
| [results/lod.md](results/lod.md) | Bounded-error model detail reduction, image comparisons, native timing and adoption requirements. |
| [results/multipart-instancing.md](results/multipart-instancing.md) | Exact multi-material batching validation and matched native results; not adopted. |
| [results/instancing.md](results/instancing.md) | Ordered instancing correctness, matched performance, production measurements, and reload validation. |
| [results/regroup.md](results/regroup.md) | Tile merging/rebalancing measurements and lifecycle validation. |
| [results/render-passes.md](results/render-passes.md) | GPU draw-pass attribution for dense moving models and instrumentation comparisons. |
| [results/shared-depth-prototype.md](results/shared-depth-prototype.md) | Shared-depth prototype findings and requirements for any future adoption. |
| [results/vertex-cache.md](results/vertex-cache.md) | Mesh cache/fetch optimization validation and matched performance results. |

The JSON, CSV, and TXT files in [results/](results/) are raw measurements or
validation output associated with those reports, rather than additional tests.

## Scene persistence

| File | Purpose |
| --- | --- |
| [scene-persistence-regression.cpp](scene-persistence-regression.cpp), [physics-persistence-api-smoke.js](physics-persistence-api-smoke.js), [animation-persistence-api-smoke.js](animation-persistence-api-smoke.js), [panel-persistence-api-smoke.js](panel-persistence-api-smoke.js) | JSON/disk roundtrips, v1–v3 compatibility, lossless panel pixels/layout/handles, independent skin/rigid poses and playback states, transform/model/body/joint/motor state, construction frames, ID/group remapping, held-body restore, boundary rejection, native rollback, cleanup and bounded atomic files. |
| [scene-persistence.py](scene-persistence.py), [scenes/scene-persistence.js](scenes/scene-persistence.js), [scenes/physics-persistence.js](scenes/physics-persistence.js), [scenes/animation-persistence.js](scenes/animation-persistence.js), [scenes/panel-persistence.js](scenes/panel-persistence.js) | Live production model/panel/animation/physics save/restore/dispose cycles, root object/HUD survival, motor/spring connections, resource counts, GL errors and grouped-buffer release using temporary storage. |
| [results/scene-persistence.md](results/scene-persistence.md) | Native roundtrip/rollback, live Frame save/restore resource evidence, and accepted headset check. |
| [results/physics-persistence.md](results/physics-persistence.md) | V2 body/joint/motor roundtrips, construction frame preservation, native rollback, v1 compatibility, live simulation cleanup and accepted headset check. |
| [results/animation-persistence.md](results/animation-persistence.md) | V3 skin/rigid playback and independent-pose roundtrips, legacy compatibility, rollback, live resource cleanup and accepted headset check. |
| [results/panel-persistence.md](results/panel-persistence.md) | V4 lossless pixels/layout/handles, raw/RLE bounds, pointer reconnection, native rollback, legacy compatibility, live mixed-scene cleanup and accepted headset check. |

## Animated-model profiling

[scenes/animation-profile.js](scenes/animation-profile.js) runs bounded animated/paused character stages with a HUD. The excluded `animation-profile-experiment` target adds phase timers to the real engine. [analyze-animation-profile.py](analyze-animation-profile.py) summarizes steady windows; see [the profiling instructions](README.md#animated-model-performance-profiling).

The [Frame animation profile](results/animation-profile.md) records the completed skeletal workload, steady phase timings, and GPU design constraints.

[scenes/gpu-skinning-profile.js](scenes/gpu-skinning-profile.js) is the bounded, matched 32-actor CPU/GPU skinning workload. The native GPU regression also supports deterministic two-hand candidate-cost profiling with `--profile-picking`. See [GPU skinning prototype instructions](README.md#gpu-skinning-prototype).

[scenes/gpu-skinning-interaction.js](scenes/gpu-skinning-interaction.js) checks
32 animated models with both-hand grip/trigger grabs and pause/resume in the
GPU experiment. B/Y finishes; actors also clear after five minutes.

[gpu-skinning-mixed-api.js](gpu-skinning-mixed-api.js) is the mixed backend
fixture invoked by the GPU regression, including failed reload rollback.

[scenes/gpu-skinning-production.js](scenes/gpu-skinning-production.js) is the
normal-engine mixed GPU/CPU acceptance scene: grab, A/X pause/resume, bumper
save/restore, B/Y finish, five-minute cleanup.

[scenes/rigid-morph-profile.js](scenes/rigid-morph-profile.js) profiles 64/256
rigid-node and morph actors, paired animated/paused, with the excluded phase
profiling executable. Eight 20-second stages; B/Y aborts.

[scenes/rigid-pose-profile.js](scenes/rigid-pose-profile.js) compares full versus
cached rigid primitive updates using 256 Linear Scale actors; 20 seconds each
animated/paused. `animation-profile-regression` also checks exact cached GPU
buffer contents and geometry over all rigid clips.

| Additional check | Coverage |
| --- | --- |
| [draw-uniform-pixels.cpp](draw-uniform-pixels.cpp) | 288 exact stereo comparisons for pass-local object/material uniform caching, alpha/depth, CPU/GPU animation and instancing switches. |

`draw-submit-experiment` measures full/cached uniforms on
[scenes/rigid-pose-profile.js](scenes/rigid-pose-profile.js) using the documented
`SF_DRAW_UNIFORM_CACHE` switch.

[vertex-array-pixels.cpp](vertex-array-pixels.cpp) compares 864 exact stereo views
for cached vertex/index attribute setup, CPU/GPU animation, LOD and instancing,
with VAO ownership/deletion checks. `vertex-array-experiment` profiles the same
[256-actor scene](scenes/rigid-pose-profile.js) using `SF_VERTEX_ARRAY_CACHE=0/1`.

[rigid-batch-pixels.cpp](rigid-batch-pixels.cpp) compares 864 exact stereo views
for adjacent same-material CPU rigid parts, material barriers, changed stream
ranges, clip changes and buffer lifetime. `rigid-batch-experiment` profiles the
[256-actor scene](scenes/rigid-pose-profile.js) with `SF_RIGID_BATCH=0/1`.

[static-vertex-pixels.cpp](static-vertex-pixels.cpp) checks 216 exact stereo views
for the test-only ordinary-draw vertex specialization across detailed/static
models, opaque/masked/blended materials, and external occlusion.
[compare-static-vertex.py](compare-static-vertex.py) pairs ordinary/static-only
vertex programs with production automatic LOD and ordered instancing enabled;
it rejects unequal storage and invalid GPU samples. The switch exists only in
`gpu-sync-experiment`; skinned assets are rejected in that mode.

The [current dense-model report](results/dense-model-current.md) records refreshed
pass attribution with production LOD/instancing, rejected allocation-mismatched
rows, and bounded static-vertex candidate comparisons.

[ordinary-program-regression.cpp](ordinary-program-regression.cpp) checks exact
mixed ordinary/GPU shader routing, CPU skin/rigid/morph fallbacks, LOD/instancing,
restored scenes, rollback and shader ownership. The
[integration report](results/ordinary-program-routing.md) records the completed
comparison without production adoption. The optional
[ordinary-program-mixed.js](scenes/ordinary-program-mixed.js) scene exercises
interactive mixed routing only when enabled in the excluded executable.

[group-occluder-regression.cpp](group-occluder-regression.cpp) checks equal-area
split tie-breaking against conservative stereo occluder work across 256 layouts.
[compare-group-policy.py](compare-group-policy.py) compares policies using
allocation, triangle, member-draw and LOD guards.
[render-work-analysis-test.py](render-work-analysis-test.py) validates rejection
of malformed, drifting or inequivalent work samples. The
[group policy report](results/group-occluder-policy.md) records the investigation.

[validate-render-work.py](validate-render-work.py) checks analytic instanced,
GPU-skinned and merged-rigid submission totals through the native renderer using
the bounded non-interactive
[render-work-validation.js](scenes/render-work-validation.js) scene.

## Model lighting

- [pbr-index-production-regression.cpp](pbr-index-production-regression.cpp):
  nineteen-model production opt-in full/reduced indexing, a generated coplanar
  ownership counterexample, broader material timing, automatic LOD and reconstructed
  reduced shader inputs, exact stereo/color/depth and
  animated fallback checks, partial allocation rollback, shared persistence,
  rejected reload and cleanup; matched color/depth timing at each LOD.
- [scenes/pbr-index.js](scenes/pbr-index.js): normal-engine cache-ordered PBR car,
  normal-map and alpha fixtures against a blue backdrop for grabbing and LOD.

- [pbr-lod-regression.cpp](pbr-lod-regression.cpp): production surface-aware PBR
  LOD, exact stereo/depth routing, image differences, 32 MiB budget exhaustion,
  partial upload rollback, reload/restore preservation, ownership cleanup and
  optional native ABBA timing.
- [scenes/pbr-lod.js](scenes/pbr-lod.js): interactive PBR car and normal-map
  fixture for automatic detail transitions and full-detail restoration on grab;
  runs in the normal engine; `pbr-lod-demo` additionally logs transitions.
- [pbr-index-regression.cpp](pbr-index-regression.cpp): test-only static PBR
  indexing and explicitly enabled vertex-cache ordering; full-input seam identity,
  verified reordered opaque/mask and order-preserving blend streams, 16/32-bit indices,
  exact stereo/alpha/overlap/depth pixels, animation fallback, buffer cleanup and an
  optional native ABBA comparison.
- [scenes/light-filtering.js](scenes/light-filtering.js): interactive combined
  acceptance of range/cone filtering with four active point/spot lights, movable
  car/normal-map targets and grabbable light markers.
- [spotlight-cone-regression.cpp](spotlight-cone-regression.cpp): conservative
  sphere/cone tests, sampled lit-point rejection checks, stereo/material and
  attachment pixels, plus matched range-only versus cone-filter GPU timings.
  See [cone-selection results](results/spotlight-cone-selection.md).
- [light-work-regression.cpp](light-work-regression.cpp): all-light versus per-object
  range selection, stereo/material pixels and optional matched ABBA GPU timing
  for broad/local point, spot and mixed lights; retains the rejected shader-work
  comparison in a diagnostic mode. See [light-work results](results/light-work.md).
- [directional-light-regression.cpp](directional-light-regression.cpp): scene-wide
  directional illumination, ray normalization, rotation-only attachment behavior,
  no-distance filtering, legacy point/spot pixel reference, mixed/prepared PBR,
  typed API rollback, shared capacity, cleanup and v10/v9 persistence.
  `--profile` records old/new point/spot and directional GPU costs;
  `--profile --reverse` reverses shader order. See
  [measurements and acceptance](results/directional-lights.md).
- [spotlight-regression.cpp](spotlight-regression.cpp): real stereo cone pixels,
  hard edges, direction/attachment rotation, point-light equivalence, unlit/depth
  invariance, type-safe APIs, mixed capacity, getter rollback and v9/v8 persistence.
  See [spotlight measurements and acceptance](results/spotlights.md).
- [point-light-regression.cpp](point-light-regression.cpp): real GLES point-light
  stereo pixels, intensity/color/range, unlit/depth invariance, attachment transforms,
  API rollback, cleanup and v8/legacy snapshots. `--profile` measures bounded
  zero/one/four/eight-light offscreen workloads. See the
  [point-light results](results/point-lights.md).
- [pbr-regression.cpp](pbr-regression.cpp): real GLES tests for opt-in material
  import/rendering, sRGB transfer, linear packed metal/roughness channels, surface
  transforms, static/skeletal/morph poses, unlit invariance, GL cleanup, API
  validation and snapshot compatibility. Does not open an OpenVR session.
- [scenes/pbr-materials.js](scenes/pbr-materials.js): non-interactive headset
  gallery for generated/supplied tangents, mirrored nodes, color encoding, unlit
  materials and emissive strength. The large sphere grids remain offscreen.
- [PBR fixture inventory](fixtures/pbr/khronos-models.json): pinned upstream Git
  blobs and SHA-256 hashes for the eight Khronos material diagnostic models.

- [culling-regression.cpp](culling-regression.cpp): front/back visibility for
  single-/double-sided, mirrored, opaque/masked/blended, PBR/unlit and instanced
  meshes; GL state cleanup. Optional `--benchmark` measures matched full-detail
  box/car workloads offscreen with CPU submission and GPU elapsed times.
- [Bounded surface fixtures](fixtures/pbr/generate.py): original reproducible
  GLBs for missing normals, mirrored geometry, morph normal/tangent data,
  unsupported texture-coordinate selection and zero normal-map UV scale.

- [PBR and sidedness validation](results/culling.md): automated coverage, pending
  headset visual acceptance, and the native culling performance comparison.

## Environment lighting

- [environment-convert-test.py](environment-convert-test.py): real HDR/EXR and
  sRGB PNG conversion, HDR range preservation, invalid encoding rejection and
  an end-to-end custom `.psenv` bake. Requires the offline tool dependencies.
- [environment-bake-test.py](environment-bake-test.py): constant-radiance and
  diffuse SH normalization, cube orientation, BRDF energy, equirectangular
  sampling, GGX broadening, `.psenv` CLI output and obsolete filename rejection
  for the offline baker.
- [environment-regression.cpp](environment-regression.cpp): actual GLES metal
  illumination, rotation/intensity, unlit invariance, generated/imported tangent
  frame and pixel equivalence, Khronos stereo views,
  malformed and obsolete-header rejection, shared texture cleanup, transactional setter/restore
  failures and v6/v7 snapshot compatibility. Optional `--benchmark` compares IBL
  off/on with one and sixteen full-detail PBR cars, without overlays or input.

- [Environment validation and cost](results/environment-lighting.md): corrected
  generated tangent orientation, automated coverage and native IBL timing.

## PBR depth optimization

- [pbr-minimal-depth-regression.cpp](pbr-minimal-depth-regression.cpp): test-only
  minimal vertex/fragment depth program at 768 and 2048 pixels per eye,
  compared byte-for-byte with the shipped
  fast-depth shader; stereo, masks, overlaps, LOD, animated poses, GL state and
  cleanup. Build `pbr-minimal-depth-regression` explicitly; run from the repo
  root with `DISPLAY=:0 EGL_PLATFORM=x11` and the SteamVR library path. `--benchmark` adds
  matched depth-only and unchanged color controls; `--benchmark --reverse`
  reverses the ABBA program order. The excluded `gpu-sync-experiment` accepts
  `SF_PBR_MINIMAL_DEPTH=1` for engine integration; normal builds ignore it.

- [profile-pbr-submission.py](profile-pbr-submission.py): fixed-view PBR
  submission attribution with probes off/on/on/off, preserving exact work and
  storage. The excluded engine's `SF_GPU_PROFILE_PBR_SUBMIT=1` samples PBR
  render-thread CPU once per second; `enqueueThreadCpuMs` distinguishes CPU
  execution from enqueue wall time. See the
  [submission cost report](results/pbr-submission.md) for scope and the separate
  128-Avocado check. This measures existing draws, not an instancing prototype.
- [profile-minimal-depth.py](profile-minimal-depth.py): bounded actual-engine
  ABBA comparison with a canonical camera, identical static geometry, eight
  lights and exact external/member work plus storage validation. Uses
  [scenes/pbr-depth-controlled.js](scenes/pbr-depth-controlled.js); includes a
  single-group color-only control. Run with the native EGL/SteamVR environment:
  `python3 tests/profile-minimal-depth.py --output /tmp/depth-controlled`.
  `--reverse` reverses program order; `--workload grouped` limits a repeat to the
  external-depth workload. The fixed camera is test-only and is not suitable for
  judging head-motion rendering.
- [pbr-depth-regression.cpp](pbr-depth-regression.cpp): 1,248 exact stereo, alpha,
  overlap, LOD and animated-pose comparisons; GL state/resource cleanup and
  matched depth/color performance workloads.
- [scenes/pbr-depth.js](scenes/pbr-depth.js): independent car and alpha-test
  surfaces against a blue backdrop for interactive external-depth inspection.
- [Recorded comparison](results/pbr-depth.md): native depth-pass timings and scope.

The [dense PBR lighting profile](results/dense-pbr-lights.md) attributes moving-car
rendering cost across zero, four and eight mixed scene lights.

[pbr-cost-profile.cpp](pbr-cost-profile.cpp) isolates PBR viewport/geometry
sensitivity with fixed storage and identical moving poses, zero/eight lights,
and full/reduced geometry. Reduced quality is diagnostic only; see the
[run instructions](README.md#pbr-geometry-and-pixel-cost-diagnostic) and
[measured sensitivity](results/pbr-cost.md).

[pbr-transform-experiment.cpp](pbr-transform-experiment.cpp) compares
GPU rotation reuse against production shader expressions, with strict stereo
pixels before matched ABBA timings. See the
[run instructions](README.md#pbr-prepared-rotation-experiment) and
[rejected-candidate results](results/pbr-transform.md).

[pbr-light-invariant-experiment.cpp](pbr-light-invariant-experiment.cpp) tests
per-fragment visibility/diffuse-factor reuse with stereo pixels across light counts before
matched ABBA measurements. See the
[run instructions](README.md#pbr-invariant-light-arithmetic-experiment) and
[comparison results](results/pbr-light-invariant.md).

[pbr-variants-experiment.cpp](pbr-variants-experiment.cpp) and
[pbr-variants-experiment.h](pbr-variants-experiment.h) test a bounded PBR
material/light-count program cache, strict mixed-state pixels, fallback/cleanup,
and matched throughput with program/compile accounting. See the
[run instructions](README.md#bounded-pbr-shader-variant-experiment) and
[pixel/performance results](results/pbr-variants.md).

[compare-pbr-variants.py](compare-pbr-variants.py) measures the same variant cache
in the real engine, reversing pair order and rejecting geometry/storage failures.

[scenes/pbr-variants.js](scenes/pbr-variants.js) validates specialized/general PBR
routing interactively with grabbing, HDR toggles, zero/four/eight lights, LOD,
normal maps and alpha overlaps. See the
[headset instructions](README.md#interactive-pbr-variant-validation).

[pbr-variants-lifecycle.py](pbr-variants-lifecycle.py) verifies prepared-program
restore, removal, unknown-state fallback, failed/successful reload, no draw-time
compilation and shutdown accounting. See the
[prepared-policy report](results/pbr-prewarm.md).

The [prepared PBR report](results/pbr-prewarm.md) records bounded startup/reload
precompilation, guarded ready-only routing, lifecycle validation and matched
native throughput, superseding the lazy prototype policy.

The [dense PBR pass refresh](results/pbr-pass-refresh.md) records fixed/adaptive
32-car attribution with precompiled shaders enabled, including rejected peak-
storage mismatches and the next depth-only shader investigation.

## Nested containers and model lifetime

- [container-hierarchy-api-smoke.js](container-hierarchy-api-smoke.js): parent
  creation/reparenting, cycle rejection, direct membership, recursive disposal,
  cross-subtree joints/attachments, v11 hierarchy maps, malformed hierarchy
  rollback, legacy flat restore, named saves and child-owned callback cleanup.
- [asset-lifetime-regression.cpp](asset-lifetime-regression.cpp): runs that API
  fixture with real bindings, then verifies GPU buffer/texture deletion, physics
  shape-cache release, hidden/live-user rejection, stable invalid IDs, getter and
  reload rollback, repeated unload/reload and restoration after release. Build
  `asset-lifetime-regression` explicitly; run from the repo root with
  `DISPLAY=:0 EGL_PLATFORM=x11` and the native SteamVR library path.
- [Validation results](results/containers-assets.md): automated resource and
  rollback checks plus user-confirmed nested-container and unload/restore tests.

## Fabric sheen

[sheen-regression.cpp](sheen-regression.cpp) checks Charlie BRDF energy and
reciprocity, factor/texture color-space and alpha-channel equivalence, unsupported
UV rejection, stereo rendering, unlit invariance, environment lighting, prepared
shader equivalence, Khronos Compare Sheen import and shared lookup cleanup.
Build `sheen-regression` explicitly and run from the repository root with the
native GLES environment. No overlays or pointer input are used.
See the [correctness and bounded profile report](results/sheen.md).

## Spotlight shadows

- [spot-shadow-api-smoke.js](spot-shadow-api-smoke.js): atomic option validation,
  getters, one-light capacity, attachments, v12 snapshots, conflicts and legacy restore.
- [spot-shadow-regression.cpp](spot-shadow-regression.cpp): native depth shadows,
  stereo coverage, alpha holes, hidden/BLEND exclusion, cross-group casters,
  prepared shaders, animation, GL-state restoration, resolution/work/time limits
  and resource cleanup. Build `spot-shadow-regression` explicitly. Run from the
  repo root with the native GLES environment; `--benchmark` adds bounded
  one/four-car 256/512/1024 comparisons without overlays or input capture.
- [shadows.js](../scripts/shadows.js): interactive light/object movement, resolution
  and shadow enable controls, status HUD and snapshot replacement.

See the [correctness, performance and headset report](results/spotlight-shadows.md).

## Directional shadows

- [directional-shadow-api-smoke.js](directional-shadow-api-smoke.js): world coverage
  validation, shared spot/directional capacity, getter mutations and v13/legacy restore.
- [spot-shadow-regression.cpp](spot-shadow-regression.cpp): `--directional` runs
  orthographic stereo/alpha/animation/GL/budget checks, fixed coverage under
  attachment translation, rotated basis and out-of-volume behavior.
  `--directional-benchmark` adds bounded one/four-car map comparisons without overlays.
- [directional-shadows.js](../scripts/directional-shadows.js): interactive sunlight
  shadow coverage, caster/light movement, controls and snapshot replacement.

See the [directional shadow validation report](results/directional-shadows.md).

## Point shadows

- [point-shadow-api-smoke.js](point-shadow-api-smoke.js): strict per-face limits,
  shared capacity, transactional getters, v14 snapshots and legacy restore.
- [spot-shadow-regression.cpp](spot-shadow-regression.cpp): `--point` exercises
  six-view matrices, atlas storage, stereo, alpha, animation, work/time limits
  and cleanup. `--point-benchmark` adds a bounded one-car offscreen comparison.
- [point-shadows.js](../scripts/point-shadows.js): interactive lamp, caster,
  resolution/enable controls and snapshot restore.

See the [point-shadow validation report](results/point-shadows.md).
