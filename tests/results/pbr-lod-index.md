# Cache-indexed reduced PBR LOD

Steam Frame, 2026-10-29. The existing opt-in `optimizeVertexCache: true` policy
now also compacts reduced PBR LOD streams when smaller. The option's default
remains false; no new JavaScript control or snapshot version is introduced.

## Implementation

Simplification still uses the same original CPU inputs, locked seams/borders,
14-float shader-input identity, surface weights, 0.001 error limit and useful
triangle-reduction thresholds. After selecting reduced triangles, cache/fetch
ordering packs their referenced inputs into compact position/surface buffers
and a 16- or 32-bit element buffer. Every level owns its index width, uploaded
vertex count, complete draw count and exact payload charge. Full-detail metadata
must not be used to interpret a reduced element buffer. Without the cache option,
or when compaction saves no storage, reduced streams remain expanded.

Color and external-depth draws select a level's complete buffer set together.
The PBR material wrapper selects its matching surface stream. Full CPU inputs,
picking, physics, stereo selection, LOD thresholds and held-object full-detail
restoration remain unchanged. This changes storage/order after simplification,
not its geometry quality or textures.

The shared 32 MiB optional budget covers positions, surface data and indices.
Exact compact payloads are reserved before GPU allocation/publication. Partial
position/surface/index failure and CPU staging exceptions release unpublished
buffers and reservations, keeping finer/full detail. Asset cleanup releases every
buffer and charge once. Engine GPU allocation estimates use each level's charged
payload, avoiding expanded-count estimates for compact vertices. Full geometry,
textures, environments and overlays remain outside the optional LOD budget.

## Automated validation

`pbr-index-production-regression` passed 288 exact stereo/color/depth/LOD/pose
image comparisons across eight models. It reconstructs compact reduced streams
and verifies the exact oriented-triangle multiset and all shader inputs against
expanded reduced controls, with identical simplification error/count. Full-detail
seams, transparency and animated fallback checks also remain enabled.

Additional checks cover shared snapshots/cache conflicts, rejected reload
preservation, full-detail partial upload rollback and reduced index rollback.
The final budget check verifies actual GL buffer sizes equal each level's charge,
compact admission when the remaining budget exactly fits the compact payload,
a fully occupied budget, correct index-width metadata and complete cleanup.
The existing LOD regression separately passed 40 exact routing comparisons,
180 quality views, its default expanded policy, budget and allocation failures.

## Matched production-path measurements

Sixteen full-quality ToyCars, fixed camera/transforms, studio lighting,
768-square offscreen eye target, alternating eyes. All assets and shaders were
initialized before timing and remain allocated across stages. Each comparison
runs control/on/on/control, warming 30 frames and measuring 120 per stage.
Every frame checks actual draw and triangle counts, GL errors and GPU disjoint.
These are offscreen production-path costs, not compositor FPS.

At LOD 1/2, both controls already use cache-indexed full detail. Only the reduced
storage differs: expanded versus cache-indexed. Both render 48 PBR draws and
1,088,352 triangles per frame. Color tests have zero PBR depth work; external-depth
tests have zero PBR color work and occlude a legacy target behind the cars.
ToyCar has one useful reduced level, so selecting 2 falls back to the same mesh
as 1. These are separate ABBA sequences, not distinct simplification levels.

| Pass | Selected LOD | GPU expanded → indexed (ms) | CPU enqueue (ms) | Synchronized wall (ms) |
| --- | ---: | --- | --- | --- |
| Color | 1 | 4.943 → 2.542 | 0.252 → 0.227 | 6.899 → 4.230 |
| Color | 2 | 4.902 → 2.429 | 0.297 → 0.254 | 6.889 → 4.104 |
| External depth | 1 | 3.049 → 1.350 | 0.098 → 0.085 | 4.736 → 2.852 |
| External depth | 2 | 3.174 → 1.266 | 0.097 → 0.082 | 4.745 → 2.832 |

The same run rechecked LOD 0 with the full cache policy off/on, rendering
1,742,976 triangles and 48 draws: color GPU 10.786 → 3.534 ms and external-depth
GPU 6.647 → 2.073 ms. Indexed reduced rendering is now cheaper than indexed full
detail on this workload. Raw stages: [samples](pbr-index/reduced-lod.json).

Shared optional reduced payload falls from 8,470,056 to 3,017,886 bytes (about
64% less). Combined with compact full detail, total shared geometry is 8,568,548
bytes versus 14,020,718 with expanded reduced streams. Retained CPU arrays,
textures and environments are unchanged and excluded. Upload latency and loading
peak memory were not measured. Clocks, thermals and scheduling were not locked;
short offscreen gains do not guarantee sustained application frame rates.

## Policy

Use reduced indexing under the existing cache opt-in, retaining expanded uploads
when that option is off. Coplanar triangle ownership can still depend on order;
these results do not justify changing the global cache default. Profile broader
model/material workloads before reconsidering it. Diagnostic expanded-reduced
controls and failure injection are compiled only into excluded test targets.

## Headset acceptance and documentation

The user accepted the normal-engine `tests/scenes/pbr-index.js` check: distance
changes, grip/trigger grabs and rotation, normal-map reflections, cutout holes,
overlaps and full detail while held looked correct. No diagnostic overrides were
used. The demo was stopped immediately after verification and its exit confirmed.
This visual acceptance is separate from automated correctness and timing results.
Documentation lint, the VitePress site build and whitespace checks passed.
