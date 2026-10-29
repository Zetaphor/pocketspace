# Remaining work

Implemented functionality belongs in the [API guides](javascript-api.md).
This roadmap contains future work, without prescribing an implementation order.

## Rendering and animation

- Finish production readiness for GPU skeletal skinning. The isolated
  [Frame prototype](../tests/results/gpu-skinning.md) improved 32 animated
  CesiumMan instances from 50.4 to 88.3 FPS with conservative joint bounds and
  lazy exact picking; correctness checks and the 32-model visual check passed.
  Deterministic ray-candidate costs and synthetic multi-part/material checks
  are recorded in the comparison. Validate active grabbing and broaden real-world
  model coverage before adoption. Support additional influence sets and
  morph-before-skin combinations, or retain explicit CPU fallback. Preserve
  transitions, per-instance poses, resource lifetime and coherent stereo
  color/depth rendering. Normal builds still use CPU skinning.
- Profile larger rigid-node and morph workloads before moving their vertex
  deformation to the GPU; the completed workload establishes a skeletal cost,
  not a bottleneck for every animation path.
- Full material lighting/PBR, including normal/tangent support and a physical
  replacement for the current approximate sheen tint.

## Scripted experiences

- Extend [rigid-body physics](physics-api.md) with deforming collider policies
  as scripted experiences require them.
- Extend the implemented [scene containers](scene-containers.md) with nested
  ownership or explicit shared-asset lifetime controls if larger experiences
  require them.

## Tracking and input

- Expose additional tracking signals only when a usable backend is available;
  hand skeletons, bare-hand optical tracking, and eye openness are not exposed.
- Improve personal calibration robustness across head movement and headset refits.
- Maintain compatibility with the undocumented vrserver controller stream and
  evaluate documented alternatives that support overlay input requirements.

## Performance validation and profiling

- Further reduce the cost of dense high-triangle model scenes; the
  [matched GPU mesh optimization results](../tests/results/vertex-cache.md) still
  fall below headset frame rates. The
  [bounded-error GPU LOD investigation](../tests/results/lod.md) improves
  both measured model workloads; the default automatic LOD headset check
  is accepted. Further investigate repeated occluder draws; object count alone is not a
  sufficient workload budget. Validate larger detailed-model counts after reducing
  their rendering cost; the accepted detailed headset case contains 32 objects.
  Preserve the common projection plane and coherent depth-cutout reprojection.
  Consider wider regrouping searches or changed cost thresholds only if further
  measurements justify them.
- Extend bounded stability checks to longer sessions or larger workloads when
  needed. Revisit allocation-site profiling if live heap totals grow or memory
  pressure appears; the completed reload/RSS investigation is recorded in the
  [test results](../tests/results/memory-reload.md).

## Conditional performance investigations

These are deferred investigations, not unimplemented fixes ready to enable.

- Investigate GPU visibility culling and LOD selection only when larger workloads
  show meaningful CPU selection cost. Include dispatch/submission overhead and
  avoid synchronous GPU readback; preserve draw order and one coherent per-object
  choice across eyes, surfaces and depth passes. Current CPU selection is not an
  established bottleneck.
- Consider GPU physics only if profiling establishes a CPU simulation bottleneck
  that the existing Jolt worker path cannot address. This would be a substantial
  backend redesign, not a switch in Jolt; account for contacts, queries, script
  callbacks and CPU/GPU synchronization before comparing end-to-end throughput.
- Consider GPU LOD generation only if model loading becomes a significant
  problem. Generation currently runs once at upload, so this targets loading
  latency rather than steady-state FPS. Compare offline asset preprocessing and
  CPU generation improvements before adding a GPU implementation; preserve
  material boundaries, UV quality, error limits and the retained full mesh.

- Revisit multi-part batching only with evidence that lower submission overhead
  outweighs material selection and additional geometry-buffer costs. The
  [combined multi-material stream experiment](../tests/results/multipart-instancing.md)
  preserved exact pixels but slowed the interleaved detailed-model scene.
  Production keeps ordered instancing limited to eligible single-part models.

- Revisit spatial occluder clusters only if a workload offers substantially more
  selective rejection or cheaper bounds processing. The
  [completed 4,096-triangle cluster comparison](../tests/results/cluster-visibility.md)
  preserved geometry and pixels, but did not improve net throughput consistently.
- Revisit material-primitive occluder rejection only for workloads with more
  spatially separated parts. The [completed comparison](../tests/results/primitive-visibility.md)
  preserved pixels and skipped triangles, but did not show a consistent
  throughput benefit on the moving ToyCar/Avocado scene. Production retains
  object-level bounds culling.
- Revisit opaque/depth shader specialization only with evidence of a larger,
  consistent benefit. The [completed comparison](../tests/results/opaque-shaders.md)
  preserved pixels but produced small dense-scene gains and higher GPU times
  in the lighter scene; production retains the general material shader.
- Revisit scene-depth reuse only if profiling shows repeated occluder work remains
  costly after bounds culling. The [prototype investigation](../tests/results/shared-depth-prototype.md)
  is complete and did not justify adoption. A new design must establish compatible
  pixel grids and correct equal-depth ownership while preserving per-eye
  opaque/masked occlusion.
- Investigate asynchronous shared-texture submission only after establishing
  explicit producer/compositor ownership, buffer-reuse, and latency guarantees.
  The [synchronous fence comparison](../tests/results/gpu-sync.md) is complete and
  showed no repeatable throughput benefit. Retain `glFinish` unless a replacement
  demonstrates a repeatable measured benefit.
