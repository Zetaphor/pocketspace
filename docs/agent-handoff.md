# Maintainer handoff

Read the root [AGENTS.md](../AGENTS.md) for contributor workflow, verification,
documentation, and headset testing instructions. This handoff covers detailed
implementation contracts. The [engine architecture](architecture.md) explains
the complete pipeline and production optimization policies.

Start with [getting started](getting-started.md) for build/run commands,
[examples](examples.md) for supported scripts, the
[JavaScript API](javascript-api.md) for exposed behavior, and
[remaining work](remaining-work.md) for future scope. Keep current contracts in
these guides; implementation history belongs in Git.

The [documentation site guide](documentation-site.md) explains VitePress, the
Markdown source layout and GitHub Pages deployment. Run `npm run docs:build`
after changing guide links, navigation or site configuration.

## Source map

| File | Responsibility |
| --- | --- |
| `src/script_api.cpp`, `src/engine_state.h` | QuickJS bindings, script loading, scene types and shared engine state. Compiled once for the engine and once with `SF_TEST_HOOKS` for regressions; keep shared globals `inline`, never `static`, in the header. |
| `src/render_host.cpp`, `src/render_host.h` | Interaction, surfaces, layout and render passes. Compiled once for the engine and once with `SF_TEST_HOOKS` for regressions. |
| `src/main.cpp` | OpenVR frame loop. |
| `src/model_shaders.h` | Stereo overlay projection, perspective depth, and material shaders. |
| `src/mesh_index.h` | Exact position/UV deduplication for shared GPU vertex/index buffers. |
| `src/model_asset.cpp` | GLB import, animation evaluation, skinning, and picking geometry. |
| `src/model_instancing.h` | Default-on ordered static-model batches (asset opt-out), streamed instance data, fallback, and shader/resource lifetime. |
| `src/controller_input.cpp` | OpenVR controller actions and haptics. |
| `src/frame_input.cpp` | Raw vrserver input, state normalization, and event queues. |
| `src/physics_world.cpp` | Scene-owned Jolt bodies, colliders, joints, motors, springs, breaking, runtime settings, stepping, contacts, and grab release. |
| `src/runtime_stats.h` | Process CPU/RAM, optional DRM memory totals, and elapsed-time FPS sampling. |
| `src/script_registry.h` | Script instances: stable IDs and generations, `ScriptScope`, launch/stop/reload, callback-error stops, paced restarts and the scratch scene. |
| `src/script_catalog.h` | Catalog discovery, stable catalog keys and per-script save namespaces. |
| `src/engine_settings.h` | The shared settings file: strict parsing, atomic writes and last-good reloading. |
| `src/event_bus.h` | Inter-script publish/subscribe: topic rules, copied payloads, deferred budgeted delivery and drop accounting. |
| `src/input_arbitration.h`, `src/native_resource_key.h` | Closest-hit selection across scripts and instance-qualified native keys. |
| `src/script_telemetry.h` | Per-runtime heap counter and callback-time windows. |
| `src/control_server.h`, `src/control_service.h` | Control socket and its operations, answered on the host thread. |
| `src/pocketspace_cli.cpp` | `pocketspace-cli` commands and REPL. |
| `src/canvas.cpp`, `src/canvas.h` | CPU RGBA canvas and FreeType text, shared by panels and the dashboard. |
| `src/panel.cpp` | Panel setup, images and GL texture upload over the canvas. |
| `src/control_client.h` | Control-socket client: JSON reading, connection and request IDs, shared by the CLI and dashboard. |
| `src/dashboard.cpp`, `src/dashboard.h`, `src/dashboard_main.cpp` | SteamVR dashboard tab: engine connection, state, drawing and clicks without VR; the OpenVR overlay loop in `dashboard_main.cpp`. |
| `src/scene_persistence.h`, `src/scene_physics_snapshot.h`, `src/scene_panel_snapshot.h`, `src/scene_animation_snapshot.h` | Versioned model/panel/animation/physics snapshots, staged restore, named bounded JSON files and atomic replacement. |
| `src/scene_lookup.h` | Object/panel ID lookup over sorted scene storage. |
| `src/panel_submission.h` | Cached panel texture, transform, and visibility submissions. |
| `src/group_buffer_budget.h` | Admission and peak accounting for grouped surface buffers and retained generations. |
| `cfg/` | Runtime OpenVR action manifest and bindings. |
| `scripts/` | Concise examples and the personal calibration generator. |
| `assets/samples/ATTRIBUTION.md` | Bundled model provenance and licenses. |

CMake defaults to Release. Keep runtime host paths configurable through its
cache variables. The launcher resolves relative paths from the checkout root.
Personal calibration files are runtime data and must remain outside Git.

After changing an API, update its reference guide and the smallest relevant
example. Run `python3 tools/lint-docs.py` to check local documentation links,
example inventory, and global binding coverage. Preserve upstream attribution
and license files when changing bundled assets.

Morph positions and weight clips use the existing CPU animation pose path.
Apply morph deltas before skinning; preserve node/mesh/zero defaults, independent
poses, fixed normalization, picking refits, and default-pose restoration on stop.
See [morph target support](animation-api.md#morph-targets).

Raw controller subscriptions follow OpenVR device identity, including connected
devices with temporarily invalid poses. `Monitor::poll` exposes device-change
refresh as normal connection loss so the host cancels stale holds before its
bounded reconnect. Retry missing initial hand state after five seconds, but
do not expire healthy delta subscriptions merely because their values are idle.

## Multiple scripts

Each running script is a `ScriptInstance` in `src/script_registry.h`, with a stable
instance ID for the engine session and a generation that increases on every
successful reload or restart. Instance `0` is scratch: always running, never
reloaded or auto-restarted, and recreated empty after a callback error or
`scratch.reset`. Enter a `ScriptScope` before calling an instance's JavaScript or
reading its scene; bindings resolve the current scene, interaction state, input
queue and save namespace through it. A scope restores the previous selection when it
ends and refuses to open while another script's callback is running.

JavaScript object, panel and group IDs stay local to their scene. Every native key
(overlay names, surface and panel maps, render caches keyed by IDs) must include the
instance ID, as `src/native_resource_key.h` does. Picking runs once per hand across
all scenes in `src/input_arbitration.h`: the closest hit wins, a panel wins an exact
tie with a model, and the lower instance ID breaks remaining ties. A hold stays with
its owner until release, stop or reload. Stopping or reloading one instance must
release only its holds, hover, queued events, overlays and resources.

Launch, stop and reload from the control socket or scratch's `scripts` object are
queued and applied between frames, never from inside a callback. A failed launch
leaves an instance in `failed`; a failed reload keeps the previous scene, callbacks,
bus subscriptions and queued deliveries. An uncaught callback error stops only its
instance; the engine sends the SteamVR notification and `engine.scriptStopped`, and
with `autoRestart` retries on the 1/2/4/8/16 s schedule. A manual stop cancels
restarts for the session.

The bus copies payloads as validated JSON text, so no QuickJS value crosses
runtimes. Delivery happens after scene updates with a per-frame budget, and the
recipients of a message are fixed when its delivery begins. Keep bus limits and
per-publisher accounting in `src/event_bus.h`.

Control requests run on the host thread between frames. Mutating operations take
the target's generation and are remembered by request ID, so a retry returns the
first response instead of running twice. Settings writes go through
`settings::write` (temporary file, flush, rename); the engine rereads the file
about once a second and keeps the last valid version on a bad edit.

`pocketspace-cli` and `pocketspace-dashboard` are separate processes that share
`src/control_client.h` and never link the engine. The dashboard draws with the
CPU canvas and submits a GLES texture; it connects only while its tab is visible,
must never start SteamVR (its unit uses `Requisite=`, its process connects as a
background application), and leaves SteamVR notifications to the engine.

## Physics and performance entry points

The [physics API coverage table](physics-api.md#api-coverage) maps the implemented
native capabilities to JavaScript. Joint settings can be changed in place;
type, anchors, and axis remain creation-only. The
[statistics reference](javascript-api.md#engine-statistics) defines measured CPU,
RAM, and FPS, optional GPU driver totals, and the separate GPU allocation estimate.
Per-script telemetry keeps process totals undivided. Each QuickJS runtime is created
with the counting allocator in `src/script_telemetry.h`; its `HeapCounter` must
outlive the runtime, so `closeScript` frees the runtime before resetting the host.
Time every script callback (update, input, bus) with `telemetry::Timed` on the
owning instance, and call `endFrame` once per frame. Sampling runs once a second;
`resources` events are built only while a client watches. `ownedGpuEstimate`
counts what one script owns, and `sharedGpuEstimate` counts shared items once.
Keep new allocations in exactly one of them.
The [stress scene](../scripts/stress.js) includes controller spawning, a HUD,
and automatic removal of fallen/distant objects. Its shared model assets remain
cached until scene replacement or explicit unused-model release, while instance resources are released on removal.

Object surfaces retain three compositor-facing color buffers and one owned depth
attachment shared by their FBOs. Only color is imported by the compositor; keep
`glFinish` and color ownership rules intact. GPU allocation estimates deduplicate
the shared depth IDs. Depth occluders are scissored to padded, conservative target
bounds per eye, with full-viewport fallback for near-plane/invalid bounds.

`loadModel` defaults to ordered GPU instancing; `{gpuInstancing: false}` opts out.
It permits consecutive same-asset batches
only for static single-primitive opaque/masked geometry. Preserve draw order;
never sort assets or split multi-part object ordering for batching. Deforming
and blended geometry retains the original path. Refresh instance transforms
from the current render snapshot; reset streamed data and shader resources on
successful reload. Instance attributes/divisors must be disabled/reset before
normal draws or panel work. Count the owned instance buffer in GPU estimates,
while leaving driver streaming overhead outside the estimate.

Automatic LOD defaults on in `loadModel`; `{lod: false}` opts out. Preserve the
full CPU and GPU mesh; `PrimitiveAsset::lod` owns up to two reduced index buffers
for unlit meshes or position/surface VBOs with optional compact indices for PBR. Only opaque static
primitives qualify; unlit meshes also require indexing. Count those buffers in GPU
estimates and delete them with the asset. `Object::lodLevel` is selected once
per frame after mutations and shared by eyes/surfaces/depth passes. Grabbed and
nearby objects restore full detail; preserve projection and distance hysteresis.
Instancing runs must match both asset and selected level. See the
[API contract](javascript-api.md#automatic-model-lod).

Flat [scene containers](scene-containers.md) own objects/panels and optionally
the single input callback. Keep root ID 0 separate from disposable positive IDs.
Disposal must reuse object/physics/interaction and deferred-panel cleanup, retain
shared surfaces/assets, and preserve callback ownership on failed reloads. Hold
a JS reference while dispatching a callback that may dispose its own owner.

[Scene persistence](scene-persistence.md) preserves model instances, panel
pixels/layout,
independent animation playback/poses and transitions, interaction flags, physics bodies and joints
with original construction frames. Restore accepts only the current snapshot version; there is no older-format support. Reject joints
crossing ownership boundaries rather than losing state silently. Validate detached JSON and stage assets/overlay
reservations before committing; reserve fresh render groups so restores cannot
merge with current surfaces or inherit split policies. `World::appendSnapshot`
rolls back new bodies/joints, shape caches and joint IDs on native failure; use
fresh object IDs and never attach to existing bodies. Rebuild animated poses
before committing scene mutations; validate saved clip
index/name/duration against supported model clips. Resolve named saves through
the caller's `ScriptHost::saveNamespace`, selected by `ScriptScope` and preserved
across reload; cross-script reads must go through `importSceneContainer`'s catalog
validation and staged restore. Keep named runtime saves outside Git. Bound panel decode storage separately from the encoded file size;
reserve new native panel overlays before committing and roll them back on later
failures. Restore normal panel canvas handles and initialize the font library
without needing a preexisting panel. Script file writes survive a later failed
reload.

Physics uses single-threaded stepping for small scenes and a bounded worker pool
for larger active scenes; scripts can override with `setPhysicsWorkers`.
Contact aggregation must remain synchronized when workers run, with all script
callbacks after the solver barrier. Keep the joint adjacency cache consistent on
creation/removal and retain the final pose when a body goes to sleep. Physics
timings in `getEngineStats` describe the latest advance, not the process sample.

Object surfaces distinguish reference resolution from allocated resolution.
`FrameBoundsCache::displayFocal` caps grouped density at `kDisplayOversample`
times the headset's pixels per meter at the surface depth; reference resolution
is the upper bound and zero focal leaves it. Independent surfaces keep reference
density: their in-place resize clears the overlay, so distance-driven resizing
flashes. `surfaceBufferPixels` gives them 25% headroom and a 60% shrink threshold
so rotating objects' crops do not reallocate (each reallocation blanks a frame).
Stereo projected bounds crop transparent margins; physical width scales with
allocated pixels to preserve reference sample density and the centered grid.
Keep immediate growth, near-plane fallback, delayed/budgeted shrinking, stable
overlay handles, and physical input-mask sizing when changing this path. Each
buffer replacement clears the compositor texture before releasing its resources.

Shared assets may use indexed GPU geometry while retaining expanded CPU triangle
streams. Route rendering through `drawModelPrimitive`; per-instance deformed
VBO overrides must use the expanded draw path. Include every shader vertex
attribute in the deduplication key if the vertex format grows. Resource estimates
count uploaded vertex counts and 16/32-bit index storage, and model teardown
releases both buffer types.

`loadModel(path, eyePixels, {optimizeVertexCache:true})` opts an asset into
meshoptimizer cache/fetch reordering during upload. Default order is unchanged.
Only complete-triangle indexed static opaque/masked primitives reorder; blended materials and
animated/skinned assets skip it. Keep CPU vertices/BVH/colliders unchanged and
preserve complete oriented triangles, exact position/UV values, and draw counts.
Explicit cache-setting conflicts on a cached path throw; omission inherits its
original setting. Do not silently enable this for assets relying on coplanar
triangle ownership. The dependency revision/digest is pinned in
`cmake/MeshCache.cmake`; preserve `third_party/meshoptimizer-LICENSE`.
Budget-limited shrinking must retain the old allocation if the full requested
replacement cannot fit; partial-density fallback is for growth only. Reject
performance comparisons where allocation sizes differ between on/off cases.

Live `createObject` calls reserve overlay handles before publishing an object;
reservation failures throw to JavaScript. Surface initialization reuses those
handles, and removal releases them immediately. Candidate script evaluation
must not call the live reserve/release hooks. The stress scene caps spawned boxes at 51 and catches creation failures, cleaning up partial bodies and showing
the error on its HUD.

Panel uploads increment `textureRevision`, including updates that reuse a GL
texture name. Submission caches belong to individual overlay handles and retain
only successful calls so failures retry. Compare the computed world transform
for head-locked/viewer-facing panels each frame. Clear caches on panel removal
and scene replacement before IDs can be reused. Preserve deferred texture
destruction and compositor synchronization.

Object and panel vectors remain sorted by increasing ID: append new IDs and
preserve order on removal. `findSceneItem` uses binary search without storing
pointers or a separate index across mutations. Resolve IDs again after vector
growth, erasure, or scene replacement. Surface synchronization walks visible
objects directly; picking and depth traversal retain scene order.

Render groups use negative surface keys (`-renderGroup`), while independent
objects use positive object IDs. Reserve both group handles before publishing the
first live member; release only after removing the last member. Native mouse
surfaces remain independent-only; grouped objects use the existing mesh picker
and per-object grab state. Rebuild frame-local member lists after script updates
so object-vector mutations cannot leave stale pointers.

`groupLayout` unions both eyes' projected bounds and requests the maximum member
reference angular density, each capped at display density. Group extents can grow sharply near the headset;
resolution is capped by 4096 pixels per eye and half the renderbuffer width, with
a warning when limited. Group buffers grow with 25% headroom in 256-pixel
buckets and shrink only after demand stays below 60% for 45 frames. Keep grouped
rendering opt-in and retain the independent stress scene as a comparison. Shared depth must exclude members from their own occluder pass
and include visible opaque/masked nonmembers.

Grouped resizing uses `Surface::previous` to retain the old visible overlay and
buffers until `submitSurface` succeeds at texture submission and showing the
replacement. Never clear the old texture before that handoff. Keep only one
pending generation; retain it on submission failure, include its allocations in
statistics, and destroy both generations on removal/reload/shutdown. Group
handles may change on resize; object IDs and mesh interaction remain stable.
Independent object surfaces retain their existing handle-preserving path.

Grouped frames must stage width, transform, and texture on the hidden overlay.
`prepareSurface` never changes the visible grouped overlay; `submitSurface`
shows the staged frame, hides the old front, then swaps handles. Keep the last
coherent visible frame on errors. If hiding the old front fails after showing
the staged frame, hide that staged handle before preparing it again; skip writes
if hiding fails. Destroy both handles during removal and release partial live
reservations if the second handle cannot be created. Per-surface color buffers
and object IDs remain independent of the alternating overlay handles.

All grouped surfaces use `groupPlane`: one head-facing plane through the scene
anchor, with each tile offset only within that plane. Do not face each tile
separately or place it at member depth; compositor late-pose reprojection would
then separate color edges from depth cutouts in other groups. Convert the tile's
world origin back to scene coordinates before setting model offsets. The grouped
stress floor participates in this same projection cohort. Independent surfaces
retain their separate projection behavior.

Object surfaces use `VROverlayFlags_IsPremultiplied`, matching GL's blended
framebuffer output. Masked fragments premultiply RGB explicitly because that
pass disables GL blending; opaque fragments have alpha one. Panels keep their
own retained-canvas alpha convention.

Group sampling density uses axial depth relative to the common plane, including
its oblique angular magnification. Using radial object range instead inflates
allocations for objects beside or below the view.

`FrameBoundsCache` refreshes after script callbacks, manipulation, animation, and
physics complete, before surface rendering. It transforms each visible object's
current geometry bounds once per frame. Shared-plane groups reuse one angular
projection per object/eye for layout and occluder tests; independent surfaces
reuse world corners with their own projection axes. The shared cache basis must
match `groupPlane`'s anchor-centered plane exactly.

Cache entries follow the current object-vector order. Resolve entries only for
visible objects belonging to that snapshot, and never use them across script
mutations or a reload without calling `update` again. Animation poses use their
current geometry bounds. Retain conservative near-plane fallback and viewport
padding; projection reuse does not change buffers, sampling density, or shaders.


Shared scene-depth reuse is not enabled. The matching-grid prototype was removed
in commit `92f4137`; see its
[measured results and adoption constraints](../tests/results/shared-depth-prototype.md).
Do not replace per-group occluder passes with an all-scene prepass based on
unculled comparisons. Current bounds culling can make per-group rendering faster,
and shared depth requires compatible grids plus explicit equal-depth ownership.


Automatic subdivision is opt-in through `setRenderGroupAutoSplit`. Script group
IDs remain 1–256; internal negative keys encode `-(group + 256*renderTile)`.
Decode group IDs with `surfaceRenderGroup`, never plain negation. New members
join a nearest existing member's tile; removal checks the runtime surface key.
Object IDs, vector order, physics, animations, and interaction state stay stable.

Split planning uses the current `FrameBoundsCache` after the physics barrier.
Try bounded quartile cuts along both angular axes, requiring valid stereo bounds,
uncapped children and at least 20% allocated-texel savings. Policies cap logical
tile memberships including hidden members. Default split-only membership remains stable. Optional merging/rebalancing uses
steady requested allocations, three matching observations over a second, and a
two-second regroup cooldown; it never changes script IDs or logical groups.

`GroupBufferBudget` charges grouped color/depth generations, including retained
snapshots, against 512 MiB before allocation. `Surface::groupBufferBytes` releases
its charge in `destroySurface`, including partial creation failure. Preflight
both outputs before structural allocation. Ordinary grouped resize attempts are
globally spaced by 100 ms, excluded during structural handoff, and delayed five
seconds per tile after failure or budget refusal. `surfaceCoverageWidth` keeps
full grouped bounds visible when the current allocation is below requested
density; never scale its physical width down below the current layout width.
This budget excludes independent surfaces, assets, panels and driver/compositor
memory. Do not describe it as a total GPU-memory limit.

A split allocates a new parent generation and child before publishing tile
membership. Keep the complete old parent in `Surface::previous`. A pending pair
must both finish rendering (or have its replacement fronts successfully hidden by
view culling) before `submitGroupSplit` shows the visible outputs. Detach both
previous chains during submission so neither old tile retires until both outputs
succeed. Restore both chains on failure; hide only the replacement fronts, keeping
old snapshots visible. Before writing either pending output's buffers, retry
hiding its current fronts; skip writes if hiding fails. Never recursively hide its retained
fallbacks while waiting for another output. Keep only one pending structural change, reset it on reload,
and include every retained generation in resource estimates. Removal/visibility
changes may eliminate either pending tile; resolve keys again before submission.


Normal builds use `glFinish` for GPU completion. The alternate fence-wait
experiment and its profiler were removed in commit `92f4137`. See the
[contract review and comparison](../tests/results/gpu-sync.md) before revisiting
synchronization: a producer-complete fence does not acknowledge compositor
consumption, and `WaitFrameSync` is pacing rather than ownership. Preserve the
existing buffer lifetime behavior; do not treat three buffers or successful
visual tests as proof of consumer completion.


`planGroupAdjustment` merges a compact pair or rebalances two tiles only when
uncapped outputs save >=20% of steady requested allocation with headroom. Never
count an ordinary delayed shrink as an adjustment saving. Hidden-member tiles
are deferred. `StableGroupAdjustment` stores keys and object IDs, not pointers,
across checks. Reset it and regroup cooldowns on scene reload. Pair evaluation
is bounded to four per check, with a sticky proposal and rotating search.

For a merge, the replacement's previous chain retains both old tiles. Both source
tiles must have no pending resize generation when planning; this bounds retained
snapshots. `preserveSurfaceFallback` transfers snapshots to the surviving pending
output on removal/hiding, including the immediate live removal hook. Count the
whole retained chain in resource estimates and destroy it on handoff or teardown.
Default policies keep `merge` and `rebalance` false; examples that enable them do
so explicitly. The shared projection plane, premultiplied alpha, and GPU producer
completion rules are unchanged.

Animation phase counters are test hooks (`SF_TEST_HOOKS`) used by
`animation-profile-regression` and `gpu-skinning-regression`. Normal builds have
no phase timers. Keep deformation/bounds fused;
separating their loops for timing would change the workload. See the
[Frame results and next design constraints](../tests/results/animation-profile.md)
before changing animation backend policy or duplicating CPU picking geometry work.


GPU skinning is automatic in normal builds. `gpu-skinning-regression` tests the
production path. CPU-reference regression programs explicitly disable skinning internally;
production defaults and mixed-scene checks must also remain covered.

Eligible assets are entirely skinned, non-morph models with one four-lane
influence set per primitive and at most 1,024 joints per skin. Other assets
retain CPU deformation. Exact indexed keys include position, UV, joints and
weights. Shared geometry belongs to the asset; palette textures belong to
independent poses and are destroyed with them. Count both in allocation estimates.

Use transformed joint-influence boxes for conservative animated bounds. Reject
ray misses before exact CPU deformation/BVH refitting; cache candidate geometry
once per evaluated pose, sharing it between hands. Preserve conservative bounds
and GPU upload dirtiness when filling that cache. Never retain unconditional CPU
deformation beside the GPU path. Common shaders apply the same skin transform
to color/depth passes; reset skin attributes and uniform state after draws.
Reload/restore must preserve independent backend state and use normal resource
teardown. See the [Frame comparison](../tests/results/gpu-skinning.md) for evidence.


The [rigid/morph profile](../tests/results/rigid-morph-profile.md) completes the
bundled 64/256-actor investigation. Dense multi-part rigid submission/upload
costs dominate its vertex arithmetic; tiny morph cubes do not establish a GPU
morph bottleneck. The 256-rigid animated/paused allocation estimates differ, so
do not treat their FPS delta as a controlled optimization comparison. Preserve
production defaults while testing bounded submission improvements.


Rigid primitive caching is enabled in production. Per-pose caches own exact world matrices, bounds/radius and pending upload bits.
Never clear pending dirtiness merely because a later evaluation is unchanged.
Only rigid non-morph parts may reuse geometry; skin/morph parts still update.
Refit the whole BVH when any part changes. See the
[Frame comparison](../tests/results/rigid-pose-cache.md) before adopting or
extending this optimization.


The [pass-local uniform cache comparison](../tests/results/draw-uniforms.md)
preserved exact pixels and removed most uniform calls, but did not improve
end-to-end throughput; it was removed in commit `92f4137`, and normal
draws retain direct uniform calls.
Do not infer GPU duration from completion-wait changes. Future submission work
must target the actual draw/vertex-state cost and preserve primitive order.


The [owned VAO comparison](../tests/results/vertex-arrays.md) was not adopted and
was removed in commit `92f4137`. A future VAO cache must destroy VAOs with
their geometry owner rather than cache them by GL name, restore VAO 0 after each
cached draw, and include driver metadata limits in allocation estimates.


Adjacent rigid batching is enabled by default with an owned production stream. Eligible CPU poses own
one `geometry.batchVbo`; `geometry.primitives[].vbo` remain zero. Never alias the
shared stream name into per-part ownership. Count the stream once and delete it
once. `drawPosePrimitive` handles unmerged ranges as well as the per-part fallback;
do not render an animated part by reading its per-part VBO directly.

Only adjacent identical-state untextured opaque/masked CPU rigid parts qualify.
Keep exact expanded triangle order, material/object barriers and skin/morph/static
fallbacks. Partial updates use fixed `PosePartState::streamFirst` offsets, retain
pending dirtiness across evaluations, and upload each changed range once. Saved
scenes serialize playback, not GL resources; rebuild independent streams through
normal restore/upload. `rigid-batch-regression` reads draw counters through
`SF_TEST_HOOKS`.
See [prototype and production measurements](../tests/results/rigid-batches.md).
The single-stream production comparison and lifecycle checks pass on Frame. The
normal-engine mixed scene passed user-confirmed visual, grabbing, pause/resume and
save/restore acceptance on 2026-10-29; the demo was stopped afterward.


The [current dense-model investigation](../tests/results/dense-model-current.md)
refreshes pass attribution with automatic LOD and ordered instancing. Its
static-only vertex shader and the later
[mixed-program integration](../tests/results/ordinary-program-routing.md) passed
exact rendering and lifecycle checks without consistent native gains, and were
removed in commit `92f4137`. Normal builds retain the general skin-capable
shader. A future mixed-program path must restore complete view/object/material
state, invalidate cached binding after instanced batches, and never delete a
borrowed host program.


[Geometry-aware grouping profiling](../tests/results/group-occluder-policy.md)
found that an equal-minimum-area partition tie-break did not justify adoption; it
and its render-work counters were removed in commit `92f4137`. Comparisons
of grouping policies must count actual selected index/skin ranges, full merged
rigid ranges and instanced multiplicity, and reject geometry/LOD drift and
incomplete member work, not just allocation mismatch.

## Opt-in model lighting

`loadModel(..., {pbr: true})` selects a separate material/surface-frame path.
`setModelLighting` stages a copy before reading script properties and resolves
its asset again before publishing. Asset-cache conflicts include explicit PBR
choice; snapshots also require matching lighting for cached lit models. Failed restores must not change lighting
on already-visible objects.

PBR keeps expanded CPU inputs but can compact full-detail GPU streams with the
existing opt-in `optimizeVertexCache` policy. Do not use the legacy five-float
identity: both UV inputs, normal/tangent/handedness and position must participate.
Only static opaque/masked primitives with storage savings are indexed in
production, with cache/fetch ordering; blended and deforming uploads remain
expanded. Stage position/surface/index buffers atomically and verify sizes before
publishing. On failure release partial buffers and retain expanded uploads.
Keep source draw count and uploaded compact vertex count distinct for drawing
and memory accounting. Generate reduced PBR LOD buffer sets from unchanged CPU inputs
regardless of full-detail indexing; select all reduced position/surface/index
buffers before consulting full-detail buffers. Optional LOD budgets remain
independent of compact full buffers. Preserve the opt-in flag in the existing
snapshot/cache-conflict policy; no new snapshot version or runtime override.
See [production validation](../tests/results/pbr-index-production.md).
MikkTSpace is vendored at upstream commit
`3e895b49d05ea07e4c2133156cfa94369e19e409`; `mikktspace.cpp` is the unmodified
upstream C source compiled as C++, with its embedded license preserved.

Production PBR LOD in `src/pbr_lod.h` retains independently owned reduced
position/surface buffers and optional indices in each primitive's LOD records.
The cache option enables compact indexing of reduced streams when smaller;
otherwise streams stay expanded. Preserve each level's index type, uploaded
vertex count and complete draw count independently of full detail. Select all
buffers and their count
together in color and depth passes; never pair reduced positions with full
surface data. Full 14-float shader-input identity, locked seam positions/borders
and surface-attribute weights preserve the validated simplification policy.

`pbrLodBudget` caps optional buffer-set payloads at 32 MiB engine-wide. Reserve a whole
set before allocating, validate all buffer sizes before publishing, and roll
back buffers/charge on any unpublished exit. `ModelLodLevel::budgetBytes` belongs
to its shared asset; `deleteModel` releases it and clears the record. Do not copy
uploaded ownership records into poses or independently owned assets. Live assets
retain their charges during restore; unavailable optional storage must keep full
geometry rather than fail publication. Saved `lod` is a policy request, not a
promise of generated levels. Budget fallback has no eviction or background retry.
Failure injection and the `SF_PBR_LOD` override are `SF_TEST_HOOKS` test hooks and
must remain unavailable in production. See the [recorded validation](../tests/results/pbr-lod-production.md).

Lit poses currently use CPU position and surface-frame deformation and disable
GPU skinning eligibility, ordered instancing, LOD and rigid merging. Preserve
independent morph/skin state, picking, fixed normalization and shared texture
ownership when extending these paths. Surface buffers belong to their shared
asset or individual pose; material maps belong only to the asset. Count and
release each once. PBR rendering restores program, texture units and enabled surface
attributes; the shared per-pass culling owner restores culling and winding before subsequent unlit/panel draws. Use the same
alpha and geometry policy for depth occluders and member-color passes. Public
behavior and current limitations are in [lighting-api.md](lighting-api.md).

All model paths honor glTF `doubleSided` through `ModelCulling`. One owner spans a
render pass, including ordinary, CPU/GPU animated, instanced and depth draws.
Keep direct GL culling mutations out of individual shader wrappers so its cached
state remains valid. Node reflections change winding without changing object
Euler rotations. Rigid merging must stop when single-sided animated parts have
different winding; any combined-material instancing must reject incompatible
sidedness/winding. Do not cull panels or controller laser geometry.

## Baked environment lighting

`setModelLighting` stages environment file loading before publishing either the
lighting struct or its shared resource pointer. Snapshots include its path,
intensity and Y rotation. Cached lighting must
match. Missing-environment failures while restoring new model assets must leave
the scene, assets, IDs and active lighting intact.

`LightingEnvironment` owns the CPU bake plus one RGB16F cubemap and RG16F BRDF LUT.
Its weak path cache does not retain scene resources. Uploads happen with a live
GLES context; destruction must run before EGL teardown, like model textures.
Reload candidates can share a live environment without mutating it. Keep the
format bounded and validate all dimensions, levels, payload lengths, float/half
values before any upload. SH coefficients may be negative; radiance/LUT data may
not. Specular mips are GGX convolutions, not ordinary image mipmaps. Diffuse SH
stores cosine-convolved radiance divided by pi; do not divide it again at shading.

PBR uses texture units 6/7 and restores their bindings to zero afterward. All
active sampler types need distinct compatible units even with IBL disabled.
The per-asset rotation is applied inversely to sampling directions; it does not
rotate geometry or follow the user's head. Unlit materials bypass IBL. Count
shared environment allocation once per resource, not per model or object. The
original studio bake and reproducible tool are in `assets/environments/` and
`tools/generate-environment.py`; custom external image licenses remain the
contributor's responsibility.

Convert MikkTSpace-generated tangent handedness to glTF by negating the generated
w component. Do not apply that conversion to imported glTF tangents. Normal-map
UV transforms and negative node determinants still apply their own frame
transforms. The paired GeneratedTangents/SuppliedTangents fixtures intentionally
use a strong green normal-map component; they must produce the same frame and
pixels under environment lighting. Orthogonality/unit-length checks alone do not
catch an inverted bitangent. See the [Khronos reference implementation](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/gltf/primitive.js).

### PBR external depth program

Color draws retain the original PBR shader. External depth uses a separately
linked startup program with the identical vertex source, pose/LOD streams, winding
and UV policy. Opaque depth skips texture sampling; masked depth preserves base
alpha times factor and strict cutoff discard; blended parts remain excluded.
Skip lighting/environment setup and disable surface attributes 8–10 for depth.
Initialize sampler units before the first depth draw (which may precede color),
preserving the caller's current program. Restore texture/attribute/program state
through the existing release path. See [validation](../tests/results/pbr-depth.md).

## Scene directional lights

Scene directionals share point/spot IDs, ownership and the eight-entry bound.
Direction is ray travel in the same local/scene axes as spotlights, normalized
once per frame. Attached object rotation affects it; translation, scale and
visibility do not. Preserve type-safe CRUD and validate getter side effects
before publication. The internal resolved range sentinel is -1; it bypasses
CPU range/cone rejection and shader distance/cone attenuation. Keep point/spot
ranges positive and their existing math intact. Prepared-color count keys include
all contributing light types; no shader compilation occurs while drawing.
Directional snapshots reject position/range/cone fields for that
type, with complete staged validation and attachment remapping. See [API](lighting-api.md#scene-directional-lights)
and [regression](../tests/directional-light-regression.cpp).

## Dynamic point lights

Scene lights are sorted, monotonically issued logical IDs, not vector pointers
or GL resources. Copy settings before evaluating script getters; resolve the
light and validate its owner/attachment again before publishing. Recheck capacity
after getters. Removing an object removes all attached lights; disposing a scope
removes its owned lights. Ownership transfer and attachment are independent.

Resolve enabled/nonzero lights once after interaction/physics per rendered frame.
Object-local offsets are meters, independent of scale; reflect Z into model axes
consistently for positions, normals and surface origins. All eyes/surfaces use
the same frame snapshot. Keep the per-program cached uniform payload free of
scene pointers, update it after removals, and skip light setup for external depth.
Unlit materials must remain invariant. Snapshots validate/stage lights and
attachment maps before native publication; require both attachment endpoints
inside the captured scope, honor total live capacity and return fresh light IDs.
See the [API](lighting-api.md#dynamic-point-lights)
and [regression](../tests/point-light-regression.cpp).

Spotlights share the eight-entry capacity and monotonically issued ID namespace.
Keep point/spot APIs type-safe. Direction uses local +Z-forward axes when attached,
rotates without translation/scale, and normalizes once in the resolved frame.
Store cone cosines in the uniform payload; keep an explicit equal-angle branch to
avoid zero division. Snapshots require type and spotlight fields. Invalid mixed sets must leave scene resources and counters intact. See the
[spotlight regression](../tests/spotlight-regression.cpp) and
[API](lighting-api.md#dynamic-spotlights).

## Per-object light range selection

Keep full lists below four active lights; the small-count comparison did not
establish a consistent benefit. Build ordered object light sets once after
animation/interaction/physics, alongside
frame bounds. Use current full/deformed geometry bounds, not visual LOD bounds.
The origin-centered sphere is rotation-invariant; retain cutoff padding and
invalid-bound fallback. Range rejection must prove no contributing fragment was
removed. Do not drop lights just because their centers or spot axes miss an object.
Cone rejection must prove the whole padded sphere lies outside a cone tangent
or apex plane. Account for the stored direction length and widen the CPU cone
for shader rounding. Invalid/hemispherical/wider cones retain the light.
Preserve retained-light order. Rebuild counts after removals/reloads and reuse
sets across both eyes, all primitives and surface passes; no callbacks mutate
scene vectors during rendering. Frame array indices match current scene order,
not persistent object IDs. The shader-work alternative remains test-only after
inconsistent/regressing measurements; normal builds keep the original BRDF loop.
See [light-work-regression.cpp](../tests/light-work-regression.cpp).


## Prepared PBR programs

`src/pbr_variants.h` owns a bounded set of color programs prepared after initial
asset upload and successful script reload. Never call its compilation path from
render traversal: `ready` only looks up current material/environment/filtered-count
keys and falls back to general PBR. Use variants only at >=1024 allocated pixels
per eye and >=4 contributing lights; unlit and depth passes retain their normal
programs. Unknown restored/new asset or lighting states must not compile in-frame.
Failed reload must retain the existing cache; successful reload clears stale keys
and prepares at most 16 slots (including compile failures). Shader state contains
no scene pointers. Keep program teardown before EGL destruction and preserve
per-program uniform caches when alternating specialized and general draws.

## Nested ownership and explicit asset release

Parent links are scene-owned integer metadata, acyclic and independent of
transforms/render grouping. Resolve the complete subtree before disposal and
use ordinary removal paths for members. Direct state arrays stay direct;
`childIds`/`parentId` expose the hierarchy. Snapshots capture subtree resource owners,
stages fresh hierarchy maps before native reservation, and commits them with
resource publication. Never retain container/object
pointers across getters or mutation.

Unloaded assets leave empty, uploaded-marked slots; IDs are never reused or
renumbered. Validate live slots both before and after option getters. Count live
paths for the 32-asset limit, including pending restore models; pending IDs still
append after all issued slots. Reject unload while any object (hidden or animated)
uses the asset. Delete model GPU resources, release asset-keyed Jolt hull/mesh
caches and drop CPU/environment ownership. Reset instance/diagnostic geometry
caches before subsequent rendering. Prepared programs own no asset pointers.
Avoid re-uploading empty slots or resurrecting stale IDs. Snapshots reload paths,
not handles. See [lifetime regression](../tests/asset-lifetime-regression.cpp).

## Fabric sheen ownership

PBR sheen maps occupy material slots 4/5 and texture units 8/9. Color storage is
sRGB; roughness uses linear alpha. The shared `SheenLookup` is owned by loaded
assets through `shared_ptr`, weakly cached, and uploaded during model upload.
Its unit-10 binding is cleared by PBR release. Unload and transactional reload
must preserve last-owner deletion and the single-count GPU estimate. Keep
Charlie BRDF evaluation and CPU albedo integration in agreement. The current
indirect sheen path is a diffuse-SH approximation, not a sheen-prefiltered cube.
See [architecture](architecture.md#fabric-sheen-pipeline) and
[material API](lighting-api.md#fabric-sheen).

## Shared shadow invariants

`SpotShadows` serves point, spot and directional lights. Shadows are opt-in.
One enabled light reserves the shared slot, including zero-intensity lights;
disabled lights retain settings without reserving it. Validate capacity after
option getters and across existing/staged snapshot lights before asset/native
reservations. Serialize logical
settings only, never map/query handles or watchdog state.

The pass retains no scene/object pointers. Resolve attachments by ID and render
once after pose upload, before eye/surface draws. Use full caster geometry,
current CPU/GPU deformation, masked alpha and normal sidedness. Hidden and BLEND
casters are excluded; cast across render groups. Receiver matrices include the
surface origin; filtered light IDs, not original vector indices, select the
shadowed contribution. Other lights, IBL and emission stay unshadowed. Clear
texture unit 11 after PBR draws and restore caller GL state. Delete the private
VAO after each update so it cannot retain unloaded asset or removed pose buffers.
The depth texture is never compositor-owned.

Geometry is limited to 128 visible casters and the configured submitted-triangle
budget (default 500000, maximum 1000000). Geometry exhaustion retries when the
scene fits. GPU timing is asynchronous: ignore three warmup updates and disjoint
samples, then require a complete rolling average of 24 valid results above the
configured threshold (default 2 ms). Never latch off on one timing spike. A trip
releases the map and requires explicit shadow options or lifecycle reset to retry;
it is not GPU preemption. Allocation/capability/budget fallback must never sample
an old map. Successful reload releases resources; failed reload preserves the
active scene/map. Count map payload once in engine GPU estimates. Diagnostics
`gpuMs` is the last valid sample, not the watchdog average.

Spotlights use range/outer-cone perspective coverage at 256/512/1024 pixels, with
up to 4 MiB estimated map payload. Directional shadows use fixed world-space
center/size/depth orthographic coverage with the same map sizes. Attachment
translation and headset motion must not move directional coverage; resolved
direction rotates its basis. Outside-volume receivers remain unshadowed. There
are no cascades or automatic coverage fitting.

Point shadows use six perspective faces in a 3×2 atlas at 256/512 pixels per face
(1.5/6 MiB estimated payload). Budget all six triangle submissions and time all
views together. Atlas shape must be part of allocation reuse, even when face
resolution matches. Keep PCF taps inside the selected face. Derive receiver-plane
slope from world-position derivatives projected through that pixel's face;
differentiating face-selected UVs creates discontinuities. Normal offset may
cross the face's XY bounds: clamp taps instead of returning unshadowed light.
Preserve these fixes for low-resolution self-shadow patterns and bright face seams.
Face-clamped filtering is not seamless cube filtering; do not claim otherwise.

All three interactive examples passed headset verification and were stopped.
Keep automated correctness, offscreen pass timing and visual acceptance distinct.
See [pipeline](architecture.md#spotlight-shadow-pipeline),
[spot API](lighting-api.md#spotlight-shadows),
[directional API](lighting-api.md#directional-light-shadows),
[point API](lighting-api.md#point-light-shadows), the
[shared native regression](../tests/spot-shadow-regression.cpp), and recorded
[spot](../tests/results/spotlight-shadows.md),
[directional](../tests/results/directional-shadows.md) and
[point](../tests/results/point-shadows.md) validation. The point profile predates
the final receiver seam correction; it is not a new final-shader comparison.
