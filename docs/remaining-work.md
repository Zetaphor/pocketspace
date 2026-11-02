# Remaining work

This roadmap contains only unfinished work. Shipped behavior belongs in the
[API guides](javascript-api.md), production policies in the
[architecture](architecture.md), and completed investigations in the
[test index](../tests/index.md). Items below do not prescribe implementation order.

The [multiple-script and dashboard plan](multi-script-plan.md) covers the next
planned development track.

## Before the event bus

In this order:

- Move rendering out of `src/main.cpp` into a separately compiled file, and link it
  from the regressions that still include `main.cpp`, so a rendering edit recompiles
  only its own file. Add precompiled headers if they help the resulting smaller files.
- Replace the native model pointer with the engine laser, then confirm it in the
  headset:
  - Remove SteamVR's input on model overlays, `overlay.surfaceHover*`,
    `overlay.pointer`, and the native `getPointer` state.
  - Add `overlay.meshHover` and report a 3D hit point on model hover and press, with
    a polled equivalent.
  - Make `laserPassThrough` control the engine ray. It defaults to `false`, so visible
    objects are hit unless marked; grabbable objects are always hit.
  - Make `enableObjectInteraction(id)` mean grabbable with defaults, and remove
    `interactive` and `autoManipulate`. Custom grab systems use nongrabbable objects
    with hover and button events.
  - Read thumbstick yaw and depth directly from the controller.
  - `setLaserVisible` alone decides whether a beam is drawn, including on panels.
    Hover, grabs and panel clicks work whether or not it is drawn.

## Material support

- Add Charlie-prefiltered environment sheen when a target experience needs sharp
  directional HDR fabric reflections. The current indirect sheen uses diffuse SH
  irradiance and directional albedo.
- Add glTF clearcoat support when a target experience requires layered finishes.
  Cover import, direct/environment shading, sample models and performance limits.
- Add transmission or other advanced glTF material extensions when needed. Define
  rendering/compositing and performance limits before implementation.

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

- Extend directional shadows to cascades or automatic coverage only if fixed
  bounded coverage is insufficient. Preserve explicit memory/work/time budgets,
  stereo/alpha correctness, attachments and persistence.
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
