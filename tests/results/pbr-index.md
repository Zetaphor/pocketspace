# Static PBR indexing investigation

Measured on Steam Frame on **2026-10-29** with the headset awake and worn.
The implementation is test-only; production retains expanded PBR geometry.

## Implementation and correctness

`pbr-index-regression` and `pbr-index-demo` compile `SF_PBR_INDEX_EXPERIMENT`.
`SF_PBR_INDEXED=0` retains expanded uploads; `1` enables full-input indexing;
`2` can also apply meshoptimizer vertex-cache/fetch ordering to static non-blend
primitives when the existing cache policy permits it. The recorded run left
`optimizeVertexCache` at its C++ default of `false`: mode 2 therefore repeated
plain indexing and did not measure enabled cache/fetch ordering. The later
[enabled-cache comparison](pbr-index-cache.md) measures that variant explicitly. Animated, skinned and morph
assets keep expanded streams. Indexing is skipped when total geometry storage
would not shrink. Neither variant generates LOD or enables instancing.

Vertex identity includes all 14 floats: position, base UV, normal, tangent with
handedness, and material-map UV. Identity is bit-exact within each primitive, so
surface seams remain independent. CPU triangles, picking, bounds, material
ownership and shared-asset lifetime stay unchanged. GPU position and surface
buffers use the same remap and share the element buffer in both color and depth
passes. Blend triangle order is retained.

Native checks compare 264 exact stereo/alpha/overlap images across eleven models,
including normal/tangent, mirrored, unlit, emissive and texture-encoding fixtures,
plus animated Fox and AnimatedMorphCube fallbacks. They reconstruct uploaded
shader inputs and verify exact oriented-triangle identity, check index-width
boundaries and buffer cleanup. All passed, along with the production PBR
regression (130 images plus API/persistence checks) and culling regression
(70 views). This is automated evidence, not user headset
acceptance of the prototype.

## Matched native measurements

One and sixteen full-detail ToyCars use the same PBR shader, procedural studio
environment, transforms, camera, 768×768 offscreen target and alternating eyes.
All variants are loaded before timing. Each comparison runs expanded/variant/
variant/expanded, warming 30 and measuring 120 frames per stage. No OpenVR session,
overlays or pointer capture are created. GPU-disjoint and GL-error checks passed.
These are offscreen rendering costs, not compositor FPS.

The table averages the two stage means for each mode. Raw stage data is in
[samples.json](pbr-index/samples.json).

| Objects | Variant | GPU ms expanded → indexed | CPU enqueue ms | Synchronized frame ms |
| ---: | --- | ---: | ---: | ---: |
| 1 | Indexing | .7235 → 1.0038 | .0169 → .0223 | 1.1859 → 1.5635 |
| 1 | Repeated indexing (cache policy off) | .7101 → 1.0237 | .0173 → .0198 | 1.1953 → 1.5665 |
| 16 | Indexing | 9.8911 → 14.1354 | .3170 → .3379 | 11.6499 → 15.7573 |
| 16 | Repeated indexing (cache policy off) | 10.0844 → 14.2719 | .3166 → .3075 | 11.5184 → 15.7985 |

Per shared ToyCar asset, position/surface/index GPU storage falls from
18,301,248 to 5,550,662 bytes (about 70% less). Textures, environment data and
retained CPU arrays are excluded and unchanged. This is retained buffer storage;
loading peak memory and upload latency were not measured.

Both plain-indexing sequences regress GPU and synchronized frame time, so plain
indexing is not adopted. The original report incorrectly described mode 2 as
cache-enabled. The [follow-up comparison](pbr-index-cache.md) explicitly enables
and verifies reordering and establishes substantially different results. GPU
clocks, thermal state and scheduling are not locked; neither investigation
identifies the hardware cause or establishes a universal indexed-draw cost.
