# Interaction status (2026-10-18)

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
  Grabbable surfaces block it; gaps remain usable. Both hands can grab when
  SteamVR assigns that hand the active native laser.
- Native trigger laser grabs move objects with the controller in 6DoF. The
  thumbstick's horizontal axis rotates yaw; its vertical axis adjusts grab
  distance, with up pushing farther. The faster rates in the current build
  were accepted by the user.
- With `directGrab` enabled, selecting an object near a controller holds it
  at the hand. Enabled pitch, yaw, and roll follow hand rotation. The final
  corrected hand rotation was accepted by the user.
- Optional colored visual beams from both controllers align with their aim.
  They are visual only. The native SteamVR laser remains the input pointer.
- The three-object gallery was clear and smooth at the default 768 pixels
  per eye. At 512 pixels, quality was unacceptable. Higher resolution caused
  lag and ghosting with three objects. Resolution is configurable per asset.

## Current API and controls

`setObjectInteraction(id, {laserPassThrough, grabbable, directGrab, laserYaw,
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


1. **Laser pitch:** `laserPitchWithStickClick` is present but not verified.
   Native right-button events arrive when the stick is clicked, yet the user
   saw no pitch motion. Log smooth-scroll events while the stick is clicked
   and the trigger is held, then correct the event handling or choose another
   workable modifier. Do not enable this option in the normal demo yet.
2. **Direct pickup gesture:** Near-hand pickup currently uses the native
   trigger selection. Grip click switches or disables the SteamVR laser and
   releases the selection. A dedicated grip-based grab needs a reliable input
   route that coexists with the native pointer.
3. **Action input:** The engine has an action manifest and Steam Frame binding
   map for grip, trigger, sticks, bumpers, face buttons, and haptics. Probes
   did not receive active action data while native overlay laser input was in
   use. Native overlay smooth-scroll events do provide thumbstick motion
   during laser grabs. Investigate a compatible action path only for controls
   that native overlay events cannot provide.
4. **Object selection:** Current pointer targets are compact billboard
   surfaces around each model, not exact mesh intersections. Add mesh-aware
   picking when selection errors become visible. Test two independent grabs
   at once before claiming simultaneous manipulation support.
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
