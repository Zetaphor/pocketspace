# Owned vertex array submission experiment

Measured on Steam Frame on 2026-10-28 at `009fb0e`. The investigation is complete
and does not justify adoption. Production vertex setup remains unchanged.

## Candidate and correctness

The excluded `SF_VERTEX_ARRAY_EXPERIMENT` creates owned GLES vertex array objects
(VAOs) for static asset buffers and per-instance expanded CPU buffers. Each VAO
retains position/UV layout and the appropriate index buffer; LOD levels receive
separate VAOs. Cached draws bind the VAO, issue the original draw, then restore
VAO 0 before panel, GPU-skin or instancing work. GPU skin retains its existing
path. No global cache keyed by GL names crosses resource/context destruction.
VAOs are deleted before their owning buffers; normal builds have no VAO fields,
setup, draws or mode switch from this experiment.

Both comparison modes allocate the same VAOs during upload, isolating draw-time
binding cost. Geometry, buffers, shader, material order, draw count and quality
settings are identical. CPU-rigid draws replace one array-buffer bind plus two
attribute-pointer calls with VAO bind/restore: one fewer setup call per draw,
without reducing the 5,120 primitive draws per frame in this scene.

Workstation and Frame regressions passed **864 exact stereo image comparisons**:
Box, ToyCar, AlphaBlendModeTest, InterpolationTest, Fox and AnimatedMorphCube;
opaque/masked/blended materials, CPU/GPU animation, instancing switches and all
three LOD choices. They verify return to VAO 0 and deletion of asset/pose VAOs.
The existing 483-view instancing regression and normal engine build passed
locally. These are native rendered checks, not a new user visual acceptance.

## Method

- ARM64 Steam Frame, zink/Turnip Adreno 750, GLES 3.2 Mesa 26.3.0-devel
  (git-abc426bb62). No power/governor settings changed for this run.
- Same binary/scene, `SF_VERTEX_ARRAY_CACHE=0` then `1`. 256 independent
  InterpolationTest instances, Linear Scale clip 1, one render group,
  768 reference eye pixels, identical 16x16 grid/scale/materials. One HUD, no
  physics; production rigid primitive caching enabled in both modes.
- Each run has 20 seconds animated then 20 paused. User was asked to keep looking
  at the scene. Both completed; processes were stopped and verified absent.
- Frame-weight 120-frame windows starting after five seconds warmup: two retained
  windows (240 frames) per stage. One bounded pair, not statistical testing.
- CPU elapsed timings. Completion wait is not GPU timestamp duration/utilization.

## Results

All durations are mean ms/frame; work excludes final pacing sleep.

| Mode | Playback | FPS | Render submission | Completion wait | Work |
| --- | --- | --- | --- | --- | --- |
| Ordinary | Animated | 20.71 | 20.648 | 24.910 | 48.276 |
| VAO | Animated | 20.74 | 20.564 | 24.970 | 48.208 |
| Ordinary | Paused | 20.77 | 21.996 | 25.181 | 48.137 |
| VAO | Paused | 20.87 | 21.827 | 25.052 | 47.916 |

Animated FPS differs only 0.14%; paused FPS differs 0.46%. These small differences
from one pair do not establish a meaningful or repeatable benefit. Render
submission and completion waits remain effectively unchanged. No production
adoption or additional headset visual check is warranted by these results.

All four stages end at 213,036,052 bytes estimated engine GPU allocation.
Animated work matches at 9,216 updated vertices and 184,320 uploaded bytes/frame.
Endpoint animated RSS was about 82.7 MiB ordinary versus 82.7 MiB VAO. Both modes
retain VAO storage, so this does not measure the cost of adding VAOs to production.
Opaque driver VAO metadata is not included in the buffer allocation estimate;
driver memory totals were unavailable. Matching allocations do not fix live
head pose/animation timing or guarantee identical pixel work during profiling.

## Decision and evidence

Keep the experiment and exact-pixel regression excluded from normal builds.
Uniform caching and vertex-layout caching have now both failed to establish a
throughput gain on this dense multi-part scene. Further work should investigate
actual draw count/driver submission cost and narrowly ordered reductions, with
new geometry/material/lifetime checks. Do not assume the earlier combined
multi-material stream would help; its prior comparison also rejected adoption.

- [Run instructions](../README.md#vertex-array-submission-comparison)
- [Ordinary log](vertex-arrays/full-frame.txt), [summary](vertex-arrays/full-summary.json)
- [VAO log](vertex-arrays/cache-frame.txt), [summary](vertex-arrays/cache-summary.json)
- [Frame pixel check](vertex-arrays/pixels-frame.txt)

Documentation lint/site build and whitespace checks pass. After keeping the
normal three-argument draw function unchanged, the local normal engine build
and all 864 experimental pixel comparisons are rechecked.
