# Scene directional lights

Implemented on Steam Frame, 2026-10-29. Scene directionals add unshadowed,
distance-independent GGX lighting to PBR models. They share the existing eight
slots, IDs, cleanup and attachment ownership with point/spot lights. The legacy
per-asset directional light remains available independently.

## Contracts

The four typed JavaScript functions are documented in the
[lighting API](../../docs/lighting-api.md#scene-directional-lights).
Direction describes ray travel in spotlight-compatible axes. Attachment rotation
changes it; translation, scale and visibility do not change illumination.
Position/range/cone fields are rejected, and getters retain the supplied
unnormalized direction in a detached value.

Resolved lights use the existing GPU layout, with negative range marking a
scene directional. CPU range/cone filtering always retains it. The shader uses
opposite ray direction as the vector toward the light, unit attenuation, and
existing metallic/roughness, normal-map, emissive, tone and alpha behavior.
Prepared variants keep their material/count keys and the same bounded policy;
no new compile-on-draw path or mesh/texture allocation is introduced.

Snapshot v10 adds directional entries. V9 point/spot and v8 point documents
remain readable, with v1–v7 retaining their no-scene-light behavior. Directional
entries in older formats are rejected. All restoration still validates/stages
before publishing, preserves ID counters on failure, and remaps attachments to
fresh object IDs. Capturing an attachment across scope boundaries is rejected.

## Automated correctness

`directional-light-regression` reports 198 stereo/reference/mixed cases:

- Basic directional brightness versus the existing per-asset directional BRDF,
  normalization, reversed direction, enabled/zero-intensity state, translation/
  scale independence and attached rotation.
- Unlit and external-depth invariance; four infinite lights survive object
  range/cone filtering even with distant stored positions.
- 80 byte-exact old/new point/spot comparisons across zero/one/four/eight lights,
  stereo, normal maps, alpha, skeletal and morph geometry. The frozen reference
  retains the previous point/spot loop, rather than comparing two new shaders.
- Mixed directional/local-light alpha invariance and prepared/general color
  comparisons: exact alpha, at most one byte RGB difference under the existing
  prepared-program tolerance. Prepared programs are confirmed used.
- Typed CRUD, detached getters, invalid values, throwing/mutating getters,
  removed light/owner handling, eight-slot capacity including disabled lights,
  cross-scope capture rejection, named-file roundtrip, v10/v9 compatibility,
  invalid snapshot rollback including light-ID counters, failed reload and cleanup.

An additional guard verifies unsupported option accessors are read once, without
re-reading them through common point-light parsing. Native point-light, spotlight,
scene-persistence, examples, PBR, environment, animation-blend and production PBR
indexing regressions also pass. The persistence suite includes physics, panel and
animation snapshots with the updated capture version. The normal engine builds.

## Matched offscreen shader cost

Active/worn Frame, sixteen full-detail ToyCars, fixed camera, alternating eyes,
768 pixels per eye, vertex-cache ordering, no environment, one member group.
Every image submits 48 primitive draws / 1,742,976 triangles. Both paths retain
the same assets, FBO/storage, material inputs and geometry. Each of 80 poses runs
old/new/new/old, discarding twenty warmup poses and averaging sixty measured
poses per stage. The repeat reverses shader order. GPU timer/disjoint/GL checks
pass; `glFinish` is retained. These are GPU workload costs, not compositor FPS.

| Existing lights | Count | First old → new GPU (ms) | Reverse old → new GPU (ms) |
| --- | --- | --- | --- |
| Point | 0 | 2.971 → 2.786 | 2.806 → 2.953 |
| Point | 1 | 3.009 → 2.945 | 3.169 → 3.033 |
| Point | 4 | 3.641 → 3.446 | 3.502 → 3.513 |
| Point | 8 | 3.910 → 4.041 | 4.194 → 4.133 |
| Spot | 0 | 2.827 → 2.954 | 2.943 → 3.072 |
| Spot | 1 | 3.017 → 3.001 | 3.194 → 3.256 |
| Spot | 4 | 3.358 → 3.617 | 3.559 → 3.711 |
| Spot | 8 | 4.302 → 4.088 | 4.248 → 4.404 |

New branching can change shader cost: four-spot samples are approximately
.15–.26 ms slower. Other changes vary in both directions, including zero-light
controls. Clocks, thermals and compositor scheduling were not locked. Do not
claim zero overhead, a general speedup, or a guaranteed percentage regression
from these short runs. Point/spot pixel behavior is preserved independently of
this timing evidence.

| Scene directional count | First GPU (ms) | Repeat GPU (ms) |
| --- | --- | --- |
| 0 | 2.794 | 2.943 |
| 1 | 2.941 | 3.183 |
| 4 | 3.494 | 3.514 |
| 8 | 4.000 | 4.047 |

Directional intensity is .2 per light; point/spot intensity is .1 with range two.
These are feature-cost samples, not photometrically matched comparisons of light
types. All directional stages use the new shader. Bounded raw measurements:
[first](directional-lights/profile-frame.json),
[reverse](directional-lights/profile-reverse-frame.json).

```sh
cmake -S . -B build
cmake --build build -j2 --target directional-light-regression
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/directional-light-regression --profile
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/directional-light-regression --profile --reverse
```

The reverse-run diagnostic binary SHA-256 is
`5f44ed0197e390a03cf1a0d3f31b619d54266b453f02670465ae25654f9e72d1`.
The final API accessor-read guard does not change the measured shader.

## Headset visual acceptance

The user accepted the normal-engine
[directional-lights.js](../../scripts/directional-lights.js) scene: rotating the
blue handle aims lighting, translation does not add distance falloff, bumper
changes color, A/X toggles the key, B/Y captures/restores the attachment, and car
grabs, highlights and head movement look correct. The accepted native binary
SHA-256 was `c854fac46edf46d3d62bf7705f9b2bcbf3ec0b872330ff4945365a47f2914b9f`.
The later API accessor guard is covered by automation and leaves rendering unchanged.

The demo was stopped immediately after acceptance and its exit verified. It
completed 6,290 frames; the final reported windows were 90 FPS with zero GL
errors, and shutdown reported zero grouped-buffer charges. This is acceptance
of this small scene, not an eight-light capacity or dense-model FPS guarantee.
No demo remains running. Documentation lint/site build and whitespace checks pass.
