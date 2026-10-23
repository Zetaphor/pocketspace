# Steam Frame overlay engine

A Linux/OpenVR engine for scripted stereo overlays alongside a VR application.
QuickJS scripts place GLB models, play node and skeletal animations, grab objects
with either controller, simulate rigid-body physics and connected mechanisms,
draw interactive panels, and read controller and eye data.
Each object has its own stereo surface with three DMA-BUF render buffers.

## Build and run

Requires CMake 3.20 or newer, a C++20 compiler, OpenVR headers and runtime, QuickJS-ng's static
library, FreeType, EGL, GLESv2, GBM, and libdrm development files. The default
library paths target Steam Frame's ARM64 Linux environment. Override `SDK_INCLUDE`,
`QJS_INCLUDE`, `QJS_LIB`, and `STEAMVR_BIN` through CMake for another installation.

```sh
cmake -S . -B build
cmake --build build -j2
./run-overlay-engine.sh
./run-overlay-engine.sh scripts/grabbing.js
```

The first CMake configuration downloads a pinned Jolt Physics source archive;
see [physics builds](docs/physics-api.md#building) for offline configuration.

Start SteamVR before running an overlay. The wrapper sets the runtime library
path and X11/EGL environment, then runs from the repository root. Its default
scene is `scripts/avocado.js`. Stop with Ctrl-C or SIGTERM. Script edits reload
automatically; a failed reload preserves the existing scene.

Inspect a model without opening VR:

```sh
./run-overlay-engine.sh --inspect assets/samples/Box/Box.glb
```

## Examples

Run any example with `./run-overlay-engine.sh scripts/<name>.js`.

| Script | Demonstrates |
| --- | --- |
| [avocado.js](scripts/avocado.js) | Load and place one model |
| [sample-gallery.js](scripts/sample-gallery.js) | Multiple assets and animated object transforms |
| [full-sphere-gallery.js](scripts/full-sphere-gallery.js) | Placement in front, beside, and behind the viewer |
| [grabbing.js](scripts/grabbing.js) | Both-hand lasers, grip/trigger selection, hand rotation, and distant yaw/depth controls |
| [input.js](scripts/input.js) | Controller snapshots and button events; right stick moves the box, right bumper toggles it |
| [panels.js](scripts/panels.js) | Retained canvas drawing and a clickable button |
| [animation.js](scripts/animation.js) | Clip playback; bumper cycles clips, A/X pauses or resumes |
| [skinning.js](scripts/skinning.js) | Two grabbable skeletal characters with independent playback |
| [stress.js](scripts/stress.js) | Add physics boxes with controllers; FPS, CPU, RAM, and measured/estimated GPU memory HUD |
| [joint-controls.js](scripts/joint-controls.js) | Live spring length/response and hinge-limit changes with controller buttons |
| [breakable-joints.js](scripts/breakable-joints.js) | Pull suspended objects free of breakable connections; bumper reconnects |
| [springs.js](scripts/springs.js) | Elastic hanging weights with different damping; grip or trigger grabs, bumper resets |
| [motors.js](scripts/motors.js) | Motor-driven blue and green panels; bumper changes angle, A/X toggles power |
| [joints.js](scripts/joints.js) | Hinged panel, tethered ball, and welded objects; grip or trigger grabs, bumper resets |
| [mesh-colliders.js](scripts/mesh-colliders.js) | Static mesh platform with an opening and ramp; grabbable ball and cube |
| [convex.js](scripts/convex.js) | Model-derived convex colliders, grabbing, and throwing; bumper resets unheld objects |
| [colliders.js](scripts/colliders.js) | Explicit box and sphere dimensions, a thin floor, and rotated walls |
| [physics.js](scripts/physics.js) | Gravity, grabbable boxes, throwing, and a collision event display; bumper resets unheld boxes |
| [tracking.js](scripts/tracking.js) | Corrected gaze display and raw finger sensor values |
| [eye-calibration.js](scripts/eye-calibration.js) | Personal calibration generator; requires an output path as described below |

## Documentation

- [JavaScript API](docs/javascript-api.md): runtime, engine statistics, models, interaction, controllers, and events.
- [Panels](docs/panels.md): canvas commands, transforms, and pointer events.
- [Physics](docs/physics-api.md): bodies, colliders, joints, motors, springs, breaking, and runtime controls.
- [Animation](docs/animation-api.md): clip controls and skeletal model support.
- [Tracking](docs/tracking-api.md): gaze, foveation centers, and finger sensors.
- [Eye calibration](docs/eye-calibration.md): generate, load, and interpret correction files.
- [Lifecycle](docs/lifecycle.md): reload, removal, disconnect, and shutdown behavior.
- [Input architecture](docs/input-architecture.md): native data sources and delivery.
- [Remaining work](docs/remaining-work.md): unimplemented and deferred features.
- [Maintainer handoff](docs/agent-handoff.md): source map and maintenance entry points.

The model renderer supports embedded base-color textures, material factors,
alpha modes, UV transforms, and an approximate sheen tint. It does not implement
full PBR lighting or morph animation. Finger sensing exposes controller
capacitance/contact rather than a hand skeleton. See the linked guides for
supported data and limits.

Sample sources and licenses are listed in [asset attribution](assets/samples/ATTRIBUTION.md).
The glTF loader uses [cgltf](https://github.com/jkuhlmann/cgltf), vendored at
commit `85cd62382dfea638278962690cf515023f33ed00`; see
[its license](third_party/cgltf-LICENSE).
