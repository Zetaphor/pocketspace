# Animated-model CPU and upload profiling

Measured on Steam Frame on 2026-10-28 using engine commit `da48f0a`.
The excluded `animation-profile-experiment` target uses the normal engine,
rendering, picking, bounds and upload paths with additional CPU timers. Normal
builds contain no animation profiling instrumentation. This is a measurement,
not a GPU animation implementation or a separate visual acceptance claim.

## Workload and method

- ARM64 Steam Frame; CPU governor `schedutil`, unchanged for this run.
- Renderer: `zink Vulkan 1.4(Turnip Adreno (TM) 750 (MESA_TURNIP))`;
  OpenGL ES 3.2 Mesa `26.3.0-devel (git-abc426bb62)`.
- Bundled CesiumMan: 4,672 triangles / 14,016 expanded vertices per instance;
  19 skin joints and independently staggered animation clocks.
- One logical render group, 768 eye pixels, no physics, one HUD panel. The
  headset was reported ready and the user was asked to keep looking at the models.
- Six 20-second stages: 8 animated, 8 paused, 16 paused, 16 animated,
  32 animated, 32 paused. Paused actors keep their deformed pose and VBOs.
- Each native window covers 120 frames. Discard windows starting in the first
  five seconds of each stage, then weight phase means by sampled frames.
  Five steady windows remain at 32 animated actors; ten at 32 paused actors.
- Timings are CPU elapsed durations. Deformation and per-vertex bounds remain
  fused in the original loop. Upload submission can include driver blocking;
  completion-wait time is not a GPU timestamp or GPU utilization measurement.

## Results

All times are mean milliseconds per engine frame. FPS includes engine pacing;
work time excludes the final pacing sleep.

| Actors | Playback | FPS | Pose | Deform + bounds | BVH refit | Upload submission | Completion wait | Work |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8 | Animated | 89.01 | 0.069 | 2.296 | 0.838 | 0.189 | 3.893 | 7.686 |
| 8 | Paused | 89.99 | 0 | 0 | 0 | 0 | 3.950 | 4.332 |
| 16 | Paused | 89.99 | 0 | 0 | 0 | 0 | 4.262 | 4.702 |
| 16 | Animated | 75.47 | 0.112 | 4.168 | 1.613 | 0.484 | 5.899 | 12.771 |
| 32 | Animated | 51.50 | 0.209 | 7.939 | 3.045 | 0.896 | 6.728 | 19.416 |
| 32 | Paused | 89.82 | 0 | 0 | 0 | 0 | 6.940 | 7.401 |

At 32 animated actors, every frame evaluates 448,512 expanded vertices and
submits 8,970,240 bytes (8.55 MiB) of dynamic geometry. Paused windows confirm
zero animation evaluations and uploads. Deformation/bounds plus BVH refits cost
10.98 ms per frame; total measured animation work including pose and upload
submission is 12.09 ms. A 90 FPS frame budget is approximately 11.11 ms.

The final HUD samples report 82.2 MiB process RSS in both 32-actor stages and
213.8 MiB estimated engine GPU allocations in both stages. Driver memory totals
were unavailable. Allocation estimates also match within each 8- and 16-actor
pair, so the paused comparison did not reduce reported buffer memory or detail.
Estimated memory is not driver VRAM usage, nor proof of identical pixel work.
The live animated shapes and projected bounds still differ from frozen poses.

## Implications

The measured animation bottleneck is per-vertex CPU processing and BVH refitting,
not clip/channel evaluation. This workload justifies investigating GPU skinning.
It does not demonstrate a GPU speedup: a prototype must measure added shader
cost and full end-to-end throughput while preserving quality and interaction.

A useful next design should retain cheap CPU node/joint pose calculation, then
move render deformation to the GPU. It must also replace unconditional full
CPU mesh deformation/refitting for picking and bounds. Keeping that complete
CPU path beside GPU skinning would retain most of the measured 10.98 ms cost.
Investigate conservative joint-influence bounds and exact CPU deformation only
for ray-candidate objects, including both hands and direct interaction. Keep
rigid physics colliders and saved animation state consistent with their contracts.

Preserve all supported influence sets, morph-before-skin order, independent poses,
transitions, stereo color/depth coherence and precise picking. Indexed deformed
rendering is worth measuring alongside GPU skinning, since the current override
path draws expanded triangles. Larger rigid-node and morph workloads still need
separate profiling before moving those paths. Do not extrapolate these FPS
numbers to independent compositor surfaces or unrelated detailed-model scenes.

## Evidence and reproduction

- [Filtered native windows and workload/HUD records](animation-profile/profile-frame.txt)
- [Weighted steady summaries](animation-profile/summary-frame.json)
- [Build, scene, and analysis instructions](../README.md#animated-model-performance-profiling)

Native counter/upload checks passed on the workstation and Frame for rigid,
skinned and morph assets. Scene tests verified stage sizes, playback mode,
completion cleanup and abort. Analyzer tests verified warmup exclusion,
weighted summaries, native/script log prefixes, and rejection of incomplete or
mismatched runs. Existing examples also passed, including the profiling scene.
