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

## Deferred performance work

- Extend opt-in, split-only render group subdivision with measured merging or
  rebalancing policies for objects that move between established tiles, preserving
  the common projection plane and coherent depth-cutout reprojection.
- Evaluate grouped rendering quality and performance across larger scenes before
  selecting broader defaults.

- Revisit scene-depth reuse only when profiling shows repeated occluder work
  remains costly after bounds culling; require compatible pixel grids and correct
  equal-depth ownership while preserving per-eye opaque/masked occlusion.
- Investigate GPU/shared-texture synchronization with explicit compositor
  ownership and buffer-reuse guarantees before replacing `glFinish`.
- Profile rendering time, GPU/shared memory, overlay count, and per-eye resolution
  as scenes grow; use measurements to choose the next optimization.
