# Prepared PBR program policy

Steam Frame ARM64, 2026-10-29. This closes the first-use compilation and lighter
workload gaps from the [lazy variant prototype](pbr-variants.md).

## Policy

Prepare programs after initial asset upload, before starting the frame loop, and
after successful script reload. Visit scene objects/materials in their existing
order and prepare current feature keys with contributing-light counts four
through eight, bounded to 16 slots including failures. Skip preparation when the
scene starts with fewer than four active/nonzero lights. Unlit materials do not
consume slots. Optional preparation failure clears the cache and uses general PBR.

Rendering performs lookup only. Ready programs require >=1024 allocated pixels
per eye, >=4 contributing lights and a lit material. Unknown material/environment/
count keys and overflow use general PBR without compiling. The key stores flags,
not scene pointers; removal and restore safely reuse ready keys. Failed reload
keeps the cache intact; successful reload replaces it. Program deletion occurs
before EGL context teardown. The cap does not bound opaque driver cache memory.

This policy deliberately leaves some workloads on general PBR, including runtime
states not represented during preparation. It does not lower visual quality or
introduce a public script option. Environment/model texture uploads retain their
existing behavior; eliminating shader compilation does not promise that all
asset-loading costs disappear.

## Prepared-path checks

The comparison harness asserts compile time remains unchanged across all timed
reference/candidate draws. Additional checks require general fallback for small
surfaces, zero lights and an unknown environment key, with no new compilation.
The lazy variant's strict pixel, cap, failed-compile and deletion comparisons
remain available as reference checks.

The bounded real-engine lifecycle check captures/restores four lights and a car,
disposes the original container, introduces a new material/environment state,
rejects an invalid script reload and accepts a subsequent valid reload. It
requires exactly two preparation generations (initial and successful reload),
no later compile-time increase, nonzero general fallback, clean GL/disjoint/fence
status, normal exit and zero grouped charges. See
[lifecycle evidence](pbr-prewarm/lifecycle-frame.txt).

## Matched engine measurements

Same 32 moving full-detail cars, fixed group, cache/fetch optimization, ordered
instancing, zero/eight mixed lights, geometry and quality as the original native
comparison. Each run lasts 16 seconds, with GPU/work windows from six seconds.
The second repetition reverses light-count and reference/candidate order.
All pairs match geometry and owned storage: 3,485,952 scene triangles,
6,971,904 stereo member triangles, 192 opaque draws, 488.29 MiB estimated owned
GPU storage and a 468 MiB grouped peak. No external occluders are drawn.

| Lights | Repeat | FPS general / prepared | GPU ms general / prepared | Render/completion ms general / prepared |
| --- | --- | --- | --- | --- |
| 0 | 1 | 34.57 / 34.58 | 21.44 / 21.30 | 28.40 / 28.54 |
| 0 | 2 | 33.48 / 28.80 | 22.40 / 27.91 | 29.25 / 34.07 |
| 8 | 1 | 25.46 / 28.89 | 31.40 / 26.26 | 39.11 / 34.18 |
| 8 | 2 | 26.28 / 27.61 | 32.09 / 28.47 | 37.67 / 36.04 |

Eight-light GPU time improves about 11–16%; render/completion improves about
4–13%. FPS improvements are workload-specific. Zero-light runs compile no variants
and execute the identical general GPU path; the second zero-light pair still
varies substantially. Live head pose, workload phase, power, scheduling and thermal
state are not locked. Do not attribute that zero-light variance to a different
shader or claim a zero-light speedup. Render/completion sums enqueue and wait
medians; it is not CPU execution alone or compositor latency.

Eight-light preparation owns 15 programs, with no draw-time cache misses in this
profile. [Native summaries](pbr-prewarm/live-frame.json). Raw logs remain on Frame
under `/tmp/pbr-prewarm-live`, `/tmp/pbr-prewarm-offscreen.log` and
`/tmp/pbr-prewarm-lifecycle.log`.


## Final implementation verification

The normal engine uses `src/pbr_variants.h`; experimental lazy compilation and
failure injection remain available only to comparison targets. Expanded GLES
checks pass 2,048 images across normal/mirrored normal maps, negative scales,
emissive materials, untextured metallic/roughness spheres, car/morph/alpha cases,
lighting counts and HDR/unlit states: alpha exact, maximum RGB difference one byte
(9,865 RGB bytes differ). The prepared profile asserts no compilation after warmup
preparation and explicitly checks small-surface/zero-light/unknown-environment
fallback. Point-light, spotlight and shared stereo-occlusion regressions pass.
The real-engine lifecycle check passes again with the final shared implementation.
Native production/comparison builds, Python/script syntax, documentation lint,
site build and whitespace checks pass. Final normal-engine visual acceptance
is recorded below.


## Final headset acceptance and adoption

On 2026-10-29 the user confirmed “Everything works—no new pauses” using the
normal `pocketspace` build and [interactive scene](../scenes/pbr-variants.js).
Instructions covered model/light-handle grabs and rotation, distance and head
motion, alpha cutouts/overlaps, reflections, eight/four/zero lights and HDR
switching. Runtime diagnostics confirmed ready specialized draws were active,
about 90 engine FPS and no GL errors. This acceptance is separate from measured
performance and the automated pixel/lifecycle checks.

**Adopt the prepared, guarded policy as the production default.** The lazy
first-use compilation prototype remains test-only. No unresolved shader-variant
adoption item remains on the roadmap. The known bounded fallback behavior is part
of the supported policy, not a requirement to compile every possible state.

The demo was stopped immediately after verification and its exit independently
confirmed. It completed 8,421 frames and released grouped charges to zero
(132 MiB peak). Raw log: Frame `/tmp/pbr-prewarm-demo.log`.
