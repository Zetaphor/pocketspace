# Continuous fast-motion validation

This workload tests continuously crossing trajectories, rather than stationary
islands separated by occasional teleports. `tests/scenes/fast-motion.js` keeps
128 or 240 kinematic physics bodies moving and rotating, with independent paths
up to roughly 4 m/s and repeated expansion/contraction. IDs and object counts stay
constant. The cases use boxes, mixed ToyCar/Avocado models, or only Avocados.
ToyCar has 108,936 triangles and Avocado 682: the 128 mixed-model scene contains
7,015,552 triangles before stereo rendering or repeated occlusion passes.

`tests/fast-motion.py` runs bounded native sessions, collects late samples after
five seconds, rejects JS/GL/buffer failures and early exits, and checks the
shutdown accounting for zero retained grouped bytes and peak usage at or below
512 MiB. The fixed cases use one logical group with automatic subdivision
disabled; adaptive cases opt into eight tiles with merge/rebalance enabled.
These are motion/throughput checks, not dynamic collision or throwing tests.

The engine preflights both structural outputs alongside retained snapshots,
charges grouped buffer generations against the budget until destruction, spaces
ordinary resize attempts by 100 ms, and backs off five seconds after allocation
failure or refusal. A smaller admitted growth bucket can improve density when
the full request cannot fit. Undersized current buffers cover the full layout
rather than clipping moving members. Budget accounting excludes assets, panels,
independent surfaces, and compositor/driver resources; it is not a total GPU
memory guarantee.

The API/planner budget tests cover transactional peaks, rejected admission
without consuming capacity, smaller bucket selection, release and complete
projection coverage below requested density. Existing presentation, stereo
occlusion, example and live regroup lifecycle regressions also run against the
changed engine.

## Frame results

Seven 16-second native runs retained eleven late samples each. Median FPS and
median GPU allocation estimates are in [the raw summary](fast-motion-frame.json).

| Moving workload | Policy | Median FPS | Median GPU estimate (MiB) | Peak grouped buffers (MiB) |
| --- | --- | ---: | ---: | ---: |
| 128 boxes | Adaptive | 71.9 | 400.0 | 418 |
| 240 boxes | Adaptive | 62.9 | 400.0 | 418 |
| 240 boxes | Fixed | 63.3 | 450.0 | 500 |
| 128 ToyCar/Avocado models | Adaptive | 18.0 | 423.0 | 418 |
| 128 ToyCar/Avocado models | Fixed | 24.0 | 473.0 | 468 |
| 128 Avocados | Adaptive | 73.0 | 416.0 | 418 |
| 128 Avocados | Fixed | 87.1 | 466.0 | 500 |

All seven runs stopped normally, retained their expected object count, released
all grouped buffer charges at shutdown, and stayed below the 512 MiB grouped
budget. No buffer creation failures or reported JS/GL errors occurred. Budget
deferrals were exercised; adaptation did not universally improve throughput.
Fixed groups used more pixel storage in these cases. No claim of equal image
quality or headset acceptance follows from these measurements.

These results justify keeping fixed groups as a supported option for mixed fast
motion and retaining automatic policies as opt-in. The dense detailed-model case
still falls far below headset frame rates in either mode and remains unresolved.
The next investigation should measure geometry and repeated occlusion work rather
than assuming an object-count or physics limit. Raw native logs remain on Frame
under `/tmp/sf-fast-motion-final`; the live lifecycle log is
`/tmp/sf-motion-final-lifecycle.log`. No persistent demo remains running.

## Limits

These short native runs do not establish headset visual acceptance, long-duration
memory stability, or a guaranteed FPS at arbitrary object counts. Fixed and
adaptive cases can use different allocation sizes and pixel densities. Throughput
comparisons therefore describe these policies as exercised, not equal-quality
microbenchmarks. The run order is not thermally controlled. Model complexity,
pose, draw repetition and surface resolution all matter alongside object count.
