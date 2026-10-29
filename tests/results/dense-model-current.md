# Current dense-model pipeline investigation

Measured on Steam Frame on **2026-10-29**, based on `590ce5c` plus the isolated
profiling changes in this report. Production defaults remain unchanged. This is
GPU attribution, a candidate comparison, and a separate bounded headset visual
check are recorded below.

## Workload and controls

The existing `fast-motion.js` scene continuously moves/rotates 128 mixed models
(64 ToyCars, 64 Avocados), or 32 ToyCars. Objects are kinematic, use 768 reference
eye pixels, and run with mesh-cache optimization, automatic LOD and ordered
instancing enabled. Interleaved multipart ToyCars remain ordinary draws under the
production eligibility rules; enabling instancing does not combine them.

The headset was awake and worn for profiling. CPU governor reported `schedutil`;
no power policy was changed. Runs are bounded to 16 seconds. FPS retains eleven
samples after five seconds; GPU medians retain ten windows after six seconds.
These are short workload measurements, not sustained thermal or compositor
latency claims. Live head pose, exact motion phase, power and device scheduling
are not locked.

`profile-render-passes.py --instancing` refreshes the historical attribution with
current policies. It clears inherited `SF_GPU_*` overrides and now rejects
storage mismatches rather than merely reporting them. The first fixed and
adaptive pairs match storage. Round two's adaptive pair has different allocation
sizes and is rejected; the runner stops before another fixed pair. Its rejected
rows remain explicitly marked `matchingStorage: false` in the evidence.

## Current pass attribution

| Matched first-round case | Whole-render GPU ms | Sampled occluder GPU ms | Sampled opaque GPU ms | Occluder share |
| --- | ---: | ---: | ---: | ---: |
| 128 mixed, fixed group | 16.26 | 0.002 | 15.85 | 0.015% |
| 128 mixed, adaptive groups | 17.49 | 11.14 | 10.18 | 52.1% |

Fixed grouping submits 512 opaque draws and 3,694,336 triangles per stereo frame,
with no external occluder draws. Adaptive sampled frames submit median 102.5
occluder draws/749,389 occluder triangles in addition to the same member triangles.
Render-thread CPU time in the sampled callbacks totals about 0.19 ms fixed and
0.26 ms adaptive. Draw/triangle counters describe submission, not visible pixels.

Whole-render and per-pass timings come from separate runs; their medians cannot
be subtracted as exact overhead or expected savings. GPU elapsed queries include
scheduling, and pass timings exclude clears and framebuffer setup. Fixed and
adaptive buffers have different allocations, so their FPS is not a quality-matched
comparison of grouping strategies. The rejected adaptive pair is not used above.

Repeated occluders remain significant with adaptive groups. They are not the only
cost: the fixed case has none and still exceeds a headset-rate rendering budget.
The measurements do not justify re-enabling the rejected shared-depth prototype.

## Static-only ordinary vertex shader candidate

The general model shader now supports skinning and has a uniform branch between
`skinPoint()` and ordinary positions. The test-only candidate replaces that
expression with `p`; dead-code elimination can remove skin state and palette
sampling from this program. Geometry, projection, texture/material code, depth
rules, scene order, LOD policy, buffers and compositor synchronization are
unchanged. The instancing shader remains unchanged in both modes.

Only the excluded `gpu-sync-experiment` reads `SF_GPU_STATIC_VERTEX=0/1`. It
rejects skinned assets in this mode instead of silently rendering them incorrectly.
This whole-program experimental switch is not suitable for mixed production
scenes; any adoption needs explicit routing between ordinary and GPU-skin programs.

`static-vertex-pixels` passed **216 byte-identical stereo comparisons** on Frame:
Box, Avocado, ToyCar, three viewpoints, both eyes, opaque/masked/blended states,
checker-mask holes, and separate/grouped surfaces with external occlusion. It
also checks that the linked specialized program has no skin-enable/palette
uniform. This establishes those static views, not mixed animated backend support.

The first two repetitions used the same pair order per case because reversing
both the case order and its index canceled the intended pair reversal. Those
runs are retained as repeated measurements, not described as counterbalanced.
The runner now keys parity to the stable case index and supports `--reverse`;
an additional explicitly reversed pair per case checks the order concern.

## Candidate measurements and decision

| Case | Pair | Ordinary FPS | Static-only FPS | Ordinary GPU ms | Static-only GPU ms |
| --- | --- | ---: | ---: | ---: | ---: |
| 128-models-fixed | repeat 1 | 33.65 | 38.17 | 19.72 | 16.92 |
| 32-cars-fixed | repeat 1 | 61.17 | 65.27 | 10.34 | 8.74 |
| 32-cars-fixed | repeat 2 | 60.99 | 63.29 | 10.42 | 9.41 |
| 128-models-fixed | repeat 2 | 37.87 | 39.39 | 16.56 | 16.53 |
| 128-models-fixed | reversed 1 | 37.06 | 39.39 | 16.51 | 16.63 |
| 32-cars-fixed | reversed 1 | 64.18 | 64.98 | 8.98 | 8.81 |

All candidate pairs match estimated GPU allocations and grouped peaks. The dense
case estimates 473.49 MiB with a 468 MiB grouped peak; 32 cars estimates 457.48 MiB
with the same grouped peak. No geometry/resolution or owned-storage reduction is
claimed; driver shader metadata is outside the allocation estimate.

FPS improves in each bounded pair, but magnitude varies: approximately 4–13% for
128 mixed models and 1–7% for 32 cars. Dense-case GPU query medians are effectively
unchanged in the later two pairs, so this does not establish a repeatable large
GPU reduction or solve the detailed-scene bottleneck. Do not pool the first cold
baseline with later runs to claim a larger gain.

**Decision:** retain this as a promising, modest ordinary-draw specialization
prototype, not a production default. The 16-car LOD visual check is accepted;
evaluate mixed CPU/GPU animation routing and lifecycle before production
integration. General occluder optimization remains open; do not
re-enable the rejected shared-depth, cluster, or material-specialization paths
based on these results.

## Evidence and limits

- [Pass attribution, including rejected rows](dense-model-current/pass-summary-frame.json).
- [Repeated candidate pairs](dense-model-current/static-repeat-summary-frame.json).
- [Explicitly reversed pairs](dense-model-current/static-reversed-summary-frame.json).
- [Frame validation and binary identity](dense-model-current/validation-frame.txt).

Native logs remain on Frame in `/tmp/sf-dense-current-profile`,
`/tmp/sf-static-vertex-comparison`, and `/tmp/sf-static-vertex-reversed`.
The normal executable rebuilt byte-identically to its pre-experiment binary;
there is no production shader/default change. The skinned-workload guard exits
with code 13 and releases grouped charges to zero.

Every completed timing run retained its expected object count, checked clean
GL/disjoint/fence/native status, exited normally, and released grouped budget
charges to zero.

## Headset visual acceptance

On **2026-10-29**, the user confirmed “Everything looks correct” with the
static-only ordinary vertex shader running `scripts/lod.js` (16 ToyCars). The
requested checks covered head movement, approach/distance and LOD transitions,
grip/trigger grabbing, rotation, and overlaps. This validates that static scene;
it does not establish mixed skeletal/rigid/morph program routing or production
lifecycle behavior. The prototype process was stopped and its exit verified.
