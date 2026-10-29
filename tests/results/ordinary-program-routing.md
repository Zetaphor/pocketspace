# Mixed ordinary/GPU vertex-program integration

Follow-up to the [static shader prototype](dense-model-current.md), measured on
Steam Frame on **2026-10-29**. The 16-car prototype had visual acceptance, but
production integration also needs mixed backend correctness and measured benefit
once routing/state costs are included.

**Decision: do not enable this optimization in production.** The integrated
routing passes its correctness checks, but reversed-order comparisons do not
establish a consistent throughput improvement. Routing and its environment
switch remain inside the excluded `gpu-sync-experiment` build. Normal rendering
continues to use the existing general vertex program.

## Integration and ownership

The isolated integration uses an ordinary primary program for static geometry,
CPU rigid/morph animation, and CPU skinning fallback. GPU-skinned poses select the
general skin-capable program. Cached locations rebind the complete view basis,
eye, projection width, object transform, material, sampler, and skin state.
Consecutive GPU draws can retain their binding inside a pass; ordinary draws,
instanced batches, and pass completion restore/invalidate that binding correctly.

`OrdinaryModelProgram` owns its linked ordinary program or borrows the existing
general program without owning it. It stores no scene/pose pointers. Host programs
may remain across successful reloads; failed reload preserves the active scene
and program. Scene restore reconstructs poses/palettes/streams independently.
Ordinary shader compile/link failure falls back to the general program, and
shutdown releases the owned program once. Mixed routing cannot combine with the
separate whole-program, opaque-shader, or visibility experiments.

The first integration switched to/rebound the ordinary program for each ordinary
object. This increased enqueue work and failed to retain the prototype benefit.
The revised primary-program design avoids those switches for ordinary scenes;
the final correctness path also handles actual instancing between GPU draws.

## Matched native measurements

The existing deterministic moving workload runs 16 seconds per mode, with mesh
cache optimization, automatic LOD and ordered instancing enabled. Two rounds
reverse both case and per-case pair order using stable case indices. Runs clear
inherited `SF_GPU_*` overrides. FPS uses late samples after five seconds; GPU
medians discard six seconds. Pair storage estimates and grouped peaks match.

| Scene | Pair | General FPS | Integrated FPS | General GPU ms | Integrated GPU ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128-models-fixed | 1 | 38.68 | 40.94 | 16.61 | 16.45 |
| 32-cars-fixed | 1 | 58.98 | 63.42 | 11.43 | 9.91 |
| 128-models-fixed | 2 | 42.07 | 39.68 | 15.70 | 17.02 |
| 32-cars-fixed | 2 | 64.70 | 52.85 | 8.19 | 11.47 |

Dense models improve approximately 5.8% in the first pair and regress 5.7% in the
reversed pair. Cars improve 7.5% in one pair and regress 18.3% in the other. The
first integration also failed to establish a useful consistent benefit; its
[raw measurements](ordinary-program-routing/initial-frame.json) are retained
separately from the [revised primary measurements](ordinary-program-routing/primary-frame.json).
Do not pool the earlier prototype's favorable samples with these comparisons.

The dense case retains 473.49 MiB estimated GPU allocation and a 468 MiB grouped
peak; cars retain 457.48 MiB and the same grouped peak. No storage or quality
reduction is claimed. Shader/driver metadata is outside the estimate. All timed
runs retained expected objects, had checked clean native/GL/disjoint/fence status,
exited normally and released grouped charges to zero.

These short runs do not lock head pose, thermal/power state or other GPU activity.
The selected automatic LOD policy is identical, but actual per-frame LOD selection
is not independently recorded in this runner. Allocation matching alone does not
prove identical raster/triangle work. The variation does not establish that the
candidate caused every regression; it does mean the evidence is insufficient to
adopt it. GPU query medians include scheduling and are separate from compositor
latency or sustained headset FPS.

The latest correctness revision retains GPU bindings across consecutive draws;
these ordinary-model workloads do not exercise that GPU-only state optimization.
A future adoption attempt would also need matched skeletal/mixed measurements.

## Validation

Final native checks and tested binary hashes are recorded in the
[validation artifact](ordinary-program-routing/validation-frame.txt). The normal
executable rebuilt successfully with routing compiled out. The final integrated
regression passed 148 exact mixed views and ten reload/restore cycles; existing
GPU skinning (18,000 picking comparisons and 30 lifecycle cycles), rigid batching
(864 exact views), and instancing (483 exact views) regressions also passed.
A bounded real-host smoke run rendered 18 mixed actors across nine assets and two
groups, with no pointer capture, lasers, or input callback. Sampled GL/fence status
was clean; normal shutdown released grouped charges to zero. No demo remains.

The mixed regression renders static, GPU-skinned, CPU-skinned, rigid and morph
objects through the real GLES passes, alongside actual instanced batches and LOD
ranges. It checks exact output, general-program restoration, cleared instance/skin
attributes, owned/borrowed program lifetime, failed reload rollback, independent
restored poses and rendering after restore. Automated correctness is separate
from visual acceptance of the earlier static-only prototype; no new mixed
headset visual acceptance or production default change is claimed.

## Reproduction and next scope

Build `ordinary-program-regression` and `gpu-sync-experiment`. Use
`compare-static-vertex.py --mixed-routing` (`--production-routing` is an alias)
for the integrated comparison. The historical JSON field `productionRouting`
identifies this integration form, not a shipped/default-on policy.
`SF_GPU_ORDINARY_VERTEX=1` exists only in the excluded executable.
The optional [mixed interactive scene](../scenes/ordinary-program-mixed.js)
exercises grabbing, pause/resume and save/restore when explicitly running that
experimental mode; it is not needed to enable a feature in the normal engine.

The repeatable-benefit requirement remains unmet, so this integration
investigation is complete without adoption. Revisit only with better-controlled
matched geometry/raster workloads and a consistent end-to-end gain. Dense-model
rendering and repeated occluder cost remain open. Raw logs remain on Frame under
`/tmp/sf-ordinary-production-compare` and `/tmp/sf-ordinary-primary-compare`.
