# PBR visibility-invariant reuse

Steam Frame ARM64, 2026-10-29. Offscreen EGL/GLES, no overlays or input capture.
This comparison evaluates a test-only shader change; production defaults remain
unchanged unless adoption is established separately.

## Candidate and controls

The shader's visibility term repeats `sqrt(nv*nv*(1-a2)+a2)` for the directional
light and each scene light. The candidate names this per-fragment value once and
reuses it. The first candidate preserves the expression, precision and arithmetic order.
It passes all 512 pixel comparisons byte-identically in both runs, but shows no
consistent timing improvement. [Root-only stage evidence](pbr-light-invariant/root-only-frame.json)
retains both runs. The final candidate additionally factors
`(1-metal)*baseColor/PI` out of the directional and per-light diffuse terms.
This preserves the mathematical BRDF while changing floating-point grouping;
it is subject to the same strict pixel gate. Light order and range/cone falloff
stay unchanged. There is no
CPU/GPU transfer, new buffer or quality setting. Compiler common-subexpression
elimination or loop-invariant motion may already remove this duplication in the
reference; fewer source expressions alone are not evidence of faster execution.

The excluded `pbr-light-invariant-experiment` alone defines
`SF_PBR_LIGHT_INVARIANT_EXPERIMENT`. It holds reference/candidate program objects
in the same real GLES renderer, sharing geometry and framebuffer allocations.
Normal builds have no runtime switch for this candidate.

The pixel gate covers 512 reference/candidate comparisons: ToyCar,
NormalTangentTest, morphing SurfaceFrames and AlphaBlendModeTest, zero/one/four/eight
mixed lights, attached spots, four Euler poses, both eyes, full/LOD-2 geometry,
and grouped/separate targets with external occlusion. Alpha must match exactly;
RGB tolerance is one byte. Profiling starts only after that gate passes.

Performance uses 32 deterministic rotating cars, full/LOD-2 geometry,
768/2048-square viewports and zero/eight mixed lights. The framebuffer stays
4096×2048; assets, sampling density, material/light settings, geometry and poses
match between modes. ABBA stages interleave within each pose, with 30 warmup and
90 measured frames per stage, alternating eyes. GPU disjoint and GL errors fail.
Wall time includes rendering and `glFinish`, not only CPU execution. Two complete
processes repeat the comparison. These offscreen timings are not headset FPS,
compositor latency or sustained thermal evidence.

## Measurements and decision

The root-only version is byte-identical across all 512 images in both runs.
Its 2048-pixel, eight-light/full-detail GPU reductions are .7% and 1.4%, while
wall reductions are 1.3% and -.4%. Other cases include regressions; this does not
establish a general throughput gain.

The factored version passes all 512 images in both runs with exact alpha,
26 changed RGB bytes and a maximum one-byte RGB delta. The table compares the
means of each run's two reference and two candidate stages; positive percentages
mean reduced time, negative mean regressions. They are not confidence intervals.

| Lights | LOD | Pixels | GPU reduction %, runs 1 / 2 | Wall reduction %, runs 1 / 2 |
| --- | --- | --- | --- | --- |
| 0 | Full | 768 | .3 / -.8 | .0 / -.4 |
| 0 | Full | 2048 | .6 / -3.7 | .8 / -2.0 |
| 0 | 2 | 768 | -1.6 / -.7 | 2.1 / .6 |
| 0 | 2 | 2048 | -3.6 / 4.4 | -1.3 / 3.2 |
| 8 | Full | 768 | 5.0 / -5.0 | .7 / -1.6 |
| 8 | Full | 2048 | 1.0 / -1.1 | 2.4 / -2.1 |
| 8 | 2 | 768 | -1.2 / -1.5 | 1.2 / -1.1 |
| 8 | 2 | 2048 | -.6 / .2 | .0 / -.3 |

**Do not adopt either version.** Passing arithmetic tolerance is insufficient
without a repeatable performance benefit. The final factored candidate remains
available only in the excluded target; there is no public switch. Results are
compatible with the compiler already reusing important terms, but do not prove
its generated instruction schedule.

Bounded evidence: [root-only stages](pbr-light-invariant/root-only-frame.json),
[factored stages](pbr-light-invariant/factored-frame.json). Each file retains
64 stage rows across two processes. The JSON `prepared` flag means the candidate
is enabled; it does not mean CPU preparation. Raw logs remain on Frame at
`/tmp/pbr-light-invariant-{1,2}.log` and `/tmp/pbr-light-factored-{1,2}.log`.

The next candidate is bounded PBR material/light-count shader specialization,
which can remove unused paths at compile time. It needs its own evidence for
program memory, state routing, mixed materials, scene light changes, exact
coverage, and native throughput. This report does not justify enabling the
previously rejected general opaque-shader or rotation experiments.

## Verification

Both candidates pass the pixel gate and complete repeated ABBA profiling with
clean GL/disjoint status. The normal `pocketspace` target builds, and the shared
GLES occlusion regression passes stereo/alpha/scissor, indexed geometry and cleanup
checks. Documentation lint/site build and whitespace checks pass. All native
processes exited; no demo remains running. No new interactive acceptance is
requested for an unadopted candidate.
