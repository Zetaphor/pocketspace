# Dense moving model draw-pass attribution

Frame completed twelve bounded 16-second native runs: two rounds of fixed and
adaptive 128 mixed models (64 ToyCars plus 64 Avocados), and fixed 128 Avocados.
Each case paired whole-render GPU timing with sampled pass timing. Case and pair
orders reversed in round two. Mesh cache optimization was enabled throughout.
The engine base was `a4a6f9a`, with the test-only instrumentation in this change.

The GPU draw workload dominates these dense cases. Fixed grouping has no
nonmember occluders in this fixture, yet opaque draws alone cost 22–24 ms.
Adaptive grouping adds substantial occluder work: about 42–46% of sampled draw
time. Optimizing only occluders cannot bring the fixed dense case to headset
rates, and CPU submission is not the primary bottleneck here.

| Case | Round | Whole-render GPU ms, baseline | Occluder GPU ms, sampled | Opaque GPU ms, sampled | Occluder share of sampled draw time |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128 mixed, fixed | 1 | 24.25 | 0.002 | 23.57 | <0.1% |
| 128 mixed, fixed | 2 | 24.70 | 0.002 | 21.94 | <0.1% |
| 128 mixed, adaptive | 1 | 35.66 | 13.35 | 18.24 | 42.4% |
| 128 mixed, adaptive | 2 | 33.15 | 15.13 | 16.60 | 46.2% |
| 128 Avocados, fixed | 1 | 8.58 | 0.002 | 7.94 | <0.1% |
| 128 Avocados, fixed | 2 | 9.41 | 0.002 | 5.76 | <0.1% |

Numbers are medians after six seconds of warmup. Pass sums come from ten sampled
frames per run, rather than every rendered frame. Each whole-render sample
aggregates a roughly one-second window; these are separate runs and different
sampling schemes, so their medians must not be subtracted as exact overhead.
The share column is the median per-frame share, not the ratio of column medians.

Mixed scenes submit 512 opaque draws and 14,031,104 triangles per stereo frame.
Adaptive runs additionally submit median 194–267 occluder draws and
5,369,918–7,290,279 occluder triangles. Fixed Avocados submit 256 opaque draws and
174,592 triangles. No blended geometry is drawn; its callback timing is below
0.002 ms. Reported render-thread CPU time inside the callbacks totals roughly
0.19–0.21 ms for fixed mixed models, 0.29–0.30 ms for adaptive mixed models, and
0.11 ms for fixed Avocados. Whole-loop enqueue wall time is roughly 1 ms,
1.9 ms, and 0.34–0.36 ms respectively. These figures exclude script/physics work
before the render-loop timer and are not full-frame CPU utilization.

## Instrumentation and limits

`SF_GPU_PROFILE_PASSES=1` in the excluded `gpu-sync-experiment` replaces its
whole-render elapsed query with elapsed queries around each occluder, opaque,
and blended callback in one frame per second. Query objects are reused and
destroyed at teardown. Results are read only when available after the existing
producer completion wait; disjoint/unavailable samples are rejected by the
runner. No synchronization, compositor ownership, resolution policy, or script
API changes were made. Production builds compile out the instrumentation.

The callbacks include CPU culling and driver calls. GPU elapsed queries can
include scheduling effects; they do not isolate vertex, raster, or fragment
costs. Timings omit clears, surface setup, layer state changes outside the draw
callbacks, and compositor rendering. Triangle counts describe submission, not
visible pixels or hardware shader invocation counts.

Every pair had matching median estimated GPU storage and peak grouped storage.
Baseline/sampled FPS medians were 30.31/31.88 and 30.23/32.87 for fixed mixed,
21.78/25.29 and 23.45/22.93 for adaptive mixed, and 77.22/74.77 and 73.17/75.79
for Avocados. These variations prevent a precise overhead estimate or an FPS
improvement claim. Fixed and adaptive allocations differ, so their FPS values
are not a quality-matched comparison of grouping strategies. Moving geometry
and elapsed query sampling also change the per-pass distribution between runs.

All runs retained 128 objects, reported no checked GL/disjoint/fence/native
errors, and returned grouped buffer charges to zero on shutdown. The largest
peak grouped charge in these runs was 468 MiB, within the 512 MiB budget.
This is profiling, not new headset visual acceptance; all demos are stopped.

## Next experiment

Compare specialized opaque and depth-only shader paths against the current
general material shader, keeping a separate alpha-tested path for masked holes.
This targets both measured draw categories without reducing model geometry or
sampling density. The current fragment shader contains material sampling,
sheen, alpha-mode branching, and conditional discard even when used for depth
occluders. Profiling identifies the relevant passes, but does not establish
which shader stage limits them or guarantee that specialization will help.
Require exact pixel comparisons for opaque/masked/blended output and matched
native throughput measurements before adoption. Conservative visibility work
remains a candidate if shader specialization shows no benefit.

See [runner instructions](../README.md#render-pass-attribution) and the
[raw summary](render-passes-frame.json). Native logs remain under
`/tmp/sf-pass-profile` on Frame, with build/validation output in
`/tmp/sf-pass-build.log` and `/tmp/sf-pass-validation.log`.
