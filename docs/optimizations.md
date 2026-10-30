# Performance options

Pocketspace applies many optimizations automatically. The options below let a
script choose geometry quality, surface layout and physics concurrency for its
own workload. They are supported by normal engine builds; no environment flags
or experimental executable are required.

## Defaults and controls

| Feature | Default | Script control | Benefit and tradeoff |
| --- | --- | --- | --- |
| Vertex-cache ordering and compact PBR indexing | Off | `loadModel(path, pixels, {optimizeVertexCache: true})` | Reuses transformed vertices and can reduce GPU storage. Keeps full-detail inputs, but reordering can change exactly coplanar triangle overlap. Applies to eligible static opaque/masked geometry. |
| Shared render surfaces | Independent surface per object | `createObject(..., {renderGroup: 1})` | Reduces overlays and render targets. Widely separated members can waste pixels; grouped layout and external-depth work can outweigh the saving. |
| Automatic group subdivision | Off | `setRenderGroupAutoSplit(1, {...})` | Splits sparse groups to reduce empty allocation while preserving sampling density. Extra tiles add overlays and passes. |
| Group merging and rebalancing | Both off | Include `merge: true` and/or `rebalance: true` in the subdivision policy | Adapts tile membership as objects move. Uses stability checks and cooldowns; allocation savings do not guarantee faster frames. |
| Automatic static-model LOD | On | `loadModel(path, pixels, {lod: false})` to disable | Reduces distant geometry; nearby and held models retain full detail. Reduced meshes can change appearance and add resident storage. Picking and physics retain full CPU geometry. |
| Ordered GPU instancing | On when eligible | `loadModel(path, pixels, {gpuInstancing: false})` to disable | Reduces draw submission for consecutive compatible instances. Does not reorder objects or guarantee a GPU-time improvement; unsupported models use ordinary draws. |
| Physics worker policy | Automatic (`-1`) | `setPhysicsWorkers(-1)`, `0`, or `1`–`4` | Automatic mode uses up to two additional workers at 64 active bodies. Explicit workers can help larger simulations, but scheduling overhead can hurt small scenes. |

Model options belong to a shared asset, not an individual object. Choose them on
its first load. Loading the same exact path inherits omitted options; conflicting
explicit options throw. Reload the script to change the choice. Moving or grabbing
an object does not make its geometry ineligible for static-mesh optimizations;
vertex deformation is the relevant distinction.

See the [JavaScript API](javascript-api.md) for eligibility, validation and group
policy limits, the [lighting guide](lighting-api.md) for PBR geometry, and the
[physics guide](physics-api.md) for worker reporting and behavior.

## Enable only what your scene needs

For a repeated static model, enable cache ordering after checking that its
coplanar surfaces still look correct. LOD and eligible instancing are already on:

```js
const asset = loadModel('assets/Avocado.glb', 768, {
  optimizeVertexCache: true
});
for (let i = 0; i < 8; i++) {
  createObject(asset, (i - 3.5) * .15, 0, 0, .12, {renderGroup: 1});
}
```

For a group that spreads out, optionally enable adaptive tiles:

```js
setRenderGroupAutoSplit(1, {
  maxSurfaces: 4,
  targetEyePixels: 2048,
  merge: true,
  rebalance: true
});
// Stop future adaptation; existing tiles remain until normal scene cleanup.
// setRenderGroupAutoSplit(1, false);
```

A single shared group and several groups are both supported. Compare them with
independent surfaces using the same models, positions, reference resolution and
motion. Grouping changes surface layout, not object identity, grabbing or physics.
See [instances.js](../scripts/instances.js),
[grouped-stress.js](../scripts/grouped-stress.js),
[lod.js](../scripts/lod.js) and [pbr.js](../scripts/pbr.js) for complete scenes.

For assets that require their original geometry and intra-primitive triangle
order, load with `{lod: false, optimizeVertexCache: false}`. LOD changes geometry,
not texture resolution; reducing the `eyePixels` argument separately lowers
render-target sampling density and can visibly pixelate the result.

PBR reduced buffers share a 32 MiB optional payload budget across assets. The
engine retains full geometry and falls back to finer levels when optional
storage is unavailable. Grouped color/depth generations have a separate 512 MiB
budget. Neither budget is a cap on total engine or headset GPU memory.

## Compare your workload

Use `getEngineStats()` to compare FPS, process CPU/RAM, available GPU-memory
reporting and physics timings. Allow allocations and adaptive layouts to settle;
keep the headset awake and worn for comparable measurements. Engine FPS measures
loop throughput, not compositor presentation rate. GPU allocation estimates are
not measured GPU duration or total device memory consumption.

Check visuals and interaction separately from speed: move your head, change
distance, grab/release objects, and inspect overlaps, alpha cutouts and LOD
transitions. A gain in one scene does not establish a universal best setting.

## Automatic optimizations and experiments

Eligible GPU skinning, lazy exact picking, rigid-pose caching, adjacent rigid
batching, target cropping, bounds/scissor reuse, panel submission caching and
physics sleeping/collider caches run automatically. They have no public script
switch. The [architecture](architecture.md) explains their eligibility and
pipeline contracts.

With at least four active, nonzero lights, local point/spot lights are filtered
automatically per object: lights whose range cannot reach its conservative current bounds are omitted from that draw.
Spotlights whose entire cone misses the bounds are omitted too, with conservative
edge padding. This keeps the same illumination and eight-light scene limit; broad overlapping
lights still require the full loop. See the
[lighting guide](lighting-api.md#dynamic-point-lights) and
[range comparison](../tests/results/light-work.md) and
[spotlight cone comparison](../tests/results/spotlight-cone-selection.md).
Scene directional lights remain in every eligible object’s light set; distance
and cone tests never reject them.

Test-only shader, culling, shared-depth, VAO, uniform-cache and synchronization
experiments are not supported production options. Their environment switches
apply only to the corresponding excluded diagnostic targets. Use the
[test index](../tests/index.md) and linked measurements for those investigations;
do not copy diagnostic flags into normal application launch instructions.
