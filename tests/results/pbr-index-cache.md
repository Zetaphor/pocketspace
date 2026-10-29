# Cache-ordered PBR indexing

Measured on Steam Frame on 2026-10-29 with the headset awake and worn.
The prototype remains excluded from production; this comparison establishes a
candidate for production/LOD integration, rather than changing engine defaults.

## Verified implementation

The excluded `pbr-index-regression` now explicitly sets `optimizeVertexCache=true`
for mode 2. Mode 0 is expanded geometry and mode 1 is plain full-input indexing;
both keep the flag false. Every timing sample records the flag. Reconstructed
ToyCar shader-input streams must show changed triangle order for opaque/mask and
unchanged order for blend. All oriented triangles and their complete 14-float
position/UV/normal/tangent/material-UV inputs must remain bit-identical as a
multiset. Animated, skinned and morph uploads retain expanded geometry.

The older fixture switched alpha modes after upload. Once cache ordering was
actually enabled, that incorrectly applied reordered opaque uploads to blend
cases and failed image equality. Alpha mode is now assigned before upload, so
blend fixtures exercise the intended order-preserving path. The alpha-test model
was added to the fixture inventory.

The final check passed **864 exact stereo/alpha/overlap images** across twelve
models, including member-color and external-depth views, mirrored and normal-map
fixtures and animation fallbacks. It checked 270 indexed primitive uploads,
16/32-bit index boundaries, reconstructed shader inputs and buffer deletion.
The complete check passed again before the repeat measurements. This is
automated correctness evidence; no interactive headset scene was launched and
no user visual acceptance is claimed.

## Matched performance

One/sixteen full-detail ToyCars, fixed camera/transforms, studio environment,
768-square offscreen eye target and alternating eyes. All three assets and the
color shader were initialized before timing. LOD and instancing were disabled
in all modes, retaining identical geometry and material quality. Each comparison
runs expanded / variant / variant / expanded, with 30 warmup and 120 measured
frames per stage. GPU timer queries, CPU enqueue and synchronized wall time are
recorded. Every frame asserts zero external-depth draws and matching color work:
3 draws / 108,936 triangles for one car, 48 / 1,742,976 for sixteen. GL-error and
GPU-disjoint checks passed. The new production depth shader therefore does not
explain these color-only gains.

Values below average the two stage means in the first run:

| Objects | Variant | GPU expanded → variant (ms) | CPU enqueue (ms) | Synchronized wall (ms) |
| ---: | --- | --- | --- | --- |
| 1 | Plain indexing | 0.765 → 1.066 | 0.020 → 0.021 | 1.357 → 1.718 |
| 1 | Cache/fetch ordered | 0.764 → 0.388 | 0.019 → 0.020 | 1.323 → 0.924 |
| 16 | Plain indexing | 10.184 → 14.128 | 0.313 → 0.323 | 11.917 → 15.860 |
| 16 | Cache/fetch ordered | 10.379 → 3.517 | 0.299 → 0.273 | 11.838 → 5.388 |

The repeat measured cache-ordered GPU time of 0.721 → 0.418 ms for one car
and 11.059 → 3.616 ms for sixteen; synchronized wall time was 1.311 → 0.905
and 12.649 → 5.519 ms respectively. The sixteen-car GPU reduction was about
66–67% across the two runs.

Per shared asset, geometry VBO/index storage is 18,301,248 bytes expanded versus
5,550,662 indexed (about 70% less), with or without cache ordering. Textures,
environment storage and retained CPU arrays are unchanged and excluded. Loading
peak memory and upload latency were not measured.

Raw stages: [first run](pbr-index/cache-enabled.json) and
[repeat](pbr-index/cache-enabled-repeat.json). These replace the unmeasured
cache-enabled conclusion in the [initial investigation](pbr-index.md), whose
original samples remain preserved.

## Decision and limits

Cache/fetch ordering warrants production integration; plain indexing still
regresses this workload. Integrate with automatic PBR LOD, geometry accounting,
asset cleanup and API policy before enabling a default. Then measure normal-engine
mixed scenes, including external occluders, and obtain interactive acceptance.
The test-only path already preserves exact external-depth pixels but its depth
throughput is not measured here.

GPU clocks, thermals and scheduling were not locked. These are short offscreen
rendering costs, not compositor FPS or a sustained-performance guarantee. The
large relative gain does not identify its hardware cause or establish results
for other meshes, material mixes or active LOD levels. No texture-resolution or
geometry-quality reduction was used.
