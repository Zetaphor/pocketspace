# Baked environment lighting validation

Validated on Steam Frame on **2026-10-29**, using `d137b99` plus the baked
environment implementation and generated-tangent handedness correction.

## Automated correctness

- `environment-bake-test.py`: constant radiance survives GGX filtering; diffuse
  spherical harmonics reproduce constant irradiance/pi; cube axes and
  equirectangular sampling agree; integrated BRDF energy remains bounded; rough
  specular filtering broadens the studio windows. Regenerating the studio bake
  reproduced its committed bytes.
- `environment-regression`: 24 rendered checks, including dark-metal illumination,
  intensity-zero equivalence, environment rotation, unlit invariance, four Khronos
  fixtures across both eyes and two rotations, and exact frame/pixel equivalence
  for independently generated and imported tangents. It also checks shared path
  ownership, GPU upload binding/alignment preservation, post-draw texture state,
  resource deletion, malformed/truncated/non-finite payload rejection, failed
  setter/restore rollback, no retained staged assets, and v6/v7 compatibility.
- `pbr-regression`: 130 material/stereo/pose views, API validation, failed reload,
  CPU fallback geometry and owned-resource cleanup passed after the tangent fix.
- `culling-regression`: 70 front/back, mirrored, alpha-mode, instancing and rigid
  animation comparisons passed after the tangent fix.
- Scene persistence and the published examples passed with v7 environment fields;
  old direct-light v6 snapshots retain an empty environment.
- Documentation lint, VitePress build and whitespace checks passed.

## Headset visual acceptance

The user accepted the environment-lit diagnostic gallery on 2026-10-29 using
`68bd49e`, after being asked to inspect generated tangent details, mirrored
reflection orientation and metallic reflections. This completes the gallery's
visual check; it is separate from automated correctness and measured cost below.

The six-surface, non-interactive gallery logged 89–90 FPS across three 900-frame
windows with no GL errors. These runtime observations are not a controlled
performance comparison.

## Tangent orientation discovered by reflections

The initial environment preview exposed an actual error in generated tangent
handedness. NormalTangentTest's generated normal maps appeared concave beside
convex reference geometry, while NormalTangentMirrorTest's imported tangents
looked consistent. The MikkTSpace callback now negates generated `w` to match
glTF's normal-map convention, as in the [Khronos renderer](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/gltf/primitive.js).
Imported glTF tangent signs are not flipped. Node reflections and normal-map UV
transforms retain their independent frame conversions.

The new GeneratedTangents/SuppliedTangents fixtures use the same plane and strong
green normal-map component, with independent tangent sources. Their surface
frames and rendered environment-lit pixels match exactly. The corrected Khronos
preview shows convex generated details and a consistent reflected horizon. This
check is stronger than the original orthogonality and nonempty-image assertions.

## Incremental rendering cost

The headset was awake/worn during this session. No overlay demo was running;
`environment-regression --benchmark` opens no OpenVR session or overlays and
captures no controller input. This compares the extra lighting feature, not two
renderers with equivalent appearance. It does not measure compositor FPS.

One and sixteen static ToyCars use full expanded PBR geometry, the general PBR
program, fixed transforms/camera, one 768×768 offscreen target and alternating
eyes. Each stage warms 30 frames and measures 120 in off/on/on/off order. The
same 128-pixel HDR cube, eight roughness levels and 128×128 BRDF lookup are already
uploaded in both modes. CPU baking/convolution is excluded because it is an
offline tool, not runtime work. Estimated shared GPU storage is **1,114,096 bytes**
(about 1.06 MiB), including conservative RGB16F padding. All GL-error and
GPU-disjoint checks passed.

The table averages the two measured stage means for each setting. Raw means are
retained in [samples.json](environment/samples.json).

| Full-detail objects | GPU ms off → on | CPU enqueue ms off → on | Synchronized offscreen frame ms off → on |
| --- | ---: | ---: | ---: |
| 1 ToyCar | .7277 → .7578 | .0176 → .0184 | 1.1682 → 1.2022 |
| 16 ToyCars | 10.1873 → 10.7489 | .2438 → .2874 | 12.0631 → 12.2563 |

This is one short ABBA comparison. GPU clocks, scheduling and thermal state are
not locked; it is not a sustained-performance guarantee. It measures the added
cube/LUT/SH work at this coverage, not the cost of every possible environment or
scene. PBR geometry fast paths remain a separate backlog item.

## External HDR conversion and bake

On 2026-10-29, the tools from `555606b` were exercised with the 1K Radiance HDR
version of [Studio Small 01 by Greg Zaal](https://polyhaven.com/a/studio_small_01),
provided by Poly Haven under CC0. Download URL:
`https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/studio_small_01_1k.hdr`.
The source and generated files were kept under `/tmp/sf-real-hdr`, outside Git.

```sh
curl --fail --location --output /tmp/sf-real-hdr/studio_small_01_1k.hdr https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/studio_small_01_1k.hdr
/tmp/sf-environment-venv/bin/python tools/convert-environment.py /tmp/sf-real-hdr/studio_small_01_1k.hdr /tmp/sf-real-hdr/studio_small_01.npy --color-space linear
/tmp/sf-environment-venv/bin/python tools/generate-environment.py /tmp/sf-real-hdr/studio_small_01.sfenv --input /tmp/sf-real-hdr/studio_small_01.npy
```

- Input: 1024×512 RGB, 1,701,440 bytes; decoded linear range
  0.000011920929 through 604, including 7,982 channel values above 1.
- Output: 852,084 bytes; 128-pixel cube, eight roughness levels, 128×128 LUT.
  SH coefficients were finite; cube/LUT values were finite and nonnegative,
  with maximum 591.
- A temporary C++ smoke program linked the actual `src/lighting_environment.cpp`
  on the desktop. `loadLightingEnvironment` accepted the bake and `upload`
  succeeded in a surfaceless EGL GLES3 context. Upload and resource deletion
  produced no GL errors; estimated GPU allocation was 1,114,096 bytes.
- Source SHA-256:
  `a2d308672f077b0872686a439d76dc3ace91274380ae2e5f02c653c3ccbc8407`.
- Bake SHA-256:
  `de2bd1f134a314545ca8c0ef1a64ad7c90ebb66b25349cc1df8410c21d936da0`.

The user subsequently accepted this bake in the headset on 2026-10-29 using
an interactive ToyCar with the existing PBR example, selecting the external bake
from `/tmp/sf-real-hdr/studio_small_01.sfenv`. The requested checks covered grip
or trigger grabbing, rotation, head movement and smooth reflections without
flicker, black patches or missing parts. The user reported “Looks correct.”
This adds headset visual acceptance to conversion, precomputation and native
loading/upload checks; no controlled performance measurement was made for this
environment. The earlier diagnostic-gallery acceptance used the procedural bake.

## Current environment format validation — October 30, 2026

The engine now accepts only `PSENV001` lighting data, and the offline generator
requires a `.psenv` output path. The obsolete bundled filename alias is removed.
Paths in the earlier measurements above record the format used at that time.

The native `environment-regression` target passed on Steam Frame, including
explicit rejection of the obsolete header, GLES illumination and stereo views,
transactional API/restore failure checks, and shared texture cleanup. The normal
`pocketspace` target rebuilt successfully. Host bake and HDR/EXR/PNG conversion
checks passed, including CLI generation of `PSENV001` and rejection of an
obsolete output filename before creating files. No interactive demo or new
performance comparison was needed for this format cleanup.
