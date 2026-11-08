# GL call reduction

Measured 2026-11-07 on Steam Frame with the headset active and all eight demos,
using `tools/benchmark-demos.py --captures all --repeat 3`. The wearer used the
headset normally.

Each PBR color draw made about 50 GL calls. Most set uniforms to the values they
already held, such as sampler units, the asset's lighting and environment, and
material flags. After each draw, `release()` also unbound about ten texture units,
even ones the draw had not bound.

`PbrProgram` now stores the last value it set for every uniform alongside its
location, and skips a set whose value matches bit for bit. Uniform values persist
per program and only `PbrProgram` sets them, so skipped calls change nothing.
`release()` unbinds only the units the program bound to a texture. The PBR
regressions, which compare exact pixels across sequences of different materials,
lights, environments, sheen and shadows, pass unchanged.

| | Reference | Uniform cache |
|---|---|---|
| CPU submit time per frame (`[profile] render_submit_ms`) | 2.61–3.00 ms | **1.52–1.79 ms** |
| Engine CPU (one core = 100%) | 59.0% (58.0–60.2) | 57.6% (57.6–59.7) |
| FPS | 49.5 (48.7–50.7) | 55.2 (50.8–56.3) |
| Engine GPU busy | 49.4% (48.9–49.6) | 46.3% (45.9–48.0) |
| Compositor GPU busy | 42.0% (40.5–42.2) | 32.8% (32.7–33.4) |

- Submission time per frame fell by about 40%, and engine CPU per rendered frame
  by about 12%.
- FPS was higher, but compositor GPU busy was nine points lower than in the
  reference runs, which suggests the view differed. The runs were not alternated
  with a rebuilt reference, so the FPS change is not attributed to this change.

## Skinning and unlit draws

GPU-skinned draws looked up three uniform locations by name per draw, in the color,
external depth and shadow passes. Callers now pass `SkinUniforms` looked up once per
program. `gpu-skinning-regression` and the shadow regressions pass unchanged.

The unlit model program was not given a value cache. Consecutive draws are mostly
different objects, so their transform uniforms rarely repeat; only material values
would be skipped, a saving below what these runs can resolve.

## Shadow pass state save

The shadow pass saves about 25 values with `glGet*` before drawing the map and
restores them afterwards. Timed in lighting-studio over 300-frame windows on Mesa
26.3 (Zink), the save took 0.011–0.016 ms and the restore 0.001–0.013 ms per
frame, so these queries do not wait for the driver thread and were left as they
are.
