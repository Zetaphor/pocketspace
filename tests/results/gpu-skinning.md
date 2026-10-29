# GPU skeletal skinning prototype

Measured on Steam Frame on 2026-10-28 at commit `76edcd7`. This excluded
experiment improved the measured workload; normal builds retain CPU skinning.
The user confirmed that the 32-model scene looked visually fine after this comparison.

## Method

- ARM64 Steam Frame, unchanged `schedutil` governor; zink Vulkan 1.4, Turnip
  Adreno 750, GLES 3.2 Mesa 26.3.0-devel (git-abc426bb62).
- Same `gpu-skinning-experiment` binary, `SF_GPU_SKINNING=0` then `1`.
  Each run used two 20-second stages: 32 animated CesiumMan actors, then 32 paused.
- Independent animation phases, 19 joints and 4,672 triangles per actor; one
  render group, 768 reference eye pixels, one HUD, no physics. User confirmed
  readiness and was asked to keep looking at the models. Both runs completed.
- Both modes use identical conservative joint-influence bounds, scene placement,
  material shaders and quality settings. No LOD/detail reduction. CPU renders
  expanded deformed vertices; GPU renders shared indexed skin vertices.
- Existing analyzer discards 120-frame windows starting in the first five seconds
  of each stage and weights means by frames. CPU animated: 5 retained windows;
  other stages: 10 each. This is one matched pair, not a long-session study.
- Times below are CPU elapsed durations. Completion wait includes synchronization
  and is not a GPU timestamp or utilization measurement.

## Results

Times are mean milliseconds per engine frame; work excludes final pacing sleep.

| Mode | Playback | FPS | Pose | Deform + bounds | BVH refit | Upload submission | Render submission | Completion wait | Work |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CPU | Animated | 50.41 | 0.194 | 7.770 | 2.979 | 0.901 | 0.085 | 7.561 | 19.835 |
| GPU | Animated | 88.26 | 0.186 | 0.051 | 0 | 0.529 | 0.118 | 7.631 | 8.776 |
| CPU | Paused | 89.10 | 0 | 0 | 0 | 0 | 0.078 | 7.948 | 8.265 |
| GPU | Paused | 89.45 | 0 | 0 | 0 | 0 | 0.134 | 7.719 | 8.102 |

Animated throughput increased about 75%; work fell about 56%. Dynamic uploads
fell from 8,970,240 to 38,912 bytes/frame (about 230 times smaller). GPU mode
processed zero CPU vertices in retained animated windows: controller rays did
not require exact candidate deformation. This does not measure sustained grabbing
or rays intersecting many actors. Exact candidates still incur CPU work.

Final animated HUD samples: CPU RSS 86.5 MiB versus GPU 72.6 MiB;
engine GPU allocation estimates 256.0 versus 247.5 MiB. The exact allocation
reduction is 8,931,328 bytes, matching removal of 32 dynamic expanded VBOs minus
32 palette textures. Shared experimental indexed buffers are present in both
modes. This reconciles reported allocation differences with animation storage,
though live head pose and projected pixel work are not fixed by these estimates.
Driver VRAM totals were unavailable. Last CPU process samples were 63.8% versus
18.8%; these are endpoint samples rather than whole-stage averages.

## Correctness and scope

Native workstation and Frame regressions passed:

- Transform-feedback GPU positions match CPU vertices within 0.00003 normalized
  units at five times, including live-source and frozen-source transitions.
- Every captured position lies within conservative bounds.
- Both-eye color images with depth testing enabled: zero pixels differed by more
  than 2/255 per channel in all ten comparisons (allowed maximum 32 pixels).
- 3,000 exact picking comparisons agree with CPU brute-force triangles. Bounds
  misses avoid CPU deformation; both hands reuse one exact cache per pose.
- Palette byte accounting, clean upload skipping, independent restored poses,
  live/frozen transition persistence and CPU fallback checks pass.
- Existing CPU animation, transition and scene-persistence regressions also pass
  locally; normal engine builds successfully. Profiling scene checks, doc lint
  and documentation site build pass.

Eligibility is deliberately limited: entirely skinned assets, no morph targets,
exactly one four-lane influence set per primitive, at most 1,024 joints per skin.
Other assets use the existing CPU path. Automated rendered comparisons cover
CesiumMan; broader assets/materials, mixed-part scenes, active interaction costs,
resource stress remain before production adoption. Visual acceptance applies to the
32-model CesiumMan scene; it does not establish coverage for every asset.
The common runtime shader handles both color and depth draws, but these image
checks do not replace headset occlusion/reprojection acceptance.

## Reproduction and evidence

See [build/run instructions](../README.md#gpu-skinning-prototype) and
[the bounded scene](../scenes/gpu-skinning-profile.js). Demos were stopped after
both completion markers and verified absent.

- [CPU filtered log](gpu-skinning/cpu-frame.txt), [summary](gpu-skinning/cpu-summary.json)
- [GPU filtered log](gpu-skinning/gpu-frame.txt), [summary](gpu-skinning/gpu-summary.json)
- [Frame correctness output](gpu-skinning/correctness-frame.txt)

Regenerate summaries with `python3 tests/analyze-animation-profile.py <log>`.
