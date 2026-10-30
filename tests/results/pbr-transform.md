# PBR rotation reuse investigation

Steam Frame ARM64, 2026-10-29. Offscreen EGL/GLES; no overlays or input capture.
Production shaders and defaults remain unchanged during this investigation.

## Rejected CPU preparation

The first prototype moved Euler sine/cosine evaluation to CPU float uniforms per
object draw while preserving the remaining shader arithmetic. It failed the
strict stereo pixel gate: across 128 comparisons, 16 alpha bytes differed,
5,625 RGB bytes differed and the maximum RGB difference was 206. Small geometry
edge shifts can cause large byte differences at silhouettes. No performance
measurements were run for that version; it was rejected rather than loosening
tolerances. The CPU variant is not retained in the executable. Its native log
remains on Frame at `/tmp/pbr-transform-run-1.log`.

## GPU reuse candidate

The retained test-only candidate passes the main vertex shader's already-computed
Euler sine/cosine values into normal/tangent rotation functions, removing duplicate
source expressions. All trigonometry remains on the GPU, with the same rotation
arithmetic, geometry, projection and material code. Compiler common-subexpression
elimination may already do this work in the reference, so source simplification
alone does not establish a performance improvement.

Only the excluded `pbr-transform-experiment` defines
`SF_PBR_TRANSFORM_EXPERIMENT`. Normal builds retain the original source and expose
no runtime switch. The candidate and reference use separate program objects in
the same renderer with the same asset buffers and framebuffer.

Correctness covers 128 stereo comparisons: ToyCar, NormalTangentTest, morphing
SurfaceFrames and AlphaBlendModeTest, full/reduced geometry, four Euler poses
including a 1000-radian yaw, attached mixed lighting, and grouped/separate targets
with external occlusion. Coverage must match exactly; RGB differences may be at
most one byte. Performance runs only after this gate passes.

The matched ABBA harness uses 32 rotating cars, full/LOD-2 geometry,
768/2048-square viewports, zero/eight mixed lights, 30 warmup and 90 measured
frames per stage, alternating eyes. Both programs share geometry, allocations,
projection and the deterministic pose sequence. GPU disjoint/GL errors fail.
Wall time includes rendering and `glFinish`; offscreen time is not headset FPS.

## Results and decision

Both GPU-reuse runs pass all 128 comparisons byte-identically: zero alpha/RGB
bytes changed. All eight geometry/light/density combinations complete their
matched timings with clean GL/disjoint status. The following percentages compare
the means of the two reference and two candidate stages per run. Positive means
less time; negative means regression. These are short-run observations, not
confidence intervals.

| Lights | LOD | Pixels | GPU reduction %, runs 1 / 2 | Wall reduction %, runs 1 / 2 |
| --- | --- | --- | --- | --- |
| 0 | Full | 768 | 4.1 / -1.1 | -0.6 / 0.9 |
| 0 | Full | 2048 | -1.7 / 5.1 | -1.1 / 2.6 |
| 0 | 2 | 768 | -7.5 / -0.2 | -1.8 / -0.5 |
| 0 | 2 | 2048 | -1.5 / 1.2 | -1.1 / 0.4 |
| 8 | Full | 768 | -1.7 / 3.6 | -1.3 / 1.2 |
| 8 | Full | 2048 | -1.2 / -1.7 | -1.7 / -1.7 |
| 8 | 2 | 768 | 2.5 / 0.3 | 1.9 / 1.8 |
| 8 | 2 | 2048 | 0.8 / 1.8 | 0.3 / 0.3 |

There is no consistent general gain. The larger full-detail/eight-light workload
regresses in both repetitions; modest reduced-geometry gains do not justify a
production routing policy. **Do not adopt.** Keep the isolated target for future
compiler/hardware investigations; there is no public option or default to enable.
The measurements are compatible with the compiler already eliminating much of
the duplicate work, but do not prove its generated instruction schedule.

[Bounded stage summaries](pbr-transform/summary-frame.json) retain all 64 measured
stage rows. Raw GPU logs remain on Frame at `/tmp/pbr-transform-gpu-{1,2}.log`.
Next investigate invariant material/light arithmetic independently, retaining
strict pixel gates and equivalent geometry, density and lighting for timing.
No interactive acceptance is requested for an unadopted candidate.

## Verification

The normal `pocketspace` target builds, and the default shared-harness GLES
occlusion regression passes stereo/alpha/scissor, indexed geometry and cleanup
checks. Documentation lint/site build and whitespace checks pass. Both GPU
experiment processes and the regression exited; no test/demo remains running.
