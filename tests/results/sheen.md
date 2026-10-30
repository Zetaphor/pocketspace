# Fabric sheen validation

Steam Frame, October 30, 2026. This validates the imported
`KHR_materials_sheen` layer; it is a material feature rather than an optimization
of existing appearance.

## Automated correctness

The native `sheen-regression`, `examples-regression`, `pbr-regression` and
`environment-regression` checks passed. Coverage includes:

- Independent finer-grid directional-energy integration, reciprocity and finite
  grazing-angle behavior. A roughness-only normalization bounds the fitted
  Charlie visibility model's excess grazing energy.
- Imported factors versus sRGB color/linear alpha textures, unsupported UV
  rejection, stereo views, unlit invariance and environment illumination.
- General/prepared shader equivalence, visible Khronos Compare Sheen geometry,
  texture-unit cleanup, shared lookup deletion and real API asset unloading.
- All 42 published examples, existing 130-image PBR checks, API rollback,
  persistence and environment-resource cleanup.

The generator in `tests/fixtures/pbr/generate.py` reproduces the three bounded
original sheen fixtures. The bundled Khronos model retains its license,
attribution, pinned revision and file hashes.

## Bounded native profile

`sheen-regression --benchmark` uses offscreen GLES, no OpenVR overlays or pointer
capture. The user confirmed headset availability for profiling/testing. One or
sixteen copies of Compare Sheen use a fixed camera, alternating eyes, 768 pixels
per eye, direct model lighting, no environment and no scene lights. Each stage
warms 12 frames and measures 36. The order is disabled/enabled/enabled/disabled.
Both states retain the same geometry, map/lookup storage and general shader;
disabling zeroes sheen color factors. GPU-disjoint and GL-error checks passed.

Pair-averaged measurements in milliseconds:

| Objects | Sheen | GPU | CPU enqueue | Completed frame |
| --- | --- | --- | --- | --- |
| 1 | Disabled | 0.0304 | 0.0137 | 0.4336 |
| 1 | Enabled | 0.0318 | 0.0196 | 0.4360 |
| 16 | Disabled | 0.2707 | 0.1547 | 1.7135 |
| 16 | Enabled | 0.3744 | 0.1664 | 1.7302 |

Individual sixteen-object GPU stages ranged from 0.204 to 0.418 ms. These small,
variable samples establish a bounded incremental feature cost for this fixture;
they are not compositor FPS, sustained thermal results, a general performance
guarantee or evidence that sheen improves performance. Color appearance differs
intentionally between states. The shared RG16F lookup has 4 KiB GPU payload;
material maps are additional and are counted by engine statistics.

## Visual validation

The user verified `scripts/sheen.js`: grip/trigger grabbing and rotation at
grazing angles, head movement, bumper direct/environment switching and A/X
lighting toggle all looked correct. The demo was stopped after acceptance, and
a process check confirmed that it exited.

Indirect sheen currently uses diffuse SH lighting and directional albedo, not a
separate Charlie-filtered HDR reflection map. Fine directional HDR sheen remains
conditional future work.
