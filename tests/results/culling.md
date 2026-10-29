# glTF sidedness and native culling comparison

Validated on Steam Frame on **2026-10-29**, using `40c4b58` plus the opt-in PBR
and shared culling implementation. This is a correctness fix with a bounded
performance comparison, not a demonstrated dense-scene frame-rate optimization.

## Correctness

The engine now honors glTF `doubleSided` in both unlit and PBR paths. Single-sided
back faces are culled; reflected node transforms reverse front-face winding.
One per-pass state owner spans ordinary, CPU/GPU animated, instanced and depth
submission. Rigid batches stop at incompatible animated winding. Panels retain
their existing rendering behavior.

Native checks passed:

- `culling-regression`: **70** front/back, mirrored, alpha-mode, PBR/unlit,
  ordinary/instanced and independently reflected rigid-animation comparisons;
  culling and front-face state restored after each pass.
- `pbr-regression`: **130** rendered material/pose/stereo views, including exact
  legacy/PBR unlit-white overlap comparisons; independent sRGB and linear packed
  channel checks (maximum one 8-bit output step for factor-versus-texture
  quantization); finite orthonormal surface frames, CPU morph normals/tangents,
  malformed/unsupported UV rejection, shared resource deletion, transactional
  failed reload, lighting setter validation and v5/v6 snapshot compatibility.
- `gpu-skinning-regression`: four assets, 18,000 exact picking comparisons,
  transform-feedback vertices, conservative bounds, stereo/material pixels, palette
  uploads, CPU fallbacks, resource cleanup and 30 production lifecycle reloads.
- `scene-persistence-regression`: JSON/disk round trips, physics, independent
  animation, retained panels, staged reservation/asset failures and cleanup.
- `examples-regression`: all published scripts, including `scripts/pbr.js`.
- `render-occlusion-regression`: **16** stereo floor/wall lean-back views, foreground
  preservation, alpha-mask holes, scissors, crop/index equivalence and cleanup.

The normal engine also completed a 20-second non-interactive run of
`tests/scenes/pbr-materials.js`: six diagnostic surfaces, 1,739 frames, a logged
900-frame window of 89.1 FPS, `gl_error=0`, no held objects or pointer capture,
and clean automatic exit. This smoke run checks startup, scene publication and
the compositor path; it is not a matched culling performance comparison.

These are automated checks. The user accepted the interactive ToyCar PBR check on 2026-10-29: rendering
remained correct during grabbing, rotation and head movement, without reported
flicker, missing parts or jumping highlights. The user also accepted the gallery's unlit and emissive appearance. The user
reported dark metallic columns in the tangent fixtures, dark blue/metallic
spheres in NegativeScaleTest, and little visible variation between the
TextureEncodingTest orbs. The encoding fixture intentionally matches equivalent
materials across each row; different rows are not required to match.

Inspection of the embedded metallic/roughness texture confirms that the third
NormalTangentTest section and the metallic half of NormalTangentMirrorTest use
roughness byte 0 and metalness byte 255. NegativeScaleTest's `So Shiny` material
is metallic with zero roughness, while its `Dark` dielectric has linear base color
[.004, .008, .02]. Their darkness is consistent with direct-only lighting and the
absence of reflected environment illumination. It does not establish a tangent
failure or prove complete tangent correctness. The reflection-based diagnostic
checks required environment lighting to provide a useful reference. The subsequent
[environment validation](environment-lighting.md) found and corrected missing
generated-tangent handedness conversion, and the user accepted the updated
headset gallery. The first PBR implementation had no environment map; the Khronos
reflection-based reference images were not pixel goldens for its direct-light
output.

## Performance controls

The headset was awake and worn; governor was `schedutil`. No overlay engine/demo
was running. The test opens no OpenVR session, creates no overlays and captures
no controller input. Two complete `culling-regression --benchmark` invocations
used off/on/on/off order for each workload. Each stage warmed 30 frames and
measured 60. All GPU disjoint and GL error checks passed.

Both settings use the same loaded assets, full geometry, object transforms,
camera, material shader, application-owned buffers/textures and ordered instancing
policy. Background VR scheduling and thermal state were not locked. One fixed
768×768 offscreen surface alternates eyes; this is **not** a live stereo
compositor FPS measurement. There are no external group occluders in this
workload. Boxes use 240 objects/2,880 submitted scene triangles; cars use 32
multipart ToyCars/3,485,952 triangles. LOD is disabled. Culling removes rasterized
back faces, not submitted vertices or triangles.

The following values are medians of four stage means per setting, across both
invocations; raw stage means are retained in [samples.json](culling/samples.json).

| Workload | GPU ms off → on | CPU enqueue ms off → on | Synchronized offscreen frame ms off → on |
| --- | ---: | ---: | ---: |
| 240 boxes | .0425 → .0144 | .0414 → .0353 | .6101 → .5378 |
| 32 full-detail ToyCars | 6.8462 → 7.0982 | .0461 → .0479 | 8.8988 → 8.8867 |

Box timings are tiny and variable; the second invocation includes an on-stage
slower than both off-stages. The car GPU median is slightly higher with culling,
while synchronized frame time is effectively unchanged. These short samples do
not establish a repeatable overall speedup. Do not describe culling as a fix for
the dense-model throughput backlog. It remains enabled to render the material's
specified sides correctly; its CPU state overhead was small in these workloads.
