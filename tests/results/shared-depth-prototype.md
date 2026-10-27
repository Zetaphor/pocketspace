# Shared scene-depth feasibility

The experiment is isolated in `tests/shared-depth-prototype.cpp`; no engine or
JavaScript rendering behavior is changed. Its baseline uses the current shaders
and group depth/color pass sequence. The candidate renders all opaque/masked
geometry to depth once per eye, then renders each group's opaque/masked color
using read-only `GL_LEQUAL` and blended color using `GL_LESS`.

## Steam Frame results

Measured 2026-10-26 against renderer baseline `cd7c019`; the experiment changes
only test code. Three rounds alternate pair order, with five warmup frames and
30 timed frames per mode, `glFinish` after each frame. These bounded offscreen
measurements do not establish headset FPS or steady-state thermal performance.

| Scene | Existing depth passes, median ms | Shared prepass, median ms |
| --- | ---: | ---: |
| 48 boxes + floor | 1.896 | 1.947 |
| 24 ToyCars + 24 Avocados + floor | 13.887 | 19.416 |

The shared prepass is approximately even for boxes and 40% slower for detailed
models in this fixed compact arrangement. The current bounds culling avoids
substantial nonoverlapping geometry; the global prepass renders it regardless.
An initial unculled baseline made the candidate appear faster, so those timings
were discarded as an unsuitable comparison.

All 18 ordinary surface comparisons matched exactly, covering both eyes, lateral
views, checker-mask holes, alpha modes, hidden geometry, and both detailed assets.
A 1% mismatch in projection scale changed 761 surface pixels. Coincident geometry
changed 6,740 surface pixels. These deliberately constructed rejection cases
confirm that matching ordinary scenes alone is insufficient for adoption.

[Raw native output](shared-depth-frame.txt) preserves every retained measurement.
The harness uses a fixed 768-pixel grid and a reused offscreen color target; it
omits compositor calls, triple buffers, physics, per-tile resampling, atlas copies,
and dynamic group allocation. It includes conservative external bounds culling
and scissoring for the existing depth passes. Both variants upload the same
shader uniforms and use the same indexed meshes. No allocation saving or live
engine speedup is claimed.

**Decision:** retain the current renderer. Revisit shared depth only for a
measured scene with substantial repeated occluder work after existing culling,
and with the correctness requirements below resolved.

## Adoption requirements

Do not enable this candidate in the engine. It changes ownership of equal-depth
fragments: current external occluders win strict-depth ties, while `GL_LEQUAL`
admits coincident members from different groups. Keeping `GL_LESS` for opaque
color instead would reject the group's own prepass depth.

Depth texels also belong to a specific eye, projection origin, and pixel grid.
The common grouped projection plane alone does not make the current independently
cropped, differently sized tiles compatible. A scaled copy or nearest-depth
lookup can change silhouettes and mask holes. Independent object surfaces use
different facing planes and are outside even this matching-grid prototype.

A production design needs explicit depth ownership/tie handling and compatible
sampling grids, with conservative fallback when those guarantees are unavailable.
Measure atlas allocation and transfer bandwidth as well as scene rendering time;
a full stereo depth24 atlas commonly occupies 128 MiB at 4096 pixels per eye,
before any additional color resources. Existing surface depth attachments cannot
be removed until attachment compatibility and lifecycle are established.

Compositor ownership, `glFinish`, resize generations, and staged grouped overlay
presentation remain unchanged. Preserve the common projection plane so late head
motion continues to align geometry color with occlusion cutouts.
