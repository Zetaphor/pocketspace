# Model loading

Measured 2026-11-07 on Steam Frame (5 cores) by starting lighting-studio once with
temporary per-phase timers, which were removed afterwards. Loading runs on the frame
loop, so every running scene stops for its duration.

| Phase | Before | After |
|---|---|---|
| ToyCar primitives (images, tangents) | 946 ms | 861 ms |
| ToyCar picking BVH | 106 ms | 78 ms |
| Lantern primitives (four large images) | 817 ms | 214 ms |
| CompareSheen primitives | 98 ms | 43 ms |
| ToyCar PBR LOD simplification (first upload) | 696 ms | 385 ms |
| ToyCar indexing (first upload) | 100 ms | 106 ms |
| Lantern textures and mipmaps (first upload) | 140 ms | 120 ms |
| Script evaluation, all models | about 2.0 s | about 1.25 s |
| First upload frame, all models | about 1.05 s | about 0.65 s |

Two changes, both producing the same output:

- `loadModelFile` decodes the images its materials reference (base color, plus the
  PBR maps for PBR models) once each, in parallel, before building primitives.
  Previously each material map decoded on demand, one at a time. stb_image keeps
  its state in thread-local storage.
- `uploadModel` prepares PBR LOD simplification for all primitives on worker
  threads, simplifying both levels of a primitive concurrently, then publishes
  them in primitive order with the existing budget and level rules.

All 47 regressions pass, including the PBR LOD and indexing regressions that compare
levels, reservations and failure rollback.

The rest is mostly single-threaded work on one primitive: ToyCar's body is one
88.8k-triangle primitive without tangents, so MikkTSpace runs over it on one core,
and its simplification is the longest LOD task. Splitting a primitive would change
tangents at the split, so further gains need cached results or loading off the
frame loop.
