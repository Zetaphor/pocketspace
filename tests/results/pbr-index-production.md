# Production PBR indexing integration

Steam Frame, 2026-10-29. The existing `optimizeVertexCache: true` option now
supports static opaque/masked PBR geometry in the normal engine. Its default
remains false. Diagnostic environment overrides are absent from production.

## Implementation and ownership

Full 14-float shader-input identity preserves position, base UV, normal, tangent
with handedness and material UV seams within each primitive. Eligible compact
uploads must save geometry storage and receive cache/fetch ordering. Blended,
animated, skinned and morph geometry retain expanded uploads. CPU geometry,
picking and collision inputs are unchanged. Exactly coplanar overlapping triangles
can depend on original order, so the established opt-in policy remains important.

Position, surface and element buffers are staged as one publication. Allocation
sizes are checked; partial failure or CPU staging allocation failure releases
unpublished buffers and falls back to expanded full detail. Existing shared asset
cleanup owns all published buffers, and existing uploaded-vertex/index counts
account for their GPU payload. No duplicate expanded full-detail GPU buffers are
retained. The source draw count stays distinct from the compact vertex count.

Reduced PBR LOD position/surface pairs continue to be generated from unchanged
CPU inputs and use their existing expanded streams, quality policy and 32 MiB
optional-payload budget. They coexist with compact full detail; color and depth
select the same geometry. Full-detail indexing is independent of that optional
budget. The existing snapshot v7 option and cache-conflict policy apply without
a format change. The concise public PBR example enables the option.

## Correctness

The production-policy check passed 288 exact stereo/color/external-depth/LOD
comparisons across eight models, including mirrored normal maps, original mixed
alpha materials and animated fallbacks. It verified reconstructed shader inputs,
unchanged CPU arrays, 33 indexed primitives and buffer cleanup. It also checks
that `SF_PBR_INDEXED=0` cannot override production, shared snapshot restore,
conflicting cache options, rejected reload preservation, CPU staging failure,
rollback after each of three partial buffer stages and full LOD-budget pressure.
Test-only fault injection is absent from the normal executable.

## Matched production-path measurements

Sixteen ToyCars, fixed camera/transforms, 768-square offscreen eye target,
alternating eyes, preinitialized shaders and studio environment. Both opt-in
states have LOD generation enabled and the same levels available. Each workload
runs off/on/on/off with 30 warmup and 120 measured frames per stage. All draws
use production upload/material paths, including the cheaper PBR depth program.
This harness creates no overlays or input capture; it is not compositor FPS.

Color-only workloads have zero external-depth work. External-depth workloads
occlude a legacy target behind the cars and have zero PBR color draws. Every
frame checks 48 actual PBR draws and matched triangles: 1,742,976 at full detail,
1,088,352 at either reduced choice (ToyCar has only one useful reduced level).
GL-error and GPU-disjoint checks passed. Values average the two stage means.

| Pass | LOD | GPU off → on (ms) | CPU enqueue (ms) | Synchronized wall (ms) |
| --- | ---: | --- | --- | --- |
| Color | 0 | 10.938 → 3.160 | 0.316 → 0.266 | 12.698 → 5.086 |
| Color | 1 | 6.739 → 4.981 | 0.308 → 0.287 | 8.387 → 7.245 |
| Color | 2 | 6.554 → 4.867 | 0.268 → 0.276 | 8.348 → 7.099 |
| External depth | 0 | 6.541 → 1.968 | 0.095 → 0.101 | 8.082 → 3.602 |
| External depth | 1 | 4.175 → 3.245 | 0.101 → 0.094 | 5.679 → 4.809 |
| External depth | 2 | 4.125 → 3.186 | 0.101 → 0.092 | 5.657 → 4.909 |

Full geometry storage falls from 18,301,248 to 5,550,662 bytes. With the same
8,470,056 bytes of reduced LOD pairs retained, total shared geometry falls from
26,771,304 to 14,020,718 bytes. Textures, environments and retained CPU arrays
are unchanged and excluded. Raw stages: [production samples](pbr-index/production.json).
The earlier [two-run full-detail prototype](pbr-index-cache.md) independently
established cache-ordering gains; plain indexing remains slower.

## Limits and follow-up

This integration improves every measured workload relative to its matched old
path, but the reduced expanded levels are now slower than cache-indexed full
detail for this mesh. Reduced levels preserve the previous rendering policy;
indexing their streams is a separate optimization to investigate before changing
defaults. Parts that stay full detail can already benefit when a reduced level is
selected. Fewer triangles alone do not guarantee less GPU time.

Clocks, thermals and scheduling were not locked. These short offscreen samples
are not sustained frame-rate guarantees. Neither geometry quality nor texture
resolution was reduced by enabling indexing. Interactive acceptance and the
remaining native regression results are recorded separately below.

## Additional native verification

The normal engine built successfully. The existing PBR regression passed 130
stereo/material/pose images plus API/persistence checks. The LOD regression
passed 40 exact routing comparisons and 180 quality views, including the 32 MiB
budget, partial upload rollback, fallback and shared-lifetime checks. The docs
linter, VitePress build and whitespace checks passed.

## Headset visual acceptance

The user accepted `tests/scenes/pbr-index.js` in the normal `sf-overlay-engine`,
with no diagnostic overrides: approaching/moving away, grip/trigger grabbing and
rotation, LOD transitions, normal-map reflections, cutout holes and overlaps
against the blue backdrop looked correct. This is separate from the offscreen
measurements. The live scene also reported `gl_error=0`; no matched compositor
frame-rate comparison is claimed.
