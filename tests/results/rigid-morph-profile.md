# Rigid-node and morph animation profiling

Measured on Steam Frame on 2026-10-28 at `d4807c1`, using the excluded
`animation-profile-experiment` target and the current production backend.
Both sampled asset types use CPU deformation. No engine defaults changed.

## Workload and method

- ARM64 Steam Frame; zink Vulkan 1.4 / Turnip Adreno 750, GLES 3.2 Mesa
  26.3.0-devel (git-abc426bb62). No governor changes were made for this run.
- InterpolationTest: 10 primitives, 110 triangles / 330 expanded vertices per
  instance. AnimatedMorphCube: one primitive, 12 triangles / 36 expanded vertices,
  two morph targets. First clip, staggered independent phases.
- Eight 20-second stages, 64 and 256 actors of each model, animated/paused.
  Alternating playback order is specified in the
  [scene](../scenes/rigid-morph-profile.js). One render group, 768 reference eye
  pixels, no physics and one HUD. The 64 actors form an 8x8 grid at 0.12 m spacing
  and scale; 256 form a 16x16 grid at 0.065 m spacing and scale. Do not compare
  count changes as identical model pixel workloads.
- User continued headset profiling and was asked to keep looking at the scene.
  The entire run completed; actors cleared and the process was stopped/verified.
  This is automated measurement, not a new user visual acceptance claim.
- Each native window contains 120 frames. Discard windows starting in the first
  five seconds of each stage, then frame-weight means. The dense rigid stages
  retain only two windows each; other stages retain 8–10. One bounded run, not
  repeated significance testing or a detailed-character benchmark.
- Times are CPU elapsed milliseconds. Upload submission includes driver blocking;
  completion wait is not GPU timestamp duration or utilization. Animated poses
  differ from paused ones, so phase timers are more informative than FPS deltas.

## Results

Work excludes the final pacing sleep. All time columns are mean ms/frame.

| Model | Actors | Playback | FPS | Pose | Deform + bounds | BVH | Upload submission | Render submission | Completion wait | Work |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| InterpolationTest | 64 | Animated | 67.91 | 0.039 | 0.279 | 0.137 | 1.197 | 3.129 | 9.543 | 14.720 |
| InterpolationTest | 64 | Paused | 71.70 | 0.000 | 0.000 | 0.000 | 0.000 | 3.162 | 10.378 | 13.939 |
| InterpolationTest | 256 | Paused | 21.26 | 0.000 | 0.000 | 0.000 | 0.000 | 21.721 | 24.414 | 47.030 |
| InterpolationTest | 256 | Animated | 19.18 | 0.196 | 1.033 | 0.509 | 5.753 | 20.842 | 22.734 | 52.131 |
| AnimatedMorphCube | 64 | Paused | 89.95 | 0.000 | 0.000 | 0.000 | 0.000 | 0.121 | 5.126 | 5.644 |
| AnimatedMorphCube | 64 | Animated | 89.91 | 0.028 | 0.052 | 0.035 | 0.038 | 0.154 | 4.992 | 5.665 |
| AnimatedMorphCube | 256 | Animated | 78.45 | 0.079 | 0.181 | 0.106 | 0.502 | 1.253 | 9.359 | 11.953 |
| AnimatedMorphCube | 256 | Paused | 79.93 | 0.000 | 0.000 | 0.000 | 0.000 | 1.414 | 9.597 | 11.471 |

At 256 rigid instances, the CPU evaluates 84,480 vertices and submits 1,689,600
bytes/frame. Each pose uploads ten expanded primitive buffers: 2,560 small
buffer updates per animated frame. Pose/deformation/refits total 1.74 ms;
upload submission adds 5.75 ms. Render submission costs 20.84 ms animated and
21.72 ms paused, independently establishing a large submission cost even without
animation evaluation. This workload has many primitives despite few triangles.

At 256 morph instances, 9,216 vertices and 184,320 bytes/frame are updated.
Pose/deformation/refits total 0.367 ms and upload submission is 0.502 ms.
Morph deformation/refits alone cost 0.287 ms. At 64 actors, animated and paused
both reach about 90 FPS. Moving these few vertices to the GPU is not justified
by this sample. It says nothing about high-triangle, many-target morph characters.

Paused windows confirm zero animation evaluations and uploads for both models.
Periodic engine diagnostics reported `gl_error=0`; no group density-cap/budget
warnings were emitted. That does not prove identical rasterized pixel work.

## Allocation and comparison limits

Final pre-transition HUD allocation estimates (bytes):

| Model | Actors | Animated | Paused |
| --- | --- | --- | --- |
| InterpolationTest | 64 | 171,922,964 | 171,922,964 |
| InterpolationTest | 256 | 173,190,164 | 213,036,052 |
| AnimatedMorphCube | 64 | 135,895,060 | 135,895,060 |
| AnimatedMorphCube | 256 | 171,684,884 | 171,684,884 |

The 256-rigid pair has different surface allocations. Its 19.18 versus 21.26 FPS
is **not a controlled attribution of animation cost** and must not be used as an
on/off optimization result. Per-phase CPU work remains measured, and the paused
case demonstrates high submission cost without deformation. Matching allocation
estimates in other pairs still do not imply identical live poses/pixels.

Process RSS reached about 74.3 MiB at the end, after 67.9 MiB in the 64-rigid
stage. Instance count and storage changed, so this is not a leak/stability test.
Driver VRAM totals were unavailable; the allocation field is an engine estimate.

## Next decision

Retain CPU rigid/morph deformation. Investigate repeated small primitive uploads
and multi-part draw/uniform submission for the dense rigid scene before porting
more vertex arithmetic. Consider skipping unchanged rigid primitive updates or
other bounded submission reductions, then compare equivalent layouts and exact
pixels. Prior multi-part batching experiments were not consistently faster;
do not silently adopt their combined stream based on this different workload.

The requested large-count rigid/morph profiling is complete for bundled feature
samples. Revisit GPU morph deformation when an actual higher-detail workload
shows substantial CPU deformation cost.

## Evidence and reproduction

- [Run instructions](../README.md#rigid-node-and-morph-profiling)
- [Filtered Frame log](rigid-morph-profile/profile-frame.txt)
- [Frame-weighted summary](rigid-morph-profile/summary-frame.json)

Regenerate with `python3 tests/analyze-animation-profile.py <log>`.
Scene/count/playback/cleanup and analyzer checks passed; documentation lint and
site build also pass. The normal engine has no phase timers.
