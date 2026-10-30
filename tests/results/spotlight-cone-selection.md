# Spotlight cone-selection validation

Steam Frame ARM64, 2026-10-29. The user confirmed readiness for active-headset
profiling. These tests are offscreen EGL/GLES: no OpenVR overlays or pointer
capture. Shadows remain deferred.

## Adopted policy

Extend the existing per-object range filter with conservative cone rejection.
For four or more active, nonzero lights, a spotlight can be omitted only if the
entire padded full/current-pose bounding sphere lies outside an infinite cone's
side tangent plane or apex plane. Range rejection still applies independently.
The CPU calculation accounts for the stored float direction length and widens
the cone slightly for shader rounding. Invalid bounds/axes and hemispherical or
wider cones retain conservative fallback; apex overlap and cone-edge spheres
retain the light. Retained-light order, per-fragment falloff and shading remain
unchanged. Points and the eight-light cap keep their existing behavior.

All decisions occur once per object/frame, after animation, interaction and
physics, and are shared by both eyes, primitives, LODs and surfaces. No new GPU
buffers or script options are introduced.

## Correctness

`spotlight-cone-regression` passes 274 reference/candidate stereo/material,
attachment and animated-pose images. Fixtures include 16 full-detail ToyCars,
generated tangents, morphing surface frames and Khronos alpha-mode geometry.
Tests use broad/narrow/mixed/backward lights, 0/1/4/8 counts, both eyes and head
movement. Alpha matches exactly; RGB differences are bounded to one byte/channel.

CPU checks cover side tangency, apex overlap, behind-light spheres, wide cones,
invalid axes and current deformed bounds. A deterministic 6,000-sphere test samples
48 interior points each (288,000 total): any sphere containing an illuminated
sample must be retained. These samples supplement the conservative plane argument;
they do not replace it.

The production point-light, spotlight and prior range-selection regressions
provide compatibility checks. The normal engine builds with the new default.
Documentation lint/site build are separate checks. The combined four-light
headset scene was subsequently accepted; see the acceptance section below.

## Matched performance comparison

Command: `spotlight-cone-regression --profile`. Reference mode keeps the previous
range filter; candidate mode enables cone rejection too. ABBA stages interleave
within each frame on the same renderer, shader, model buffers and render target.
Each stage has 30 warmup and 90 measured frames, alternating eyes. GPU timer
queries reject disjoint samples. Wall time includes submission and `glFinish`,
not CPU execution alone.

The workload is 16 full-detail ToyCars at .2-meter scale in a 4×4 grid, .2 yaw/
.1 pitch, and a 768-square target. No automatic LOD selection is run. Models use
PBR/cache ordering, directional intensity .3 and ambient .04; no baked environment.
Lights use intensity .15, color [1,.8,.6], range 2 and fixed positions. Broad
spot half-angles are 1.2/1.4 radians; narrow spots use .15/.25. Mixed sets alternate
points/spots; backward spots aim away from all cars. Geometry, lighting settings
and sampling density match between modes, and the rendered result is compared.

The table averages the two reference and two candidate stages in each run.
Pair counts describe object/light pairs sent to the shader, not drawn triangles.

| Scene, 8 lights | Pairs, range / cone | Run | GPU ms, range / cone | Wall ms, range / cone |
| --- | --- | --- | --- | --- |
| Broad spots | 128 / 128 | 1 | 4.7755 / 4.7700 | 6.5954 / 6.6771 |
| Broad spots | 128 / 128 | 2 | 4.8194 / 4.5916 | 6.6527 / 6.6039 |
| Narrow spots | 128 / 36 | 1 | 4.4905 / 3.6347 | 6.5067 / 5.6604 |
| Narrow spots | 128 / 36 | 2 | 4.2475 / 3.6145 | 6.3611 / 5.6533 |
| Mixed | 128 / 82 | 1 | 4.5478 / 3.9857 | 6.4872 / 6.2539 |
| Mixed | 128 / 82 | 2 | 4.3706 / 4.1368 | 6.4278 / 6.1440 |
| Backward spots | 128 / 0 | 1 | 3.9411 / 3.3293 | 6.0202 / 5.1775 |
| Backward spots | 128 / 0 | 2 | 4.3471 / 3.6578 | 6.4996 / 5.5225 |

Repeated eight-narrow-spot cases improve GPU elapsed time by about 15–19% and
render/completion wall time by about 11–13%. Mixed sets improve GPU time by about
5–12% and wall time by about 4%. Backward lights improve both by about 14–16%.
Broad cones retain all pairs and show no meaningful repeatable total-frame gain.
Zero/small-count controls expose measurement noise; small differences are not
claimed as improvements. These are workload-specific gains over range filtering,
not general headset FPS guarantees or justification for increasing the light cap.

## Combined headset acceptance

On 2026-10-29 the user verified the normal-engine
[light-filtering scene](../scenes/light-filtering.js): two point lights, two
spotlights, a grabbable car and normal-map target, and grabbable light markers.
Instructions covered cone-edge and range-boundary movement, aiming the spotlights,
head motion and range/cone controls. The user reported everything looked correct.
The demo was stopped immediately afterward and its exit verified. This acceptance
is distinct from the automated pixel and performance comparisons above.
