# PBR submission cost triage

Steam Frame, 2026-10-29. **PBR instancing remains conditional; no batching
prototype or production change is adopted.** The measured render-thread
submission work is small relative to GPU rendering in the tested scenes. This
does not prove that instancing cannot help another scene or reduce CPU use;
it does not establish it as the next throughput fix for these workloads.

Production ordered instancing still excludes PBR. The previous
[multi-material stream investigation](multipart-instancing.md) covered ordinary
materials and is not evidence that lit-model batching works. A PBR implementation
would additionally need compatible per-object filtered light lists, material
maps/environment state, correct normal/tangent frames, and original object/part
ordering. Draw-count reduction alone is not a performance result.

## Measurement scope

The excluded `gpu-sync-experiment` now reports `enqueueThreadCpuMs` beside
`enqueueMs`. Both cover the render-submission interval before GPU completion;
thread CPU excludes sleeping/descheduling, while wall time includes them. Neither
covers the entire engine frame, script update, physics, input or earlier bounds
and light-list selection.

`SF_GPU_PROFILE_PBR_SUBMIT=1` samples one frame per second. On that frame,
`PBR_SUBMIT_SAMPLE` records render-thread CPU and draw counts in external-depth,
opaque-color and blended-color order. The scope includes ready-program lookup,
PBR view/object/light/material setup, the draw call, PBR release and restoration
of the original program. It excludes generic state work before that branch,
GPU execution and work on separate driver threads. Clock calls remain included;
these are diagnostic costs, not a prediction of removable CPU time. Allocation,
material quality, shader choice, geometry and draw order are unchanged.

CPU category medians, total sampled-frame medians and whole-window medians are
computed separately; do not add their medians as an exact frame decomposition.
GPU elapsed time and the `glFinish` wall wait are also different measurements:
waits can include queuing and scheduling. No CPU measurement establishes headset
visual correctness or a candidate's GPU/FPS benefit.

## Detailed-model controls

`profile-pbr-submission.py` uses the fixed-view/static-car fixture from the
[minimal-depth comparison](pbr-minimal-depth.md), with the shipped depth shader in
both modes. Each workload runs probes off/on/on/off, 16 seconds per stage, using
windows from six seconds onward. The helper rejects mismatched work, surface
generations/resolutions, owned allocations, grouped peaks, GPU errors, disjoints,
fence failures, early exits and incomplete cleanup. It requires the sampled PBR
draw counts to match all submitted draws in these all-PBR scenes.

| Workload | Probes | GPU (ms) | Enqueue wall (ms) | Enqueue thread CPU (ms) | Sampled PBR thread CPU (ms) |
| --- | --- | --- | --- | --- | --- |
| 32 cars, four groups | off | 43.278 | 3.233 | 1.720 | — |
| 32 cars, four groups | on | 43.354 | 3.301 | 1.763 | 1.746 |
| 32 cars, single group | off | 19.791 | 1.582 | .995 | — |
| 32 cars, single group | on | 19.737 | 1.567 | 1.000 | .895 |

Values average the two stage medians per probe mode. The small observed timing
changes do not establish zero probe overhead. Stage-level evidence is retained
in [controlled-frame.json](pbr-submission/controlled-frame.json).

Each scene retains 3,485,952 full-detail triangles and 192 stereo member draws /
6,971,904 triangles. Four groups add exactly 237 external-depth draws /
8,605,944 triangles, use 768 final pixels per eye, an 80 MiB grouped peak and
115,651,138 estimated owned bytes. The single group has no external depth,
1280 final pixels per eye, a 58 MiB grouped peak and 92,582,466 estimated owned
bytes. These two layouts are **not quality-equivalent grouping alternatives**;
compare probes within each layout. Prepared-color policy is enabled in both:
four-group surfaces are below its 1024-pixel threshold; the single group can use
prepared color programs. Fifteen color programs are prepared at startup.

## Lower-detail repeated-model check

A bounded 16-second `fast-motion.py --case 128-avocados-fixed` run uses eight
lights, PBR, vertex-cache ordering, automatic LOD and ordered instancing enabled.
PBR objects retain original draws. The scene uses live tracking and moving
objects; this is attribution, not a matched candidate comparison.

- 256 opaque stereo draws / 174,592 triangles, no external depth or blended
  draws; 87,296 scene triangles and 128 full-LOD objects in every late window.
- GPU median 5.974 ms; enqueue wall 1.761 ms; enqueue thread CPU 1.244 ms;
  sampled PBR thread CPU 1.210 ms.
- GPU-completion wall wait 11.498 ms, with .101 ms render-thread CPU during the
  wait. Do not treat their difference from GPU time as a measured cause.
- Observed FPS median 71.92, a 468 MiB grouped peak and 508.69 MiB estimated owned
  allocations. The group reaches the 4096-pixel ceiling and records replacement
  deferrals: this is not a full-density capacity claim or a new quality default.
  The native runner verified shutdown and zero remaining grouped charges.

[Avocado samples and workload metadata](pbr-submission/avocados-frame.json)
retain whole-window and sampled PBR values, ceiling warnings and deferrals.
Automatic LOD was enabled, but this sample remained at full LOD and allocated no
extra LOD index bytes. Main-thread submission is not the largest measured cost;
background driver CPU is not separately measured. Instancing could save some CPU,
but there is no demonstrated submission bottleneck or measured instanced GPU
benefit to justify implementing a broad PBR geometry path from these results.

## Reproduction and limits

Keep the headset active for normal power. From the repository root on Frame:

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/profile-pbr-submission.py --output /tmp/pbr-submit-profile
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  SF_GPU_SYNC_MODE=finish SF_GPU_PBR_VARIANTS=1 SF_GPU_PROFILE_PBR_SUBMIT=1 \
  SF_PBR_MINIMAL_DEPTH=0 SF_PBR_DEPTH_FIXED_VIEW=0 \
  python3 tests/fast-motion.py --engine build/gpu-sync-experiment \
    --case 128-avocados-fixed --vertex-cache --instancing --pbr-lights 8 \
    --seconds 16 --output /tmp/pbr-submit-avocados
```

Native diagnostic binary SHA-256:
`6419a1bddb388f34dc7b42b3e4dce8ce4bdaeb288d1c51560454ade232412f53`.
Clocks/thermals/compositor scheduling were not locked. All nine processes exited,
zero grouped-buffer charges remained, and no demo is left running. Native build,
Python compilation, documentation lint/build and whitespace checks passed.
No new interactive visual acceptance is claimed: the changes only instrument
excluded profiling code and add a measurement runner/report.
