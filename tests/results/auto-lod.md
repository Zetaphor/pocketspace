# Default automatic model LOD

`loadModel` now defaults to `lod: true`; `lod: false` retains full geometry at all
sizes. The user explicitly requested the default. This implements production
selection on top of the earlier [fixed-detail investigation](lod.md); its timing
results below use the normal production executable, without environment overrides.

## Rendering contract

Full CPU and GPU meshes remain allocated. Eligible indexed opaque static
primitives receive shared half/quarter-target index buffers, with a relative
position/UV metric limited to 0.001 and locked borders. Reductions saving less
than 20% are omitted; identical-count quarter levels reuse the finer half-level
fallback. Small, masked/blended and deforming geometry retains full detail.
Textures, picking and physics geometry remain unchanged.

Each frame selects one level per object after input/animation/physics mutations.
The conservative bounding-sphere projection uses both eyes, current headset
projection/resolution, depth and an off-axis enlargement factor. Both eyes and
all surfaces/color/depth passes use that same level. Nearby and grabbed objects
restore full geometry. Size and near-distance hysteresis prevent threshold
oscillation. Instancing runs require matching asset and selected level.
[The API guide](../../docs/javascript-api.md#automatic-model-lod) documents exact
thresholds, cache rules, fallback and memory accounting.

## Validation

- `lod-api-smoke.js` verifies default-on behavior, explicit opt-out, strict boolean
  types, throwing getters, omitted-option inheritance and explicit cache conflicts.
- `auto-lod-regression` checks hysteresis, near restoration, off-axis bounds and
  invalid near-plane projections. Twenty-six exact image comparisons verify actual
  LOD index draws against expanded references, each instancing level and mixed-level
  barriers with 16/32-bit indices, plus opt-out/material/deformation guards. Full CPU vertices remain unchanged; all generated GL buffers are deleted.
- Existing production occlusion and 483-view instancing regressions pass.
- All 23 final example scripts load successfully, including `scripts/lod.js`.
- Production reloads between 16 ToyCars and empty scenes twice. Both empty phases
  report zero owned GPU bytes; active phases retain stable estimates. Normal exit
  releases all grouped budget charges.
- The normal binary excludes `SF_GPU_MODEL_LOD`; that override still belongs only
  to the separate fixed-detail harness.

These are native/automated checks. Interactive headset acceptance of default LOD
transitions, close inspection and grabbing remains on the roadmap.

## Production Frame performance

Two reversed-order pairs per scene use 16-second normal-engine runs. Mesh-cache
optimization is enabled in both paths; instancing is disabled to isolate LOD.
All runs retain their expected object count, have clean logs and normal shutdown,
and release grouped budget charges. The comparison rejects different grouped
peaks or GPU-estimate differences beyond the recorded LOD index-buffer bytes.

| Scene | Pair | Full FPS | Automatic FPS | Extra index bytes |
|---|---:|---:|---:|---:|
| 32-cars-fixed | 1 | 48.60 | 71.96 | 495,864 |
| 32-cars-fixed | 2 | 48.10 | 71.97 | 495,864 |
| 128-models-fixed | 1 | 33.18 | 46.73 | 495,864 |
| 128-models-fixed | 2 | 32.13 | 43.95 | 495,864 |

ToyCar's extra shared indices total 495,864 bytes (0.473 MiB), regardless of whether
32 or 64 copies are present. Avocado's possible reduction is too small to justify
extra buffers. Grouped peaks are 468 MiB in both paths in every pair. These are
specific workload measurements, not a general frame-rate or quality guarantee;
LOD generation also adds one-time loading work. The controlled fixed-detail
results are separate and are not pooled with these automatic-selection runs.

## Artifacts

- [Production timing and allocation checks](auto-lod-frame.json).
- [Production reload measurements](auto-lod-lifecycle-frame.json).
- [Validation and tested source/binary hashes](auto-lod-validation-frame.txt).
- [Build/run instructions](../README.md#production-automatic-model-lod).

Base `d5c61cc` plus this implementation; retained hashes identify the tested
production binary. Raw Frame outputs remain under `/tmp/sf-auto-lod-comparison`
and `/tmp/sf-auto-lod-lifecycle`, with regression logs prefixed
`/tmp/sf-auto-lod-`. No demo remains running.
