# Remaining work

This roadmap contains only unfinished work. Shipped behavior belongs in the
[API guides](javascript-api.md), production policies in the
[architecture](architecture.md), and completed investigations in the
[test index](../tests/index.md). Items below do not prescribe implementation order.

## Material support

- Add Charlie-prefiltered environment sheen when a target experience needs sharp
  directional HDR fabric reflections. The current indirect sheen uses diffuse SH
  irradiance and directional albedo.
- Add other advanced glTF material extensions, such as clearcoat and transmission,
  when a target experience requires them. Define rendering/compositing and
  performance limits before implementation.

## Scripted experiences

- Define collider behavior for skeletal/morph deformation when experiences need
  it. Existing colliders remain rigid and use default-pose geometry; updating
  their shape with animation is not implemented.

## Tracking and input

- Improve calibration tolerance to headset refits and accuracy outside the
  calibrated pose/angle coverage. The current head-relative affine correction
  does not adapt to a changed fit automatically.
- Expose hand skeletons, bare-hand optical tracking, eye openness or additional
  eye signals only when a usable backend supplies them.
- Evaluate documented controller-input alternatives that meet overlay input
  requirements and can replace dependence on the undocumented vrserver stream.

## Deferred lighting features

- Add point-light or directional-light shadows only when a target experience
  requires them. Point lights need multiple shadow views; directional sources
  need a bounded coverage/cascade policy. Preserve explicit memory/work/time
  budgets, stereo/alpha correctness, attachments and persistence.
- Design scalable light selection beyond the shared eight-light limit only
  when an intended scene requires it and profiling supports the design.

## Conditional performance work

These require new workload evidence, rather than being fixes ready to enable.
Completed prototypes and rejected approaches are recorded in tests, not listed
as open tasks here.

- Reduce dense high-triangle GPU cost when a representative scene establishes
  an actionable bottleneck. Require matched geometry, sampling density,
  external/member draw work and allocation budgets; preserve stereo projection,
  draw order, transparency and compositor buffer ownership.
- Extend PBR geometry acceleration beyond its current static indexing/cache/LOD
  paths only with measured benefit. Instancing must preserve filtered light sets,
  material/surface frames and object/part order; animated GPU paths must preserve
  normal/tangent deformation, bounds and exact picking.
- Move morph deformation to the GPU only when higher-detail morph workloads
  establish meaningful CPU deformation/refit cost.
- Consider GPU visibility/LOD selection only when CPU selection is a demonstrated
  bottleneck. Avoid synchronous readback and preserve one coherent object choice
  across eyes, surfaces and depth passes.
- Consider GPU physics only when CPU simulation remains a bottleneck after Jolt
  worker tuning. Account for contacts, queries, callbacks and synchronization.
- Improve model loading/LOD-generation latency only when it is significant.
  Compare offline preprocessing and CPU improvements before a GPU generator;
  preserve material boundaries, UV quality, error limits and the full mesh.
- Extend stability checks to longer sessions or larger workloads as usage
  requires. Investigate allocation sites if live heap growth or memory pressure
  appears.
