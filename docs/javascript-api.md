# JavaScript API

This document describes the QuickJS API exposed by `sf-overlay-engine`.

## Controller input

`getControllerInput("left"|"right")` returns the latest globally observed Steam
Frame controller state. It is populated from SteamVR's local `vrserver` input
WebSocket and does not require the controller to hover one of this engine's
overlays.

The stable normalized fields are `valid`, `hand`, `buttons`, `analog`, and
`capacitive`; `raw` preserves the original `/input/...` component paths and
values. The current implementation is adding these normalized projections while
retaining raw paths for forward compatibility with SteamVR changes.

```js
const right = getControllerInput("right");
print(JSON.stringify(right.raw));
```

`pollInputEvents()` drains low-level controller events observed since the last
call. Events include `type`, `hand`, `control`, `objectId`, `down`, and `value`.
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
