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
- Manipulation activation accepts the unified raw trigger-click state as well as
  the legacy native pointer state, allowing future controller-only grabs while
  preserving the verified laser path.

## Verification

Deckard successfully built the engine after adding the native monitor source.
The standalone monitor captured 77 unique components in a bounded run without
overlay hover. OpenVR action polling separately captured thumbstick actions but
not the complete controller surface.
