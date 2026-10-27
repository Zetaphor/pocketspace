# Scene reload memory attribution

Frame completed seven 30-second phases (210.07 seconds, six reloads) using the
production engine from `314da39`. The existing motion soak alternated 128
Avocados and 16 ToyCars plus 16 Avocados, with adaptive grouping and mesh cache
optimization enabled. A test-only preloaded sampler recorded glibc `mallinfo2`
and `/proc/self/smaps_rollup` once per second. No trimming was performed.

The roughly 80 MiB RSS rise is largely accounted for by **free allocator arena
space**, rather than a growing live heap. Allocator-reported live bytes here are
`uordblks + hblkhd`: arena allocations plus separately mapped malloc allocations.
The first Avocado phase has 16 MiB of separately mapped allocations; subsequent
phases have none. Comparing `uordblks` alone would incorrectly suggest growth.

| Phase | Scene | Live heap MiB | Free arena MiB | RSS MiB | Anonymous RSS MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | 128 Avocados | 48.86 | 5.09 | 84.75 | 42.07 |
| 2 | 32 mixed | 63.03 | 30.90 | 124.82 | 82.13 |
| 3 | 128 Avocados | 48.91 | 81.06 | 164.47 | 121.91 |
| 4 | 32 mixed | 63.02 | 23.75 | 121.33 | 78.84 |
| 5 | 128 Avocados | 48.93 | 81.85 | 165.07 | 122.72 |
| 6 | 32 mixed | 63.04 | 23.73 | 121.16 | 78.84 |
| 7 | 128 Avocados | 48.93 | 81.84 | 164.95 | 122.72 |

These are sampler medians from seconds 15 through 28 of each phase, measured
relative to its first record. The final window contains 12 samples, the others
13. Live heap values sum the two per-field medians. Free arena space grows by
76.75 MiB while RSS grows by 80.20 MiB. RSS minus anonymous RSS stays near
42–43 MiB. Later matching phases settle within roughly 0.02 MiB of live heap
allocation. This supports allocator retention/fragmentation as the dominant
cause in this workload; it does not identify individual allocation sites,
establish exact page residency for every heap block, or rule out small leaks.
Allocator totals include driver allocations made through glibc; this test does
not separately attribute those allocations to engine versus driver code.

Review also confirmed that successful reloads destroy the old scene vectors,
physics world, model/panel resources and JavaScript runtime. All six reloads
committed, object counts remained correct, no checked engine/rendering error
occurred, and grouped buffer accounting returned to zero on shutdown with a
512 MiB peak. The demo stopped normally. This is bounded memory attribution,
not a new visual acceptance or an equal-quality performance comparison.

Keep normal reclamation behavior. Forcing `malloc_trim` or changing allocator
policy is not justified by this plateau and needs separate memory-pressure and
frame-pacing measurements before adoption. Revisit allocation-site profiling if
longer runs show growing live totals or a larger workload creates memory pressure.

## Reproduction

Compile on Frame with glibc support:

```sh
cc -shared -fPIC -Wall -Wextra -Werror -O2 tests/memory-sampler.c \
  -pthread -o /tmp/sf-memory-sampler.so
```

Create an executable wrapper that exports `SF_MEMORY_TRACE` to an absolute CSV
path and `LD_PRELOAD=/tmp/sf-memory-sampler.so`, then `exec`s the production
engine with `"$@"`. Pass the wrapper to `tests/soak-motion.py --engine`, using
`--phases 7 --phase-seconds 30 --output /tmp/sf-memory-reload`. Set the normal
Frame runtime library path. Export the preload variables inside the wrapper,
so only the engine is instrumented, rather than the Python runner or shell.
The sampler adds one detached thread for the process lifetime and has no
production build/API integration; its overhead is not a performance baseline.

[Raw allocator/RSS samples](memory-reload-frame.csv) and the
[soak validation summary](memory-reload-frame.json) retain the measurements.
Frame logs remain in `/tmp/sf-memory-reload/engine.log` and `stderr.log`.
