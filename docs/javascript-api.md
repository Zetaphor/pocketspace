# JavaScript API

For a comparison of optional optimizations, defaults and tradeoffs, see
[performance options](optimizations.md).

Scripts execute as QuickJS globals, without Node.js/browser modules, filesystem
APIs, or timers. Define an optional `update(dt)` function at load time. The host
caches it and supplies `1 / 90` seconds at its 90 Hz target; use `Date.now()`
when actual elapsed time matters. Native animation uses elapsed wall time.
`print(...values)` writes to the engine log. Relative asset paths resolve from
the process working directory; the launcher uses the repository root.

Start with a complete model-loading scene: [avocado.js](../scripts/avocado.js).

## Engine statistics

`getEngineStats()` returns the latest cached, process-wide sample and current-scene physics timings. Process counters are
sampled roughly once per second; querying does not read `/proc` or synchronize
the GPU. Scene counts are current at the time of the query.

| Field | Meaning |
| --- | --- |
| `fps` | Measured engine-loop frames per elapsed second, including rendering and pacing; not the HMD/compositor presentation rate. |
| `cpuPercent` | Process user+system CPU time divided by elapsed time. 100% means one fully occupied core; multithreaded usage can exceed 100%. |
| `ramBytes` | Process resident RAM (`VmRSS`), in bytes. |
| `vramBytes` | Driver-reported process GPU memory total from DRM fdinfo, in bytes, or `null` when unavailable. Duplicate DRM clients are counted once. |
| `gpuAllocatedEstimateBytes` | Estimated engine GPU allocation size in bytes: allocated stereo color/depth buffers, model textures and vertex/index buffers (including retained LOD indices), deformed VBOs, the reusable instancing buffer when allocated, and panel textures. |
| `sampleId` | Increasing sample number; zero before the first sample. |
| `physicsAdvanceMs` | Wall time of the latest physics-world advance in milliseconds, including solver, contact handling, and joint breaking; excludes host pose synchronization. |
| `physicsSolverMs` | Solver wall time summed over the latest advance's fixed steps, including worker waits. |
| `physicsSteps` | Fixed steps in the latest advance (0–8). |
| `physicsActiveBodies` | Active rigid bodies after the latest advance. |
| `physicsWorkers` | Additional worker threads used by the latest advance; zero when no step ran. |
| `sampleSeconds` | Elapsed seconds covered by the latest sample; zero before sampling. |
| `objectCount`, `panelCount` | Current scene counts, including hidden instances. |

FPS, CPU, RAM, and memory estimates are `null` until sampled. Unavailable measured
counters remain `null`. The GPU estimate excludes driver/compositor overhead,
allocator padding except measured color-buffer stride, and small utility textures.
It assumes four bytes per depth pixel, counts shared depth attachments once,
and is not measured GPU residency. RAM and
GPU values can overlap on systems with shared memory; do not add them together.
Steam Frame GPU memory uses shared system RAM rather than a separate VRAM pool.

Run [stress.js](../scripts/stress.js) for colored, grabbable physics boxes and a
statistics panel including solver time, active bodies, and worker count. Either bumper adds one object; A or X adds five; B or Y clears
the spawned objects. The scene starts with four boxes and caps at 51 spawned boxes.
Spawning stops on a creation error and displays the reason on the HUD; clear or
remove objects to free resources before trying again. The engine's 256-object
limit and SteamVR's system-wide overlay budget still apply. Each model uses one
overlay, in addition to the HUD and two controller lasers.
Boxes below scene Y = −1.2 meters or more than 3 meters from the scene origin
are removed every 100 ms, reclaiming their physics body, overlay buffers, and
spawn capacity. Shared model assets remain cached for subsequent spawns.
It uses the normal 768-pixel-per-eye object surfaces. The HUD refreshes twice per
second; the underlying statistics sample once per second. When measured GPU
memory is unavailable it explicitly displays the allocation estimate instead.

## Scene ownership

[Scene containers](scene-containers.md) group objects, panels, lights, child
containers and an optionally owned input callback for recursive cleanup. Ownership
is independent of render grouping and transforms. Use `createSceneContainer({parent})`
and `setSceneContainerParent(id, parentId)` to build or reparent a hierarchy.

Transfer object and panel ownership between cleanup scopes: [container-transfer.js](../scripts/container-transfer.js).
For child ownership and unused model release, see
[nested-containers.js](../scripts/nested-containers.js) and
[asset-lifetime.js](../scripts/asset-lifetime.js).

## Coordinates and models

Scene coordinates are meters: +X right, +Y up, +Z forward. The fixed standing-space
anchor starts one meter ahead and 0.2 meters below the initial HMD position.
Imported geometry is centered and normalized to a unit maximum default-pose
extent. Object scale specifies that extent in meters. Animated poses can extend
beyond it. Positions and rotations below are relative to the scene anchor.

| Function | Behavior |
| --- | --- |
| `setModelLighting(assetId, options)` | Set directional light, color, intensity, ambient and exposure for a PBR asset; see [lighting](lighting-api.md). |
| `loadModel(path, eyePixels = 768, options = {})` | Load a GLB and return an asset ID. Reference resolution must be a multiple of 64 from 256 through 2048. Boolean option `pbr` enables [model lighting](lighting-api.md) and defaults to `false`. Boolean option `optimizeVertexCache` defaults to `false`; `gpuInstancing` and `lod` default to `true`; see shared geometry, ordered GPU instancing and automatic LOD below. Runtime buffers may crop transparent margins at the same model pixel density. The same exact path is cached; a conflicting explicit resolution or option throws. Up to 32 simultaneously loaded assets per scene; `unloadModel` releases unused assets. |
| `unloadModel(assetId)` | Release a loaded model only when no live object uses it; released IDs remain invalid. See [asset lifetime](scene-containers.md#releasing-unused-model-assets) and [asset-lifetime.js](../scripts/asset-lifetime.js). |
| `createObject(assetId, x, y, z, scale, options = {})` | Return an object ID; finite coordinates and scale greater than zero and at most 2 meters are required. Up to 256 objects. Optional `container` assigns [lifetime ownership](scene-containers.md). Optional `renderGroup` is an integer from 0 through 256; zero gives an independent overlay, positive IDs share a stereo surface. |
| `setRenderGroupAutoSplit(groupId, options = {})` | Opt a logical render group into stable automatic subdivision. `options` accepts `maxSurfaces` (integer 1–16, default 4) and `targetEyePixels` (multiple of 64 from 256–4096, default 2048), plus boolean `merge` and `rebalance` (both default `false`). Pass `false` to stop future adaptation; existing tiles remain. |
| `setObjectPosition(id, x, y, z)` | Set position. |
| `setObjectYaw(id, radians)` | Set yaw. |
| `setObjectRotation(id, pitch, yaw, roll)` | Set rotation in radians. |
| `setObjectScale(id, scale)` | Set positive scale, at most 2 meters. |
| `setObjectVisible(id, visible)` | Show/hide rendering and selection. |
| `removeObject(id)` | Remove the instance and its interaction state. |

Invalid IDs and invalid arguments throw. During live scene updates, object
creation reserves the native handles its surface needs and throws if SteamVR
cannot allocate them; a failed reservation leaves no object behind. Removal of
the last surface member releases its handles immediately so subsequent spawns
can reuse the available capacity. Assets remain loaded until explicitly released with `unloadModel` or the scene
is replaced. Instances share source materials/textures; object transforms and
animation playback remain independent. Render resolution trades clarity for
GPU time and memory. By default each object has its own surface. Opaque and
alpha-masked models occlude one another using per-eye geometry depth before
surface submission. Occluder drawing is limited to conservative target bounds;
near-plane cases retain the full viewport. Each surface keeps three independent
color buffers and reuses one depth attachment while rendering them. Masked holes remain transparent. Blended materials are not
opaque occluders; ordering between blended surfaces remains compositor-dependent.

Object surfaces crop centered, transparent borders using conservative bounds from
both eyes. Pixel count and physical overlay width shrink together, preserving
the reference pixel spacing and centered sample grid; `loadModel` still defaults
to a 768-pixel reference. This removes unused pixels rather than scaling the
model into fewer samples. Near-plane crossings or invalid bounds retain the full
reference surface. Buffers grow immediately when required, and shrink after 45
consecutive eligible frames, with at most two shrinks per frame. Resize keeps the
overlay handle and interaction state; the native intersection mask retains its
reference physical extent within the cropped surface. Allocation estimates count
the actual current buffers, including crops.

Load several models and update their transforms: [sample-gallery.js](../scripts/sample-gallery.js).

### Choosing an object rendering approach

Scripts choose rendering per object through the final `createObject` options.
The choice is independent of the model asset: a group may contain boxes,
detailed models, animated instances, and static scenery together.

| Approach | `renderGroup` | Visible 3D surfaces | Native handles |
| --- | --- | --- | --- |
| Independent objects (default) | Omit it or use `0` | One per visible object | One per allocated object surface. |
| Single scene surface | Give all 3D objects the same positive ID | One for the whole scene | Two for the shared surface, including hidden staging. |
| Multiple grouped surfaces | Assign different positive IDs to subsets | One per nonempty visible group | Two per allocated group, including hidden staging. |
| Adaptive grouped surfaces | Positive IDs plus `setRenderGroupAutoSplit` | One or more tiles per opted-in group | Two per allocated tile; extra handles during transitions. |

For example, these are alternative ways to create the same content:

```js
// Independent surfaces: native rectangular input can be enabled per object.
createObject(floorAsset, 0, -.55, 0, 1.2);
createObject(boxAsset, -.2, 0, 0, .14);
createObject(carAsset,  .2, 0, 0, .14, {renderGroup: 0});
```

```js
// One visible stereo surface: include the floor and every 3D model in group 1.
createObject(floorAsset, 0, -.55, 0, 1.2, {renderGroup: 1});
createObject(boxAsset, -.2, 0, 0, .14, {renderGroup: 1});
createObject(carAsset,  .2, 0, 0, .14, {renderGroup: 1});
```

```js
// Separate groups can concentrate pixels around different parts of the scene.
createObject(floorAsset, 0, -.55, 0, 1.2, {renderGroup: 3});
createObject(boxAsset, -.2, 0, 0, .14, {renderGroup: 1});
createObject(carAsset,  .2, 0, 0, .14, {renderGroup: 2});
```

Configure physics and grabbing per instance with `setObjectPhysics` and
`enableObjectInteraction` as usual. Group IDs are scene-local integer labels
from 1 through 256, not object IDs.
Membership is fixed at creation. Changing an object's group requires removing
and recreating it; the new object has a new ID, and physics, interaction, and
animation settings must be applied again. Existing objects are never migrated
when another object uses a different group ID.

A single surface reduces repeated scene traversal and depth passes, but must
cover all its members at one sampling density. Large or widely separated groups
can allocate more GPU memory or reach the resolution ceiling. Several compact
groups may be faster. Use `getEngineStats` and visual checks for the intended
scene instead of assuming one approach always wins. Panels created with
`createPanel` and controller lasers remain separate overlays in all three modes;
"single scene surface" refers to 3D geometry, not every overlay owned by the app.
Automatic subdivision is a separate opt-in; it preserves logical group membership
and individual object IDs while allowing several render tiles.

### Shared render group behavior

`createObject(asset, x, y, z, scale, {renderGroup: 1})` opts an object into a
shared stereo surface by default. Each allocated tile uses two overlay handles, alternating a
visible overlay and a hidden staging overlay. Transform, width, and texture are
prepared on the hidden overlay before it is shown; the previously visible
overlay is then hidden. Group membership is fixed at creation and scoped to the
scene. Physics, transforms, animation, visibility, mesh picking, and grabbing
remain per object. Native rectangular pointer state/events are unavailable for
grouped objects; use mesh hover/controller events and the interaction helper.
The shared surface passes native overlay input through to other applications.

A live creation reserves both handles when its group needs them; failure rolls
back the reservation before publishing the object. Removing the last member
of a tile releases both of its handles. Hidden members do not render; an entirely hidden group
releases its surface during synchronization and recreates it when shown again.
Both eyes share conservative group bounds and geometry depth, including occlusion
by objects outside the group. Grouped surfaces use different texture regions on
one common projection plane so late head-pose reprojection moves box silhouettes
and their depth cutouts together. Include mutually occluding geometry such as
floors in render groups too; independent surfaces use their own planes and can
reproject differently during head movement. Alpha blending retains scene-order
limitations. Object framebuffer colors use premultiplied alpha, including masked
and blended materials, and the native overlay is configured to match.

Surface dimensions grow with projected bounds and the most demanding member's
reference angular pixel density. This can use more memory when members spread
apart or approach the headset. Resolution is bounded by 4096 pixels per eye and
half the GPU's maximum renderbuffer width; reaching that ceiling logs a warning
and reduces sampling density. Keep groups compact to avoid this limit. Buffer
growth uses 25% allocation headroom rounded to 256 pixels. Shrinking requires
the requested size to stay below 60% of the allocation for 45 frames, with at
most two shrinks per frame. A resize renders and shows a replacement overlay
before detaching the previous one; it temporarily needs another overlay pair
and buffer set. Failed allocation keeps the previous surface available. Each
surface retains three color buffers and one shared depth attachment.

[grouped-stress.js](../scripts/grouped-stress.js) uses four compact groups for up to 240
colored boxes plus a floor in its own render group and a statistics panel. Its body
groups opt into at most two tiles each, with merging and rebalancing enabled. Bumper adds 10, A/X
adds 40, and B/Y clears. Fallen/distant bodies are removed. The original
[stress.js](../scripts/stress.js) keeps its independent surfaces and 51-box cap.


### Automatic render group subdivision

A render group draws its objects into one stereo image that SteamVR displays as
an overlay. If two clusters move far apart, that image must also cover the empty
space between them. Maintaining the requested detail then needs a larger texture,
which consumes more GPU memory and can reach the resolution ceiling.

Automatic subdivision lets the engine replace that image with smaller tiles
around subsets of the objects. It reduces allocated empty space while preserving
the requested sampling density; it does not lower model detail or texture quality.
It is useful for sparse or spreading groups. Extra tiles also mean more overlays,
scene traversal, and depth passes, so lower allocation does not guarantee higher
FPS. Keep tightly packed scenes on one surface when measurements favor it.
`getEngineStats().gpuAllocatedEstimateBytes`, `fps`, and `cpuPercent` can help
compare the intended layout after its allocations settle.

```js
setRenderGroupAutoSplit(1, {
  maxSurfaces: 4, targetEyePixels: 2048, merge: true, rebalance: true
});
createObject(boxAsset, -.6, 0, 0, .1, {renderGroup: 1});
createObject(carAsset,  .6, 0, 0, .1, {renderGroup: 1});
// Stop further adaptation without changing existing tiles or object IDs:
// setRenderGroupAutoSplit(1, false);
```

The function returns `undefined`. Invalid group IDs or numeric options throw
`RangeError`; options must be an object, `undefined`, or `false`.
The policy is scene-local and can be configured before or after creating members.
Groups without a policy retain the requested single surface. The engine checks
at most every 500 milliseconds and starts at most one split at a time. A tile
must exceed `targetEyePixels` or reach the resolution ceiling. Bounded candidates
along both projected axes must preserve member sampling density, avoid the
ceiling in both children, and reduce estimated total allocated texels by at
least 20%, including normal allocation headroom. `targetEyePixels` is a trigger,
not a hard per-tile size limit or a promise that a useful split exists.

After an allocation failure the engine waits at least five seconds before
trying another split, avoiding repeated allocation pressure.

A split retains the complete parent surface until both replacements can be
presented. Allocation failure keeps the current membership and tile; presentation
failure keeps the parent available and retries. Splitting temporarily needs
another two overlay pairs and two complete buffer sets alongside the retained
parent. Overlay limits or allocation failures can prevent a split. Statistics
include retained generations until they are released.

Grouped native color/depth buffers share an engine-wide 512 MiB allocation
budget. All three stereo color buffers, the shared depth attachment, and retained
replacement generations count against it. This is a conservative surface-storage
estimate, not a limit on total GPU memory: models, panels, independent surfaces,
driver overhead, and compositor resources are outside this budget. A structural
change must fit both complete new outputs alongside the old snapshots before
allocation starts; otherwise membership stays unchanged and adaptation waits five
seconds before retrying.

Ordinary grouped buffer resizing admits at most one attempt per 100 ms and does
not run during a structural handoff. Allocation failure or budget refusal retains
the current buffers and delays that tile's next attempt for five seconds. Growth
can use a smaller 256-pixel allocation bucket when the full request cannot fit;
the engine keeps the current allocation if no larger bucket can be admitted.
A shrink must admit its full requested allocation; otherwise it keeps the
existing sharper buffers rather than allocating a lower-density replacement.
While growth is delayed, denied, or awaiting presentation, the current buffer
renders the entire moving group at reduced pixel density instead of clipping its
bounds. Fast-moving objects therefore need not wait for regrouping to remain
visible. These limits bound allocation pressure; they do not guarantee a target
FPS or full reference sharpness for arbitrary object counts or motion.

With the default split-only policy, existing objects keep their tile membership
after a split. Enabling `merge` or `rebalance` allows the engine to change render
tile membership under the constraints below, while logical group and object IDs
remain unchanged. New objects join the nearest existing member's
tile. Physics, animation, grabbing, and script IDs are unchanged. Tiles share the
same projection plane and per-eye opaque/masked occlusion rules. Hidden objects
retain logical tile membership; `maxSurfaces` counts those memberships too.
Removing all members releases their tile, and entirely hidden tiles follow the
normal surface synchronization rules.

`merge` and `rebalance` default to `false`, preserving split-only behavior.
Disabling the policy stops future splits, merges, and rebalances; it leaves current
tiles intact. Reducing `maxSurfaces` prevents additional splits and does not force
a merge. With merging disabled, reload the scene or remove and recreate its group
members to start from one tile again. A single large object, invalid
near-plane bounds, overlapping clusters, or insufficient allocation savings can
leave a group unsplit. Moving an isolated object close to the headset can still
reach the resolution ceiling. Use several explicit compact groups for layouts
that need direct control over surface count.


#### Merging and rebalancing

Set `merge: true` to let two tiles in the same logical group combine when their
members come back together. The combined image must fit within `targetEyePixels`,
avoid the resolution ceiling, and reduce estimated steady allocation by at least
20%. Requiring a compact result avoids immediately splitting the merged tile
again. This can reduce overlay count and repeated rendering; it is not a guarantee
of higher FPS in every scene.

Set `rebalance: true` to let two existing tiles exchange members when objects have
moved between spatial clusters. The engine tries bounded cuts along both projected
axes, keeps the tile count unchanged, and requires at least 20% less estimated
steady allocation. Both outputs preserve member sampling density and avoid the
resolution ceiling. Output labels are chosen to minimize membership changes.

Merging takes priority when it qualifies. Allocation comparisons use current
projected demand with normal headroom, rather than oversized buffers awaiting
ordinary shrinking. A proposal must have the same object-to-output assignment on
at least three checks spanning one second. Regrouping waits at least two seconds
after the group's previous structural change completes. At most four candidate
pairs are evaluated per check, using a rotating search while a persistent proposal
is rechecked first. These constraints limit churn from brief movements. Tiles
containing hidden members defer merging/rebalancing until those members are
visible again. Pairs containing blended materials are excluded to preserve their
existing compositor order; opaque and masked materials can regroup. Unchanged groups and invalid near-plane bounds stay as they are.

Both old tiles remain available until every visible replacement is ready to show;
view-culled replacements can complete the handoff without being shown. A rebalance
retains both old buffer sets on either output's presentation failure. It hides
replacement fronts before reusing their buffers, retries failed hiding, and keeps
the fallbacks. Removing or hiding a pending output transfers its fallback snapshots
to the surviving output until the handoff can finish. Object IDs, interaction,
physics, and animation are preserved; this moves render membership only.

A merge temporarily adds one overlay pair and buffer set before releasing the two
old tiles. A rebalance temporarily adds two pairs and buffer sets. Retained
snapshots are included in GPU allocation estimates. Allocation failure leaves
current membership intact and uses the existing five-second retry backoff.
Already-started handoffs finish even if a script subsequently disables the policy.
Each configuration call replaces that group's options; omitted options return to
their defaults, including `merge: false` and `rebalance: false`.

Try adaptive groups with merging and rebalancing enabled: [grouped-stress.js](../scripts/grouped-stress.js).

### Shared model geometry

Unlit shared GPU meshes reuse identical position/UV vertices within each material
primitive when vertex and index buffers together use less storage. By default,
full-detail triangle order, UV seams, and material boundaries are preserved.
Automatic LOD retains that full mesh and selects reduced index streams for
smaller objects; opt out with `lod: false` to always render the full mesh. Indices use 16 bits when
possible, otherwise 32 bits. Picking, collision construction, and per-instance
CPU animation retain their expanded geometry; deformed instances draw from their
own dynamic vertex buffers.

For static opaque or masked models, opt into GPU vertex-cache optimization at
load time:

```js
const car = loadModel('assets/samples/ToyCar/ToyCar.glb', 768, {
  optimizeVertexCache: true
});
```

This reorders whole triangles inside each indexed material primitive so nearby
draws can reuse transformed vertices, then packs GPU vertices in first-use order
for vertex fetch locality. It runs once during GPU upload and keeps
every triangle, its winding, exact vertex/UV data, textures, and reference
resolution. It does not simplify geometry or alter picking/collision meshes.
Objects can still move, rotate, be grabbed, and use physics; the static-geometry
restriction concerns vertex deformation, not object motion.

For PBR, this option also enables full-input indexing of static opaque/masked
primitives when compact GPU storage is smaller. Identity includes both UV inputs,
normal and tangent/handedness as well as position. Compact full-detail buffers
coexist with reduced PBR LOD streams, which also receive compact indices when
smaller under the same cache option. Failed optional staging
keeps expanded full detail; the option does not enable plain indexing for blends.
The retained CPU geometry and picking remain unchanged.
Blended primitives and assets with animation or skinning retain their original
GPU triangle order, even when the option is enabled. Non-indexed primitives also
retain their original stream.

The option defaults to `false` because reordering can change ownership of exactly
coplanar overlapping triangles within an opaque/masked primitive. Leave it off
for assets that rely on that order, and also set `lod: false` to retain the full
geometry at every size. Material and object draw order remain
unchanged. A cached path keeps its original setting when the option is omitted;
explicitly requesting a different setting throws. Choose the setting on the
first load or reload the script to change it. The value must be a boolean.

GLB triangle primitives across mesh nodes, node transforms, embedded base-color
textures, material factors, alpha modes, UV transforms, and approximate sheen
are supported. Both lit and unlit rendering honor glTF `doubleSided`: single-sided
materials cull their back faces, with winding corrected for mirrored node
transforms. The rule also applies to depth occluders, animated objects and ordered
instancing. Double-sided materials retain both faces. Sidedness controls rendering;
picking and physics continue using the complete mesh. [Animation](animation-api.md) covers skinning, morph targets and clip limits.

Opt-in `pbr: true` adds [physical model lighting](lighting-api.md), material maps,
normals, tangents and [fabric sheen](lighting-api.md#fabric-sheen). Legacy rendering remains the default. PBR currently uses
full expanded meshes and CPU animation instead of the unlit geometry fast paths.

### Ordered GPU instancing

Repeated static models use ordered GPU instancing by default:

```js
const asset = loadModel('assets/Avocado.glb', 768);
for (let i = 0; i < 16; i++) {
  createObject(asset, (i % 4 - 1.5) * .16, (Math.floor(i / 4) - 1.5) * .16,
    0, .12, {renderGroup: 1});
}
```

The renderer combines consecutive eligible copies of the same asset at the
same selected LOD level into one
GPU draw per eye/pass. Sharing a `renderGroup` lets their visible geometry use
the same surface. Single, grouped, and adaptive rendering keep their existing
surface membership, projection, and occlusion rules. Instancing changes neither
triangle detail nor texture resolution, and works with moving, rotated, grabbed,
and physics-driven objects.

Eligible assets have one opaque or masked material primitive and no animation,
skinning, morph targets, or per-instance deformation. Multi-part models such as
ToyCar, blended materials, isolated instances, and incompatible runs keep the
original drawing path. Instancing preserves object order and the selected geometry
index stream; it does not sort objects by asset to manufacture batches. Interleaving different
assets can therefore prevent batching. Masked holes use the same material shader.

`gpuInstancing` must be a boolean and defaults to `true`. Pass
`{gpuInstancing: false}` on first load to use individual draws. A cached path inherits
its first-load setting when the option is omitted; an explicitly conflicting
setting throws. Reload the script to change the setting. Unsupported geometry
falls back normally rather than causing a load error. `optimizeVertexCache` is
independent and can be combined with this option.

Instancing primarily reduces CPU draw submission; GPU-bound workloads may see
little FPS improvement. Measure the actual scene when evaluating its performance benefit.
The renderer lazily creates a shared shader and a reusable 7 KiB instance buffer;
the buffer is included in `gpuAllocatedEstimateBytes`. Driver streaming copies
remain outside that estimate. Initialization failure retains original draws,
with another initialization attempt after scene reload. Instancing resources
are released on successful scene replacement and engine shutdown.

See [instances.js](../scripts/instances.js) for a grabbable example.

### Automatic model LOD

`loadModel` enables automatic level of detail by default:

```js
const car = loadModel('assets/samples/ToyCar/ToyCar.glb');
// Keep the full mesh at every distance for this separately loaded asset.
const exact = loadModel('assets/Avocado.glb', 768, {lod: false});
```

The engine retains full geometry and generates up to two shared reduced levels
per eligible static opaque primitive, targeting half and quarter triangle counts.
Unlit meshes use reduced index buffers with a relative position/UV error limit of
0.001, UV weights of 0.1 and locked mesh borders. [PBR meshes](lighting-api.md)
include normals, tangents, handedness and material UVs in simplification, lock
surface seams and borders, and upload reduced position/surface buffers. With
`optimizeVertexCache: true`, reduced PBR streams also receive compact cache-ordered
indices when smaller. Each level owns its index width and compact vertex count;
its geometry error and selected triangle count remain unchanged.
PBR uses the same combined error limit with additional surface-attribute weights.
Each PBR level must save at least 20% of triangles, and a second must save at least
20% beyond the first. Targets may not be reached; this is lossy geometry, not a
promise of identical pixels or a forced triangle budget.

Animated, skinned, morph, CPU-deformed, masked and blended geometry keeps full
detail. Small meshes and ineffective reductions also retain the original path.

Each object gets one detail decision per frame after input, animation and
physics updates. The engine estimates its projected bounding-sphere diameter
from both eyes, headset projection/resolution and off-axis placement. Both eyes,
all grouped/independent surfaces, and color/depth occlusion passes use that same
choice. Instancing batches only consecutive instances with matching asset and
selected detail. Textures, reference resolution, picking and physics/collision
meshes remain unchanged.

Grabbed objects always use the full mesh. The nearest sphere point inside 35 cm
also restores full detail; after restoration it stays full until beyond 40 cm.
For other objects, the conservative diameter estimate starts quarter detail
below 102 pixels, half detail below 255 pixels, and full detail above that.
Hysteresis retains quarter detail through 138 pixels and half detail through
345 pixels. Invalid/near-plane projections keep full detail. If a requested
level is unavailable, the renderer uses a finer available level or the full mesh.
These thresholds concern estimated headset pixels, independently of `eyePixels`.

`lod` must be a boolean. A cached exact path inherits its first-load setting when
the option is omitted; an explicitly conflicting setting throws. Reload the
script to change it. Use `{lod: false}` for assets requiring unchanged geometry
or for visual comparisons. `optimizeVertexCache` remains independently opt-in.

Reduced geometry is generated once during GPU upload and shared by every copy
of the asset. Full geometry stays allocated, so LOD adds storage while reducing
rendering work. Optional PBR position/surface pairs share a **32 MiB engine-wide
buffer-payload budget**, separate from textures, full meshes, unlit LOD indices
and overlay buffers. Reservations include both buffers before either is
published. If a pair cannot fit or its GPU allocation fails, that primitive keeps
a finer available level or full detail; loading and scene restore do not fail
because optional detail is unavailable. Some primitives of an asset may reduce
while others remain full.

Budget admission follows asset/primitive upload order. There is no eviction or
background retry for an already uploaded asset; reloading the script rebuilds
availability. Held objects use full geometry while their shared reduced buffers
remain resident. Removing an object does not unload its cached asset. Scene
reload/shutdown or actual asset destruction releases the buffers and budget;
`gpuAllocatedEstimateBytes` includes allocated pairs once per shared asset.
The budget limits payload bytes, not driver overhead or total engine VRAM.
Generation can add loading time. Scene saves retain `lod`, not generated levels
or reservations; restored availability depends on the current budget.
See [lod.js](../scripts/lod.js) for a detailed grabbable-model example and
[pbr.js](../scripts/pbr.js) for lit geometry with the same automatic policy.

## Scene lighting

Load receivers with `{pbr: true}` to use scene-wide lighting. Lights add to each
asset's `setModelLighting` settings; they do not affect unlit models, panels or
lasers. All three types share eight live slots, including disabled lights, and
cast no shadows.

| Light type | Controls | Guide and complete example |
| --- | --- | --- |
| Point: position, range and distance falloff | `createPointLight`, `setPointLight`, `getPointLight`, `removePointLight` | [Point-light API](lighting-api.md#dynamic-point-lights), [lights.js](../scripts/lights.js) |
| Spot: position, direction, range and cone | `createSpotLight`, `setSpotLight`, `getSpotLight`, `removeSpotLight` | [Spotlight API](lighting-api.md#dynamic-spotlights), [spotlights.js](../scripts/spotlights.js) |
| Directional: parallel rays without distance falloff | `createDirectionalLight`, `setDirectionalLight`, `getDirectionalLight`, `removeDirectionalLight` | [Directional API](lighting-api.md#scene-directional-lights), [directional-lights.js](../scripts/directional-lights.js) |

Each type supports color/intensity, enabled state, optional object attachment and
container ownership. Typed functions reject another type's ID. Attachments are
resolved after interaction/physics; removing their object removes its lights.
Directional attachments follow rotation only. Container disposal removes owned
lights, and [v10 snapshots](scene-persistence.md#scene-directional-lights-v10)
preserve all three types with remapped attachment IDs.

## Grabbing and selection

```js
const asset = loadModel('assets/samples/Box/Box.glb');
const box = createObject(asset, 0, 0, 0, .3);
enableObjectInteraction(box, {grabButton: 'both'});
setObjectInteraction(box, {
  laserYaw: true, depthAdjust: true,
  handRotationAxes: {pitch: true, yaw: true, roll: true}
});
setLaserVisible('left', true);
setLaserVisible('right', true);
```

`setObjectInteraction(id, options)` updates only supplied fields:

| Option | Default on a new object | Meaning |
| --- | --- | --- |
| `grabbable` | `false` | Enable native grab/movement handling. |
| `grabButton` | `'trigger'` | `'trigger'`, `'grip'`, or `'both'`; either allowed button can acquire/hold. |
| `laserPassThrough` | `true` | Let native laser input pass through the surface; grabbable forces this false. |
| `directGrab` | `true` | Use near-hand holding when acquired close to the controller. |
| `laserYaw` | `false` | Distant grab: stick left/right changes yaw. |
| `depthAdjust` | `false` | Distant grab: stick up moves farther, down moves nearer. |
| `handRotationAxes` | All `false` | `{pitch, yaw, roll}` chooses hand rotation axes for direct grabs. |

Acquisition requires a current transformed triangle hit. Empty mesh gaps never
fall back to a bounding sphere. Near-hand mode is selected after that hit when
the controller-to-object-center distance is below `0.10 + scale * 0.5` meters;
there is no proximity-only pickup. Static picking uses a shared per-asset BVH;
animated instances refit their own picking geometry. Each hand can hold its own
object. A held object releases after all allowed buttons are up for five frames.

`enableObjectInteraction(id, {grabButton})` enables native grabbing with the
chosen button. Its options object recognizes `grabButton`; configure other
fields with `setObjectInteraction`. The legacy one-argument form marks the
object interactive without newly enabling automatic movement, for scripts that
handle movement themselves. `enableObjectSurface(id)` is a compatibility no-op.

`setLaserVisible(hand, visible)` controls the engine's selection beam for
`'left'` or `'right'`. Its raw-controller grabs are independent of native laser
focus. SteamVR owns panel pointer delivery.

`getPointer(id)` returns native surface state `{valid, down, x, y, hand}`;
`hand` is `'left'`, `'right'`, or `null`. Coordinates are surface-local, not
panel pixels. This is not the custom beam's grab state. `getPointer()` is usable
only when exactly one object is interactive.

## Controllers

`getController(hand)` returns `{valid, connected, x, y, z, rotation}`.
`rotation` is a row-major 3×3 matrix. Positions use scene coordinates.
`connected` can remain true during temporary pose loss; check `valid` before
using pose data. An invalid pose returns zero position/rotation values.

`getControllerInput(hand)` reads the cached raw controller stream:

| Field | Contents |
| --- | --- |
| `valid`, `hand` | Stream validity and requested hand. |
| `raw` | Component paths mapped to boolean or finite numeric values. |
| `buttons` | `{down, touched}` for trigger, grip, bumper, thumbstick, a/b/x/y, menu, system, view, and dpad_up/down/left/right. |
| `analog` | Numeric `trigger`, `grip`, and `thumbstick: {x, y}`. |
| `capacitive` | Sensor projections, including ring, pinky, thumbrest, and dpad. |

Missing convenience values are false/zero. Unsupported structured values are
invalidated. Use [getFingerTracking](tracking-api.md) when missing values and
freshness must be distinguished from real zero.

`getInput(hand)` reads optional OpenVR action bindings. It returns `valid`,
`gripClick/Touch/Value`, `triggerClick/Touch/Value`, `thumbstick: {x,y}`,
`thumbstickClick/Touch`, `bumperClick/Touch`, `systemClick/Touch`, `menuClick`,
`viewClick`, `aClick`, `bClick`, `xClick`, `yClick`, and
`dpadUp/Down/Left/RightClick`. Action availability depends on SteamVR focus and
bindings; unavailable values are false/zero.

`pulse(hand, seconds = .05, amplitude = .5)` requests action-based haptics and
returns success. Both numbers must be finite within `[0, 1]`; availability
still depends on native action bindings. All hand arguments use `'left'` or
`'right'`.

Read controller poses and raw input, and request haptics: [input.js](../scripts/input.js).

## Events

`onInputEvent(callback, options = {})` replaces the single callback. Optional
`{container: id}` gives it [container lifetime](scene-containers.md); omission
keeps root lifetime. `pollInputEvents()`
drains an independent queue, so polling does not consume callback delivery.
Delivery happens in the host loop. The bounded queue evicts the oldest 1024
entries when it exceeds 4096; consumers should drain it regularly.

Events include `type`, `hand`, `control`, `objectId`, `x`, `y`, `down`, `value`,
`dx`, `dy`, `dz`, `dpitch`, `dyaw`, and `droll`; interpret fields by event type.

| Type | Meaning |
| --- | --- |
| `controller.button` | Raw boolean path in `control`, state in `down`. |
| `controller.value` | Raw numeric path in `control`, number in `value`. |
| `physics.jointBreak` | Automatic joint release with joint/object IDs and estimated force/torque; see [breakable joints](physics-api.md#breakable-joints). |
| `physics.collisionStart`, `physics.collisionStay`, `physics.collisionEnd` | Opt-in pair contacts with object IDs, point/normal, penetration, and closing speed; see [physics events](physics-api.md#collision-events). |
| `overlay.pointer` | Native surface pointer update. |
| `overlay.surfaceHoverStart`, `overlay.surfaceHover`, `overlay.surfaceHoverEnd` | Native rectangular surface hover. |
| `overlay.meshHoverStart`, `overlay.meshHoverEnd` | Actual model hit transitions. |
| `overlay.grabStart`, `overlay.grabMove`, `overlay.grabEnd` | Native object grab lifecycle. |
| `overlay.move`, `overlay.rotate` | Object transform changes with position/rotation deltas. |
| `panel.hoverStart`, `panel.hover`, `panel.hoverEnd` | Panel hover with top-left-origin pixel coordinates. |
| `panel.pointerDown`, `panel.pointerUp` | Panel button transition; `control` is `left`, `right`, or `other`. |

Raw controller events can carry the latest native surface-hover `objectId`,
or zero. That field is not a promise that the hand owns or selected that object.
Panel IDs and object IDs are separate numeric namespaces; distinguish them by
`type`. Animation does not emit completion events; inspect playback state.

Combine a scoped pointer callback with the independent polled queue: [containers.js](../scripts/containers.js).

## Additional APIs

- [Panels](panels.md): `createPanel`, canvas drawing, `panelLoadFont`,
  `panelSetTransform`, `panelSetRotation`, `panelSetVisible`, `removePanel`, and
  compatibility functions `panelClear`, `panelText`, `panelPresent`,
  `panelCanvasClear`, `panelCanvasText`.
- [Physics](physics-api.md): `setObjectPhysics`, `getObjectPhysics`,
  `removeObjectPhysics`, `setPhysicsGravity`, `setObjectVelocity`,
  `setObjectAngularVelocity`, `applyObjectImpulse`, `setPhysicsCollisionEvents`,
  `createPhysicsJoint`, `getPhysicsJoint`, `setPhysicsJoint`, `removePhysicsJoint`,
  `setPhysicsJointMotor`.
- [Lighting](lighting-api.md): `setModelLighting`, `createPointLight`,
  `setPointLight`, `getPointLight`, `removePointLight`, `createSpotLight`,
  `setSpotLight`, `getSpotLight`, `removeSpotLight`.
- [Animation](animation-api.md): `getAnimations`, `playAnimation`, `pauseAnimation`,
  `resumeAnimation`, `stopAnimation`, `seekAnimation`, `getAnimationState`.
- [Tracking](tracking-api.md): `getEyeTracking`, `getCalibratedEyeTracking`,
  `getEyeFoveationCenters`, `getFingerTracking`.
- [Calibration](eye-calibration.md): `saveEyeTrackingCalibration`,
  `getEyeTrackingCalibration`.
- [Lifecycle](lifecycle.md): reload and resource ownership.

Combine model interaction and physics in a complete script: [physics-controls.js](../scripts/physics-controls.js).

## Scene persistence

Use `captureSceneContainer(id)` / `restoreSceneContainer(snapshot)` for in-memory
JSON snapshots and `saveSceneContainer(id, name)` / `loadSceneContainer(name)` for
named files. See [scene save and restore](scene-persistence.md) for the complete
v11 schema (including v1–v10 compatibility), panels and pixels, animation
playback, physics bodies/joints, storage paths,
fresh object/joint/group mappings, limits, and unsupported resources.

Capture and restore an in-memory scene with fresh handles: [scene-snapshot.js](../scripts/scene-snapshot.js).
