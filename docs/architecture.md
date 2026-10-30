# Engine architecture

The engine is a C++20 Linux process combining a QuickJS scene runtime, OpenVR
tracking and overlays, a GLES 3 renderer, retained panels, and Jolt rigid-body
physics. It renders models into stereo overlay textures and submits those
textures to SteamVR. SteamVR composes them with the user's VR environment.

This guide describes the shipped pipeline and resource ownership. Public
contracts live in the [JavaScript API](javascript-api.md) and subsystem guides;
precise maintenance constraints live in the [maintainer handoff](agent-handoff.md).
Measurements and rejected alternatives are linked separately below.

## Pipeline at a glance

```text
Script + GLB assets + optional saved scene
                 |
        QuickJS candidate scene
                 |
      validation / staged commit
                 |
           active scene <----- input callbacks / update(dt)
                 |                         ^
       animation + interaction      OpenVR / vrserver
                 |
         Jolt fixed-step advance
                 |
     asset / pose uploads + render snapshot
                 |
    LOD + cached bounds + surface/group layout
                 |
       per-surface, per-eye GLES passes
       external depth -> opaque/masked -> blended
                 |
        GPU completion / overlay submission
                 |
           SteamVR compositor
```

The diagram shows data flow. The exact host-loop order, including when queued
input is delivered, is described below. Panels use a separate retained-canvas
and overlay submission path rather than the model passes.

## Process startup and execution model

The executable loads the selected script, initializes OpenVR overlay services,
EGL/GLES, and the GBM/DRM resources used for shared textures. The scene anchor
provides the transform between script coordinates and OpenVR standing space.
Scripts use meters, with positive Z forward; the host converts this convention
when building OpenVR transforms and shader axes.

QuickJS bindings, scene mutation, interaction, and rendering execute on the host
thread. Scripts run as globals and may define `update(dt)`; there is no browser
DOM, Node.js runtime, or general JavaScript timer service. Getters normally read
cached native snapshots. Jolt may use worker threads during a simulation step,
but these threads never call JavaScript or mutate the host's scene vectors.

The host targets 90 iterations per second. `update(dt)` receives `1 / 90`, while
native animation and physics consume measured elapsed time. Missed deadlines are
dropped rather than repaid through bursts of frames. Engine FPS therefore
measures loop throughput, not headset or compositor presentation rate.

## Scene state, ownership, and transactions

A scene owns assets, model instances, panels, animation playback, a physics world,
input callback references, and container metadata. Model instances refer to
shared assets; each animated instance owns its independent pose and playback.
Removing an instance releases its instance resources but retains its shared asset
until scene replacement or shutdown.

Objects and panels have increasing IDs stored in sorted vectors. Binary lookup
resolves IDs when needed. Callbacks can create, remove, restore, or dispose
objects, so pointers into these vectors cannot survive mutations. Frame-local
member lists and bounds are rebuilt after updates.

[Containers](scene-containers.md) are flat cleanup scopes, not transform nodes.
They own related objects/panels and optionally the input callback. Disposing one
uses the same cleanup paths as individual removal, including held state, bodies,
and attached joints, while retaining scene-shared assets.

Script modification triggers evaluation in a new QuickJS context and candidate
scene. Successful evaluation replaces the active scene after detaching old
surfaces and clearing stale interaction and input state. Failure preserves the
active scene and callback. File writes and other external side effects are not
rolled back. See [lifecycle](lifecycle.md) for disconnect and shutdown behavior.

[Scene persistence](scene-persistence.md) uses versioned snapshots of logical
state: asset references, models, panels, animation and transitions, bodies, and
joints. It does not serialize GL names, native overlay handles, or live pointers.
Restore validates detached data and stages assets, overlays, poses, and physics
before publishing fresh IDs. Failed staging releases its reservations and leaves
the active scene intact. Restored poses rebuild independently owned GPU resources.

## Host-loop order

Each iteration follows this order in `src/main.cpp`:

1. Process SteamVR quit events and refresh tracked controller roles and poses.
2. Periodically check the script timestamp and attempt transactional reload.
3. Advance native animation from elapsed time and evaluate changed poses.
4. Synchronize model surfaces and panels; drain native overlay pointer events.
5. Poll requested tracking actions and the raw controller stream. Apply connection
   cleanup, compute custom-beam mesh hits, and attempt bounded reconnection.
6. Dispatch queued input callbacks, update native grabs, then call `update(dt)`.
7. Advance scene physics and synchronize resulting object poses/events.
8. Upload newly loaded assets and dirty animation data.
9. Refresh head/eye poses, choose frame-level LOD, rebuild current bounds and
   surface membership/layout, then render visible model surfaces.
10. Complete GPU rendering and submit coherent overlay frames. Update statistics
    and pace the next iteration.

Physics events produced after callback dispatch enter the queues for subsequent
delivery. Scripts can also poll events. Rendering uses the state after callbacks,
manipulation, and physics; interaction picking occurs earlier in the iteration.

## Input, tracking, and interaction

OpenVR supplies HMD/controller poses, role assignment, overlay pointer events,
action input, haptics, optional gaze, and system foveation centers. A separate
local vrserver WebSocket monitor supplies global controller boolean/scalar
component deltas, including finger capacitance/contact values. That interface is
undocumented and depends on SteamVR implementation details. These values are
finger sensors rather than a skeletal hand-tracking system.

The host normalizes these sources into cached hand/tracking snapshots and
independent polling/callback queues. Gaze polling is opt-in and subject to its
coexistence restrictions; optional calibration corrects the reported gaze ray.
See [input architecture](input-architecture.md), [tracking](tracking-api.md), and
[eye calibration](eye-calibration.md) for validity and source contracts.

Custom beams test transformed model triangles and select the nearest mesh hit.
A rectangular overlay hover does not establish a mesh hit. Per-object grab
configuration allows grip, trigger, or both. Native manipulation maintains the
controller/object relationship; physics-backed holds feed targets into the
physics world and release through its velocity/throw handling. Disconnect,
removal, disposal, and reload cancel stale holds through common cleanup paths.
Temporary pose loss and actual controller disconnection have different behavior.

## Asset loading and geometry

`src/model_asset.cpp` imports GLB data with cgltf: primitives, embedded base-color
textures, material state, transforms, supported animation channels, skins, and
morph targets. Shared assets retain expanded CPU triangles for picking, bounds,
and collider construction. A picking BVH accelerates exact triangle tests.

Static GPU geometry deduplicates exact position/UV tuples into vertex and index
buffers, choosing 16- or 32-bit indices as appropriate. This changes storage,
not the CPU geometry. Optional vertex-cache optimization reorders eligible
static opaque/masked triangles only when explicitly requested; default triangle
order remains unchanged. Animated and blended geometry retain their required
order and paths.

Automatic LOD retains the full mesh and adds up to two reduced levels for
eligible opaque static primitives: index buffers for unlit indexed meshes, and
owned position/surface buffers with optional compact indices for PBR meshes. One projected-size/distance decision
per object is reused by both eyes, surfaces, and depth passes. Hysteresis limits
switching; nearby or grabbed models use full detail. Picking and physics continue
to use full CPU geometry. `lod: false` disables this quality/performance policy.

## Animation and deformation

Playback belongs to the object, while clips and source data belong to the asset.
The evaluator samples channels, blends transitions, composes node transforms,
and updates geometry/bounds. Stopping returns to the supported default pose;
save/restore retains independent playback and transition state. See the
[animation guide](animation-api.md) for supported glTF features.

The deformation backend is selected from asset capabilities:

| Path | Work and resource ownership |
| --- | --- |
| GPU skeletal skinning | Eligible assets are entirely skinned, non-morph, have one four-lane influence set per primitive, and at most 1,024 joints per skin. Indexed skin geometry is shared by the asset; each pose owns its joint-palette textures. Color and depth shaders use the same skin transform. |
| CPU skinning/morph fallback | Unsupported GPU combinations remain on the CPU. Morph deltas precede skinning. Each pose owns its deformed geometry and uploads dirty vertex data. |
| CPU rigid animation | Exact world-matrix caches reuse unchanged non-morph parts, including their bounds. Changed parts update geometry and pending upload ranges. Any changed part causes a whole picking-BVH refit. |

GPU-skinned bounds use conservative transformed joint-influence boxes. A ray
that misses those bounds avoids CPU deformation. A candidate requiring exact
picking computes CPU geometry/BVH lazily, cached once per evaluated pose and
shared between both hands. This avoids running full CPU skinning beside every
GPU update while preserving exact selection.

Eligible rigid animated poses use one owned expanded vertex stream instead of
per-part VBOs. Fixed offsets allow dirty ranges to upload once and individual
parts to draw from that same stream. Adjacent untextured opaque/masked parts with
identical material state merge into one draw, preserving triangle and object
order. Textured/material barriers remain separate draws; skin, morph, and static
paths keep their own behavior. Stream ownership is never aliased into per-part
buffer fields, and unchanged evaluation never clears a pending upload.

## Physics

Each scene owns one Jolt world. The API creates static, dynamic, and kinematic
bodies with supported primitive, convex, or static mesh colliders. Collider shape
construction/caching is distinct from visual LOD and animation deformation;
animated render meshes do not imply deforming physics colliders. Fixed, hinge,
and distance constraints provide the implemented motors, limits, springs, and
break thresholds. See [physics coverage](physics-api.md#api-coverage).

Elapsed time accumulates into fixed 1/90-second steps with at most eight steps of
backlog.
Small scenes step without additional workers. Automatic mode enables a bounded
pool at 64 active bodies, up to two additional workers subject to available CPU
threads; scripts may override the worker setting. Sleeping-body synchronization
avoids repeatedly copying unchanged poses, while retaining the final sleep pose.

Held unconstrained bodies follow kinematic targets. Joint-connected dynamic
holds use bounded velocity control so the solver can enforce their constraints.
Contact callbacks aggregate synchronized native data; JavaScript delivery occurs
after the solver barrier. Joint breaking is evaluated after solving, removes the
constraint, and queues an event. Joint adjacency and shared collider caches avoid
repeated relationship/shape work and are maintained through removal and restore.

## Surface layout and visibility

By default, model instances render through independent surfaces. Scripts may
instead assign a render group, sharing a stereo surface among multiple objects.
Grouping reduces compositor overlays and repeated render targets without changing
mesh interaction or logical object identity. Both approaches remain supported.

A `FrameBoundsCache` transforms current visible-object bounds once after scene
updates. Grouped layout and occluder checks reuse per-eye angular projections;
independent surfaces reuse world corners in their own projection basis. Invalid
or near-plane bounds fall back conservatively rather than dropping geometry.

Stereo projected bounds crop transparent margins. Reference resolution expresses
sampling density; allocated pixels and physical overlay width change together to
retain it. Group tiles share one head-facing, anchor-centered plane so compositor
reprojection does not separate color edges from depth cutouts on other tiles.
Density uses axial depth and oblique magnification rather than radial range.

Group allocations grow with headroom and bucketed sizes; shrinking is delayed.
Per-eye dimensions are capped by 4,096 pixels and hardware limits. The grouped
buffer budget admits color/depth generations, including retained replacements,
against 512 MiB. It excludes independent surfaces, assets, panels, and driver or
compositor memory, so it is not a total GPU memory cap.

Optional automatic subdivision evaluates bounded angular cuts that save at least
20% of allocation while keeping stereo bounds and sampling constraints. Optional
merge/rebalance uses stable proposals and cooldowns. Logical group IDs and object
IDs stay fixed; only internal tile membership changes. Structural replacements
retain complete old snapshots until every required output is ready for handoff.

## Stereo render passes and draw submission

Each model surface has a side-by-side stereo color target. Each eye uses its
actual eye position projected onto the surface plane, with perspective-correct
depth for the modeled geometry. For each eye the renderer performs:

1. Depth-only occlusion from visible opaque/masked objects outside this surface.
   Conservative bounds reject non-occluders and scissor work to padded target
   bounds. Members are excluded from their own external occluder pass.
2. Opaque/masked color rendering for surface members with depth testing.
3. Blended material rendering in preserved scene/primitive order.

There is no general transparency sorting guarantee. Material support includes
base-color textures/factors, UV transforms, alpha modes, and an approximate
sheen tint. Opt-in PBR assets use `src/pbr_asset.h`, `src/surface_frame.h` and
`src/pbr_program.h`: retained normals/tangents and morph deltas, MikkTSpace tangent
generation, a separate expanded surface-frame VBO, sRGB color/emissive textures,
linear data textures, and a directional-light GGX metallic/roughness shader with optional baked image-based
lighting. `src/lighting_environment.cpp` validates PSENV001 resources, shares them
by exact path through weak cache entries, and lazily uploads an HDR prefiltered
cubemap and BRDF lookup. The [split-sum IBL design](https://google.github.io/filament/Filament.html)
separates roughness-filtered radiance from the integrated BRDF.
The offline NumPy baker supplies GGX roughness levels
and nine diffuse SH coefficients; the render loop performs no convolution.
Intensity and Y rotation belong to each model asset; shared environment data is
immutable. Rotation sine/cosine are prepared on the CPU. Environment upload
preserves caller texture bindings and unpack alignment. Units 6/7 are reserved
for the cube/LUT during PBR draws and cleared before subsequent draws. GPU
estimates count each referenced environment once, with RGB16F padding allowance.
[Native correctness, incremental cost and headset visual acceptance](../tests/results/environment-lighting.md)
are recorded as separate validation evidence.
Scene point lights and spotlights are logical CPU records (up to eight combined), with independent IDs
and container ownership. The renderer resolves object-local meter offsets after
interaction/physics into one frame snapshot, shared by all eyes and surfaces.
A reusable frame-local array builds an ordered light subset for each visible
PBR object. Fewer than four active lights keep the full list to avoid selection
and per-object upload overhead. A double-precision range/sphere test uses the full current pose's
origin-centered bounds, with conservative padding and invalid-bound fallback.
Only lights unable to reach the enclosing sphere are removed. For spotlights,
conservative cone tangent/apex-plane tests can reject the entire sphere too.
Direction length and a widened CPU cone account for the shader's float inputs;
hemispherical/wider or invalid cones retain range-only behavior. Per-fragment
spotlight falloff remains unchanged. These subsets are reused across eyes, primitives, LODs
and surfaces without per-draw selection or allocations. Empty light frames clear
counts without rebuilding geometry tests. No scene IDs or light settings change. See the
[range-selection validation and measured comparison](../tests/results/light-work.md)
and [cone-selection comparison](../tests/results/spotlight-cone-selection.md).
PBR shaders receive positions relative to each surface in the same model axes as
surface normals. Cached uniform payloads avoid repeated uploads at unchanged
surface origins. The bounded forward loop adds GGX point illumination with smooth
range cutoff and inverse-square attenuation; disabled/zero-intensity lights are
excluded. Unlit materials and external-depth draws bypass the lighting work.
Spotlights additionally rotate/normalize emitted direction with attachments and
multiply direct illumination by a squared cone fade, with an explicit hard-edge
case for equal cone angles. Uniform payloads include direction and cone cosines;
point lights bypass the cone branch. Lights cast no shadows. V9 snapshots store
light types and cone data; v8 retains point lights. Snapshots stage owned lights, remap attachments and
publish fresh IDs; cross-container capture attachments are rejected. Removal,
container disposal and reload share normal scene cleanup. See the
[point-light API](lighting-api.md#dynamic-point-lights) and
[regression](../tests/point-light-regression.cpp) and
[bounded Frame comparison](../tests/results/point-lights.md).
Spotlight correctness and matched point/spot timings are recorded in the
[spotlight comparison](../tests/results/spotlights.md).

Static PBR supports full-input indexing with `optimizeVertexCache: true` (default
false). Every 14-float shader-input tuple participates in identity, preserving
position, both UV inputs, normal/tangent seams and material boundaries. Only
static opaque/masked primitives whose compact buffers save space are eligible;
blend and deforming uploads remain expanded. Cache/fetch ordering is required in
production: [plain indexing regressed](../tests/results/pbr-index.md), whereas
[explicitly enabled ordering](../tests/results/pbr-index-cache.md) improved native
full-detail throughput. CPU triangles and animated uploads stay expanded.
Position, surface and element buffers are staged together, allocation sizes are
verified, and failed staging releases partial buffers and falls back to expanded
uploads. The existing uploaded-vertex/index counts account for compact storage;
shared asset deletion releases each buffer once. The excluded diagnostic builds
alone allow plain indexing or `SF_PBR_INDEXED` overrides. See
[production validation](../tests/results/pbr-index-production.md) and the
[broader material/default assessment](../tests/results/pbr-cache-materials.md).
Production surface-aware PBR LOD in `src/pbr_lod.h` uses full-input CPU indexing
while simplifying. It locks coincident surface seams and primitive borders,
then stores selected original inputs in owned position/surface VBOs. With
`optimizeVertexCache: true`, reduced streams also receive cache/fetch ordering
and compact 16/32-bit indices when that saves storage. Each level owns its index
type and uploaded vertex count independently of full detail; draw and surface
bindings select the complete level together. Without the option, reduced streams
remain expanded. Simplification targets/error policy and selected triangles are
unchanged by this storage optimization. See
[reduced indexing validation](../tests/results/pbr-lod-index.md).
Full streams stay resident for near and held objects. The existing object-level
selector supplies the same detail choice to color/depth passes and both eyes.
[Budgeted production validation](../tests/results/pbr-lod-production.md) records the quality,
throughput and extra-memory tradeoff.

`src/pbr_lod_budget.h` admits optional PBR buffer sets under one 32 MiB payload limit
across assets. Each exact buffer-set reservation precedes GPU allocation/publication;
failed allocation or CPU staging exceptions release staged buffers and the
reservation. Missing levels fall back to a finer level or full geometry without
rejecting an asset or restore. Admission follows upload order, with no eviction
or retry for already uploaded assets. Asset destruction returns the charge;
object instances and their held state do not own separate charges. The budget
excludes full geometry, textures, unlit LOD indices and surfaces. Derived levels
and reservations are rebuilt rather than serialized.
Material UV0 transforms and sampler settings are independent. Surface frames use
inverse-transpose normals and orthogonalized tangents, including mirrored node
handedness. PBR CPU poses update surface buffers alongside positions and bounds;
shared textures remain asset-owned. PBR currently bypasses legacy indexing,
instancing, rigid batching and GPU skinning to preserve seams and deformation.
All model paths share a per-pass `ModelCulling` owner: it applies `doubleSided`
and node-transform winding consistently to member-color, alpha and external-depth
draws, and skips redundant driver state calls. Instanced batches use the same
material state; rigid merging stops at incompatible animated winding. Culling and
front-face state reset before panel/other passes. The [native comparison](../tests/results/culling.md)
found no repeatable overall throughput gain; this policy implements glTF sidedness. Unlike the old unlit path,
single-sided back faces are now culled for unlit assets too.
The [bounded PBR shader-variant prototype](../tests/results/pbr-variants.md)
remains isolated to excluded test targets. Production uses the general PBR color
program; specialization still needs bounded prewarming, lighter-workload routing
and lifecycle/headset validation before adoption.

Legacy, PBR color and PBR depth programs link during startup, before live scene publication;
reloads do not compile a new shader after committing their candidate scene.
The separate program restores legacy program, texture-unit and attribute state
before subsequent objects/panels. Both member-color and external occluder passes
use the same geometry and alpha policy. Directional/environment lighting state is per shared asset and
is captured in snapshot v7 and later, with transactional cache-conflict validation; v6
retains its direct-light behavior with environments disabled.
External PBR depth draws use a dedicated program with the same vertex shader,
geometry, deformation, LOD, winding and UV policy. Opaque depth returns before
texture sampling; masked depth evaluates base alpha times factor and the identical
strict alpha-cutoff discard before returning. Blended primitives remain excluded
from external depth. Depth draws skip lighting/environment setup and surface-frame
attributes; normal color draws retain their original shader. Sampler units are
initialized before a possible first depth draw, preserving the caller's program.
No extra geometry or texture storage is introduced. See the
[depth-pass validation](../tests/results/pbr-depth.md) for matched workloads and limits.
See [lighting API](lighting-api.md) for controls, color mapping and limitations.

Ordered GPU instancing is enabled by default for eligible consecutive same-asset,
single-primitive, static opaque/masked objects. Runs must also match selected LOD.
A reusable streamed instance buffer carries current transforms. No asset sorting
or cross-object reordering is performed; multipart, deforming, blended, and
ineligible objects fall back to normal draws. Attribute divisors and skin state
are reset before other draw paths and panel work.

## GPU buffers and compositor handoff

A model surface owns three compositor-facing GBM color buffers. DMA-BUF file
descriptors back EGL images used by GLES renderbuffers and OpenVR shared texture
handles. Their FBOs share one owned depth attachment; only color is imported by
the compositor. Resource destruction releases imports, GL objects, descriptors,
and GBM allocations through their owning surface.

Production waits with `glFinish` before publishing rendered frames. This proves
producer completion, not compositor consumption. Three buffers and successful
visual checks are not a consumer-completion protocol; changing synchronization
requires preserving the existing ownership contract rather than assuming frame
pacing or a GL fence acknowledges compositor use.

Grouped presentation stages transform, width, and texture on a hidden overlay,
then shows the replacement and hides the old front before swapping roles. Resize
and split/merge generations retain old coherent frames until submission succeeds.
Failures preserve/retry fallbacks instead of exposing partially updated content.
Independent interactive surfaces retain their handle-preserving path.

## Panels

Panels maintain retained canvas pixels/layout, text through FreeType, and images.
Raster updates advance a texture revision even when the GL texture name is reused.
Each native panel overlay caches successful texture, transform, and visibility
submissions, retrying failed calls. Head-locked and viewer-facing transforms are
computed from current tracking. SteamVR supplies panel pointer coordinates in
pixels; panels do not use the model triangle picker.

Removal detaches overlays before deferred texture destruction. Persistence stores
bounded panel pixel/layout data and recreates normal canvas handles during staged
restore. See [panels](panels.md) for canvas and input behavior.

## Removal and shutdown

Object removal cancels hover/grabs, removes its physics body and attached joints,
and releases instance-owned poses and independent surfaces. Group surfaces remain
while other members need them. Panel cleanup detaches the native overlay before
releasing its texture. Successful reload retires the old scene, including shared
assets, per-pose buffers/palettes, streamed instance state, and retained surface
generations; failed reload leaves those active resources in place.

Ctrl-C, SIGTERM, and acknowledged SteamVR quit unwind native overlays, GPU/shared
buffers, EGL, input, and OpenVR resources through scoped cleanup. The engine exits
on SteamVR shutdown and must be relaunched after SteamVR restarts. Forced process
termination cannot execute cleanup.

## Statistics and performance policy

`getEngineStats()` reads cached process CPU/RSS, optional DRM process GPU totals,
engine FPS, scene counts, allocation estimates, and latest physics timings.
Process samples update roughly once a second; getters do not synchronize the GPU.
Shared memory means RAM and GPU values can overlap. The allocation estimate counts
owned buffers/textures, deduplicates shared depth, and includes retained surface
generations, pose streams, palette textures, unlit LOD indices, PBR LOD buffer sets,
and instance data. It
excludes driver/compositor overhead and is not measured GPU residency.

Script-facing defaults, opt-ins and tradeoffs are collected in
[performance options](optimizations.md). The production optimizations and their controls are:

| Optimization | Production policy | Evidence |
| --- | --- | --- |
| Cropped stereo targets, bounds/scissor reuse, shared depth attachments per surface | Automatic; retain conservative fallback and sampling density | [Render passes](../tests/results/render-passes.md), [fast motion](../tests/results/fast-motion.md) |
| Grouped rendering and adaptive tiles | Script-selected groups; subdivision/merge/rebalance opt-in | [Overlay comparison](../tests/results/overlay-comparison.md), [subdivision](../tests/results/auto-split.md), [regrouping](../tests/results/regroup.md) |
| Exact indexed geometry | Automatic for supported shared geometry | [Maintainer contracts](agent-handoff.md) |
| Vertex-cache reordering | Explicit asset opt-in | [Vertex cache](../tests/results/vertex-cache.md) |
| Automatic static-model LOD | Default on; asset opt-out; PBR extras capped at 32 MiB | [Unlit LOD](../tests/results/auto-lod.md), [PBR LOD](../tests/results/pbr-lod.md) |
| Ordered static instancing | Default on; asset opt-out | [Instancing](../tests/results/instancing.md) |
| GPU skinning and lazy exact picking | Automatic when eligible; CPU fallback | [Skinning](../tests/results/gpu-skinning.md) |
| Rigid pose caching | Automatic for rigid non-morph parts | [Pose cache](../tests/results/rigid-pose-cache.md) |
| Adjacent rigid draws in one owned stream | Automatic when eligible | [Rigid batches](../tests/results/rigid-batches.md) |
| Panel submission caching, sorted-ID lookup, physics sleeping/workers/caches | Automatic with explicit physics worker override | [Runtime contracts](agent-handoff.md), [physics API](physics-api.md) |

The excluded profiling executable records actual per-pass submission counts,
selected scene triangles and LOD distributions. Controlled comparisons also
check complete member geometry; matching allocation estimates alone does not
establish equivalent work. A [grouping tie-break comparison](../tests/results/group-occluder-policy.md)
retains these guards and remains experimental after negligible native gains.

Measurements are workload-specific, not FPS guarantees. Tests/results document
correctness, measured performance, and user headset acceptance separately.
Normal builds do not enable the rejected uniform-cache, owned-VAO, primitive or
cluster occluder-culling, specialized opaque shader, shared-scene-depth, or
alternate GPU synchronization experiments. Their isolated targets and reports
remain available through the [test index](../tests/index.md). A separate
[static-only ordinary vertex shader prototype](../tests/results/dense-model-current.md)
and its [mixed-program integration](../tests/results/ordinary-program-routing.md)
remain test-only after inconsistent native gains; production retains the general
skin-capable vertex program.
GPU morph work is
deferred until a workload establishes sufficient CPU cost.

## Source map and maintenance

| Area | Main sources |
| --- | --- |
| Host loop, scene bindings, surfaces, layout, passes | `src/main.cpp` |
| Import, CPU geometry, animation evaluation, pose upload | `src/model_asset.cpp`, `src/model_asset.h`, `src/animation.h` |
| GPU skinning, rigid batching, indexing, LOD, instancing | `src/gpu_skinning.h`, `src/gpu_skinning_asset.h`, `src/rigid_batching.h`, `src/mesh_index.h`, `src/model_lod.h`, `src/pbr_lod.h`, `src/model_instancing.h` |
| Projection/material shaders and picking | `src/model_shaders.h`, `src/picking_bvh.h` |
| Tracking and input | `src/controller_input.cpp`, `src/frame_input.cpp`, `src/tracking_state.h`, `src/eye_calibration.h`, `cfg/` |
| Physics | `src/physics_world.cpp`, `src/physics_world.h` |
| Panels | `src/panel.cpp`, `src/panel_submission.h` |
| Snapshots and ownership lookup | `src/scene_persistence.h`, `src/scene_*_snapshot.h`, `src/scene_lookup.h` |
| Statistics and buffer admission | `src/runtime_stats.h`, `src/group_buffer_budget.h`, `src/pbr_lod_budget.h` |

Keep this guide aligned with shipped behavior when changing a pipeline stage or
production default. Keep exact implementation invariants in the
[handoff](agent-handoff.md), public options in API guides, and benchmark histories
in [recorded results](../tests/index.md). Follow [AGENTS.md](../AGENTS.md) for
verification and resource-lifetime rules.

## Pocketspace compatibility identifiers

The public executable, launcher and documentation use Pocketspace. The legacy
`sf-overlay-engine` build/executable aliases and launcher still resolve to the
same runtime. Keep the existing SteamVR application key, overlay/mailbox keys,
scene-container format string and default state directory stable across this
branding change so bindings, input and named saves remain compatible.
Lighting-environment writers emit `PSENV001` (`.psenv`); readers also accept the
legacy `SFENV001` payload with unchanged size/value validation. The bundled old
environment path is an alias rather than a second GPU-owned resource format.
