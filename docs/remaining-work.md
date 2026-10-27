# Remaining work

Implemented functionality belongs in the [API guides](javascript-api.md).
This roadmap contains future work, without prescribing an implementation order.

## Rendering and animation

- Morph target geometry and morph-weight animation.
- Clip blending and transitions between animations.
- GPU skeletal skinning for larger animated scenes.
- Full material lighting/PBR, including normal/tangent support and a physical
  replacement for the current approximate sheen tint.

## Scripted experiences

- Extend [rigid-body physics](physics-api.md) with deforming collider policies
  as scripted experiences require them.
- Define persistence and save/restore APIs for scene state.
- Define richer scene ownership/lifetime APIs for larger scripted experiences.

## Tracking and input

- Expose additional tracking signals only when a usable backend is available;
  hand skeletons, bare-hand optical tracking, and eye openness are not exposed.
- Improve personal calibration robustness across head movement and headset refits.
- Maintain compatibility with the undocumented vrserver controller stream and
  evaluate documented alternatives that support overlay input requirements.

## Performance validation and profiling

- Characterize RSS retention across repeated mixed-model reloads, separating
  reusable allocator/driver caches from live allocations. The
  [five-minute motion/reload check](../tests/results/motion-soak.md) completed;
  its warmup memory rise and later plateau need attribution before changing
  reclamation behavior.
- Profile larger moving box/model scenes to identify the next bottleneck. Measure
  rendering time, GPU/shared memory, overlay count, and per-eye resolution, and
  evaluate grouped rendering quality before changing defaults. Subdivision,
  merging, and rebalancing are implemented; consider wider regrouping searches or
  different cost thresholds only if these measurements justify them. Preserve
  the common projection plane and coherent depth-cutout reprojection.
- Further reduce the cost of dense high-triangle model scenes; the
  [matched GPU mesh optimization results](../tests/results/vertex-cache.md) still
  fall below headset frame rates. Compare geometry
  visibility, repeated occluder draws, batching/instancing and model LOD before
  choosing an implementation; object count alone is not a sufficient workload
  budget. Validate larger detailed-model counts in the headset after reducing
  their rendering cost; the accepted detailed headset case contains 32 objects.

## Conditional performance investigations

These are deferred investigations, not unimplemented fixes ready to enable.

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
