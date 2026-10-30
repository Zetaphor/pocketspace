# Directional shadow validation

Steam Frame, October 30, 2026. This validates opt-in orthographic shadows over
explicit world-space coverage. Shadows change output; these are feature costs,
not an equivalent-output optimization comparison.

## Automated correctness

Native `spot-shadow-regression --directional`, the original spotlight mode,
`directional-light-regression`, `scene-persistence-regression` and
`examples-regression` passed. The example inventory now contains 44 scripts.
Checks cover stereo, masked silhouettes, hidden/blended exclusion, cross-group
casters, current CPU/GPU animation, prepared/general shaders, GL restoration,
resolution/work/time budgets, reset and cleanup. Directional-specific checks
cover fixed coverage under attachment translation, rotated basis, vertical light
directions, finite matrices and out-of-volume receivers. The sloped receiver fixture checks every supported resolution using the public
default bias, requiring no self-shadow pattern and preserving real caster shadows.
The receiver-plane correction accounts for the complete filtered footprint.

API checks cover invalid coverage vectors/ranges/types, throwing/mutating
getters, shared spot/directional slot conflicts, transactional rollback, v13
snapshots, attachment remapping and older directional snapshots defaulting off.
Existing directional-light checks retain their 198-view stereo/reference matrix.

## Bounded performance

No OpenVR overlays or pointer capture. The user confirmed an active, worn headset.
One/four cache-indexed ToyCars use full-detail casters, direct light, no environment,
a fixed 2-meter square and 4-meter depth. Map sizes are 256/512/1024; each color
pass renders one alternating eye at 768 pixels. Each stage warms 12 frames and
measures 36, in disabled/enabled/enabled/disabled order. The profiling watchdog
threshold is 16 ms. GPU-disjoint and GL-error checks passed.

Means of paired stages, milliseconds except map payload:

| Cars | Map pixels | Shadow GPU | Color GPU off/on | CPU enqueue off/on | Completed map + one eye off/on | Map MiB |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 256 | 0.200 | 0.392 / 0.362 | 0.023 / 0.044 | 0.837 / 1.471 | 0.25 |
| 1 | 512 | 0.212 | 0.394 / 0.426 | 0.023 / 0.053 | 0.863 / 1.545 | 1 |
| 1 | 1024 | 0.215 | 0.434 / 0.424 | 0.025 / 0.049 | 0.865 / 1.449 | 4 |
| 4 | 256 | 0.481 | 1.122 / 1.150 | 0.081 / 0.149 | 2.243 / 3.338 | 0.25 |
| 4 | 512 | 0.540 | 1.103 / 1.365 | 0.095 / 0.147 | 2.405 / 3.363 | 1 |
| 4 | 1024 | 0.568 | 1.152 / 1.095 | 0.098 / 0.146 | 2.447 / 2.973 | 4 |

These are bounded map-plus-one-eye costs, not full stereo compositor FPS.
Scheduling variation prevents monotonic comparisons across resolutions, and
these results do not establish a maximum scene size. Active map estimates omit
driver/program and in-flight allocation overhead.

The final profile includes the receiver-plane correction. Both spotlight and
directional native checks passed after its deployment.

## Interactive validation

The normal-engine `scripts/directional-shadows.js` scene ran near 90 FPS with
active shadows and no GL errors. Initial user feedback identified self-shadow
patterns on the yellow cube and floor at 256 and 512. The receiver-plane
correction was then deployed and the native checks/profile rerun.

The user confirmed that the pattern is gone and shadows look correct after
checking low resolutions, moving the caster, rotating the light and moving their
head. The demo was stopped and its exit verified after acceptance.

Coverage remains explicitly bounded; outside receivers are unshadowed and
coverage edges do not fade. Bias may still require tuning for grazing geometry,
scene scale or thin details. This acceptance covers the sample scene rather
than every possible coverage/depth configuration.
