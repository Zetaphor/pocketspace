# Remaining work

This roadmap contains only unfinished work. Shipped behavior belongs in the
[API guides](javascript-api.md), production policies in the
[architecture](architecture.md), and completed investigations in the
[test index](../tests/index.md). Items below do not prescribe implementation order.

## Material support

- Add Charlie-prefiltered environment sheen when a target experience needs sharp
  directional HDR fabric reflections. The current indirect sheen uses diffuse SH
  irradiance and directional albedo.
- Add glTF clearcoat support when a target experience requires layered finishes.
  Cover import, direct/environment shading, sample models and performance limits.
- Add transmission or other advanced glTF material extensions when needed. Define
  rendering/compositing and performance limits before implementation.

## Rendering quality

- Explore antialiasing for model surfaces. Surfaces have no MSAA or
  supersampling, so model edges alias, most visibly on independent surfaces
  viewed up close at their fixed reference resolution. Compare quality, GPU time
  and buffer memory against raising `eyePixels`; preserve stereo projection,
  depth cutouts, transparency and compositor buffer ownership.

## Scripted experiences

- Add a line drawing API for scene-space lines and segments, such as strings,
  ropes, laser guides and measurement lines. Scripts can only draw lines on panel
  canvases today, so scene lines must be modelled as geometry.
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

## Measured performance work

These come from the demo measurements in
[demo-resource-usage.md](../tests/results/demo-resource-usage.md) and
[display-density-cap.md](../tests/results/display-density-cap.md). With all eight
demos the engine runs at about 48 FPS and total GPU busy is above 90% at the
maximum clock, so gains need less GPU work. The device uses Zink on Turnip (no native freedreno GL
in this Mesa build), and kernel tracing is on, so every GL call also costs a
trace marker in Mesa's driver threads. Measure changes with
`tools/benchmark-demos.py --repeat 3` as [the test guide](../tests/README.md#demo-benchmark)
describes.

- Reduce the remaining external-occluder depth cost. With all demos it was about
  half of the engine's GPU time per frame before clustered depth draws, which
  recovered about 40% of it ([depth-clusters.md](../tests/results/depth-clusters.md)).
  Small meshes still draw whole, such as desk-toys' cradle balls (46 depth draws
  per frame). Instanced depth draws or a lower cluster threshold are candidates.
- Reduce compositor overlay cost. Grouping each demo's small objects cut
  compositor GPU busy by about six points, but the groups drew more pixels and
  used about 220 MiB more GPU memory, and some were limited by the group budget.
  Automatic grouping would need to leave out large and near objects and keep
  density; measure it against the same workload before adopting it.
- Reduce the point-light shadow cost in lighting-studio: about 3 ms GPU per frame,
  and 80 FPS against 90 with shadows off. Per-face caster culling and skipping
  receiver lookups did not help. Next suspects are the `glGet*` state save and
  restore in `src/spot_shadows.h` and the per-frame timer query (both force
  driver-thread syncs), and the render-pass switch on Zink; also reuse the map
  when neither the light nor any caster moved.
- Cut redundant GL calls. The PBR path sets about 60 uniforms per draw,
  including the environment's nine SH coefficients and light arrays; cache last
  values per program and set per-scene values once. Count and remove avoidable
  `glGet*` and query polls on the render path.
- Explain the memory kept after scripts stop: after every demo was stopped the
  engine held 192 MiB RSS and about 685 MiB of driver GPU memory across two DRM
  clients, against 62 MiB and 7 MiB fresh. Decide whether it is a leak, allocator
  cache or retained surface generations, then fix or bound it.
- Fix GPU memory reporting: `src/runtime_stats.h` reads `drm-memory-total:`, but
  the msm driver writes `drm-total-memory:`, so `vramBytes` is null and the
  monitor demo shows `n/a`.

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
