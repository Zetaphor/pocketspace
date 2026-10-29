# Pass-local draw uniform caching

Measured on Steam Frame on 2026-10-28 at `f831f42`. This experiment is complete
and does not justify production adoption. Normal rendering keeps its original
uniform submissions; cache code and counters compile only into the excluded
`draw-submit-experiment` target and native test harnesses.

## Candidate and checks

Cache exact object/material values within one draw pass and avoid redundant
uniform calls. Reset validity for every pass/eye/surface; never cache texture or
buffer bindings. Instance draws use their own programs and restore the baseline
program without changing its uniforms. Skip object uniform writes when it has
no primitive in that pass. No draw sorting, batching, shader, geometry, detail,
alpha convention, buffer lifetime or synchronization changes.

The native real-GLES regression passed 288 **exact** stereo image comparisons
on both the workstation and Frame: Box, ToyCar, AlphaBlendModeTest,
InterpolationTest, Fox and AnimatedMorphCube; opaque/masked/blended materials,
CPU rigid/morph and GPU skinning, depth occlusion, instancing switches, and
external uniform writes between passes. Direct checks verify redundant calls
are skipped and changed material fields are submitted. The existing instancing
regression also passed 483 exact views locally.

Review exposed a separate CPU upload compatibility issue: hand-constructed pose
geometry had no per-part upload state. `5d8b43f` initializes that state and retains
full uploads when no evaluated cache exists. This fix remains in production;
it is independent of this rejected uniform optimization.

## Method

- ARM64 Steam Frame, zink/Turnip Adreno 750, GLES 3.2 Mesa 26.3.0-devel
  (git-abc426bb62); no runtime power/governor settings changed.
- Same executable/scene with `SF_DRAW_UNIFORM_CACHE=0` then `1`. Production rigid
  primitive caching is enabled in both. 256 independent InterpolationTest actors,
  Linear Scale clip 1, one render group, 768 reference eye pixels, same grid,
  scales/materials, one HUD, no physics.
- Each run: 20 seconds animated then 20 paused. User was asked to keep looking
  at the models; both runs completed and processes were stopped/verified.
- Frame-weight 120-frame windows starting after five seconds warmup: two retained
  windows / 240 frames per stage. One matched pair, not statistical testing.
- CPU elapsed durations; completion wait is not a GPU timestamp. Matching
  allocations do not fix live head pose or exact animation timing.

## Results

All times are mean ms/frame. Work excludes final pacing sleep.

| Mode | Playback | FPS | Render submission | Completion wait | Work |
| --- | --- | --- | --- | --- | --- |
| Full | Animated | 20.59 | 21.248 | 24.862 | 48.561 |
| Cached | Animated | 20.33 | 19.412 | 27.221 | 49.181 |
| Full | Paused | 21.33 | 21.327 | 24.676 | 46.879 |
| Cached | Paused | 20.74 | 19.591 | 27.655 | 48.222 |

Periodic counters consistently show 25,600 uniform calls/frame in full mode and
1,550 in cached mode: 94% fewer. Of the removed calls, 21,490 are value-cache
hits; 2,560 are object writes in empty blended passes that are no longer
attempted. Draw counts/order remain unchanged (5,120 primitive draws across two
eyes in this grouped opaque workload).

Animated render submission falls 1.84 ms, but completion wait rises 2.36 ms.
End-to-end throughput is slightly lower in both cached stages, with no clear
benefit. These wait durations cannot establish that GPU execution became slower;
producer work and synchronization overlap differently. Do not adopt the candidate
based on call counts or CPU submission time alone.

All stages end at 213,036,052 bytes estimated engine GPU allocation. Animated
uploads match exactly at 184,320 bytes / 9,216 updated vertices per frame.
Endpoint RSS is about 74.8 MiB in both modes; driver VRAM totals unavailable.
No density/detail setting was changed. The result does not establish a general
regression for every scene, only absence of a measured benefit here.

## Decision and reproduction

Keep this bounded experiment and correctness check for future investigations,
but compile its cache machinery out of normal rendering. No additional visual
acceptance is required for an optimization that is not being adopted. Next
investigate actual primitive draw/vertex-state submission cost or narrowly
ordered draw reductions with a new design; the prior combined material stream
is not justified by this result.

- [Instructions](../README.md#draw-uniform-submission-comparison)
- [Full log](draw-uniforms/full-frame.txt), [summary](draw-uniforms/full-summary.json)
- [Cached log](draw-uniforms/cache-frame.txt), [summary](draw-uniforms/cache-summary.json)
- [Frame pixel result](draw-uniforms/pixels-frame.txt)

The post-isolation normal engine builds locally and all 288 pixel comparisons
still pass. Documentation lint/site build and whitespace checks pass.
