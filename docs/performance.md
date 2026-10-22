# Performance measurements

October 22, 2026, Deckard, uncommitted changes on engine revision `d7db4b9`.
Workload: `scripts/performance-test.js`, one static Toy Car (108,936 triangles),
768 pixels per eye, two tracked controllers, custom lasers. Runtime samples
include wearer movement, so use the fixed CPU benchmark for algorithm comparison.

| Build / picker | Runtime FPS | Picking ms/frame | GL finish wait ms/frame |
| --- | --- | --- | --- |
| Unoptimized / world-space triangles | 47.0 | 14.656 | 3.233 |
| Release / world-space triangles | 90.0 | 3.856–4.023 | 4.075–4.232 |
| Release / model-space ray | 90.0–90.1 | 1.874–2.625 | 1.550–1.678 |

All reported runtime samples had `gl_error=0`. Render submission CPU time was
0.018–0.026 ms. The GL finish wait is CPU wall time waiting for submitted GPU
work, not an isolated GPU timer measurement; differences can reflect scheduling
and system load. The engine caps itself at roughly 90 FPS, so extra headroom
does not raise the steady-state FPS. These are engine timings, not independent
compositor frame-time or wearer smoothness measurements.

Runtime logs on Deckard: `/tmp/performance-baseline.log`,
`/tmp/performance-release.log`, `/tmp/performance-local-ray.log`.
All bounded runs exited cleanly (1732, 2665, and 2669 frames respectively).

## Changes

Single-configuration CMake builds default to Release when no build type is
selected. Explicit Debug and other build types remain supported. Deckard's
existing build was explicitly configured with `-DCMAKE_BUILD_TYPE=Release`.

Picking now inversely transforms a controller ray into normalized model space
once per object. It rejects misses against local axis-aligned bounds and reads
triangle vertices directly, avoiding per-triangle scale/rotation/translation.
Bounds never create hits inside openings. The mesh acquisition correctness
fixes remain in place.

The fixed CPU benchmark sweeps 200 frames with both hands, without starting
SteamVR or creating a graphics context. On Deckard, Release world-space picking
took 3.6288 ms per two-hand frame; model-space picking took 2.4372 ms (32.8% less).
Both reported 364 hits. Distance sums were 335.880378 and 335.880366 meters,
consistent within floating-point rounding.

## Reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/sf-overlay-engine --benchmark-picking assets/samples/ToyCar/ToyCar.glb
timeout -s TERM -k 5 30 ./run-overlay-engine.sh scripts/performance-test.js > /tmp/performance.log 2>&1
python3 tests/picking-regression.py
```

The regression test compiles the actual picker code with synthetic model types
and the hollow-frame asset. It covers both hands, empty openings, rail hits,
invalid tracking, a small triangle hit/miss, and 4000 transformed-ray comparisons
against a frozen world-space oracle. It needs Python 3 and a C++20 compiler,
but no OpenVR, GL libraries, or headset.

Next scaling work: profile increasing model counts, including repeated assets,
before adding a BVH. The current scan remains linear in candidate triangles.
Headset retest of frame openings after the model-space optimization remains
useful; the earlier wearer-confirmed fix preceded this optimization.
