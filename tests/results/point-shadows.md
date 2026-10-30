# Point shadow validation

Steam Frame, October 30, 2026. Opt-in six-view point-light shadows are a feature
with additional GPU work, not an equivalent-output optimization.

## Automated checks

The native shadow regression passed in point, spotlight and directional modes.
Point coverage includes six face-center projections, atlas payload at both
supported resolutions, default-bias receiver cleanliness, real caster shadows,
stereo, masked holes, hidden/blended exclusion, cross-group casters, prepared
shaders, current CPU/GPU animation, state restoration, budgets and cleanup.
API checks cover per-face resolution limits, invalid values, getter mutations,
shared slot conflicts, v14 snapshots and older point snapshots defaulting off.
Existing point-light, persistence and example checks passed, including the new
lamp controls. The example inventory contains 45 scripts. Docs lint/build passed.

## Bounded profile

The user confirmed the headset was awake and worn. Offscreen GLES creates no
OpenVR overlays and captures no pointer input. One cache-indexed ToyCar uses
full-detail six-view casters, direct light and no environment. Each stage warms
12 frames and measures 36, in disabled/enabled/enabled/disabled order. Color
renders one alternating eye at 768 pixels. The profiling limit is 16 ms and
1000000 submitted triangles. GPU-disjoint and GL-error checks passed.

Means of paired stages, milliseconds except estimated map payload:

| Face pixels | Six-view GPU | Color GPU off/on | CPU enqueue off/on | Completed map + one eye off/on | Map MiB |
| --- | --- | --- | --- | --- | --- |
| 256 | .774 | .435 / .518 | .021 / .097 | .897 / 2.733 | 1.5 |
| 512 | .759 | .431 / .501 | .023 / .095 | .825 / 2.551 | 6 |

Scheduling variation prevents cross-resolution speed claims. These are not full
stereo compositor FPS. Four full-detail cars exceed the one-million-triangle
six-view submission budget and fall back rather than rendering the map; an
initial older benchmark reached and confirmed that limit. The final bounded
profile intentionally measures only the one-car workload that fits.

## Visual validation

The initial interactive lamp check identified a white line beneath the caster.
The face-boundary correction projects world-position derivatives through one
selected face and clamps offset receiver taps instead of returning unshadowed
light at the edge. A native test checks continuous shadow interiors across a
face boundary at both resolutions. The user confirmed that the white line is gone and the shadows look correct
at both supported resolutions after moving the lamp and head. The final scene
ran near 90 FPS with active shadows and no GL errors. The demo was stopped and
its exit verified after acceptance. All three shadow modes passed again after
the seam correction. The profile above predates that receiver-only correction;
it is not a separate final-receiver performance comparison.
Known scope: one shared shadow light, 256/512 pixels per face, nonseamless
face-clamped filtering, opaque/masked casters and explicit work/time limits.
