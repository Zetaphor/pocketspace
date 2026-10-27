# Maintainer handoff

Start with the [README](../README.md) for build/run commands and examples, the
[JavaScript API](javascript-api.md) for exposed behavior, and
[remaining work](remaining-work.md) for future scope. Keep current contracts in
these guides; implementation history belongs in Git.

## Source map

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | QuickJS bindings, scene lifecycle, interaction, rendering, and OpenVR host loop. |
| `src/model_shaders.h` | Stereo overlay projection, perspective depth, and material shaders. |
| `src/model_asset.cpp` | GLB import, animation evaluation, skinning, and picking geometry. |
| `src/controller_input.cpp` | OpenVR controller actions and haptics. |
| `src/frame_input.cpp` | Raw vrserver input, state normalization, and event queues. |
| `src/physics_world.cpp` | Scene-owned Jolt bodies, colliders, joints, motors, springs, breaking, runtime settings, stepping, contacts, and grab release. |
| `src/runtime_stats.h` | Process CPU/RAM, optional DRM memory totals, and elapsed-time FPS sampling. |
| `src/panel.cpp` | Retained panel rasterization, fonts, and images. |
| `cfg/` | Runtime OpenVR action manifest and bindings. |
| `scripts/` | Concise examples and the personal calibration generator. |
| `assets/samples/ATTRIBUTION.md` | Bundled model provenance and licenses. |

CMake defaults to Release. Keep runtime host paths configurable through its
cache variables. The launcher resolves relative paths from the checkout root.
Personal calibration files are runtime data and must remain outside Git.

After changing an API, update its reference guide and the smallest relevant
example. Run `python3 tools/lint-docs.py` to check local documentation links,
example inventory, and global binding coverage. Preserve upstream attribution
and license files when changing bundled assets.

## Physics and performance entry points

The [physics API coverage table](physics-api.md#api-coverage) maps the implemented
native capabilities to JavaScript. Joint settings can be changed in place;
type, anchors, and axis remain creation-only. The
[statistics reference](javascript-api.md#engine-statistics) defines measured CPU,
RAM, and FPS, optional GPU driver totals, and the separate GPU allocation estimate.
The [stress scene](../scripts/stress.js) includes controller spawning, a HUD,
and automatic removal of fallen/distant objects. Its shared model assets remain
cached until scene replacement, while instance resources are released on removal.

Object surfaces retain three compositor-facing color buffers and one owned depth
attachment shared by their FBOs. Only color is imported by the compositor; keep
`glFinish` and color ownership rules intact. GPU allocation estimates deduplicate
the shared depth IDs. Depth occluders are scissored to padded, conservative target
bounds per eye, with full-viewport fallback for near-plane/invalid bounds.

Physics uses single-threaded stepping for small scenes and a bounded worker pool
for larger active scenes; scripts can override with `setPhysicsWorkers`.
Contact aggregation must remain synchronized when workers run, with all script
callbacks after the solver barrier. Keep the joint adjacency cache consistent on
creation/removal and retain the final pose when a body goes to sleep. Physics
timings in `getEngineStats` describe the latest advance, not the process sample.

Object surfaces distinguish reference resolution from allocated resolution.
Stereo projected bounds crop transparent margins; physical width scales with
allocated pixels to preserve reference sample density and the centered grid.
Keep immediate growth, near-plane fallback, delayed/budgeted shrinking, stable
overlay handles, and physical input-mask sizing when changing this path. Each
buffer replacement clears the compositor texture before releasing its resources.
