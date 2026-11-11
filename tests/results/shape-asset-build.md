# Shape asset build cost

How long `createShapeAsset` takes to build a Thing and how long its first upload
takes, for the [builder plan](../../docs/builder-plan.md).

## Method

[shape-asset-regression.cpp](../shape-asset-regression.cpp) built in the
production-optimized `build` tree (Release, not `SF_FAST_TESTS`) and run on Steam
Frame's ARM64 CPU and GPU with an offscreen EGL context. Build time covers shape
generation, transforms, material grouping, normalization and the picking BVH.
Upload time covers `uploadModel` followed by `glFinish`: indexing, automatic LOD,
depth clusters and buffer upload. Tessellation is 24 segments around curved
shapes and 12 rings across a sphere or torus tube. Three runs.

## Results

| Thing | Parts | Materials | Triangles | Build | Upload |
| --- | --- | --- | --- | --- | --- |
| One cube | 1 | 1 | 12 | 0.05 ms | — |
| One sphere | 1 | 1 | 528 | 0.26 ms | — |
| Table with mixed shapes | 9 | 4 | 1,566 | 0.68 ms | — |
| 64 colored cubes | 64 | 64 | 768 | 0.51 ms | — |
| Worst case: 256 tori | 256 | 64 | 147,456 | 156–213 ms | 85–108 ms |

Generating and transforming the 256 tori takes about 10 ms; most of the
worst-case build is the picking BVH over 147,456 triangles.

## Decision

The worst case does not fit in one frame. Building stays synchronous: baking is a
deliberate action, like `loadModel`, which already stalls for large models.
Typical Things of tens of parts build in under a millisecond. The cost is
documented with `createShapeAsset`.
