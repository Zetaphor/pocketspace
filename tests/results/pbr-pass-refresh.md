# Dense PBR pass attribution after shader preparation

Steam Frame ARM64, 2026-10-29, production source `9428323` plus the profiling-tool
changes in this report. The headset was active for bounded measurements. No
production rendering/quality policy changes are made by this investigation.

## Workload and verification

32 moving full-detail ToyCars, eight mixed point/spot lights, PBR, 768 reference
pixels per eye, cache/fetch optimization and ordered instancing. Fixed/adaptive
group cases use the existing time-based trajectories; the adaptive case enables
up to eight surfaces with normal split/merge/rebalance and memory admission.
Geometry stays full-detail deliberately, avoiding moving LOD thresholds. This is
high-triangle cost attribution, not a production automatic-LOD FPS claim.

The runner now explicitly enables prepared PBR in `gpu-sync-experiment`; clearing
inherited `SF_GPU_*` options previously also cleared this opt-in experiment flag.
Normal `pocketspace` already enables the policy automatically. Each completed run
reports 15 prepared programs, 441,495 program-binary bytes and no draw-time cache
misses. The diagnostic executable hash was
`2287b7435c9806f7b8d5ac05c6a07b2e7ff66f02845a39ba9d3efef36887fa37`.

Command: `profile-render-passes.py --case 32-cars-fixed --case 32-cars-adaptive
--no-lod --instancing --pbr-lights 8 --output /tmp/pbr-pass-refresh`.
Whole-render and per-pass runs each last 16 seconds. GPU/work windows start at
six seconds; per-pass queries sample one frame per second. Object counts,
GL/disjoint/fence status, stable/complete stereo geometry, paired storage and
shutdown accounting are checked. All completed runs terminate and release grouped
charges to zero; there is no controller interaction or persistent demo.

## Accepted attribution

| First matched pair | Whole-render GPU ms | Sampled occluder GPU ms | Sampled opaque GPU ms | Median per-frame occluder share |
| --- | --- | --- | --- | --- |
| Fixed group | 21.94 | .0016 | 20.53 | approximately zero |
| Adaptive groups | 25.26 | 11.05 | 14.96 | 51.9% |

The share is the median of per-sampled-frame ratios, not a ratio of the component
medians. Whole-render and pass medians are separate runs and cannot be subtracted
as exact overhead or projected savings. Pass queries exclude framebuffer setup,
clears and compositor work; GPU elapsed time includes scheduling effects.

Each mode submits 3,485,952 scene triangles and 6,971,904 stereo member triangles
in 192 opaque draws. The adaptive all-frame work samples additionally report
median 92.6–95.5 occluder draws and 3.36–3.47 million occluder triangles per stereo
frame. Sampled callback CPU medians are .20 ms for occluders and 1.05 ms for opaque
draws; these exclude CPU work outside those callbacks.

Within each accepted instrumentation pair, storage matches. Fixed/adaptive modes
use different allocations: 488.29 versus 400.29 MiB estimated owned storage and
468 versus 380 MiB grouped peaks. Their timing difference is therefore not an
equivalent-density comparison of grouping strategies. Do not recommend replacing
adaptive grouping with fixed grouping on these numbers alone.

## Rejected repetition

Round two's adaptive pair matches geometry and median owned storage, but grouped
peak allocation differs: 380 versus 508 MiB. The runner correctly rejects it and
stops before the second fixed pair. Its rows retain `matchingStorage: false` in
[the evidence](pbr-pass-refresh/summary-frame.json); its timing medians are not
used as accepted comparison evidence. Transient generations matter even when
median allocation agrees. Do not relax admission or comparison checks to retain
these measurements.

## Fixed-group repeat

A separate fixed-group instrumentation pair passes the same geometry/storage
checks: whole-render GPU 27.92 ms, sampled opaque GPU 26.70 ms and sampled
occluder GPU .0016 ms. The extra pair repeats whole/pass order rather than
counterbalancing it. Its absolute timings differ substantially from the first
pair despite identical submitted geometry/storage, reinforcing the live-view,
scheduling/power and workload-phase limitations. Both accepted fixed pairs
attribute almost all sampled GPU pass time to member rendering.

[Fixed-repeat evidence](pbr-pass-refresh/fixed-repeat-frame.json); raw logs on
Frame under `/tmp/pbr-pass-refresh-fixed-repeat`.

## Next target

Repeated external occlusion remains a substantial GPU cost after prepared color
shaders. The next candidate is a dedicated minimal **PBR depth-only vertex/fragment
program**, preserving full geometry and alpha-mask sampling while eliminating
unused color/surface-frame shader paths. Current fast-depth shading exits early
in the fragment program but retains the general PBR vertex/program interface.
Any gain needs pixel-exact alpha/stereo occlusion checks and matched comparisons
against the currently shipped fast-depth path, not an older full-color baseline.

This is an investigation target, not an adopted optimization or an instruction
to enable the previously rejected general opaque-shader/shared-depth prototypes.
Fixed groups still have expensive member work without external occluders, so
this will not solve every dense-model workload. No new interactive visual
acceptance is claimed for profiling alone.

[Bounded results including rejected rows](pbr-pass-refresh/summary-frame.json).
Raw logs remain on Frame under `/tmp/pbr-pass-refresh`.


## Verification

Both fixed pairs and the first adaptive pair pass the runner's native checks;
the second adaptive pair is explicitly rejected. Python compilation, render-work
analysis checks, documentation lint/site build and whitespace checks pass.
All engine/profile processes have exited. No new headset visual test is needed
for these tooling/report-only changes.
