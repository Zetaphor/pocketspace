# Dense moving-model lighting profile

Steam Frame ARM64, 2026-10-29. The user confirmed readiness for active-headset
profiling. This investigation changes only test workloads and tooling; production
rendering, light limits, quality and defaults are unchanged. Shadows remain deferred.

## Workload and method

`profile-render-passes.py --case 32-cars-fixed --no-lod --instancing
--pbr-lights N` runs 32 moving/rotating ToyCars in one fixed render group, with
768 reference pixels per eye, PBR, cache/fetch optimization, full geometry,
directional intensity .3 and ambient .04. There is no HDR environment. The
trajectory is the existing time-based fast-motion scene. Kinematic bodies move
without controller interaction; lasers are hidden and no pointer capture is enabled.

Counts are zero, four or eight additional scene lights. Mixed lights alternate
points and spots, intensity .15, color [1,.8,.6], two-meter range; spots face +Z
with .3/.6-radian cone half-angles. Positions are fixed while objects move.
Production range/cone selection remains enabled. Zero still uses the same PBR
material path. Light counts intentionally change illumination, so this is cost
attribution rather than a pixel-equivalent optimization comparison.

Each run lasts 16 seconds. FPS excludes the first five seconds; GPU/work samples
exclude the first six. Each count has two whole-render and two sampled-pass runs,
ordered whole/pass/pass/whole. Counts run in order 0, 8, 4; that order is not
counterbalanced across counts. Live head pose, motion phase, scheduling and
thermal state are not locked. Small timing differences are not conclusions.
Separate per-pass queries sample one frame per second and exclude framebuffer
setup/clears and compositor work. Do not subtract their medians from whole-render
timing as exact overhead or projected savings.

All within-count instrumentation pairs pass storage and geometry validation.
Across all full-detail counts, allocations match at 488.29 MiB estimated owned
GPU storage and 468 MiB peak grouped buffers. Every steady frame has 32 full-detail
objects, 3,485,952 scene triangles and 6,971,904 stereo member triangles across
192 opaque draws. There are no external occluder draws or blended triangles.
Fixed groups therefore isolate the member-rendering cost; these results do not
measure adaptive-group occlusion cost.

## Full-detail measurements

Ranges below show the two run medians, not statistical confidence intervals.

| Scene lights | Engine FPS, whole runs | Whole-render GPU ms | Sampled opaque GPU ms | Sampled opaque thread CPU ms |
| --- | --- | --- | --- | --- |
| 0 | 31.99–32.51 | 23.11–24.03 | 21.06–25.95 | .84–.88 |
| 4 | 26.88–27.02 | 29.71–30.18 | 28.76–28.88 | .80–.94 |
| 8 | 24.49–24.61 | 31.52–31.87 | 30.12–30.46 | .94–.99 |

The fixed-group case spends almost all sampled GPU pass time in opaque member
rendering. Even the zero-scene-light PBR baseline exceeds an 11.1 ms GPU budget;
light count adds further cost. Submission callback CPU time is much smaller than
GPU time. This does not measure all CPU work or isolate vertex processing from
fragment shading inside the opaque pass.

## Automatic LOD follow-up

An initial automatic-LOD pair was rejected rather than used for performance
claims: live LOD choices changed, and its PBR draw counter reported full-detail
counts rather than actual selected geometry. The renderer already drew the
selected LOD; the issue was confined to the excluded profiler's accounting.
The counter now uses `modelDrawVertexCount` with the same selected level as the
draw call. [Rejected original evidence](dense-pbr-lights/lod-8-rejected.json)
remains explicit. The full-detail measurements above are unaffected because
selected and full-detail geometry coincide.

The [counter-corrected follow-up](dense-pbr-lights/lod-8-counter-fixed.json)
passes complete stereo geometry accounting in both runs: median scene triangles
2,176,704 and stereo member triangles 4,353,408, with 32 objects at LOD 2.
One run still changes LOD choices within its measurement windows, so the strict
comparison rejects it for geometry drift. Its timings are not used. The runner
stops before the planned zero-light automatic-LOD pair. A future automatic-LOD
comparison needs controlled pose/distance or phase-aligned samples. Every
profiling process has exited; no demo remains running.

## Decision and evidence

The next investigation should isolate geometry versus fragment/material cost
inside the PBR member path. These results do not justify moving physics or light
selection to the GPU, increasing the light cap, enabling rejected shared-depth
experiments, or reducing production visual quality. A shader or geometry candidate
still needs pixel correctness, equivalent-quality throughput comparisons and
separate interactive acceptance before adoption.

Bounded summaries: [zero](dense-pbr-lights/full-0.json),
[four](dense-pbr-lights/full-4.json), [eight](dense-pbr-lights/full-8.json).
Native build: `gpu-sync-experiment`; script syntax/Python compilation,
documentation lint and site build pass. Native runs validate object counts,
GL/disjoint/fence status, complete stereo geometry, normal process exit and
zero grouped-buffer charges after shutdown. Raw logs remain on Frame under
`/tmp/pocketspace-dense-lights-{0,4,8}`. This is profiling evidence, not a new
user-confirmed visual test or a sustained thermal/latency measurement.
