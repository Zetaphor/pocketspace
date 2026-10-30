# PBR geometry and pixel sensitivity

Steam Frame ARM64, 2026-10-29. The user confirmed active-headset profiling
readiness. This offscreen investigation opens no overlays and captures no input.
It changes no production shader, resolution, geometry policy or default.

## Controlled workload

`pbr-cost-profile` uses the existing real GLES render-pass harness with 32
ToyCars in an 8×4 grid, .16 scale, .22-meter spacing and deterministic rotations.
Both eyes alternate at a fixed head position [0,0,-1.2]. All objects share a
PBR/cache-optimized asset. Lighting is directional intensity .3, ambient .04,
no HDR environment, and zero/eight fixed mixed point/spot lights. Scene lights
use intensity .15, range 2, and .3/.6-radian spot half-angles. Production
per-object range/cone filtering remains enabled.

Cases independently select full/LOD-2 geometry and 384/768/2048-square viewports.
One 4096×2048 color/depth backing allocation and the same full/reduced model
buffers remain allocated in every case. Projection and material settings stay
fixed. Each case sees exactly the same rotation/eye sequence; case order reverses
each frame. Each has 30 warmup and 90 measured frames, repeated in two processes.
GPU elapsed queries reject disjoint status; measured wall time includes rendering
and `glFinish`. No readback occurs in measured frames. Returning to the baseline
pose/settings after all switches must reproduce its image byte-for-byte.

Full geometry submits 3,485,952 triangles per eye; LOD 2 submits 2,176,704,
a 37.6% reduction. LOD is fixed by the harness, so there is no distance-selection
drift. All objects belong to one render target, without external occluders.

These deliberately different-quality cases attribute cost. They are not
pixel-equivalent optimization candidates. Smaller viewports change coverage,
texture sampling, shading and raster work; reduced geometry can also change
coverage/overdraw. Geometry sensitivity therefore does not isolate pure vertex
ALU or bandwidth, and viewport sensitivity does not isolate the BRDF alone.
The fixed backing clear contributes the same storage work in each case.

## Measured GPU sensitivity

Ranges are the two run means, not confidence intervals. Each value is one eye's
render with alternating eyes across frames; wall/query totals are not stereo FPS.

| Lights | Geometry | 384 GPU ms | 768 GPU ms | 2048 GPU ms |
| --- | --- | --- | --- | --- |
| 0 | Full | 6.67–6.73 | 6.64–6.69 | 8.85–9.23 |
| 0 | LOD 2 | 3.89–4.27 | 4.71–4.73 | 6.88–6.98 |
| 8 | Full | 6.70–7.57 | 7.79–7.92 | 12.99–13.33 |
| 8 | LOD 2 | 4.71–5.07 | 5.52–6.05 | 10.97–11.14 |

At full detail, quartering viewport pixels from 768 to 384 gives no consistent
benefit without scene lights, and a variable benefit with eight lights.
At 768, reduced geometry lowers GPU time by about 22–30%. Geometry is therefore
a substantial cost in this controlled workload.

At 2048, pixel count is 7.1 times the 768 case; full-detail GPU time rises about
33–38% without scene lights and 64–71% with eight. At that larger viewport,
reducing geometry saves about 21–25% without lights and 16% with eight. Shading
and raster work become more significant at larger sampling densities. This
rules out a universal geometry-only explanation and supports investigating both
vertex work and per-fragment material/light work.

Next candidates should preserve quality: investigate repeated per-vertex
transform preparation and reuse of invariant PBR material/light arithmetic.
Neither candidate is implemented or proven by this sensitivity study. Any change
needs automated pixel comparisons (including normal maps, alpha, deformation,
stereo and attachments) and matched native throughput at multiple densities.
Do not infer that the rejected light-loop prototype should be enabled.

## Evidence and limits

[Bounded measured summaries](pbr-cost/summary-frame.json) contain each case/run.
Raw final logs remain on Frame at `/tmp/pbr-cost-wide-{1,2}.log`.
The excluded native target builds with assertions enabled and passes its GL,
disjoint and restored-baseline checks in both final runs. The shared harness's
existing `render-occlusion-regression` also passes its stereo/alpha/scissor,
indexed geometry and cleanup checks with the default viewport unchanged.
Documentation lint, site build and
whitespace checks are separate verification. There is no new user-confirmed
visual acceptance because production rendering has not changed.

The earlier [live-engine profile](dense-pbr-lights.md) uses different projection,
coverage, allocations and trajectories. Do not compare absolute milliseconds
between these workloads or turn offscreen timing into headset FPS predictions.
This is a short sensitivity study, not sustained thermal or compositor latency
validation. No quality reduction is adopted.

All profiling/check processes exited normally; no demo remains running.
