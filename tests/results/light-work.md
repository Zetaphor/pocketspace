# Light range-selection optimization

Steam Frame ARM64, 2026-10-29. The user confirmed the headset was awake and worn
for profiling. All comparisons are offscreen EGL/GLES; no OpenVR overlays or
pointer capture. Shadows were explicitly deferred in the roadmap.

## Adopted behavior

With four or more active, nonzero point/spot lights, build an ordered light list
per visible PBR object once per frame. Remove only lights whose range cannot
reach the origin-centered sphere enclosing its full/current deformed bounds.
Double-precision tests, meter/relative padding and invalid-bound fallback keep
rejection conservative. Reuse lists across eyes, primitives, surfaces and LODs.
Fewer than four lights keep full lists; empty frames clear counts directly.

The shader, eight-light cap, range/cone settings, BRDF, geometry, resolution,
light order, attachment and persistence behavior are unchanged. No extra GPU
buffers are allocated; bounded CPU arrays reuse frame storage. No script switch
is required. Spot cone rejection remains per fragment.

## Correctness

`light-work-regression` checks 600 reference/candidate images across 16 ToyCars,
generated tangents, surface frames and the Khronos alpha-mode fixture. Coverage
includes both eyes, two head poses, broad/local point/spot/mixed lights, 0/1/4/8
counts, material alpha modes and unlit rendering. Alpha must match exactly;
RGB differences are bounded to one byte per channel. CPU checks cover cutoff
boundaries, rotation, invalid bounds, small-count bypass and deformed-pose bounds.

The real GLES point-light and spotlight regressions separately cover emitted
lighting, cone/range controls, attachment transforms, resource cleanup, API
failures and snapshot/reload behavior. Production and excluded targets build on
Frame with regression assertions enabled. Documentation lint/site build are
separate checks. This optimization did not receive a new interactive headset
acceptance; the existing light demos were accepted before it, and no demo was
launched or left running during these measurements.

## Controlled performance comparison

Command: `light-work-regression --profile`. The final timing comparison toggles
selection on the **same** renderer, shader, model buffers and render target.
ABBA stages interleave within each frame, rather than timing long blocks on
different renderers. Each stage uses 30 warmup and 90 measured frames, alternating
eyes. GPU timer queries reject disjoint samples. Wall time includes submission
and `glFinish`, not CPU execution alone.

The detailed workload is 16 full-detail ToyCars, .2-meter scale, on a 4×4 grid
with fixed .2 yaw/.1 pitch and a 768-square target. Automatic LOD selection is
not run. Models use PBR and cache ordering, directional intensity .3, ambient .04,
and no baked environment. Broad lights have range 2 meters; local lights have
range .18 and are spread across the scene. Intensities (.15), colors, positions,
cones (.2 inner/.35 outer), geometry and view match between reference and filtered
modes. Mixed lights alternate points and spots. No contributing geometry or
illumination is deliberately removed. Each configuration is pixel-compared first.

The table averages the two reference and two candidate ABBA stages. Eight-light
cases follow the adopted path; early prototypes also filtered smaller counts,
which did not establish consistent gains and led to the final four-light threshold.

| Layout | Light types | GPU ms, all / filtered | Wall ms, all / filtered |
| --- | --- | --- | --- |
| Broad | Point, 8 | 4.1950 / 4.2072 | 6.1451 / 6.0465 |
| Broad | Spot, 8 | 4.2641 / 4.2767 | 6.0481 / 6.1378 |
| Broad | Mixed, 8 | 4.3303 / 4.4283 | 6.2788 / 6.2956 |
| Local | Point, 8 | 3.9513 / 3.6072 | 5.8821 / 5.6208 |
| Local | Spot, 8 | 4.0325 / 3.6436 | 6.0477 / 5.5570 |
| Local | Mixed, 8 | 3.3781 / 3.1320 | 5.3993 / 5.2007 |

Eight local lights reduce measured GPU elapsed time by about 7–10% and total
render/completion wall time by about 4–8%. Earlier repeat runs supported the same
local-light direction; their larger apparent gains are not used as the headline.
Broad-light cases show no consistent benefit: all objects retain the same lists.
Zero-light controls also show timing noise, so small deltas are not evidence of
improvement. These results support the bounded range filter, not a general FPS
guarantee or an increase to the eight-light cap.

## Rejected shader-work alternative

A first prototype skipped the BRDF for back-facing light directions and delayed
halfway-vector construction until after spot-cone rejection. It passed 216
matched images, but native timings were inconsistent and the eight-spot case
regressed. Production retains the original shader loop. The excluded target
keeps this alternative under `--profile-shader`, with selection disabled in both
modes; its frozen loop lives in `light-work-reference.h`. It has no normal-build
runtime option. Future shader work needs repeatable throughput gains before adoption.
