# Two-script soak

Measured on Steam Frame on 2026-11-05 at `b26a234` plus the
uncommitted Step 11 documentation, with the headset awake. Production build from a
clean `cmake --build build --clean-first -j1`. Default quality settings.

## Method

`python3 tests/multi-script-soak.py --output <dir> --minutes 10` started the engine
through `run-pocketspace.sh` with `tests/scenes` as the catalog and a throwaway
settings file (no autostart, `autoRestart` off). It started `event-bus-peer.js`
(one noninteractive panel) and `tracking-smoke.js` (no panels or overlays) over the
control socket, reloaded them in turn every 60 seconds, kept a watch connection
open, and read `stats` every 30 seconds. No input was captured. At the end it
stopped both scripts and sent SIGTERM.

## Results

| Measure | Value |
| --- | --- |
| Duration, samples, reloads | 10 minutes, 19 samples, 9 reloads (each generation increased by one) |
| Watch events | 607, none an error or an unexpected stop |
| FPS | 89.7 to 90.2, mean 90.0 |
| Process CPU | mean 9.3%, max 11.0% |
| RSS | 70.6 MiB at 30 s, 73.3 MiB by 3.5 min, then 73.3 to 73.5 MiB to the end |
| JavaScript heap | `event-bus-peer.js` 195 KiB and `tracking-smoke.js` 207 KiB in every sample, across reloads |
| GPU allocation estimate | 0.54 MiB in every sample |
| Callback time | `event-bus-peer.js` 2.5 to 9.1 ms/s; `tracking-smoke.js` 27.9 to 41.4 ms/s |
| Engine log | `gl_error=0` throughout |
| Shutdown | exit code 0; control socket removed; no engine process left |

RSS rose by about 3 MiB during the first few minutes and then stayed within 0.2 MiB
for the remaining six and a half minutes, so the reload cycle did not accumulate
memory over this run. Heap sizes did not change across reloads. This is a bounded
check of a light display-only workload, not a long-session or heavy-scene result.
