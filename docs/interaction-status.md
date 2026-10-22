# Interaction status (2026-10-21)

This engine is an AR-in-VR SteamVR overlay. It coexists with a primary VR app
and other overlays; it has no full scene mode. Sigma is the committed source
checkout; Deckard is the SteamVR build and runtime host. See
`panel-integration-status.md` for the later panel and raw input validation.

## Headset verified

- Stereo GLB rendering is upright, converged, and world anchored beside
  Frametop. The avocado texture maps correctly. Box, Toy Car, and Lantern
  render as distinct models; the feature gallery displays alpha, skin, and
  morph sample assets in their default poses.
- Objects can be placed throughout the sphere around the user. Front, side,
  and rear models remained visible; one dragged behind the starting view
  remained visible and grabbable after turning around.
- Noninteractive surfaces let the SteamVR laser pass through to Frametop.
  Grabbable surfaces block it; gaps remain usable. Both hands can now grab
  independently through their colored custom lasers and raw trigger input.
- Native trigger laser grabs move objects with the controller in 6DoF. The
  thumbstick's horizontal axis rotates yaw; its vertical axis adjusts grab
  distance, with up pushing farther. The faster rates in the current build
  were accepted by the user.
- With `directGrab` enabled, selecting an object near a controller holds it
  at the hand. Enabled pitch, yaw, and roll follow hand rotation. The final
  corrected hand rotation was accepted by the user.
- Optional colored beams from both controllers align with their aim and select
  grabbable models. Each beam stops at the target and holds that length during
  a grab until depth adjustment or trigger release.
- The three-object gallery was clear and smooth at the default 768 pixels
  per eye. At 512 pixels, quality was unacceptable. Higher resolution caused
  lag and ghosting with three objects. Resolution is configurable per asset.

## Current API and controls

`setObjectInteraction(id, {laserPassThrough, grabbable, grabButton, directGrab, laserYaw,
depthAdjust, handRotationAxes: {pitch, yaw, roll}})` configures each object.
Grabbable objects force `laserPassThrough` off. `setLaserVisible(hand, bool)`
controls the optional visual beam. `loadModel(path, eyePixels)` chooses the
resolution, with 768 as the default. `getController(hand)` gives a tracked
pose; `getPointer(id)` reports the native pointer. `getInput(hand)` and
`pulse(hand, ...)` expose the action mapping, subject to the limitation below.

Run `./run-overlay-engine.sh scripts/native-grab-demo.js` for the current
three-object interaction demo. The script uses laser yaw, depth adjustment,
and all three hand rotation axes. Thumbstick up pushes a distant held object
farther away; down pulls it closer.

## Known gaps and next steps

The SteamVR `IVRInput` action API is not a complete global monitor on Steam
Frame: thumbstick actions were observed without overlay hover, but trigger,
grip, and other actions remained inactive. The raw controller-state WebSocket
served by `vrserver` does expose all Frame controller components without focus.
The project now includes `scripts/steam-frame-input.py`, based on Frametop's
implementation, as an independent diagnostic route. The native monitor also
delivers raw state and events to JavaScript on both hands; see
`javascript-api.md` and `input-architecture.md`. A Deckard inventory
reported 77 live components, 37 left-hand and 40 right-hand, including all
public button, touch, scalar, thumbstick, and capacitive paths. It is
undocumented SteamVR behavior and may change across updates.


1. **Laser pitch — no longer planned:** The user removed this feature from
   active work. Existing `laserPitchWithStickClick` behavior remains experimental
   and unverified; no further implementation or validation is planned.
2. **Grab button choice — implemented:** Per-object `grabButton` selects trigger,
   grip, or either button for existing laser and direct grabs. Trigger remains
   the default. Near-hand mode still requires current mesh selection; this does
   not add proximity-only pickup. Native/synthetic checks and wearer acceptance
   are distinguished in [API verification](javascript-api.md#grab-button-verification).
3. **Action input:** The engine has an action manifest and Steam Frame binding
   map for grip, trigger, sticks, bumpers, face buttons, and haptics. Probes
   did not receive active action data while native overlay laser input was in
   use. Native overlay smooth-scroll events do provide thumbstick motion
   during laser grabs. Investigate a compatible action path only for controls
   that native overlay events cannot provide.
4. **Object selection:** Colored beams use transformed model triangle hits
   with a small bounds fallback. The avocado and box passed a two-hand headset
   grab test. Assess complex and concave models before claiming exact mesh
   selection for every asset; optimize high-triangle picking separately.
5. **Performance:** Keep the 768-pixel default and measure frame time with
   more objects, textures, and animations. Preserve smooth head movement and
   Frametop responsiveness before adding complex rendering features.
6. **Engine scope:** Continue toward scripted games and dynamic experiences
   through overlays: animation playback, materials/lighting, scene lifetime,
   physics, persistence, and richer input. Placement must remain full sphere.

The Arcturus Camera Switch installation uses a background overlay app and a
configurable controller chord; the exact global input polling method was not
established from its installed binary. This remains a research lead, not a
verified API solution.
