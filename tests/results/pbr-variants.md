# Bounded PBR shader specialization

Steam Frame ARM64, 2026-10-29. The user confirmed active-headset profiling
readiness. Production behavior and defaults are unchanged during this prototype.

## Candidate and ownership

Compile material-map presence, material-unlit/double-sided flags, alpha mode,
environment enable and per-object filtered light count into constant GLSL values.
All BRDF expressions, textures, sampling, geometry, projections, ordering and
range/cone behavior are retained. Depth passes use the existing depth program.
Each candidate draw resolves the current material/light key before binding a
program and rebuilding view/object/material/light uniforms. No asset or scene
pointers are stored in the cache.

The test-only cache owns at most 16 additional program slots. Unseen keys after
that bound, or remembered compile failures, use general PBR. Cache clear deletes
owned programs and failure records. Program count excludes existing general,
depth and model programs. Program-binary lengths are reported, but are not total
driver RAM/VRAM storage; driver compiler/cache memory is outside this accounting.

The excluded `pbr-variants-experiment` and `gpu-sync-experiment` alone define
`SF_PBR_VARIANT_EXPERIMENT`. Only the latter reads `SF_GPU_PBR_VARIANTS=0/1`.
Normal builds have no shader-variant runtime option. Compilation remains
synchronous on first use: this is a prototype, not a production warm-up policy.

## Automated correctness

Both offscreen runs pass **1,024** comparisons with exact alpha and a maximum
one-byte RGB difference (5,168 changed RGB bytes). Cases include ToyCar,
NormalTangentTest, morphing SurfaceFrames and AlphaBlendModeTest, zero/one/four/
eight mixed lights, attached spots, four Euler poses, full/reduced geometry,
stereo, separate/grouped targets, environment on/off and material unlit state.
Each normal comparison asserts no fallback, so specialized paths are exercised.

Separate checks fill the 16-slot cache, exercise overflow, remember injected
compile rejection, compare fallback pixels exactly, clear the cache and verify
program deletion. These are native GLES checks, not user visual acceptance.
The prototype subsequently passed interactive acceptance; see below.

## Offscreen matched measurements

Reference/candidate ABBA stages share one renderer, model buffers, framebuffer,
projection and deterministic rotating-pose/eye sequence. Workload: 32 cars in an
8×4 grid, .16 scale/.22-meter spacing, PBR cache/fetch optimization, directional
intensity .3/ambient .04, no HDR, and zero/eight mixed lights. Geometry is fixed
full/LOD-2; viewports are 768/2048 with the same 4096×2048 backing allocation.
Each stage has 30 warmup and 90 measured frames, alternating eyes. GPU disjoint
and GL status must remain clean. Wall time includes rendering and `glFinish`.

Both runs retain 12 specialized programs, zero fallback draws and 336,500 bytes
of reported program binaries. Total compile time is 48.59 ms in the first run
and 9.84 ms in the second, illustrating driver-cache sensitivity. These compile
costs occur during warmup in the fixed-position offscreen profile. They must not
be mistaken for acceptable live-scene first-use latency.

The table compares each run's mean of two reference and two candidate stages.
Positive percentages mean reduced time; negative mean regression. These are
short-run observations, not confidence intervals or headset FPS predictions.

| Lights | LOD | Pixels | GPU reduction %, runs 1 / 2 | Wall reduction %, runs 1 / 2 |
| --- | --- | --- | --- | --- |
| 0 | Full | 768 | 9.2 / 5.5 | 3.9 / 1.0 |
| 0 | Full | 2048 | 11.1 / 26.0 | 8.8 / 16.4 |
| 0 | 2 | 768 | 5.5 / 7.5 | -6.8 / -.6 |
| 0 | 2 | 2048 | 29.6 / 20.6 | 17.5 / 12.6 |
| 8 | Full | 768 | 17.9 / 11.7 | 7.4 / 3.5 |
| 8 | Full | 2048 | 28.0 / 27.7 | 21.5 / 18.3 |
| 8 | 2 | 768 | 13.5 / 14.5 | 5.9 / 7.4 |
| 8 | 2 | 2048 | 36.4 / 35.1 | 26.5 / 25.3 |

Unlike the arithmetic-reuse candidates, this demonstrates repeatable substantial
GPU and wall gains in the larger lit workload. The smaller zero-light/LOD-2 case
regresses in wall time, so unconditional routing is not established.
[Offscreen stage evidence](pbr-variants/offscreen-frame.json).

## Whole-engine comparison

`compare-pbr-variants.py` runs the same candidate through the excluded
`gpu-sync-experiment` host: 32 moving full-detail cars in one fixed group, 768
reference eye pixels, normal grouped allocations/compositor submission, cache/fetch
optimization and ordered instancing enabled. Actual grouped buffers retain their
normal dynamic allocation policy. Both repetitions compare zero/eight lights;
the second reverses count and candidate/reference order. Runs last 16 seconds;
GPU/work samples start at six seconds. No interaction or pointer capture is enabled.

All pairs match geometry and owned storage: 32 full-detail objects, 3,485,952
scene triangles, 6,971,904 stereo member triangles, 192 opaque draws, no external
occluders, 488.29 MiB estimated owned GPU storage and a 468 MiB grouped peak.
Each run exits normally and grouped charges return to zero.

| Lights | Repeat | FPS reference / variant | GPU ms reference / variant | Render/completion ms reference / variant |
| --- | --- | --- | --- | --- |
| 0 | 1 | 35.97 / 36.01 | 19.99 / 18.82 | 27.30 / 27.31 |
| 0 | 2 | 26.31 / 35.99 | 29.37 / 18.77 | 37.19 / 27.35 |
| 8 | 1 | 28.51 / 34.56 | 28.37 / 21.25 | 34.58 / 28.29 |
| 8 | 2 | 28.26 / 28.48 | 28.95 / 26.21 | 35.06 / 33.39 |

Render/completion is the sum of per-window median enqueue and wait timings,
excluding compositor submission/pacing; it is not CPU execution alone.
Eight-light GPU times improve about 25% and 9%, with render/completion reductions
of about 18% and 5%. FPS magnitude varies substantially. The second zero-light
reference is much slower than the first despite matched geometry/storage; do not
pool it into a headline zero-light gain. Live view, workload phase, scheduling,
power and thermal state are not locked. These bounded results support further
integration, not guaranteed FPS or a universal production default.

Zero-light runs retain three variants (71,603 binary bytes); eight-light runs
retain 15 (441,495 bytes). There are no fallback draws. Total compile times are
2.53/4.23 ms for zero lights and 87.34/9.32 ms for eight lights. Moving objects
can encounter new filtered-count keys after warmup; compile totals do not prove
all compilation was outside measured windows. This first-use work must be
handled before adoption.

[Whole-engine evidence](pbr-variants/live-frame.json). Raw logs remain on Frame
under `/tmp/pbr-variants-live`; offscreen logs are `/tmp/pbr-variants-{1,2}.log`.
The JSON `prepared` flag in offscreen rows means specialization enabled, not
CPU preparation. Whole-engine rows use the `variants` flag.

## Decision

Keep this as a promising **test-only prototype**. Before production integration:
prepare a bounded useful variant set outside steady-state rendering; use general
PBR for unknown/failed keys; validate cold-start/reload/restore and program
cleanup; establish a routing policy that protects lighter workloads; repeat
matched native comparisons and revalidate the production integration in the headset.
Do not enable the experiment by default or describe it as shipped API behavior.

## Verification

Both offscreen processes pass the pixel/cache/fallback/deletion checks and ABBA
profiling. All eight whole-engine runs pass object counts, stable complete stereo
geometry, storage matching, GL/disjoint/fence status, normal termination and zero
grouped charges after shutdown. The normal `pocketspace` target builds, and the
shared GLES occlusion regression passes stereo/alpha/scissor, indexed geometry
and cleanup. Python compilation, documentation lint/site build and whitespace
checks pass. All profiling/regression processes exited; no demo remains running.


## Prototype headset acceptance

On 2026-10-29 the user confirmed “Everything looks correct” in
[the interactive variant scene](../scenes/pbr-variants.js), running through
`gpu-sync-experiment` with specialization enabled. Instructions covered grip/
trigger grabs and rotation, approach/distance and head movement, normal maps,
alpha cutouts and overlaps against blue, eight/four/zero lights, HDR toggles,
and pose reset. This is user-confirmed visual acceptance of the prototype,
separate from automated correctness and measured throughput.

The scene reached the 16-program cap and exercised general-program fallback:
16 retained programs, 533,744 reported binary bytes, 173.13 ms total first-use
compile time and 138,712 fallback draws. These totals describe the whole session,
not individual-frame latency or total driver memory. They reinforce the need
for bounded prewarming and a deliberate routing policy before adoption.
The process completed 11,234 frames, released grouped charges to zero, and was
stopped immediately after verification; its exit was independently confirmed.
Raw log: Frame `/tmp/pbr-variants-demo.log`.
