# Opaque color and depth shader specialization

The experiment preserves pixels but is **not enabled in production**. Dense
workloads show small gains, while the lighter model case has higher GPU timings
without a throughput gain. This does not justify adding multiple production
shader paths based on the tested workloads.

The baseline is the general material fragment shader. The experimental opaque
color shader keeps texture sampling, factor multiplication, sheen, and opaque
alpha output, but removes alpha-mode branches and discard. Its depth-only shader
has no texture sampling or discard. Both use the unchanged vertex shader and
the same geometry, projection, depth testing, draw ordering, and sampling policy.

## Pixel and lifecycle checks

`opaque-shader-pixels` passed 216 byte-identical GLES comparisons: Box, Avocado,
and ToyCar; three viewpoints with object rotations; both eyes; individual and
combined groups; opaque, masked, blended, and checker-mask materials. The
comparison includes factors, sheen, foreground occlusion, and masked holes.
Only opaque primitives use specialized shaders in this harness; other parts
retain the general shader. This is a bounded equivalence check, not proof for
all possible assets or a new headset visual acceptance.

The native experiment intentionally supports opaque-only assets. An explicit
AlphaBlendModeTest run rejected the scene with status 13 and released grouped
allocation charges. The production executable builds successfully and contains
no `SF_GPU_OPAQUE_SHADER` option. The existing GLES occlusion regression also
passes with the default harness path.

## Native measurements

Frame completed twelve bounded 16-second runs, with two rounds and reversed
case/pair orders. Each pair uses the same instrumented executable with the
experimental flag disabled/enabled. Mesh cache optimization stays enabled and
per-pass profiling stays disabled; whole-render GPU queries measure both modes.
Six seconds of warmup are excluded from GPU medians. FPS uses the motion runner's
existing five-second warmup. All pairs match median estimated GPU storage and
peak grouped allocation charges.

| Scene | Round | General FPS | Specialized FPS | General GPU ms | Specialized GPU ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128 mixed, fixed | 1 | 31.86 | 32.35 | 23.23 | 22.45 |
| 128 mixed, fixed | 2 | 28.30 | 29.00 | 26.43 | 25.91 |
| 128 mixed, adaptive | 1 | 24.95 | 25.29 | 30.32 | 30.64 |
| 128 mixed, adaptive | 2 | 23.31 | 24.72 | 32.96 | 31.00 |
| 128 Avocados, fixed | 1 | 71.99 | 72.02 | 6.28 | 7.47 |
| 128 Avocados, fixed | 2 | 71.99 | 72.02 | 6.29 | 6.78 |

Mixed scenes contain 64 ToyCars and 64 Avocados. Fixed dense throughput improves
1.5–2.5%, with GPU times 2–3.4% lower. Adaptive throughput improves 1.4–6.1%,
while GPU time changes from 1% higher to 5.9% lower. Fixed Avocado FPS remains
near 72, with GPU timings 7.8–18.9% higher. The native prototype also changes
program binding and material uniform writes, so these measurements describe the
whole path rather than isolated shader arithmetic. Short runs, moving geometry,
driver scheduling and changing clocks limit interpretation; they do not prove
that every specialized shader implementation would have the same result.

All cases retain 128 objects, have sufficient valid GPU samples and no checked
GL/disjoint/fence/native errors, and return grouped charges to zero at shutdown.
Peak grouped charges are 468 MiB for fixed scenes and 418 MiB for adaptive mixed
scenes, within the 512 MiB limit. No demo remains running. No engine/API default
or deployed production rendering behavior changes as a result of this experiment.

## Reproduction and follow-up

See [test instructions](../README.md#opaque-shader-specialization-experiment).
`SF_GPU_OPAQUE_SHADER=1` exists only in the excluded `gpu-sync-experiment` target.
The native benchmark refuses any non-opaque asset; it is not a general-purpose
renderer setting. Its programs are created only when enabled and destroyed
before graphics teardown. The separate pixel harness exercises generic fallback
for masked/blended parts.

The [raw summary](opaque-shaders-frame.json) retains paired results. Frame logs
remain under `/tmp/sf-opaque-comparison`, with build output in
`/tmp/sf-opaque-build.log`, pixels in `/tmp/sf-opaque-pixels.log`, existing renderer
checks in `/tmp/sf-opaque-validation.log`, and rejection in
`/tmp/sf-opaque-reject.log`. The engine base is `c7de226` plus this experiment.

Keep specialization deferred. The next candidate is conservative visibility
within model primitives, measured against both opaque and repeated occluder work.
Any such change must fall back safely for near-plane/invalid bounds and changing
animated geometry, preserve masked holes, and demonstrate exact output and a
matched throughput benefit before adoption.
