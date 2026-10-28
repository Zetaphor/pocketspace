# Ordered model GPU instancing

Ordered instancing is enabled by default in `loadModel`;
`gpuInstancing: false` opts out. The measurements below predate the default
change and explicitly select each path. It batches consecutive eligible
copies of the same static single-primitive opaque/masked asset within an eye/pass.
It preserves triangle/object order, model detail, UVs, material behavior, reference
resolution, and surface membership. Multi-part models, blended materials,
deforming geometry, incompatible runs and singletons retain original draws.
See the [API contract](../../docs/javascript-api.md#ordered-gpu-instancing) and
[grabbable example](../../scripts/instances.js).

## Correctness and lifetime

The production batching class passes 387 exact GLES image comparisons, plus
additional index/deformation/capacity checks. Coverage includes Box, Avocado,
ToyCar fallback, a textured quad, stereo, separate/combined groups, rotations,
near-plane views, factors/sheen, masked holes, blended fallback, and a rotated
scene anchor. A coplanar textured-quad oracle proves that reversing input order
changes the original image, then verifies that instancing preserves the original
ownership. It does not merely compare indistinguishable overlapping objects.

Further checks exercise nonindexed and 32-bit indexed paths (regular models
cover 16-bit indices), animation/skin/morph policy fallback, actual per-instance
deformed VBO fallback, the 256-instance capacity, and an intervening asset barrier.
They verify restored attribute/divisor/program state and GL buffer release.
No object sorting or multi-part reordering is used to manufacture batches.

API checks run through the real QuickJS loader: strict boolean parsing, getter
exceptions, default-on behavior, explicit opt-out, cached-path inheritance, explicit conflicts and
stored native flags. Existing lifecycle, GLES occlusion and all 22 published
examples pass. Shader/buffer initialization failure retains original draws;
resources are reset on successful scene replacement and destroyed on shutdown.

The instanced vertex source uses the same transform/projection expressions as
the regular shader, with offset/scale/angles supplied by instance attributes.
The fragment shader is unchanged. One reusable streamed buffer holds at most
256 records (7,168 bytes); the GPU estimate includes this buffer when allocated.
Streaming orphans/driver overhead are not measured residency. The allocation is
lazy in production, and assets without eligible batches do not require it.

## Matched Frame measurements of the integrated implementation

Four 16-second profiling runs alternated original and instanced paths over two
rounds of 128 moving Avocados in one fixed group. Both paths request the same
API option and prewarm identical shader/buffer storage in the excluded profiling
target; only its test flag forces baseline submission. Mesh cache optimization
stays enabled. Whole-render GPU samples discard six seconds of warmup; FPS uses
the motion runner's five-second warmup. Buffer estimates and peak grouped storage
match within each pair, including the 7 KiB instance buffer.

| Round | Original FPS | Instanced FPS | Original enqueue ms | Instanced enqueue ms | Original GPU ms | Instanced GPU ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 80.78 | 86.94 | 0.339 | 0.056 | 7.78 | 6.62 |
| 2 | 77.13 | 85.79 | 0.347 | 0.055 | 8.67 | 7.01 |

Visible model submission drops from 256 individual stereo draws to two instanced
draws per frame: 256 submitted instances and 254 saved draw calls. Enqueue wall
time falls about 83–84%; this is draw-loop submission timing, not total process
CPU usage. FPS rises about 7.6–11.2% in these pairs. Triangle count remains
174,592 per stereo frame. Grouped peak storage stays at 468 MiB, within the
512 MiB group-buffer budget, and shutdown returns grouped charges to zero.

All runs retain 128 objects, sufficient valid GPU samples, and no reported
GL/disjoint/fence/native errors, including warmup. GPU elapsed queries include
scheduling effects and do not establish exclusive occupancy or isolate shader
stages. Clocks, pacing, and head/view variation limit these short comparisons;
the measured result is workload-specific, not a general engine FPS guarantee.
Dense multi-part ToyCar scenes do not receive this batching benefit.

## Production and reload validation

Two non-instrumented 12-second production runs compare the real API disabled
and enabled. FPS medians are 76.98 and 86.71 respectively. Peak grouped storage
is 468 MiB in both. The allocation estimate increases by exactly 7,168 bytes,
accounting for the instance buffer rather than changing color/depth density.
This single production pair corroborates the profiling trend, without adding
a separate universal improvement claim or new headset visual acceptance.

One production process also completes four ten-second phases: sixteen instanced
models, empty scene, sixteen instanced models, empty scene. All three script
reloads commit. Late empty-scene samples report **zero owned GPU allocation
estimate** both times, while each active scene retains sixteen objects and
renders after resource recreation. Active-phase buffer sizes differ; this is a
lifecycle check, not a matched-throughput comparison. It does not claim that
driver caches or process RSS return to zero. No persistent demo remains running.

## Prototype evidence and rejected comparison

An earlier prototype showed similar fixed-scene submission savings and a
matching first two-group pair (82.94 to 88.75 FPS). Its reversed two-group pair
was rejected because grouped peaks differed (342 versus 380 MiB). That pair's
FPS change is not optimization evidence. Mixed ToyCar/Avocado controls formed
no batches, as expected from their interleaved multi-part draw order; their small
timing differences are not instancing gains. Two subsequent prototype fixed
pairs reproduced the eligible-scene trend before integration.

Prototype measurements used test-only uniform lookup/resource bookkeeping and
are kept separate from the final implementation. They are not pooled into the
table above. The engine base is `36a4cfd` plus this change; recorded source/binary
hashes identify the tested production build.

## Reproduction and artifacts

See [test instructions](../README.md#ordered-gpu-instancing). The normal engine
uses the API option; `SF_GPU_INSTANCING` and profiling counters exist only in the
excluded `gpu-sync-experiment` executable.

- [Integrated matched timing](instancing-frame.json).
- [Production API off/on measurements](instancing-production-frame.json).
- [Production reload/resource-release summary](instancing-lifecycle-frame.json).
- [Separate prototype and rejected measurements](instancing-prototype-frame.json).
- [Final correctness evidence and source/binary hashes](instancing-validation-frame.txt).

Logs remain on Frame in `/tmp/sf-instancing-api-profile`,
`/tmp/sf-instancing-production-off`, `/tmp/sf-instancing-production-on`, and
`/tmp/sf-instancing-lifecycle`. Final pixels/order/anchor checks are in
`/tmp/sf-instancing-order-check.log`; API/lifecycle checks are in
`/tmp/sf-instancing-api-validation.log`; the final example run is in
`/tmp/sf-instancing-examples-final.log`.
