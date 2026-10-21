# Panel integration status (2026-10-20)

DeepSeek Harness handoff: `session-ccca95f8-9c7a-4acf-8a91-8a5d2b8044fb`. Sigma's `~/Code/sf-overlay-mvp` is the committed source checkout. Deckard's copy is the SteamVR build and runtime target.

## Implemented

- Retained `panel.canvas` clear, text, rectangle, line, circle, image, and present; file-backed image and FreeType font support; explicit panel removal.
- World, viewer-facing, head-locked, and arbitrary transforms. Arbitrary rotation is a row-major 3x3 basis relative to the scene anchor.
- Optional native-laser panel interaction with hover and pointer events, pixel coordinates, and button state. Panel, controller, mesh, and object manipulation events reach both callback and queue.
- Panel overlay, texture, and JavaScript callback cleanup on removal, shutdown, and script reload. A failed reload keeps the prior scene active.
- `scripts/panel-test-suite.js` displays controller values and button clicks, event counts, all four transform modes, model manipulation, drawing primitives, image load, and panel removal.

## Verified on Deckard and in the headset

- Deckard builds `sf-overlay-engine` with `cmake --build build -j2`; `git diff --check` passes on Sigma.
- A longer test-scene run held about 90 FPS with `gl_error=0`; it exited cleanly on SIGTERM. The user saw the dashboard and three smaller panels in front, could read their labels, and confirmed their transform behavior.
- The user confirmed that the green panel click target increments, and Deckard logged pixel coordinates within it. The user also confirmed avocado hover, grab, move, and rotate behavior and a clean visual reload without ghost panels.
- The dashboard showed live raw input from both controllers. The user confirmed trigger, grip, and thumbstick values change. The physical main trigger, upper bumper, and thumbstick click indicators worked on both hands; Deckard logged `/input/trigger/click`, `/input/bumper/click`, and `/input/thumbstick/click` events from left and right. An API probe received all eight expected trigger, bumper, stick, and grip click/value/axis paths per hand through both callback and queue.
- A reload probe loaded a valid revision, retained the previous scene through an intentionally broken edit, then loaded a corrected revision. The test scene also reloaded cleanly while the user watched in the headset.
- The temporary panel was removed after two seconds, and the engine logged `font=true` and `image=true` on first draw. Framecorder recording `~/Videos/framecorder/2026-10-20_23-03-49.mp4` captured the earlier missing-font state and helped identify the font-path issue.

## Defects corrected during validation

- Panel submission passed the address of a `GLuint` instead of the OpenGL texture handle to SteamVR. Correcting the handle resolved the first-frame stall and allowed graceful shutdown.
- The test used negative panel depth even though the scene anchor already begins about one meter in front of the HMD. Standing-anchor panels now use `z=0`; the head-locked panel uses positive depth.
- Deckard's fonts live under `/usr/share/fonts/TTF` and `/usr/share/fonts/noto`; the renderer now searches those paths, restoring all panel labels and status text.
- The native WebSocket client wrote a sixteen-byte mask in a frame that permits four bytes. The connection opened but SteamVR could not decode subscriptions, leaving raw input at zero. Four-byte masking restored live input. Event queues are bounded for scripts that do not poll them. The test dashboard now displays per-control press event counts from callback and queue alongside current button state.

## Maintenance note

SteamVR's `vrserver` input WebSocket is undocumented. If a SteamVR update changes its messages, compare `scripts/steam-frame-input.py` with the native monitor before changing controller normalization. The standalone monitor and the headset dashboard are the current regression probes.
