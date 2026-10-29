# Dynamic point-light validation

Steam Frame ARM64, 2026-10-29. The user confirmed the headset was ready to be
worn for profiling. The normal production path now supports up to eight
unshadowed point lights for PBR assets; directional and baked environment
lighting remain independent per-asset settings.

## Automated correctness

`point-light-regression` uses real EGL/GLES without an OpenVR session. It checks
both eyes, increasing intensity, red light color, range cutoff, zero intensity,
disabled-light equivalence, unlit-material invariance and unchanged external-depth
silhouettes. Attachment rotation/translation is checked independently. Native
script tests cover invalid values, throwing/mutating getters, capacity, light
ownership, object cleanup, container disposal, failed reload preservation,
in-memory and named-file restoration with fresh attachment/light IDs, v7
compatibility and malformed v8 rollback.

The existing PBR regression passes 130 stereo/material/pose images and API checks.
Environment, container, example and persistence regressions provide separate
compatibility checks. Deliberately rejected reloads are expected diagnostic
messages; tests exit on failures rather than intentionally aborting.

## Bounded performance comparison

Command: `point-light-regression --profile`, using `DISPLAY=:0`,
`EGL_PLATFORM=x11` and the SteamVR ARM64 library path. No overlays or input capture.
The same 768×768 offscreen target is used throughout, alternating left/right
eyes. Each case has 60 warmup and 180 measured frames. Light counts run
0, 1, 4, 8, 8, 4, 1, 0. Timings include GPU timer queries with disjoint rejection;
wall time includes submission and `glFinish`, not just CPU execution.

Workloads are one full-detail Box or 16 full-detail ToyCars (108,936 source
triangles each), arranged in a 4×4 grid at .2-meter scale. LOD is not selected
during this comparison; all instances use level zero. Cars use PBR and explicit
cache ordering; direct directional lighting is zero and ambient is .04, with no
baked environment. Every light is in range, has intensity .15 and range 2 meters.
Geometry, head/eye positions and target size remain fixed for each workload.

| Workload | Point lights | GPU ms, forward / reverse | Wall ms, forward / reverse |
| --- | --- | --- | --- |
| Box | 0 | .0166 / .0401 | .3897 / .3148 |
| Box | 1 | .0195 / .0199 | .3739 / .3445 |
| Box | 4 | .0485 / .0885 | .3331 / .3190 |
| Box | 8 | .0575 / .0579 | .3373 / .3994 |
| 16 ToyCars | 0 | 3.1890 / 3.4015 | 5.0871 / 5.4120 |
| 16 ToyCars | 1 | 3.2807 / 3.4245 | 5.1968 / 5.3395 |
| 16 ToyCars | 4 | 3.6450 / 3.6704 | 5.6763 / 5.5943 |
| 16 ToyCars | 8 | 3.9887 / 4.1006 | 5.8794 / 6.0819 |

The detailed workload adds approximately .7–.8 ms GPU time at eight lights versus
zero in the corresponding order. The tiny Box timing is more sensitive to noise.
This supports a bounded initial forward implementation; it does not justify
raising the cap or predict arbitrary dense-scene/headset FPS. It also does not
compare the zero-light shader against the previous shader revision.

## Headset acceptance

The user confirmed the normal-engine `scripts/lights.js` scene looked and
behaved correctly on 2026-10-29, after instructions to check grip/trigger marker
grabs, head motion, color changes, toggling and snapshot restore. The demo was
stopped immediately afterward and its exit verified. This acceptance is separate
from the automated correctness checks and offscreen timings above.
