# Budgeted production PBR LOD

Validated on Steam Frame on **2026-10-29**, using `ee20a68` plus the production
integration. The [prototype report](pbr-lod.md) records surface/geometry quality
and the earlier interactive car acceptance. Production uses the same simplified
streams and selection policy, with bounded additional storage and failure
rollback.

## Production behavior

- `lod: true`, already the default, now enables eligible static opaque PBR LOD.
  `{lod: false}` keeps full geometry; existing cache-conflict and snapshot rules
  apply. No new JavaScript global or snapshot version is introduced.
- One 32 MiB engine-wide payload budget admits optional reduced position/surface
  pairs. Full geometry, textures, environments, unlit LOD indices and overlay
  buffers are excluded. Object copies share the asset allocation/charge.
- The complete pair is reserved before allocation and published only after both
  GPU buffer sizes are verified. CPU allocation exceptions or unpublished GPU
  failures release staged resources and charges. Finer/full detail remains valid.
- Insufficient storage skips an optional reduction without rejecting a load or
  restore. Asset/primitive upload order determines admission. No eviction or
  automatic retry occurs for already uploaded assets. Asset destruction returns
  the charge; removing an instance does not evict its cached model.
- Saved scenes store the `lod` request, not generated buffers or admission state.
  Restore under pressure can keep new assets full-detail while preserving old
  assets and their existing allocations.
- Production compiles neither failure injection nor the diagnostic environment
  override. The excluded `pbr-lod-demo` adds transition logs; the normal engine
  runs `tests/scenes/pbr-lod.js` directly.

## Automated checks

The PBR LOD regression covers 40 exact stereo/depth/overlap comparisons against
independently routed reduced streams, 180 measured full/reduced quality views,
original complete shader-input tuples, unchanged CPU geometry/picking counts,
material/deformation/opt-out guards and buffer cleanup. Added checks cover:

- Default generation without an enabling environment variable.
- Exact 32 MiB admission and arithmetic overflow rejection.
- Exhausted/nearly exhausted reservations preserving full-detail buffers.
- Failure after allocating the position VBO but before the surface VBO, checking
  deletion immediately before GL names can be reused, and charge rollback.
- CPU staging allocation failure after reserving a pair, with charge rollback.
- Five independently loaded cars whose actual charges remain bounded, with
  selective fallback and complete release across moved asset storage.
- Failed script reload preserving active context, GPU buffers and charges.
- Restore under exhausted budget preserving saved policy and the active asset.

The production PBR suite checks 130 material/stereo/pose images, API/persistence
behavior and default ToyCar LOD generation. Existing automatic LOD checks cover
26 views and policy/hysteresis/restoration; culling covers 70 views. Full scene
persistence, published examples and 264 indexed-geometry experiment comparisons
also passed. The indexing experiment explicitly disables production LOD so its
comparison stays isolated. Documentation lint, site build and whitespace checks
are run for this integration.

## Matched native rendering cost

The headset was awake and worn. The bounded implementation was measured with the
same one/sixteen ToyCar workload and procedural studio lighting as the prototype:
fixed source models, transforms and camera, one 768×768 offscreen target,
alternating eyes, full/reduced/reduced/full order, 30 warmup and 120 measured
frames per stage. All streams are already resident in all stages; generation is
excluded. Source geometry, texture resolution, materials and lighting match;
selected geometry intentionally differs because LOD is lossy.

No OpenVR session, overlays or pointer capture are created. GPU-disjoint and
GL-error checks passed. The table averages the two stage means for each setting;
[raw means](pbr-lod/production-samples.json) are retained.

| Objects | Requested level | GPU ms full → reduced | CPU enqueue ms | Synchronized frame ms |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | .7456 → .4628 | .0185 → .0174 | 1.1782 → .9993 |
| 1 | 2 | .7309 → .4635 | .0214 → .0198 | 1.1576 → 1.1616 |
| 16 | 1 | 9.9438 → 6.3742 | .3023 → .2838 | 11.6820 → 7.9827 |
| 16 | 2 | 10.1360 → 6.0882 | .3193 → .2940 | 11.8707 → 7.8241 |

Requested level 2 resolves to the same 68,022-triangle stream as level 1; full
ToyCar geometry has 108,936 triangles. Its extra 8,470,056 bytes fit the budget,
so the reduction matches the visually accepted prototype. Sixteen-car GPU time
improves about 36–40% in these short comparisons. The lighter synchronized result
is noisy, including one slightly slower reduced sequence. GPU clocks, scheduling
and thermal state are not locked; these are fixed-detail offscreen costs, not
compositor FPS, automatic-policy measurements or a sustained-performance claim.

## Normal-engine headset check

The final interactive check in the normal production executable is pending.
