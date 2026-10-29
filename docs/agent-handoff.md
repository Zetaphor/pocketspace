# Maintainer handoff

Start with the [README](../README.md) for build/run commands and examples, the
[JavaScript API](javascript-api.md) for exposed behavior, and
[remaining work](remaining-work.md) for future scope. Keep current contracts in
these guides; implementation history belongs in Git.

The [documentation site guide](documentation-site.md) explains VitePress, the
Markdown source layout and GitHub Pages deployment. Run `npm run docs:build`
after changing guide links, navigation or site configuration.

## Source map

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | QuickJS bindings, scene lifecycle, interaction, rendering, and OpenVR host loop. |
| `src/model_shaders.h` | Stereo overlay projection, perspective depth, and material shaders. |
| `src/mesh_index.h` | Exact position/UV deduplication for shared GPU vertex/index buffers. |
| `src/model_asset.cpp` | GLB import, animation evaluation, skinning, and picking geometry. |
| `src/model_instancing.h` | Opt-in ordered static-model batches, streamed instance data, fallback, and shader/resource lifetime. |
| `src/controller_input.cpp` | OpenVR controller actions and haptics. |
| `src/frame_input.cpp` | Raw vrserver input, state normalization, and event queues. |
| `src/physics_world.cpp` | Scene-owned Jolt bodies, colliders, joints, motors, springs, breaking, runtime settings, stepping, contacts, and grab release. |
| `src/runtime_stats.h` | Process CPU/RAM, optional DRM memory totals, and elapsed-time FPS sampling. |
| `src/panel.cpp` | Retained panel rasterization, fonts, and images. |
| `src/scene_persistence.h`, `src/scene_physics_snapshot.h`, `src/scene_panel_snapshot.h` | Versioned model/panel/animation/physics snapshots, staged restore, named bounded JSON files and atomic replacement. |
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

## Physics and performance entry points

The [physics API coverage table](physics-api.md#api-coverage) maps the implemented
native capabilities to JavaScript. Joint settings can be changed in place;
type, anchors, and axis remain creation-only. The
[statistics reference](javascript-api.md#engine-statistics) defines measured CPU,
RAM, and FPS, optional GPU driver totals, and the separate GPU allocation estimate.
The [stress scene](../scripts/stress.js) includes controller spawning, a HUD,
and automatic removal of fallen/distant objects. Its shared model assets remain
cached until scene replacement, while instance resources are released on removal.

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
full CPU and GPU mesh; `PrimitiveAsset::lod` owns up to two reduced index buffers.
Only indexed opaque static primitives qualify. Count those buffers in GPU
estimates and delete them with the asset. `Object::lodLevel` is selected once
per frame after mutations and shared by eyes/surfaces/depth passes. Grabbed and
nearby objects restore full detail; preserve projection and distance hysteresis.
Instancing runs must match both asset and selected level. The separate
`SF_MODEL_LOD_EXPERIMENT` fixed-detail harness remains test-only and disables
production LOD generation when its environment override is present. See the
[API contract](javascript-api.md#automatic-model-lod).

Flat [scene containers](scene-containers.md) own objects/panels and optionally
the single input callback. Keep root ID 0 separate from disposable positive IDs.
Disposal must reuse object/physics/interaction and deferred-panel cleanup, retain
shared surfaces/assets, and preserve callback ownership on failed reloads. Hold
a JS reference while dispatching a callback that may dispose its own owner.

[Scene persistence](scene-persistence.md) v4 preserves model instances, panel
pixels/layout,
independent animation playback/poses, interaction flags, physics bodies and joints
with original construction frames; v1–v3 saves remain readable. Reject joints
crossing ownership boundaries rather than losing state silently. Validate detached JSON and stage assets/overlay
reservations before committing; reserve fresh render groups so restores cannot
merge with current surfaces or inherit split policies. `World::appendSnapshot`
rolls back new bodies/joints, shape caches and joint IDs on native failure; use
fresh object IDs and never attach to existing bodies. Rebuild animated poses
before committing scene mutations; validate saved clip
index/name/duration against supported model clips. Keep named runtime saves
outside Git. Bound panel decode storage separately from the encoded file size;
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
reference angular density. Group extents can grow sharply near the headset;
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
Independent interactive surfaces retain their existing handle-preserving path.

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


Shared scene-depth reuse is not enabled. The isolated matching-grid prototype in
`tests/shared-depth-prototype.cpp` uses the real GLES shader/pass harness; see
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


GPU completion profiling is isolated behind `SF_GPU_SYNC_EXPERIMENT` in the
excluded `gpu-sync-experiment` target. Normal builds keep `glFinish` and compile
out its environment options, GPU queries, and alternate fence wait. See the
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
