# PBR external-depth optimization

Validated on Steam Frame on 2026-10-29. Production enables the cheaper external
PBR depth pass automatically; no JavaScript option or material-quality change.
Color passes retain the original shader. A separate startup-linked depth program
uses the identical vertex source and preserves pose, LOD, UV, winding and projection.
Opaque depth returns before texture sampling. Masked depth applies the identical
base alpha times factor and strict cutoff discard, then returns before lighting.
Blended primitives remain excluded from external depth. Lighting/environment
setup and normal/tangent attribute bindings are skipped for depth. Sampler units
are initialized before the first draw; program/texture/attribute cleanup remains
in the existing ownership path. No additional geometry or texture buffers.

## Correctness and visual acceptance

The native regression passed 1,248 exact image comparisons covering stereo,
front/back and mirrored winding, alpha modes/factors/cutoffs, overlaps, LOD and
animated poses. Independent opaque < masked < blended visible-target coverage
checks ensure the depth comparisons exercise real occlusion. Program preservation,
disabled surface attributes, shader deletion and geometry-budget cleanup passed.
PBR (130 images plus API/persistence), environment and culling (70 views)
regressions also passed, and the normal engine built successfully.

The user accepted the normal-engine `tests/scenes/pbr-depth.js` headset scene:
head movement, grip/trigger grabbing and rotation, overlaps and alpha-cutout holes
against the blue background looked correct, without reported flickering or missing
parts. This visual acceptance is separate from the offscreen timing evidence.

## Measurements

Awake/worn Frame, 768-square offscreen eye target, alternating eyes, fixed camera,
full-detail ToyCars, studio environment preuploaded, bounds/scissor enabled.
Both programs were initialized before final timing. Each mode ran in original /
optimized / optimized / original order, 30 warmup plus 120 measured frames per
stage. Values below average the two stage means. GPU timer queries, enqueue CPU
and synchronized wall time were collected; disjoint/error checks passed.

The external-depth workloads rendered 12 draws / 435,744 triangles for four cars
and 48 / 1,742,976 for sixteen cars, with zero PBR color draws. Color-only controls
rendered the same counts as color, with zero external depth. Counts matched
across modes, eyes and stages. The external target was behind the occluders.

| Workload | GPU original → optimized (ms) | Enqueue CPU (ms) | Synchronized wall (ms) |
| --- | --- | --- | --- |
| 4 cars, external depth | 2.490 → 1.563 | 0.072 → 0.043 | 3.683 → 2.634 |
| 16 cars, external depth | 10.292 → 6.442 | 0.304 → 0.143 | 12.008 → 8.043 |
| 4 cars, color only | 2.459 → 2.525 | 0.059 → 0.059 | 3.562 → 3.623 |
| 16 cars, color only | 10.260 → 10.178 | 0.322 → 0.289 | 11.814 → 11.773 |

The sixteen-car depth case reduced GPU time by about 37% and enqueue CPU by 53%.
Color-only controls use the same original shader in both modes; their small
measured differences remain recorded rather than attributed to the optimization.
Clocks and thermals were not locked. These are offscreen workload timings, not
whole-compositor FPS or a guarantee of sustained application performance.

An initial variant routed color through the augmented shader too. Two runs
confirmed depth gains but showed a small four-car color cost. Final production
therefore selects the new program only for external depth. Raw samples preserve
all runs: [initial](pbr-depth/initial.json), [repeat](pbr-depth/repeat.json), and
[final selective routing](pbr-depth/selective.json). No geometry, triangle count,
texture resolution or material-quality reduction was used.
