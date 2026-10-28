# Multi-part model batching investigation

The combined multi-material stream prototype is **not adopted**. It preserves
exact pixels and reduces draw counts, but materially slows the mixed moving-model
workload. Production keeps default-on ordered instancing for eligible static
single-primitive opaque/masked models; multi-part assets retain original draws.
No new script API or production rendering mode is introduced.

## Approach and correctness

The test-only path concatenates each asset's uploaded GPU vertices and indices
in primitive order, adding a per-vertex material slot and using 32-bit combined
indices. Original triangle/index order is retained, including vertex-cache
optimization selected by the fixture. Complete instances retain scene order;
assets are never sorted. Up to eight opaque/masked primitives use their original
texture objects, dimensions, sampler filtering, material factors and sheen.
Explicit texture gradients are computed before material selection to preserve
mipmap sampling through shader branches. Material uniform locations are cached.
The original model buffers remain allocated alongside the combined copy.

Consecutive eligible copies batch together; isolated multi-part objects combine
their primitives into one draw. Interleaved assets therefore retain their
ownership order. Blended, animated, skinned, morph and CPU-deformed geometry falls
back. Instance attributes and material-slot attributes are disabled after draws;
reset releases the combined buffers and shader as well as instance resources.

The excluded `multipart-instancing-pixels` target exercises the actual renderer:

- 483 exact stereo/group/material/rotation/near-plane comparisons, including
  ToyCar, mixed opaque/masked primitives, original textures and checker holes.
- A separate coplanar material-order oracle: reversing two coincident primitives
  changes the reference pixels; singleton and repeated combined draws preserve
  the original result. A mixed blended asset retains original draws.
- Nonindexed/32-bit geometry, deformation fallback, the 256-instance capacity,
  intervening asset barriers, attribute state and buffer release.

The production `instancing-pixels` target passes its expanded 483-view suite with
multi-part assets still using the original renderer. The production binary does
not contain `SF_GPU_MULTIPART_INSTANCING`.

## Matched Frame measurements

Frame runs use mesh-cache optimization, the normal common projection plane,
full geometry and the existing reference resolution. Each case has two 16-second
pairs with reversed candidate order. GPU timing discards six seconds of warmup;
FPS medians use the fast-motion runner's late samples. GPU errors, fence failures,
disjoint samples and mismatched owned allocations invalidate comparisons.
Both original and prototype paths retain the combined copy for matched storage;
this does not measure the prototype's additional memory against normal production.
Grouped storage peaks are 468 MiB for all retained runs, below the 512 MiB budget.

| Scene | Pair | Original FPS | Combined FPS | Original GPU ms | Combined GPU ms |
|---|---:|---:|---:|---:|---:|
| 32-cars-fixed | 1 | 49.71 | 54.73 | 15.90 | 15.73 |
| 32-cars-fixed | 2 | 48.39 | 54.71 | 15.87 | 15.87 |
| 128-models-fixed | 1 | 32.68 | 26.05 | 22.25 | 28.41 |
| 128-models-fixed | 2 | 31.38 | 25.96 | 23.68 | 28.47 |

The repeated-car case reduces 192 stereo primitive draws to two batches, saving
190 draws per frame. The mixed case reduces 512 draws to 256, saving 256 draws,
without sorting the interleaved assets. Triangle detail is unchanged. These draw
savings do not imply lower GPU time; the mixed case demonstrates the opposite.
This design does not justify broad multi-part instancing or a new default.
A narrower repeated-only design would need its own measurements and memory policy.

## Artifacts and reproduction

- [Final matched results](multipart-instancing-frame.json), including separate
  preliminary timings before cached material locations and explicit gradients.
- [Pixel validation and tested source/binary hashes](multipart-instancing-validation-frame.txt).
- [Commands and supported test flags](../README.md#multi-part-instancing-experiment).

Raw Frame logs remain under `/tmp/sf-multipart-final` and the separate preliminary
`/tmp/sf-multipart-comparison`. Validation logs are `/tmp/sf-multipart-pixels.log`
and `/tmp/sf-multipart-production-pixels.log`. The code base is `f3c0de5` plus
this investigation; the retained hashes identify the measured implementation.
The comparison runner terminates every engine process after each bounded run.
