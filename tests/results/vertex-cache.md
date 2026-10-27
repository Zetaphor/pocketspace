# Static GPU mesh cache/fetch optimization

`loadModel(path, eyePixels, {optimizeVertexCache:true})` opts an asset into
meshoptimizer 1.0 triangle-cache optimization followed by vertex-fetch packing.
It runs during GPU upload, preserving exact vertex/UV data and complete oriented
triangles. Opaque/masked indexed static geometry qualifies; blended primitives,
animated/skinned assets, incomplete triangle tails and non-indexed streams retain
their original order. CPU picking, collision and deformation streams stay intact.
The default remains false because coplanar overlapping triangles can depend on
their original order. See the [API contract](../../docs/javascript-api.md#shared-model-geometry).

## Matched native comparisons

On Frame, `tests/compare-vertex-cache.py` alternated one instrumented executable
with the option off/on, reversing pair and case order over two rounds. Each run
lasted 16 seconds and retained eleven FPS samples after five seconds and ten
GPU profile windows after six seconds. Bodies continuously moved and rotated
using [the fast-motion workload](fast-motion.md). The scene contained 64 ToyCars
and 64 Avocados (7,015,552 triangles before stereo/occlusion passes).

| Policy / round | FPS off | FPS on | GPU query ms off | GPU query ms on |
| --- | ---: | ---: | ---: | ---: |
| Adaptive / 0 | 18.45 | 24.74 | 43.73 | 30.40 |
| Adaptive / 1 | 18.44 | 24.58 | 43.17 | 30.09 |
| Fixed / 0 | 23.94 | 30.59 | 32.41 | 23.86 |
| Fixed / 1 | 24.00 | 25.65 | 31.89 | 28.65 |

All four pairs matched median GPU storage estimates and peak grouped buffer
allocations. Adaptive cases used 423.02 MiB median estimated GPU storage with
418 MiB peak grouped buffers; fixed cases used 473.02 MiB and 468 MiB respectively.
The comparison rejects storage mismatches. These gains did not come from a lower
allocation resolution. GPU elapsed-query time fell 10–30%; FPS increased 7–34%
in the paired runs. The fixed-case spread shows that the size of the gain varies.
GPU elapsed queries include device scheduling and do not establish exclusive GPU
occupancy; headset pacing/power and thermal state were not independently locked.
The detailed scene remains well below a headset frame-rate target.

The [raw matched summary](vertex-cache-frame.json) records every run. A separate
matched 128-Avocado pair found effectively unchanged throughput (75.12 versus
74.84 FPS), so no general lightweight-model gain is claimed; see its
[raw summary](vertex-cache-light-frame.json). A 12-second run of the final normal
engine, with instrumentation compiled out, reached 28.83 median FPS for the fixed
detailed scene and released all grouped buffers; see the
[production check](vertex-cache-production-frame.json).

## Correctness and lifecycle

- 162 stereo comparisons were byte-identical across Box, Avocado and ToyCar,
  three object rotations, three viewpoints, both eyes, and opaque, masked-hole
  and blended materials, including floor/wall occlusion.
- GPU buffer readback verifies exact vertex/UV values and preservation of complete
  oriented triangle multisets after vertex repacking. Blended and deformation
  cases retain their original index order. Existing 16/32-bit upload, expanded
  draw fallback, UV seam and cleanup checks also passed.
- An incomplete indexed triangle tail bypasses optimization, avoiding an invalid
  call into a library that requires complete triples.
- API/example checks cover boolean validation, invalid options, throwing getters,
  cached identity and explicit-setting conflicts. The grouped stress example
  explicitly enables the option for its static mesh assets.
- Physics API, script rollback/reload, panel submission, grouped planner/budget,
  stereo occlusion and bounded native regroup lifecycle checks passed. Normal
  shutdown released all grouped buffer charges. No persistent demo remains.
- A budget-constrained shrink now keeps existing sharper buffers unless its full
  requested replacement fits. Partial admission remains a growth fallback. A
  rejected earlier lightweight comparison had different allocation sizes; those
  FPS values are excluded from the matched results above.

A software 32-entry LRU diagnostic reduced ToyCar vertex misses from 267,541 to
79,747 per complete model. This is a locality diagnostic, not a hardware shader
invocation count. [Validation output](vertex-cache-validation-frame.txt) retains
per-primitive counts and pixel-check results.

An earlier CPU-computed clip-matrix prototype was not adopted: detailed-model
GPU query medians were essentially unchanged (31.64 versus 31.53 ms fixed,
42.57 versus 42.55 ms adaptive). Its raster coverage matched, but it supplied no
repeatable throughput benefit. Production keeps the existing vertex shader.

Raw logs remain on Frame under `/tmp/sf-cache-validated-comparison`,
`/tmp/sf-cache-validated-light`, and `/tmp/sf-cache-production-check`; final native
reload output is `/tmp/sf-cache-delivery-lifecycle.log`. Earlier exploratory
triangle-only and mismatched-allocation runs are not the retained comparison.
The user accepted the interactive headset visual check on 2026-10-27 with mesh
optimization enabled: boxes/models, wide/compact movement, grabbing/rotation and
head-motion occlusion were reported as good. The user subsequently accepted
128 continuously moving Avocados with mesh optimization, one fixed group, and a
stats panel. Over approximately 72 seconds, 63 late samples had median 71.96 FPS
and 466.87 MiB estimated GPU allocation; all retained 128 objects. Grouped buffer
peak was 468 MiB, and shutdown released all grouped charges. The raw headset log
is `/tmp/sf-fast-models-headset.log` on Frame. The demo is stopped.
The user also accepted the same continuous-motion Avocado scene with automatic
grouping enabled on 2026-10-27. Approximately 40 seconds produced 33 late samples
with median 71.98 FPS and 416.87 MiB estimated GPU allocation, retaining all 128
objects. Grouped buffer peak was 504 MiB, below the 512 MiB budget; shutdown
released all grouped charges. Budget deferrals occurred and retained existing
tiles/buffers. The raw log is `/tmp/sf-fast-adaptive-headset.log` on Frame, and
the demo is stopped.

The user subsequently accepted an adaptive continuous-motion scene with 16
ToyCars and 16 Avocados and mesh optimization enabled. Approximately 59 seconds
produced 54 late samples, median 58.34 FPS and 449.88 MiB estimated GPU allocation,
retaining all 32 objects. Grouped buffer peak was 498 MiB, and shutdown released
all grouped charges. The raw log is `/tmp/sf-detailed-motion-headset.log` on Frame;
the demo is stopped. This is visual acceptance at 32 detailed models, not a
headset-rate performance claim. A subsequent
[five-minute motion/reload run](motion-soak.md) completed its native checks.
The 128-object detailed headset mix and multi-hour memory behavior remain outside
that validation scope.
