# Minimal PBR depth shader investigation

Closed without adoption on Steam Frame, 2026-10-29. Production retains the
shipped fast-depth program. The candidate remains available in excluded tests.
The reference is the **shipped fast-depth shader**, rather than the older full
PBR color shader used as a depth pass in the original depth investigation.

The candidate uses the existing model position/UV vertex source without PBR
surface-frame outputs, plus a minimal fragment shader. Opaque depth avoids
texture sampling; masks retain base texture alpha times material factor and the
identical strict cutoff. Geometry, pose, LOD, winding, projection, buffers and
texture storage remain unchanged. Blended materials remain excluded from external
depth. It uses the existing PBR binding/cleanup path and compiles before drawing.

## Automated comparison

The initial and reversed-order runs each passed 1,248 byte-exact stereo images:
opaque/masked/blended policy, threshold/factor boundaries, front/back/mirrored
winding, independent groups, LOD, skeletal animation and morph poses. Independent
coverage checks prove real external occlusion and visible cutout holes. Program
preservation, disabled surface attributes, shader deletion, geometry-budget cleanup,
GL error and GPU timer disjoint checks passed.

Build the excluded `pbr-minimal-depth-regression` target explicitly. From the
repository root, with the EGL display and SteamVR library path set, run:

```sh
./build/pbr-minimal-depth-regression --benchmark
./build/pbr-minimal-depth-regression --benchmark --reverse
```

`mode=0` is shipped fast-depth; `mode=1` is minimal depth. The excluded
`gpu-sync-experiment` accepts `SF_PBR_MINIMAL_DEPTH=1` for integration testing;
normal builds have no such switch. Keep `SF_GPU_PBR_VARIANTS=1` to match production
color-program preparation when using that profiling engine.

The expanded run also passed 2,496 exact comparisons across 768- and
2048-pixel eye targets, with the same state and cleanup checks.

## Offscreen measurements

Awake Frame, fixed camera, alternating eyes, 768 pixels per eye, full-detail
ToyCars, preuploaded studio environment, bounds/scissor enabled, glFinish retained.
Programs link before timing. Four stages each contain 30 warmup and 120 measured
frames; first run uses baseline/candidate/candidate/baseline, second reverses it.
Values average the two stage means for each mode. Raw bounded samples are in
[offscreen-frame.json](pbr-minimal-depth/offscreen-frame.json).

| Workload | Run | GPU baseline → candidate (ms) | Synchronized wall (ms) |
| --- | --- | --- | --- |
| 4 cars, external depth | first | 1.669 → 1.001 | 2.512 → 1.932 |
| 4 cars, external depth | reverse | 1.601 → 1.048 | 2.652 → 1.941 |
| 16 cars, external depth | first | 6.208 → 4.199 | 7.755 → 5.612 |
| 16 cars, external depth | reverse | 6.471 → 4.197 | 7.844 → 5.600 |
| 4 cars, color control | first | 2.345 → 2.308 | 3.416 → 3.364 |
| 4 cars, color control | reverse | 2.422 → 2.611 | 3.602 → 3.592 |
| 16 cars, color control | first | 9.628 → 9.969 | 11.255 → 11.512 |
| 16 cars, color control | reverse | 10.010 → 10.242 | 11.548 → 11.899 |

Depth workload counts match across modes/stages: four cars produce 12 depth
draws / 435,744 triangles, sixteen produce 48 / 1,742,976; zero PBR color draws.
Color controls use identical color shaders and the same draw/triangle counts,
with no PBR depth draws. Their timing differences are variation, not a candidate
benefit. No geometry/texture storage is added; driver program memory is not
measured. Clocks, thermals and compositor scheduling are not locked. Offscreen
savings do not establish whole-engine FPS or headset visual acceptance.

## Whole-engine integration

Four bounded 16-second runs used the excluded engine, production prepared color
programs, 32 full-detail cars, eight lights, adaptive groups, vertex-cache ordering
and ordered instancing. Program order was baseline/candidate/candidate/baseline.
The flag was confirmed in each log; GPU error, timer disjoint, process shutdown
and allocation-budget checks passed. Late windows begin at six seconds.
[Bounded results](pbr-minimal-depth/engine-frame.json) retain all four stages.

| Stage | Program | Whole-render GPU median (ms) | FPS median |
| --- | --- | --- | --- |
| 0 | shipped | 24.983 | 29.38 |
| 1 | minimal | 25.252 | 29.27 |
| 2 | minimal | 25.042 | 30.12 |
| 3 | shipped | 28.844 | 26.97 |

All runs used the same 400.294 MiB owned-allocation estimate and 380 MiB grouped
buffer peak, 3,485,952 scene triangles, 6,971,904 stereo member triangles and 192
member draws. External depth work varied from 93.11 to 98.86 draws per frame
(3.38–3.59 million triangles) as motion and live view changed. These are matched
member geometry/storage, **not exact external-work comparisons**. The mixed whole
results do not establish a repeatable FPS gain; averaging the later slower
baseline into a claimed speedup would hide this limitation. The controlled comparison below resolves the adoption decision; these moving
runs alone were inconclusive.

## Headset visual acceptance

The user accepted the candidate in `tests/scenes/pbr-depth.js`: grip/trigger
movement and rotation, head motion, cutout holes and overlaps against the blue
background looked correct. The profiling engine confirmed minimal depth enabled,
with no reported GL errors, disjoint timer samples or fence failures. The demo
was stopped after acceptance and its exit verified: 3,706 completed frames and
zero remaining grouped-buffer charges. This establishes visual acceptance,
separately from the inconclusive whole-engine performance comparison.

## Controlled engine comparison and decision

**Do not adopt.** The controlled comparison does not establish a repeatable
whole-render benefit sufficient to change production. The isolated depth shader
result remains valid for its offscreen workload; it does not predict equivalent
savings in the engine. This investigation is complete, rather than pending more
headset checks or an automatic default change.

`tests/profile-minimal-depth.py` runs the actual profiling engine with
`tests/scenes/pbr-depth-controlled.js`. Thirty-two stationary full-detail ToyCars
and eight fixed lights share four fixed groups. Canonical identity scene axes,
a fixed head position `[0, .2, 1]` and the headset's unchanged eye offsets remove
head-tracking variation. The helper never requests controller interaction. This
camera override is compiled only in the experiment and is unsuitable for judging
head-motion rendering. Normal builds retain normal tracked views.

Each stage lasts 16 seconds, using timer windows starting at six seconds. The
first run includes grouped and color-only controls in baseline/candidate/candidate/
baseline order; a grouped repeat uses candidate/baseline/baseline/candidate.
Values below average the two stage medians for each mode; they are not raw-frame
means or whole-compositor FPS. GPU error, disjoint and fence checks passed.
All twelve processes shut down normally with zero remaining grouped-buffer
charges. No demo remains running.

| Workload | Order | Whole GPU baseline → candidate (ms) | Enqueue CPU (ms) | GPU completion wait (ms) |
| --- | --- | --- | --- | --- |
| Four groups | first | 43.021 → 42.924 | 3.256 → 3.207 | 46.333 → 46.158 |
| Four groups | reverse | 44.380 → 43.252 | 3.278 → 3.180 | 47.784 → 46.725 |
| Single-group color control | first | 19.509 → 19.460 | 1.571 → 1.579 | 23.251 → 23.227 |

The grouped difference is approximately 0.2% first and 2.5% in reverse order;
the unchanged color control also varies by approximately 0.2%. Clocks, thermals
and compositor scheduling remain uncontrolled. These results do not reproduce
the isolated 32–40% gain as a reliable engine improvement. Do not subtract the
single-group GPU cost from the four-group GPU cost to estimate depth time: group
layouts, pixel allocations and prepared-color eligibility differ.

Each grouped frame submits exactly 237 external-depth draws / 8,605,944 triangles
and 192 opaque member draws / 6,971,904 triangles; no blended draws. All 32 objects
retain full LOD, totaling 3,485,952 scene triangles. Every measured work window,
all grouped stages and the repeat match these counts. Group creation sequences,
widths, eye resolutions, allocation estimates and grouped-buffer peaks also match:
512-pixel initial surfaces grow to 768 per eye, peak grouped storage is 80 MiB,
and estimated owned allocation is 115,651,138 bytes. The color control has the
same member geometry, zero external depth, a 1280-pixel final eye target, a
58 MiB grouped peak and 92,582,466 estimated owned bytes. Its work/storage match
between its own modes; it is not a quality-equivalent alternative grouping policy.

Both modes enable production prepared-color policy and compile fifteen bounded
color programs. Grouped targets are below the 1024-pixel variant threshold and
use general color; the color control can use prepared programs. Baseline and
candidate modes within each workload preserve this policy. Shader-driver memory
is not included in owned buffer estimates. The candidate shader links only at
startup and introduces no additional mesh/texture buffers.

[First controlled samples](pbr-minimal-depth/controlled-first-frame.json) and
[reverse samples](pbr-minimal-depth/controlled-reverse-frame.json) retain each
stage's medians, geometry, storage and surface-generation sequence. Native
profiling binary SHA-256:
`cc6e4aecbb22001ba405313515f63ea3eaadd107f53f475e46e34e8a78936307`.

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/profile-minimal-depth.py --output /tmp/depth-controlled-first
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/profile-minimal-depth.py --reverse --workload grouped \
    --output /tmp/depth-controlled-reverse
```

The runner refuses a competing engine, checks startup flags, verifies completed
GPU samples, rejects changed work/storage/surface generations, and stops each
stage with SIGTERM before starting another. Keep the headset active for normal
power even though the rendered camera is fixed.
