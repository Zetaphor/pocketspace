# Controller input architecture

## Goals

The engine separates global controller observation from overlay interaction.
SteamVR's documented `IVRInput` action API remains useful for bindings, but it
is focus-sensitive on Steam Frame. Legacy `GetControllerState` is not reliable
for this overlay (`IsInputAvailable()` was false on Deckard). The global path
therefore reads SteamVR's local `vrserver` input WebSocket.

## Data path

1. `frame_input::Monitor` opens a localhost WebSocket to `127.0.0.1:27062`.
2. It requests `input/getstate.json` to discover `/user/hand/left` and
   `/user/hand/right` Frame controllers.
3. It sends `request_input_state_updates` through the `input_server` mailbox.
4. WebSocket JSON component updates are decoded into per-hand raw state and a
   native event queue.
5. The main 90 Hz loop calls `frameInput.poll()` before manipulation and script
   execution.
6. QuickJS bindings expose state and drainable events.

WebSocket client frames must carry exactly four masking bytes. A previous native
implementation accidentally wrote sixteen masking bytes, so the connection
opened but SteamVR could not decode its subscription messages; controller
state remained at zero. The corrected frame writer uses four random bytes.
Both hands emit `controller.button` events for `/input/trigger/click`,
`/input/bumper/click`, `/input/thumbstick/click`, and `/input/grip/click`;
`controller.value` covers trigger/grip values and thumbstick x/y axes.
The event queues are bounded at 4096 entries each, so scripts that do not
poll cannot accumulate input indefinitely.

The wire protocol is undocumented SteamVR behavior. Keep the reference monitor
`scripts/steam-frame-input.py` alongside the native implementation: it is a
small independent probe and a regression oracle after SteamVR updates.

## State model

The raw component key is the SteamVR path, for example
`/input/trigger/value`, `/input/thumbstick/x`, or
`/input/capsense/trigger`. Boolean components become button/touch events;
scalar components become value events. Unknown future components are preserved
under `raw` rather than discarded.

The controller inventory observed on Deckard contained 77 live paths: 37 left
and 40 right, including buttons, touches, trigger/grip values, thumbstick axes,
and capacitive sensors. Left/right hardware exposes different face-button and
D-pad paths, so consumers must inspect `raw` and avoid assuming symmetry.

## Focus and ownership

The WebSocket monitor observes input without taking controller focus from the
primary VR application. Native overlay pointer events remain separate and are
used to identify surface hover and pointer grabs. Surface hover transitions
emit `overlay.surfaceHoverStart` and `overlay.surfaceHoverEnd`. The manipulation
loop adds `overlay.grabStart`, `overlay.grabMove`, and `overlay.grabEnd` events
while preserving the existing native behavior. Move events carry position
deltas and rotate events carry pitch/yaw/roll deltas. Queue and callback consumers
receive independent copies. Future explicit capture can associate raw
controller events with a grabbed object without changing the underlying global
state stream. Raw controller events are stamped with the latest surface-hover
object ID for that hand, or zero when no surface is hovered.

## Failure modes and maintenance

- SteamVR not running: connection startup fails; action and pose paths remain
  available where supported.
- SteamVR restarts: the native monitor marks a closed socket unavailable and the
  main loop retries connection periodically; controller paths are resubscribed
  on a successful reconnect.
- Protocol/schema changes: compare native output with `scripts/steam-frame-input.py`
  and update frame parsing, component normalization, and this document.
- Controller role changes: refresh controller discovery and resubscribe paths.
- Input focus: do not substitute `GetControllerState` for the WebSocket route;
  it is documented but unavailable in the tested overlay context.
- Manipulation activation accepts raw trigger click or value state as well as
  the legacy native pointer state. The two-hand avocado/box headset probe
  verified independent custom-laser grabs without native pointer focus.

## 2D panel path

Panels retain a CPU RGBA8 canvas, rasterize text through FreeType, and upload
on the next render frame after `present`. SteamVR overlays use OpenGL texture
handles and the engine clears and destroys them when panels are removed or a
script reloads. Interactive panels use SteamVR's native laser as mouse input.
They emit hover and pointer events, with top-left pixel coordinates, through
the same callback and queue as controller and model events. World,
viewer-facing, head-locked, and arbitrary transform modes are active.
Arbitrary orientation is relative to the standing anchor.

## Verification

Deckard built the engine and ran the panel test scene at about 90 FPS with no GL
error. After the four-byte mask fix, the native monitor delivered live events
through both callback and queue; snapshots showed valid raw state for both
hands. The user confirmed that trigger, grip, and thumbstick values update for
both hands in the headset dashboard. The standalone Python monitor remains an
independent comparison probe.
