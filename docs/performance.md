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

The initial model-space implementation still scanned candidate triangles
linearly. The BVH follow-up below replaces that runtime scan.
Headset retest of frame openings after the model-space optimization remains
useful; the earlier wearer-confirmed fix preceded this optimization.

## Follow-up hot-loop cleanup

The first implementation and measurement notes are committed in `4ee849f` and
`f9b238d`. Follow-up changes cache shader uniform locations, compute both eye
positions once per frame, reuse the ready-surface vector, and transfer the
drained raw-input queue instead of copying all events and strings. The frame
limiter drops missed deadlines, preventing uncapped catch-up bursts after a
slow workload. Shared-texture GPU synchronization remains unchanged.

The bounded Toy Car check `/tmp/performance-loop-cleanup.log` held 90 FPS,
reported `gl_error=0`, and stopped cleanly at 2221 frames. Render-submission CPU
time was 0.011–0.017 ms; this small sample does not establish an FPS improvement
over the already capped 90 FPS baseline. The 4000-ray regression test passed.
Reusing the ready-surface vector was added after that Toy Car run and is covered
by the subsequent two-object runtime check.
That final check (`/tmp/performance-loop-two-objects.log`) created both surfaces,
reported 90 FPS with `gl_error=0`, delivered hover events from both hands, and
exited cleanly at 1319 frames. This is runtime/log verification, not a new wearer
confirmation of visual or interaction behavior.

Other opportunities need separate measurements: replacing quadratic
object/surface lookup scans; indexed geometry
instead of expanded triangle streams; and reducing redundant overlay submissions.
Changing shared-texture fences or `glFinish` requires validating compositor
ownership and texture reuse, rather than simply deleting synchronization.

## Shared triangle BVH

Follow-up on `bd44825`: build a median-split BVH once per loaded asset, after
normalizing geometry. Nodes contain conservative local bounds; leaves reference
the existing primitive vertices without duplicating geometry. All instances
share the index. Traversal visits nearer nodes first, rejects nodes beyond the
nearest triangle hit, and uses a bounded stack without per-ray allocations.
Bounds remain rejection-only, preserving the hollow-frame opening.
Geometry edits or future deformation require rebuilding/updating this static index.

The CPU benchmark now runs both linear and BVH modes, comparing each ray's
hit/miss and nearest distance, and exits nonzero on a mismatch.

| Asset | Linear ms / two-hand frame | BVH ms / two-hand frame | Matching hits |
| --- | --- | --- | --- |
| Toy Car | 2.8994–2.9078 | 0.0023–0.0052 | 364 |
| Lantern | 0.0727 | 0.0008 | 60 |
| Hollow frame | 0.0007 | 0.0007 | 76 |

Toy Car's distance checksum was identical between modes (335.880366).
The BVH has 32,767 nodes for 108,936 triangles. Construction took 78.845 ms
and reserved 3,227,336 bytes (3.08 MiB) on Deckard, once per loaded asset.
The narrow fixed sweep is a repeatable algorithm benchmark, not a claim that
the whole engine is hundreds of times faster.

The 25-second single-car runtime check `/tmp/performance-bvh.log` held 90 FPS,
reported picking at 0.003–0.014 ms/frame and `gl_error=0`, and exited at 2213
frames. A separate 25-second run of `scripts/performance-multi-model-test.js`
rendered four Toy Car instances at 768 pixels per eye, sharing one BVH:
`/tmp/performance-bvh-four-cars.log` reported 89.9–90.0 FPS, all four surfaces
ready, picking 0.013–0.014 ms, GL finish wait 4.563–7.776 ms, `gl_error=0`, and
clean shutdown at 2213 frames. Runtime grab activity was present; no new wearer
confirmation of appearance or selection accuracy was collected.

`python3 tests/picking-regression.py` now includes 8000 BVH-versus-scan rays
across sparse, layered geometry, parallel rays, inside origins, nearest hits,
and index rebuild, plus the prior 4000 transformed hollow-frame comparisons.
It also passed with `--checked` (libstdc++ container assertions). Optional
`--sanitize` was attempted but unavailable locally: GCC's ASan runtime library
is missing and the installed Clang cannot use the installed GCC 16 headers.

Rendering/GPU completion is now the larger measured cost in the four-car scene.
Measure geometry submission and pixel workload before pursuing indexed geometry
or changing texture synchronization. Object/surface lookup scans remain a
separate CPU scaling opportunity.
