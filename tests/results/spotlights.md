# Spotlight validation

Steam Frame ARM64, 2026-10-29. The user confirmed readiness for active-headset
profiling. Spotlights share the eight-entry light budget with points, use the
same PBR BRDF/range attenuation and add a squared angular fade. No shadows or
volumetric beams are implemented.

## Automated correctness

`spotlight-regression` passes real GLES tests in both eyes: narrow/wide/reversed
cones, direction normalization, equal-angle hard edges, fully illuminated
hemisphere equivalence to point lights, alpha silhouettes, unlit/depth invariance
and attachment rotation on all three axes. API checks cover wrong-type IDs,
invalid vectors/angles, getter mutation/removal, mixed light capacity, cleanup,
failed reload preservation, named-file roundtrips, v9 mixed snapshots and v8
point-light compatibility. Invalid restores preserve the active scene.

The existing point-light regression, PBR's 130 stereo/material/pose images,
scene persistence checks, and all 38 examples plus three rendering scenes pass.
The flashlight example also exercises fresh light/attachment IDs after restore.

## Matched performance comparison

`point-light-regression --profile-spots` opens no OpenVR session or overlays and
captures no pointer input. The headset was available for the measurements.
Each type runs 0, 1, 4, 8, 8, 4, 1, 0 lights, with 60 warmup and 180 measured
frames per case. The Box and 16-full-detail-ToyCar workloads retain the
[point-light comparison settings](point-lights.md): 768-square target, alternating
eyes, fixed geometry/view, explicit cache ordering, no baked environment, and
no automatic LOD selection. GPU queries reject disjoint measurements. Wall time
includes submission and `glFinish`; it is not CPU-only duration.

Spotlights use direction +Z, inner half-angle 1.2 and outer half-angle 1.4 radians.
Every timed spotlight configuration is pixel-compared against the same point
lights before timing, proving identical illumination of this workload. This
isolates cone overhead instead of crediting reduced illumination as a speed gain.
Each light has intensity .15 and range 2 meters.

| Type | Lights | GPU ms, forward / reverse | Wall ms, forward / reverse |
| --- | --- | --- | --- |
| point | 0 | 3.2817 / 3.3554 | 5.2979 / 5.4688 |
| point | 1 | 3.2862 / 3.2423 | 5.3183 / 5.3193 |
| point | 4 | 3.6324 / 3.6617 | 5.5401 / 5.7551 |
| point | 8 | 3.9865 / 4.3902 | 5.8115 / 6.1974 |
| spot | 0 | 3.2923 / 3.5243 | 5.3828 / 5.4763 |
| spot | 1 | 3.5478 / 3.5634 | 5.5122 / 5.6057 |
| spot | 4 | 4.1459 / 4.2377 | 6.0471 / 6.2003 |
| spot | 8 | 4.7188 / 4.7160 | 6.7172 / 6.8761 |

Eight spotlights cost about 4.7 ms GPU time in the detailed test, compared with
about 3.3–3.5 ms for no scene lights. Cone shading adds work relative to points;
this does not establish a universal scene FPS or justify increasing the light
limit. Narrower cones may reject more fragments, but that is a different lighting
workload. The simple Box cases also pass, with timing noise dominating the small
absolute cost. The comparison uses the current shader for both light types; it
does not compare zero-light overhead against a prior revision.

## Headset acceptance

The user confirmed the normal-engine `scripts/spotlights.js` scene looked
correct on 2026-10-29 after instructions to check close grip/trigger handle grabs,
rotation and translation, head motion, narrow/wide cones, A/X off/on and B/Y
capture/restore. The demo was stopped immediately afterward and its exit verified.
Automated checks and timings are separate from this acceptance.
