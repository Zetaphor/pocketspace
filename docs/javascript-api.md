# JavaScript API

This document describes the QuickJS API exposed by `sf-overlay-engine`.

## Controller input

`getControllerInput("left"|"right")` returns the latest globally observed Steam
Frame controller state. It is populated from SteamVR's local `vrserver` input
WebSocket and does not require the controller to hover one of this engine's
overlays.

The stable normalized fields are `valid`, `hand`, `buttons`, `analog`, and
`capacitive`; `raw` preserves the original `/input/...` component paths and
values. These projections are implemented. Missing components read as false or
zero, so check `valid` and inspect `raw` before assuming a path is present.
The WebSocket schema is undocumented and may change with SteamVR.

```js
const right = getControllerInput("right");
print(JSON.stringify(right.raw));
```

For Steam Frame, `buttons.trigger.down` is the main trigger click,
`buttons.bumper.down` is the upper trigger button, and
`buttons.thumbstick.down` is the stick press. Each exists for both hands.
`analog.trigger` and `analog.grip` expose their continuous values; the
thumbstick axes are `analog.thumbstick.x` and `.y`. The diagnostic panel shows these clicks and values separately. All listed
channels emit events on **both** `onInputEvent(callback)` and
`pollInputEvents()`; `control` carries the raw SteamVR path and `hand` is
`left` or `right`.

| Physical control | Event `type` | Event `control` | Read |
| --- | --- | --- | --- |
| Main trigger press | `controller.button` | `/input/trigger/click` | `down` |
| Main trigger pull | `controller.value` | `/input/trigger/value` | `value` |
| Upper trigger (bumper) | `controller.button` | `/input/bumper/click` | `down` |
| Thumbstick press | `controller.button` | `/input/thumbstick/click` | `down` |
| Thumbstick movement | `controller.value` | `/input/thumbstick/x`, `/input/thumbstick/y` | `value` |
| Grip press / pull | `controller.button` / `controller.value` | `/input/grip/click`, `/input/grip/value` | `down` / `value` |

`pollInputEvents()` drains low-level controller events observed since the last
call. Events include `type`, `hand`, `control`, `objectId`, `down`, and `value`.
Both callback and polling queues retain at most 4096 events; drain the polling
queue each update if every event matters.
Movement events additionally include `dx`, `dy`, and `dz`; rotation events
include `dpitch`, `dyaw`, and `droll`. Raw controller events use the latest
surface-hover object ID for that hand, or zero when no target is hovered.
Use it from `update(dt)` for deterministic processing. `onInputEvent(callback)`
registers a callback convenience layer; it receives the same event objects as
they arrive. Registering a new callback replaces the previous callback.

```js
for (const event of pollInputEvents()) {
  if (event.type === "controller.button" && event.down)
    print(`${event.hand} ${event.control}`);
}
```

The existing `getInput(hand)` action API remains available for SteamVR action
bindings, but raw global monitoring uses `getControllerInput` and
`pollInputEvents`.

## 2D panels

`createPanel({widthPixels, heightPixels, widthMeters, interactive})` creates a retained
RGBA panel. `interactive` defaults to false. When enabled, SteamVR mouse events
produce `panel.hoverStart`, `panel.hover`, `panel.hoverEnd`, `panel.pointerDown`,
and `panel.pointerUp` in both the callback and the event queue. These events use
the panel ID in `objectId`; their `panel.*` type distinguishes them from model
events. Pointer and hover events include pixel `x`/`y` coordinates measured
from the top-left of the panel. Pointer events set `control` to the mouse
button and `down` to its state. The engine sets OpenVR mouse scale to the panel pixel dimensions and keeps the last mouse move position for button events. A panel receives native laser input only when it is visible.

```js
const panel = createPanel({widthPixels: 1024, heightPixels: 512,
  widthMeters: 1.2, interactive: true});
panel.canvas.clear(12, 18, 29, 255);
panel.canvas.text(24, 24, "Controller Debug", 255, 255, 255, 255, 32);
panel.canvas.rect(20, 80, 200, 100, 80, 160, 240, 255, false);
panel.canvas.line(20, 80, 220, 180, 255, 255, 255, 255);
panel.canvas.circle(300, 120, 32, 240, 180, 80, 255, true);
panel.canvas.image(360, 90, "assets/panel-test.png");
panel.canvas.present();
panelSetTransform(panel, "viewerFacing", 0, .2, 0);
panelSetVisible(panel, true);
// Later: removePanel(panel);
```

`panelLoadFont(panel, path, pixels)` selects a FreeType font file. The text API
returns false when the font cannot be loaded; `image` returns false when the
image cannot be loaded. `panel.canvas.present()` requests an upload on the next
render frame, after the GL context exists. Legacy `panelClear`, `panelText`,
`panelPresent`, `panelCanvasClear`, and `panelCanvasText` remain available.

Transform modes are `world` (standing anchor), `viewerFacing` (faces the current
HMD pose), `headLocked` (follows the HMD), and `arbitrary` (absolute rotation).
For arbitrary mode, call `panelSetRotation(panel, r00,r01,r02,r10,r11,r12,r20,r21,r22)`
with a row-major 3x3 orientation matrix relative to the scene standing
anchor. Its position also uses standing-anchor coordinates. The standing anchor
is initialized about one meter in front of the headset; `z=0` uses that
anchor and positive `z` moves farther forward. `headLocked` positions are
relative to the headset itself, so a positive `z` (around 1 meter) is needed
to place that panel in front of the eyes. `removePanel` releases the retained texture; the engine
removes its SteamVR overlay on the next frame. Script reload removes the old
panels, overlays, callback, and model surfaces before installing the new scene.

OpenVR reference: [SetOverlayMouseScale](https://github.com/ValveSoftware/openvr/wiki/IVROverlay%3A%3ASetOverlayMouseScale) and the [official overlay sample](https://github.com/ValveSoftware/openvr/blob/master/samples/helloworldoverlay/openvroverlaycontroller.cpp).

Run `./run-overlay-engine.sh scripts/panel-test-suite.js` on Deckard for a visual
integration check. It shows raw controller state, callback and queue counts,
model hover/grab/move/rotate events, panel pointer events, four transform modes,
text and drawing primitives, image loading, and panel removal. The labels
report observed events; headset appearance and control behavior still require
an operator to inspect the display and use the controllers.

## Object interaction

Objects are created with `createObject` and configured with
`setObjectInteraction`. Existing laser-grab, direct-grab, depth, yaw, and hand
rotation options remain the compatibility layer for native manipulation.

Interaction events are delivered through the input event queue and callback.
The event model includes controller button/value events and overlay pointer and
surface-hover start/end events. Object IDs are the stable JavaScript identity;
native overlay handles are implementation details and may be exposed only in
diagnostic event fields. Grab start/move/end, `overlay.move`, and `overlay.rotate` events are emitted by
native manipulation. Move/grab events include `dx`, `dy`, and `dz`; rotation
events include `dpitch`, `dyaw`, and `droll`. Mesh hover emits `overlay.meshHoverStart` and
`overlay.meshHoverEnd` using the controller aim ray and transformed model
triangles. Queue polling and callbacks receive independent copies of each
event.

## Existing object API

```js
const asset = loadModel("assets/Avocado.glb", 768);
const id = createObject(asset, 0, 0, 0, .3);
setObjectInteraction(id, {
  grabbable: true,
  directGrab: true,
  laserYaw: true,
  depthAdjust: true,
  handRotationAxes: {pitch: true, yaw: true, roll: true}
});
```

See `README.md` for the complete existing object and pointer function list.

## Interaction test script

The repository includes `scripts/input-interaction-smoke.js`, which creates one
grabbable object, registers the callback API, reads the normalized state, and
drains queued events. Run it with:

```sh
./run-overlay-engine.sh scripts/input-interaction-smoke.js
```

A headset/controller session is required to observe controller-valid state,
hover transitions, mesh-hover transitions, or grab deltas. If no controllers are
tracked, the script still verifies API loading and the complete empty-state
shape.
