# Broader PBR cache/default assessment

Steam Frame, 2026-10-29. Cache optimization remains opt-in. Broader imported-model
correctness passed, and a generated counterexample establishes that changing the
default could change the appearance of order-dependent geometry.

## Imported-model correctness

The production-policy regression now compares nineteen imported models, with
684 exact stereo/color/external-depth/LOD/pose images and 174 indexed primitive
uploads. It retains original CPU/shader-input checks, animated and transparent
fallback, snapshots/cache conflicts, rejected reload preservation, exact buffer
accounting, partial upload rollback and budget admission/cleanup checks.

The inventory includes ToyCar; normal/tangent and mirrored variants;
NegativeScaleTest; AlphaBlendModeTest; TextureEncodingTest; Fox;
AnimatedMorphCube; Lantern; MetalRoughSpheres; MetalRoughSpheresNoTextures;
UnlitTest; EmissiveStrengthTest; Box; RiggedSimple; RiggedFigure; CesiumMan;
InterpolationTest; and Avocado.

Each model compares the cache policy off/on across both eyes, two poses/views,
three detail choices and three member sets. Eligible smaller fixtures use the
same reduced triangle sets/error with expanded or compact storage. The sphere
grids contain 501,776 and 1,040,409 triangles and intentionally disable LOD in
both variants. This keeps optional-budget admission from producing unequal
geometry; their three requested detail choices all retain full detail. Animated
models likewise exercise their supported expanded fallback, rather than claiming
indexed animation coverage. No imported-model pixel difference was observed.

## Coplanar counterexample

A generated 8×8 grid has two exactly overlapping layers within one material
primitive. The layers have independent red/blue texture coordinates, shared
vertices within each layer, and alternating original first-triangle ownership.
It uses production PBR uploads and full detail with the cache policy off/on.
Both opaque and masked variants have fully covered texture alpha.

Reconstruction verifies the exact same oriented triangles and complete 14-float
shader inputs after reordering. Alpha coverage stays identical in both eyes,
but cache ordering changes which layer wins equal-depth ownership:

| Material | Left-eye changed color pixels | Right-eye changed color pixels |
| --- | ---: | ---: |
| Opaque | 27,633 | 27,618 |
| Masked | 27,633 | 27,618 |

The regression requires this difference for the bounded fixture instead of
mistaking it for geometry corruption or weakening imported-model comparisons.
Raw counts: [coplanar samples](pbr-index/coplanar.json). The fixture is generated
in C++; no additional external assets or licenses are introduced.

## Matched native measurements

The correctness checks passed again before profiling. Four full-detail workloads
compare cache policy off/on at a fixed camera, 768-square offscreen eye target
and alternating eyes, with studio lighting and unchanged material inputs. Both
assets and shader programs are preinitialized and remain allocated across stages.
Each workload runs off/on/on/off, with 30 warmup and 120 measured frames per
stage. LOD is disabled in both variants. Every frame verifies actual draws and
triangles, zero work in the opposite PBR pass, GL errors and GPU disjoint.
External-depth cases occlude a legacy target behind the models. No OpenVR
session, overlays or controller input capture are created.

Work counts per frame are identical across variants:

| Model | Objects | PBR draws | Triangles |
| --- | ---: | ---: | ---: |
| Avocado | 16 | 16 | 10,912 |
| Lantern | 16 | 48 | 86,304 |
| MetalRoughSpheres | 1 | 5 | 501,776 |
| MetalRoughSpheresNoTextures | 1 | 123 | 1,040,409 |

The table averages the two measured stage means for each mode:

| Model / pass | GPU off → on (ms) | CPU enqueue (ms) | Synchronized wall (ms) |
| --- | --- | --- | --- |
| Avocado / color | 0.139 → 0.162 | 0.080 → 0.085 | 1.042 → 1.143 |
| Avocado / external depth | 0.078 → 0.106 | 0.039 → 0.041 | 0.972 → 0.967 |
| Lantern / color | 0.485 → 0.191 | 0.233 → 0.227 | 2.273 → 1.992 |
| Lantern / external depth | 0.350 → 0.200 | 0.091 → 0.077 | 1.818 → 1.741 |
| MetalRoughSpheres / color | 3.099 → 0.729 | 0.040 → 0.033 | 3.916 → 1.524 |
| MetalRoughSpheres / external depth | 1.944 → 0.528 | 0.015 → 0.013 | 2.547 → 1.114 |
| MetalRoughSpheresNoTextures / color | 6.646 → 1.653 | 0.932 → 0.855 | 9.522 → 4.629 |
| MetalRoughSpheresNoTextures / external depth | 4.168 → 1.153 | 0.407 → 0.348 | 6.872 → 3.580 |

Per shared asset, full geometry payload excludes unchanged textures, environments
and retained CPU arrays:

| Model | Expanded bytes | Cache-indexed bytes |
| --- | ---: | ---: |
| Avocado | 114,576 | 26,828 |
| Lantern | 906,192 | 264,484 |
| MetalRoughSpheres | 84,298,368 | 17,358,304 |
| MetalRoughSpheresNoTextures | 174,788,712 | 35,826,750 |

The two dense sphere cases improve color GPU time by about 75–76%, while the
small Avocado workload adds approximately 0.02–0.03 ms GPU time. The latter is a
small absolute cost in this short run and is not independently repeated; it
cannot establish a universal small-mesh penalty. It does show that these samples
do not support claiming every model becomes faster. Clocks, thermals and
scheduling were not locked; these are offscreen workload costs, not compositor
FPS or sustained application guarantees. Upload time and loading peak memory
were not measured. Raw stages: [material samples](pbr-index/materials.json).

## Default decision

Keep `optimizeVertexCache` defaulting to false. Imported-model success and measured
speedups cannot guarantee unchanged appearance for all user assets when the
coplanar case provably depends on order. Scripts can explicitly enable it for
assets validated with their intended materials and view conditions. Transparent
and deforming uploads retain their established fallback. This investigation
changes tests/documentation only, not production policy or API defaults.

Automated images and offscreen timings are separate from user visual acceptance;
no interactive headset scene is launched for this assessment.

Documentation lint, the VitePress build and whitespace checks passed before
commit. The completed profiling process exited normally; no demo remains from
this assessment.
