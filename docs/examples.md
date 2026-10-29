# Example scripts

Start with [build and run](getting-started.md) if you have not set up the engine.
Each script is a complete scene you can run, read and adapt. Try grabbing for
interaction, panels for UI, physics for simulation, or PBR for lit models.

Run any example with `./run-pocketspace.sh scripts/<name>.js`.

| Script | Demonstrates |
| --- | --- |
| [pbr.js](../scripts/pbr.js) | Opt-in PBR lighting, environment reflections, and hand rotation |
| [avocado.js](../scripts/avocado.js) | Load and place one model |
| [sample-gallery.js](../scripts/sample-gallery.js) | Multiple assets and animated object transforms |
| [full-sphere-gallery.js](../scripts/full-sphere-gallery.js) | Placement in front, beside, and behind the viewer |
| [grabbing.js](../scripts/grabbing.js) | Both-hand lasers, grip/trigger selection, hand rotation, and distant yaw/depth controls |
| [input.js](../scripts/input.js) | Controller pose/raw input and haptics; right stick moves the box, right bumper toggles it |
| [panels.js](../scripts/panels.js) | Retained canvas drawing and a clickable button |
| [blending.js](../scripts/blending.js) | Smooth rigid, skeletal and morph transitions; bumper changes clips/phases, A/X pauses or resumes |
| [morph.js](../scripts/morph.js) | Morphing cube; A/X pauses or resumes, bumper restarts |
| [animation.js](../scripts/animation.js) | Clip selection, pause/resume, stop/default pose and right-stick scrubbing |
| [skinning.js](../scripts/skinning.js) | Two grabbable skeletal characters with independent playback |
| [containers.js](../scripts/containers.js) | Bumper opens/closes a tool with grabbable objects, a clickable panel and an owned callback |
| [scene-save.js](../scripts/scene-save.js) | Save model arrangements with bumper and restore with A/X |
| [physics-save.js](../scripts/physics-save.js) | Save and restore bodies, hinge motors, springs, and motion |
| [animation-save.js](../scripts/animation-save.js) | Preserve independent animated poses and playback state |
| [panel-save.js](../scripts/panel-save.js) | Preserve panel pixels and layout, then edit and click the restored UI |
| [lod.js](../scripts/lod.js) | Sixteen detailed grabbable cars with automatic LOD and full detail while held |
| [instances.js](../scripts/instances.js) | Sixteen grabbable Avocados sharing a surface with ordered GPU instancing |
| [grouped-stress.js](../scripts/grouped-stress.js) | Adaptive stereo groups with splitting, merging, and rebalancing; up to 240 grabbable physics boxes |
| [stress.js](../scripts/stress.js) | Add physics boxes with controllers; FPS, CPU, RAM, GPU memory, and physics timing HUD |
| [joint-controls.js](../scripts/joint-controls.js) | Live spring length/response and hinge-limit changes with controller buttons |
| [breakable-joints.js](../scripts/breakable-joints.js) | Pull suspended objects free of breakable connections; bumper reconnects |
| [springs.js](../scripts/springs.js) | Elastic hanging weights with different damping; grip or trigger grabs, bumper resets |
| [motors.js](../scripts/motors.js) | Motor-driven blue and green panels; bumper changes angle, A/X toggles power |
| [joints.js](../scripts/joints.js) | Hinged panel, tethered ball, and welded objects; grip or trigger grabs, bumper resets |
| [mesh-colliders.js](../scripts/mesh-colliders.js) | Static mesh platform with an opening and ramp; grabbable ball and cube |
| [convex.js](../scripts/convex.js) | Model-derived convex colliders, grabbing, and throwing; bumper resets unheld objects |
| [colliders.js](../scripts/colliders.js) | Explicit box and sphere dimensions, a thin floor, and rotated walls |
| [physics.js](../scripts/physics.js) | Gravity, grabbable boxes, throwing, and a collision event display; bumper resets unheld boxes |
| [tracking.js](../scripts/tracking.js) | Corrected gaze, per-eye foveation centers and raw finger sensor values |
| [eye-calibration.js](../scripts/eye-calibration.js) | Personal calibration generator; requires an output path |
| [scene-snapshot.js](../scripts/scene-snapshot.js) | In-memory capture/restore with fresh IDs; bumper captures, A/X restores |
| [container-transfer.js](../scripts/container-transfer.js) | Transfer object/panel ownership; B/Y transfers, A/X disposes left, bumper disposes right |
| [physics-controls.js](../scripts/physics-controls.js) | Impulses, gravity, body removal/reattachment and a moving kinematic platform |
| [panel-placement.js](../scripts/panel-placement.js) | World/head/arbitrary placement, rotation, visibility and panel removal/recreation |
| [surface-pointer.js](../scripts/surface-pointer.js) | Native surface pointer, optional action input and haptics; requires SteamVR pointer focus |

## Functionality coverage

Examples demonstrate supported workflows rather than every spelling of the API.
Each script starts with its purpose and controls; inline comments explain units,
ownership, input-source differences and other behavior that is easy to misread.

- Models/transforms and full-sphere placement: `avocado`, `sample-gallery`, `full-sphere-gallery`.
- Grabbing, grip/trigger policies and native/custom input: `grabbing`, `input`, `surface-pointer`.
- Retained canvas, pointer clicks, placement and panel lifetime: `panels`, `panel-placement`, `panel-save`.
- Clip playback, seeking/stopping, blending, skeletal/morph instances: `animation`, `blending`, `skinning`, `morph`, `animation-save`.
- Box/sphere/convex/static-mesh colliders, contacts and throwing: `physics`, `colliders`, `convex`, `mesh-colliders`.
- Joint types, motors, springs, breaking and live settings/state: `joints`, `motors`, `springs`, `breakable-joints`, `joint-controls`.
- Gravity, impulses, kinematic targets and body lifecycle: `physics-controls`.
- Container ownership, callback lifetime, transfers, memory/file snapshots: `containers`, `container-transfer`, `scene-snapshot`, and the `*-save` examples.
- PBR/environment lighting, automatic LOD, instancing and adaptive grouping: `pbr`, `lod`, `instances`, `grouped-stress`.
- Engine statistics, worker policy and bounded object cleanup: `stress`, `grouped-stress`.
- Raw/corrected gaze, foveation, finger sensing and calibration generation: `tracking`, `eye-calibration`.

[Panel compatibility globals](panels.md#pointer-events-and-compatibility) and the
no-op `enableObjectSurface` are documented for existing scripts; examples use the
canvas API and current interaction helpers instead. [Custom fonts](panels.md)
are guide-only because font paths depend on the runtime host. Optional action
input/haptics, native pointers and eye data depend on SteamVR focus, bindings and
tracking availability and may be unavailable on a given runtime.
Calibration requires the [documented output-path setup](eye-calibration.md).

The two stress scenes are interactive examples with scene-specific caps, not
hardware performance guarantees. Automated harnesses and experiments live in
[tests](../tests/index.md).
