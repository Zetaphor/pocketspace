# Conservative primitive occluder visibility

The test-only candidate preserves the checked pixels and removes some submitted
occluder triangles, but shows no consistent throughput benefit on the moving
ToyCar/Avocado workload. It is **not enabled in production**. Production retains
its object-level occluder bounds checks.

## Candidate and correctness

The existing renderer can reject an entire nonmember object if it cannot
occlude the target tile. The candidate adds a second check using each material
primitive's own bounds. It caches normalized static bounds on first use,
refreshes transformed/stereo bounds after scene mutations each frame, and clears
the asset cache on successful script reload. It expands angular bounds by at
least two texels before rejection to retain raster edges. Invalid/nonfinite or
near-plane bounds fall back to drawing. Single-primitive assets and animated,
skinned, morphing, or per-instance deformed geometry also retain the baseline
path. Blended geometry continues to be excluded from opaque occluders.

Visible member/color draws are unchanged: group textures already cover their
member objects, so primitive bounds do not eliminate their geometry in these
fixtures. Fixed single-group scenes have no nonmember occluders and are negative
controls. This is not hidden-surface removal within a model.

`primitive-visibility-pixels` checks 216 byte-identical GLES views across ToyCar,
Avocado, and a deliberately separated multi-primitive fixture; opaque, masked,
and blended materials; both eyes; different groups; rotated objects; and four
viewpoints including a near-plane view. It requires actual primitive rejection.
Additional CPU checks cover known off-tile geometry, animated/skinned/morph
fallback, near-plane and invalid bounds, cache reset, and empty scene replacement.
This bounded equivalence evidence does not prove correctness for every asset
or provide new headset visual acceptance.
The existing projected-bounds regression also passes 3,430 stereo comparisons,
the GLES occlusion suite passes, and the production executable builds without
the experimental option.

## Native measurements

Frame completed twelve bounded 16-second runs on engine base `867c1d9` plus this
candidate, using two rounds with reversed case and pair orders. Mesh cache
optimization remains enabled; opaque shader specialization and per-pass queries
are disabled. GPU samples exclude six seconds of warmup; the motion runner's
FPS samples use five seconds. Every pair matches median estimated GPU storage
and peak grouped allocation charges.

| Scene | Round | Baseline FPS | Candidate FPS | Baseline GPU ms | Candidate GPU ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128 mixed, fixed | 1 | 28.59 | 29.35 | 26.31 | 25.10 |
| 128 mixed, fixed | 2 | 30.20 | 28.59 | 24.46 | 25.96 |
| 128 mixed, adaptive | 1 | 27.38 | 23.32 | 28.24 | 31.91 |
| 128 mixed, adaptive | 2 | 23.29 | 23.81 | 32.60 | 31.98 |
| 128 Avocados, fixed | 1 | 80.70 | 72.02 | 7.72 | 7.29 |
| 128 Avocados, fixed | 2 | 78.62 | 72.09 | 8.54 | 7.00 |

Mixed scenes contain 64 ToyCars plus 64 Avocados. Adaptive candidate runs skip
median 11.2–11.6 draws and 435,589–443,284 submitted triangles per stereo frame.
Bounds preparation wall time is about 0.058–0.059 ms/frame. Fixed mixed controls
skip nothing and spend about 0.052–0.055 ms/frame on preparation; single-primitive
Avocados skip nothing and spend about 0.0016 ms/frame. The GPU timer begins after
cache preparation, so preparation cost appears in FPS, not GPU query duration.
These counters count submitted triangles, not hardware shader invocations.

The adaptive pair changes from slower to slightly faster between rounds, while
fixed controls vary despite eliminating no draw work. Light candidate FPS also
falls while GPU duration falls. This demonstrates that short-run FPS and GPU
query variation cannot be attributed solely to the number of skipped triangles;
driver scheduling, clocks and pacing are not isolated by these comparisons.
The measured skips are real, but they do not establish a useful net speedup.

All native cases retain 128 objects, have sufficient valid GPU samples and no
checked GL/disjoint/fence/native errors, and return grouped buffer charges to
zero on shutdown. Fixed scenes peak at 468 MiB grouped storage and adaptive
mixed scenes at 418 MiB, within the 512 MiB budget. All demos are stopped.

## Reproduction and follow-up

See [instructions](../README.md#primitive-occluder-visibility-experiment) and the
[raw summary](primitive-visibility-frame.json). The candidate is selected only
by `SF_GPU_PRIMITIVE_VISIBILITY=1` in the excluded `gpu-sync-experiment` target;
production builds compile out its code, option, caches, and counters. No script
API or deployed production behavior changes.

Logs remain on Frame under `/tmp/sf-primitive-comparison`, with build output in
`/tmp/sf-primitive-build.log`, initial pixels in `/tmp/sf-primitive-pixels.log`,
and final correctness/production checks in `/tmp/sf-primitive-validation.log`.

ToyCar has only three large material primitives. Finer spatial clusters could
offer more selective rejection, but would add bounds and draw-submission cost.
Measure their potential skipped work and CPU cost before committing to a new
mesh representation. Preserve triangle detail, animated fallback, masked holes,
and the common projection plane in any follow-up.
