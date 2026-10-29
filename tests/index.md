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
| [examples-regression.cpp](examples-regression.cpp) | Published examples and rendering test scenes exercised through real script bindings without a VR session. |
| [eye-calibration-math.cpp](eye-calibration-math.cpp) | Gaze correction math, head-relative transforms, and calibration bounds. |
| [eye-calibration-test.cjs](eye-calibration-test.cjs) | Calibration generator behavior with simulated gaze, jitter, invalid samples, and validation failures. |
| [gaze-accuracy-test.cjs](gaze-accuracy-test.cjs) | Gaze measurement scene behavior using a simulated clock and tracking input. |
| [gaze-analysis-test.py](gaze-analysis-test.py) | Offline gaze analyzer fitting and quality filtering against a known affine mapping. |
| [grab-buttons-regression.py](grab-buttons-regression.py) | Native grab state machine for grip, trigger, and either-button selection without VR or graphics. |
| [lifecycle-input-regression.cpp](lifecycle-input-regression.cpp) | Raw input device replacement, missing-state retry, idle streams, connection upgrades, reconnects, queues and resource cleanup using a local server. |
| [lifecycle-script-regression.cpp](lifecycle-script-regression.cpp) | Script loading, failed-reload rollback, callbacks, and scene cleanup without VR or graphics. |
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
| [profile-render-passes.py](profile-render-passes.py) | Pairs whole-render GPU timing with sampled occluder, opaque, and blended draw attribution on moving models. |
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
