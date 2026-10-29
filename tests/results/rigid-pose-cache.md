# Unchanged rigid primitive update cache

Measured on Steam Frame on 2026-10-28: executable `6ef48fb`, corrected scene
`d17c18d`. The excluded `rigid-pose-experiment` target toggles cached/full updates
with `SF_RIGID_POSE_CACHE=1/0`. Production default remains full updates pending
headset visual acceptance.

## Behavior and correctness

Per-pose primitive caches compare exact world matrices, including ancestors.
Only non-skinned, non-morph parts can skip deformation. Cache exact part bounds
and maximum vertex radius, then union them without changing global geometry
bounds. Refit the BVH whenever any part changes; skip it only when all parts
remain unchanged. Pending primitive upload bits survive multiple evaluations
before rendering. Every VBO is initially allocated/uploaded, retaining draw
order, shared material ownership, geometry detail and allocation estimates.

Native workstation and Frame checks compare cached/full CPU vertices, exact
bounds/radius, every BVH node bound, and actual mapped VBO contents over all nine
InterpolationTest clips, endpoints, repeated timestamps and frozen transitions.
All pass. Existing CPU animation and transition tests pass locally with caching
enabled, as does the 21,000-ray GPU skinning suite and its 30 mixed-scene
reload/restore cycles. Morph and skin deformation still follow their existing
paths. This is geometry/buffer equivalence, not a new stereo pixel capture.

## Workload and method

- ARM64 Steam Frame; unchanged runtime settings, zink/Turnip Adreno 750,
  GLES 3.2 Mesa 26.3.0-devel (git-abc426bb62).
- Same binary, 256 independent InterpolationTest actors in one render group,
  768 reference eye pixels, identical 16x16 grid/scale/materials. One HUD,
  no physics. **Linear Scale**, clip index 1, staggered phases.
- Full-update run then cached run; each 20 seconds animated, 20 paused. User was
  asked to keep looking at models. Both runs completed; profiler processes stopped.
- Frame-weight steady 120-frame windows after five seconds warmup. Two retained
  windows (240 frames) per stage. One pair, not a long-session significance study.
- An earlier setup labeled Linear Scale but played clip zero (STEP). It was
  discarded. The corrected scene explicitly plays clip 1; its regression checks
  the playback index as well as the metadata label. Only corrected logs are kept.
- Timings are CPU elapsed ms/frame; completion wait is not a GPU timestamp.

## Results

| Mode | Playback | FPS | Pose | Deform + bounds | BVH | Upload submission | Render submission | Completion wait | Work |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Full | Animated | 18.94 | 0.127 | 1.000 | 0.444 | 5.654 | 20.147 | 24.403 | 52.786 |
| Cached | Animated | 20.94 | 0.138 | 0.245 | 0.786 | 0.566 | 20.665 | 24.390 | 47.749 |
| Full | Paused | 21.68 | 0 | 0 | 0 | 0 | 21.045 | 24.194 | 46.127 |
| Cached | Paused | 21.45 | 0 | 0 | 0 | 0 | 21.655 | 24.303 | 46.620 |

Animated throughput improved 10.5%; work fell 9.5%. Updated vertices and uploaded
bytes fell 89.1%: 84,480 to 9,216 vertices and 1,689,600 to 184,320 bytes/frame.
All 256 poses still update each frame, but only one of ten primitive buffers
changes in this clip (256 versus 2,560 primitive uploads). Upload submission
fell about 90%. This benefit does not rely on STEP holds.

BVH time increased in this run; the implementation still refits the complete BVH
when a part changes. Do not claim a selective BVH optimization or explain this
variation without additional measurement. Draw submission remains about 20 ms
and dominates the remaining CPU work. Paused throughput did not improve.

All four stages ended at exactly 213,036,052 bytes estimated engine GPU allocation.
Geometry/buffers are equivalent and there is no detail/resolution setting change.
Endpoint RSS was 74.9 MiB full versus 74.7 MiB cached; these samples do not
establish memory savings. Driver memory totals were unavailable. Live head pose
and animation timing can vary pixel work despite matching allocations.

## Evidence and next step

- [Full filtered log](rigid-pose-cache/full-frame.txt), [summary](rigid-pose-cache/full-summary.json)
- [Cached filtered log](rigid-pose-cache/cache-frame.txt), [summary](rigid-pose-cache/cache-summary.json)
- [Frame correctness checks](rigid-pose-cache/correctness-frame.txt)
- [Run instructions](../README.md#rigid-primitive-update-comparison)

Reproduce summaries with `tests/analyze-animation-profile.py`. Scene, analyzer,
documentation lint and site checks pass. Next validate animation blending and
pause/resume visually with caching enabled, then adopt the cache if accepted.
Further optimization should target draw/uniform submission independently.
