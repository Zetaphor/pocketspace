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
