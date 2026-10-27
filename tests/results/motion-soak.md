# Five-minute continuous-motion and reload check

The production engine completed a 300.12-second bounded native run on Frame.
`tests/soak-motion.py` alternated five one-minute phases between 128 Avocados and
16 ToyCars plus 16 Avocados. Automatic grouping and GPU mesh cache optimization
were enabled throughout. Objects continuously crossed tile boundaries and
rotated, using the same trajectories as earlier visual checks. An in-headset HUD
showed the phase, FPS and estimated GPU memory; no controller input was needed.

The runner atomically replaced its script between phases and verified all four
reload commits. It retained separate stdout/stderr logs, checked process liveness,
and stopped the engine with SIGTERM after the fifth phase. Each phase retained
50 samples after a ten-second warmup and preserved its expected object count.

| Phase | Objects | Median FPS | Median RSS (MiB) | Median estimated GPU allocation (MiB) |
| --- | --- | ---: | ---: | ---: |
| 1 | 128 Avocados | 75.83 | 85.04 | 416.87 |
| 2 | 16 cars + 16 Avocados | 48.72 | 123.96 | 477.88 |
| 3 | 128 Avocados | 72.93 | 163.73 | 444.87 |
| 4 | 16 cars + 16 Avocados | 68.09 | 120.23 | 415.88 |
| 5 | 128 Avocados | 72.14 | 164.08 | 476.87 |

No early exit, JS load/update error, reported GL error, surface presentation
error, or buffer creation failure occurred. Grouped buffer peak reached the
512 MiB budget and shutdown returned all grouped allocation charges to zero.
Budget deferrals retained existing buffers/tiles. The demo stopped normally.

RSS increased by 79.04 MiB between the first and final Avocado phases, but only
0.35 MiB between the last two Avocado phases. It also decreased in the intervening
detailed phase. This is consistent with a warmup/retention plateau during this
run; it does not identify the retained allocations or prove the absence of leaks.
Separate allocator/driver retention from live allocations before attributing
that rise to a leak or changing memory reclamation behavior.
The subsequent [allocator measurement](memory-reload.md) accounts for most of
the rise as free heap space retained for reuse, with stable live heap totals.

Dynamic allocation sizes differed across phases. This is a lifecycle/stability
check, not an equal-quality performance comparison. FPS variation in the detailed
case is not an optimization claim. GPU figures are engine estimates and the
512 MiB budget covers grouped buffers, not total compositor/driver memory.
The run does not cover the 128-object detailed mix or multi-hour operation, and
does not add a new user visual-acceptance claim beyond earlier headset feedback.

[The raw summary](motion-soak-frame.json) retains phase metrics. Logs remain on
Frame in `/tmp/sf-five-minute-motion-soak/engine.log` and `stderr.log` in the same
directory. The runner defaults to five 60-second phases and supports longer
explicit durations. There is no persistent demo running.
