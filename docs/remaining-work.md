# Steam Frame overlay engine: remaining work

Status: October 22, 2026. Source of record: Sigma's `/home/zetaphor/Code/sf-overlay-mvp`; Deckard is the SteamVR build and headset test host. The verified custom-laser implementation was committed in `5c3c85b`; check current `HEAD` before starting work.

## Verified baseline

- The avocado and box headset probe supports independent custom-laser grabs from both hands. The beams stop at the selected model, keep their grab length until push/pull or release, and quick hand motion does not release a model while its trigger is held.
- The two-object probe ran at about 90 FPS with `gl_error=0`. The panel test also verified readable rendering, panel pointer events, raw controller input, and clean script reloads.
- These results cover the tested scenes and headset session. They do not establish behavior for every model or larger scene.

## Next: selection and controls

Performance follow-up: [measurements and reproduction](performance.md). Release
builds and model-space ray picking brought the single Toy Car scene to 90 FPS;
the fixed two-hand CPU benchmark improved from 3.6288 to 2.4372 ms. Multi-model
scaling remains open. Per user direction, the Frametop report is out of scope.
The subsequent shared triangle BVH reduced Toy Car's fixed two-hand picking
benchmark from about 2.9 ms to 0.0023–0.0052 ms, preserving all hit results.
Four Toy Car instances at 768 pixels per eye held 89.9–90 FPS; rendering/GPU
waits now dominate that scene. See the performance document for memory/startup
costs and validation scope. Next performance work should measure rendering cost.

Selection validation now has a dedicated seven-case scene and synthetic hollow
frame fixture: see [model selection test](model-selection-test.md). Headset
validation is pending; this does not complete item 1.

Wearer testing reproduced false center grabs in the hollow-frame cases (1–2).
The pending triangle-only picker fix removes the spherical hit fallback and
passes a standalone geometry check; Deckard headset retest remains pending.
The first retest found missed rear-box picks; follow-up fixes initialize triangle
edges before cross products and populate normalized model bounds. Toy Car is
temporarily excluded from the selection scene after a reported Frametop disruption;
its log showed about 35 FPS, but the disruption's cause is unconfirmed.
The final retest fixed the native rectangular-surface override and required
fresh mesh hits for acquisition. The wearer confirmed the reported selection
problem fixed; the run held 90 FPS with `gl_error=0`. See the test document for
logs and scope. Remaining selection coverage includes explicit per-hand checks
and the excluded high-triangle car; performance isolation is the next priority.

1. **Validate model selection beyond the avocado and box.** The picker tests transformed triangles and uses a small bounds fallback for near misses. Exercise concave, thin, rotated, overlapping, and high-triangle models, including Toy Car and Lantern. Check false picks through gaps, nearest-object choice, per-hand isolation, hover stability, and the beam endpoint. Tighten or replace the fallback where it selects empty space. Done when both hands can select and release the intended model in those cases without selecting a nearer gap or an object behind it.
2. **Optimize high-triangle picking.** The avocado/box probe held 90 FPS, but the earlier Toy Car probe ran near 31 FPS with a 108,936-triangle asset. Measure render time separately from picking time, then add a suitable acceleration structure or cheaper broad phase if picking is material to the slowdown. Done when the chosen multi-model test scene meets an agreed headset frame-time target without lag or ghosting.
3. **Resolve laser pitch.** `laserPitchWithStickClick` is experimental: stick-click events were observed, but the user did not see pitch motion. Record smooth-scroll input while click and trigger are held, choose a nonconflicting control, implement it, and verify pitch direction and rate in the headset. Keep it out of normal demos until verified.
4. **Decide and implement a dedicated grip pickup gesture.** Near-hand pickup currently starts with the trigger. Define how grip pickup, laser grab, and release should interact, using the available raw grip input without relying on SteamVR native laser focus. Verify two-hand pickup and that panel/native pointer interactions still work.

## Input and reliability

5. **Protect the raw-input integration against SteamVR changes.** The `vrserver` WebSocket is undocumented. Compare the native monitor with `scripts/steam-frame-input.py` after SteamVR updates; verify discovery, reconnect, resubscription, button/value normalization, and behavior during transient tracking loss. Evaluate a documented action path only if it can provide the needed controls in this overlay context.
6. **Broaden lifecycle testing.** Exercise repeated scene reloads, object and panel removal, controller disconnect/reconnect, SteamVR restart, and shutdown while one or both models are held. Verify no stale grab state, ghost overlays, resource leaks, or retained input callbacks.

## Deferred performance improvements

Deferred performance work (tracking/API work takes priority):

- Preserve shared vertices and use indexed rendering (`glDrawElements`); measure
  vertex processing, GPU time, and memory against expanded triangle streams.
- Replace repeated object/surface scans with direct lookups as object counts grow.
- Avoid redundant unchanged panel texture, transform, and visibility submissions.
- Investigate explicit GPU/shared-texture synchronization only with compositor
  ownership and buffer-reuse validation; do not simply remove `glFinish`.

## Current priority: eye and finger tracking

Investigate Steam Frame's documented OpenVR/OpenXR APIs and the local vrserver
interfaces, then expose supported signals through the JavaScript API with explicit
source, availability, validity, coordinate-space, and freshness information.
Distinguish controller capacitive/skeletal finger tracking from bare-hand optical
tracking, and combined gaze from per-eye gaze/openness. Preserve overlay coexistence
with the primary VR application. Research findings will be recorded in
[tracking research](tracking-research.md).

The initial [tracking API](tracking-api.md) now exposes combined OpenVR gaze,
per-eye foveation NDC, and global raw finger sensors. Headset alignment/finger
exercises and primary-app coexistence are next. Native skeletal activation,
bare-hand tracking, eye openness, and the experimental mmap backend remain open.
Wearer follow-up: finger sensing worked after removing a Spigen controller
cover. Raw gaze alignment was closest at the right target and worse at center/
left. Next use `scripts/gaze-accuracy-test.js` to capture a nine-point error grid
before choosing calibration or projection changes; do not treat validity as
proof of accuracy or silently alter the raw gaze API.

## Later engine milestones

7. **Scale scene performance.** Measure frame time, GPU/shared memory, overlay count, and Frametop responsiveness as objects, textures, panels, and per-eye resolution increase. The current 768-pixel default is the known usable starting point; 512 pixels looked too blurry, while three 2048-pixel surfaces lagged in headset testing.
8. **Add animation and rendering features.** Implement glTF animation playback, skinning, morph animation, and fuller materials/lighting. The current gallery loads sample assets but shows animated models in their default pose; sheen is only approximated.
9. **Define game-state features.** Specify and then implement scene lifetime rules, physics, and persistence for scripted experiences. Preserve full-sphere placement and coexistence with the primary VR app.

## Verification rule

For each headset-facing change, record the exact scene, Deckard build revision, runtime log, and what the wearer actually saw or felt. A successful build, OpenVR call, or event log alone does not establish headset behavior.

## Latest gaze findings

Latest gaze measurement: two completed grids showed stable median angular
errors of 20.14 and 18.71 degrees, predominantly downward. Native panel geometry
checks passed. A panel-specific affine fit did not validate consistently enough
to apply globally. The next gaze task is explicit head-relative calibration
with quality gating and independent validation; raw gaze remains unchanged.
See [tracking API measurements](tracking-api.md) for exact logs and residuals.
