# Spatial occluder clusters

The 4,096-triangle cluster candidate preserves checked geometry and pixels but
does not establish a useful net throughput gain. It is **not enabled in
production**. Original color geometry/order, engine settings, API behavior, and
deployed production rendering remain unchanged.

## Candidate and controls

Static opaque/masked primitives are spatially partitioned by triangle centroids
into clusters of at most 4,096 triangles. Additional index buffers reference
the existing GPU position/UV buffers; no vertex is simplified or removed.
Triangle orientation and duplicates are preserved, verified by reading back
uploaded indices and comparing complete oriented triangle multisets. Only
nonmember depth-occluder draws use the reordered buffers. Color draws retain
their original index stream and ordering.

Three modes separate layout changes from rejection:

- `original`: original depth draws; extra cluster buffers still allocated.
- `full`: reordered cluster indices, one full range per primitive, no culling.
- `cull`: padded bounds reject off-tile/deeper clusters; contiguous surviving
  ranges merge into one draw. Invalid or near-plane projections retain draws.

All modes allocate identical experimental index storage: 657,708 bytes for
mixed ToyCar/Avocado assets and 4,092 bytes for Avocados alone. These are additional
to the normal engine GPU allocation estimate, not dedicated VRAM measurements.
Asset buffers are shared across instances and released on reset/shutdown.
Animated, skinned, morphing, and per-instance deformed geometry retains the
original path. Asset caches reset on scene reload. Incomplete/invalid streams
retain the source draw path.

## Correctness validation

`cluster-visibility-pixels` passes 216 exact GLES views in each of full and
culled modes (432 comparisons to original output), covering stereo, rotated
models, group membership, opaque/masked/blended materials, and near-plane views.
A dense separated-island fixture puts all islands inside a single material
primitive and requires actual rejection, demonstrating finer selection than
the previous material-primitive experiment.

Buffer checks exercise indexed/nonindexed 16/32-bit streams, readback triangle
preservation, invalid GPU positions, deforming-asset/per-instance fallback,
cache reset, and actual GL buffer release. The existing GLES occlusion and
example regressions pass. Production builds successfully and contains no
`SF_GPU_CLUSTER_MODE` option. These are bounded checks, not proof for every
possible asset or new headset visual acceptance.

## Controlled native results

Frame completed eighteen valid 16-second runs on base `98c8997` plus this
experiment: two rounds across three scenes and three modes, reversing mode/case
orders. Mesh cache optimization stays enabled; primitive rejection, shader
specialization, and per-pass timing experiments stay disabled. GPU medians
exclude six seconds of warmup; FPS uses the motion runner's five-second warmup.
All three-mode sets match median estimated GPU storage, peak grouped charges,
and extra index bytes. GPU error checks include warmup samples.

The two-group fixture contains 64 models in each fixed group, with 64 ToyCars
and 64 Avocados overall. Models rotate and move rapidly through overlapping
regions, with stable membership and no automatic regrouping. Fixed controls
have one group and therefore no nonmember occluder draws.

| Scene | Round | FPS original / full / cull | GPU ms original / full / cull |
| --- | ---: | --- | --- |
| 128 mixed, fixed | 1 | 29.75 / 31.13 / 31.12 | 24.53 / 24.16 / 23.20 |
| 128 mixed, fixed | 2 | 30.57 / 31.30 / 29.76 | 24.09 / 23.93 / 24.39 |
| 128 mixed, two groups | 1 | 28.83 / 29.60 / 28.95 | 25.67 / 24.39 / 24.72 |
| 128 mixed, two groups | 2 | 29.45 / 27.20 / 28.64 | 25.37 / 26.15 / 24.96 |
| 128 Avocados, fixed | 1 | 75.04 / 74.65 / 74.02 | 8.43 / 8.59 / 9.44 |
| 128 Avocados, fixed | 2 | 77.88 / 80.75 / 72.94 | 8.68 / 8.14 / 9.10 |

In the two-group cull runs, medians show 267,363–297,740 skipped submitted
triangles per stereo frame, from about 4.25–4.40 million eligible occluder
triangles. Unchanged color draws submit another 14,031,104 triangles. Skips are
therefore only approximately 1.5–1.6% of combined submitted geometry. Bounds
preparation takes about 0.83–0.87 ms/frame, versus 0.006–0.007 ms/frame for
original/full modes. Submitted occluder ranges average about 156–162 per frame;
contiguous merging limits extra draw calls. Counters describe submitted work,
not hardware vertex invocation counts.

Compared with the original renderer, two-group culling is about 0.4% faster in
one round and 2.7% slower in the other. Fixed controls vary despite having no
eligible occluders, so their differences cannot be attributed to cluster skips.
Clocks, scheduling, and pacing are not isolated by these short comparisons.
Full reordered drawing also shows no consistent net benefit. These results do
not justify adopting the candidate or claim to rule out every cluster design.

Preparation includes cached asset setup and per-frame world/stereo projections,
before whole-render GPU timing starts. FPS includes this cost. Cluster tests and
range submission occur inside the timed render loop. Between-round view/allocation
differences prevent treating rounds as equal-quality FPS comparisons; storage
matches within each three-mode set. Two-group peak grouped charges are 380 MiB
in round one and 342 MiB in round two; fixed controls peak at 468 MiB. All valid
runs remain within the 512 MiB budget, retain 128 objects, report no checked
GPU/native/surface/panel errors, and release grouped charges on shutdown.

## Rejected adaptive run and reproduction

An initial automatic-grouping trio was rejected: original chose a different
tile/allocation history (500 MiB peak) from full/cull (418 MiB). Its FPS values
are not evidence for a cluster gain. The experiment was repeated using the
controlled two-group fixture rather than relaxing the matching checks. The
[initial data](cluster-visibility-rejected-frame.json) also includes the first
fixed-group control trio, which is not used in the table above.

See [instructions](../README.md#spatial-occluder-cluster-experiment) and the
[controlled summary](cluster-visibility-frame.json). Logs remain on Frame under
`/tmp/sf-cluster-controlled` and `/tmp/sf-cluster-comparison`; final correctness
output is `/tmp/sf-cluster-validation.log`. All demos are stopped. Keep clusters
deferred unless a workload offers larger rejection savings or cheaper bounds
processing sufficient to demonstrate a repeatable net benefit.
