# Rendering comparison experiment

This experiment leaves the published examples and engine defaults unchanged.
Run from the repository root on the VR runtime host:

```sh
./run-overlay-engine.sh tests/scenes/overlay-comparison.js
```

The HUD names the current renderer, geometry, body count, FPS, CPU, process RAM,
and estimated GPU allocations. Grip or trigger grabs individual bodies.

| Control | Action |
| --- | --- |
| Bumper | Cycle 16, 48, 96, 160, and 240 bodies. |
| A/X | Switch colored boxes and alternating Khronos ToyCar/Avocado models. |
| B/Y | Switch single and grouped rendering. |

Every switch rebuilds the same deterministic layout at the selected count.
Bodies use the same position and 0.14-meter scale in each renderer; boxes use box colliders
and detailed models use cached convex hulls. Fallen or distant bodies are removed
and the HUD reports the remaining count. The floor participates in the shared
projection plane in both modes.

Single mode puts all 3D geometry, including the floor, into one render group.
It renders one stereo texture and displays one visible scene overlay. The hidden
staging handle, statistics panel, and controller lasers are additional handles.
Grouped mode uses four spatial body groups plus a floor group. Both paths use
the engine's current density policy, 4096-pixel ceiling, alpha convention, and
staged presentation. Compare head motion, floor occlusion, close-up model detail,
and independent grabbing/hand rotation before increasing counts. Neither path bypasses the
resolution ceiling; inspect runtime warnings when objects spread apart. The
default compact layout uses 0.16-meter spacing, a 1.5-meter floor, and a
0.8-meter forward offset. `--layout wide` in automated runs uses 0.2-meter
spacing, a 1.8-meter floor, and no forward offset. Compare renderers within a
layout; changing layouts changes visibility and sampling demand.

## Bounded automated runs

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/compare-overlays.py --counts 48 96 --seconds 12 --warmup 5 \
  --output /tmp/overlay-comparison
```

Use `--engine /absolute/path/to/sf-overlay-engine` to test an isolated build.
The runner refuses to start if another engine process is running. It tests both
renderers with boxes and detailed models, retaining the same asset cache,
positions, scales, HUD, and object count. Kinematic bodies, hidden lasers, and
disabled interaction keep geometry fixed; interactive headset runs use dynamic
bodies. Keep the headset awake and your head steady during measurements.

The runner terminates each scene with SIGTERM, preserves per-case logs and
`summary.json`, discards warmup samples, verifies object counts, and reports
median FPS, CPU, RSS, GPU allocation estimates, maximum allocated eye pixels,
and groups that hit the resolution ceiling. A faster capped run is not a
quality-matched win against an uncapped run. GPU estimates are owned
render resources, not measured dedicated VRAM. Pair order alternates by geometry.
Use `--geometries models --reverse-order` to repeat just the model pairs in
the opposite order. These short runs show trends, not a hardware-wide throughput
guarantee. Repeat close results and compare visually before selecting a rendering mode.

`examples-regression` also checks the experiment's switches, 96-body scene
replacement, model colliders, and fallen-body cleanup using real bindings.

See the [recorded comparison](results/overlay-comparison.md) for measured results
and resolution-limit caveats.

## Shared scene-depth prototype

`shared-depth-prototype` is an offscreen experiment, excluded from normal builds.
It reuses the actual GLES shader/pass harness to compare per-group occluder
rendering with one opaque/masked scene prepass and read-only depth for group
colors. It opens no OpenVR session and leaves no headset demo running.

```sh
cmake --build build --target shared-depth-prototype render-occlusion-regression -j2
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  ./build/shared-depth-prototype --bench
```

Omit `--bench` for pixel comparisons only. Checks cover stereo, lateral views,
opaque/masked/blended/hidden geometry, checker-mask holes, ToyCar and Avocado,
coincident surfaces, and mismatched projection grids. Expected rejection cases
must exhibit pixel differences; these are experimental findings, not crashes.
The benchmark alternates order across three rounds, uses 48 bodies plus a floor,
five groups, two eyes, a fixed common 768-pixel grid, bounded warmup/measurement,
and `glFinish` per frame. Baseline occluders use bounds culling and scissors.

This is a matching-grid feasibility test, not an engine FPS comparison. It reuses
one offscreen color target, omits compositor submission, physics, triple buffers,
and dynamic group layouts. Timing includes CPU submission and completed GPU work.
See [results and adoption requirements](results/shared-depth-prototype.md).
