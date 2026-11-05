# Telemetry overhead

Measured on Steam Frame on 2026-11-04 at `7ded632`, with the headset awake.
The question: does a client watching for once-a-second `resources` events cost
the engine measurable frame rate or CPU?

## Method

Each run started the production engine with two display-only scripts from
`tests/scenes` (no input capture), waited, then read `pocketspace-cli stats` at
30, 40, 50 and 60 seconds. Runs 2 and 4 kept `pocketspace-cli watch --json`
attached from startup to the end. Runs alternated without and with a watcher.
The engine was stopped and confirmed gone after each run. Default quality
settings; `stats` calls were the same in every run. `cpuPercent` comes from
process CPU ticks over a one-second sample, so its resolution is about one
percentage point.

## Steady workload

`event-bus-peer.js` (one panel) and `tracking-smoke.js` (no panels or overlays).

| Run | Watcher | FPS samples | CPU samples (%) | Mean CPU | `resources` events |
| --- | --- | --- | --- | --- | --- |
| 1 | no | 90.0, 90.0, 90.1, 90.1 | 7.0, 7.9, 7.9, 8.9 | 7.9% | 0 |
| 2 | yes | 90.0, 90.0, 90.1, 90.1 | 10.0, 8.0, 8.9, 8.9 | 9.0% | 59 |
| 3 | no | 90.0, 89.7, 90.1, 90.0 | 7.0, 8.0, 7.9, 8.9 | 8.0% | 0 |
| 4 | yes | 90.0, 90.0, 90.0, 90.2 | 8.0, 7.9, 7.0, 6.9 | 7.5% | 59 |

Every run held 90 FPS. Watched runs averaged 8.2% CPU and unwatched runs 7.9%.
The difference is within the one-point resolution, and the two watched runs
differ from each other by more than that. RSS was 69.2 to 69.6 MiB throughout.
Per-script callback time was steady: about 2 ms/s for `event-bus-peer.js`,
23 to 27 ms/s for `tracking-smoke.js` and 0.04 ms/s for scratch.

## Variable workload

`fast-motion.js` (128 moving objects that regroup) and `event-bus-peer.js`.

| Run | Watcher | FPS samples | CPU samples (%) |
| --- | --- | --- | --- |
| 1 | no | 70.4, 89.3, 70.1, 89.9 | 42.6, 14.9, 64.1, 11.0 |
| 2 | yes | 68.0, 70.1, 69.9, 66.3 | 57.2, 53.3, 51.0, 54.5 |
| 3 | no | 78.7, 63.9, 83.5, 54.1 | 38.8, 60.9, 45.7, 62.9 |
| 4 | yes | 71.2, 73.9, 71.8, 90.0 | 43.5, 42.9, 42.3, 12.0 |

The scene itself moves frame rate between 54 and 90 FPS and CPU between 11% and
64% within one run, in both conditions. These runs cannot resolve a watcher's
cost and are kept only to show that. `fast-motion.js` callback time stayed at
22 to 26 ms/s across runs.

## Result

No measurable overhead from watching: frame rate and CPU match within sampling
resolution on the steady workload. The once-a-second sampling itself runs with
or without a watcher. Driver GPU memory (`vramBytes`) was unavailable on this
device, as in earlier reports.
