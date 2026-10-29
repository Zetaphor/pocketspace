# Eye and finger tracking API

The engine exposes documented OpenVR gaze/foveation data and raw controller
capacitance from the local vrserver stream. Bare-hand optical tracking, native
hand skeletons, eye openness, and per-eye 3D poses are not exposed.

## Optional personal correction

See [eye calibration](eye-calibration.md) for `--eye-calibration FILE`, the headset
generator, and `getCalibratedEyeTracking()`. `getEyeTracking()` remains
raw. Both gaze getters expose a nullable row-major `headBasis` matrix for
head-relative calculations; validity and coexistence policies remain as below.

## Combined gaze

```js
const gaze = getEyeTracking();
if (gaze.valid) {
  print(JSON.stringify({origin: gaze.origin, direction: gaze.direction}));
}
```

Calling this getter requests gaze polling for the current script's lifetime.
The call reads a cached frame snapshot; it never performs an IPC call itself.
The first call can return unavailable data until the next native update.
Reloading to a script that does not call it stops gaze polling. No gaze-based
hover, click, or grab is automatically enabled.

| Field | Meaning |
| --- | --- |
| `source` | `"openvr"` |
| `space` | `"scene"`: the same fixed anchor and meters as objects/controllers; +X right, +Y up, +Z forward |
| `configured` | The eye action and set handles were acquired; not evidence that hardware is tracking |
| `available`, `active` | The native action's `bActive` flag |
| `reportedValid`, `tracked` | The native `bValid` and `bTracked` flags |
| `valid` | Active, reported valid, recently polled, finite nondegenerate geometry, and a scene anchor is available |
| `fresh` | The native poll completed within 250 ms; not a device capture-time measurement |
| `origin`, `target`, `direction` | `{x,y,z}` when valid; otherwise `null`. `target` is the fixation point; `direction` is normalized |
| `pollAgeMs` | Time since the local native query completed, or `null` before querying |
| `headBasis` | Nullable row-major 3×3 matrix mapping head-local +Z-forward coordinates into scene space. |
| `sampleAgeMs` | Always `null`; the OpenVR action does not provide a capture timestamp |
| `suppressed`, `sceneAppPid` | Whether another scene process prevented action polling, and its PID |
| `error` | OpenVR input error number when a native query/update was attempted, otherwise `null` |
| `status` | `notRequested`, `notReady`, `sceneAppActive`, `inactive`, `invalid`, `error`, `valid`, `stale`, `anchorUnavailable`, or `invalidGeometry` |

The gaze action is placed in a separate set at the SDK's minimum global overlay
priority. If `GetCurrentSceneProcessId()` reports any nonzero PID, the engine
does not activate that set or query the gaze action, clears the current gaze
snapshot, and returns `status: "sceneAppActive"` with null geometry. There is no
opt-out of this policy yet. It preserves the existing controller-action set.
This suppression is part of the current runtime policy.

## Per-eye foveation centers

```js
const centers = getEyeFoveationCenters();
if (centers.valid) print(JSON.stringify(centers.left));
```

This requests `IVRSystem::GetEyeTrackedFoveationCenter` polling for the current
script. It does not activate the gaze action. Fields are `source: "openvr"`,
`space: "ndc"`, `projection: "IVRSystem.GetProjectionMatrix"`, `available`,
`valid`, `pollAgeMs`, `sampleAgeMs`, and `left`/`right` `{x,y}` objects (null
when invalid). `available` means the SDK entry point was polled, not that the
current data is valid. `sampleAgeMs` is null; validity also rejects nonfinite
coordinates and local polls older than 250 ms.

These are projection-dependent NDC points, not independent 3D eye poses,
pupil diameters, eye openness, or gaze confidence. They are not clamped. This
read-only system API remains separate from scene-app suppression of gaze actions;
it does not acquire the gaze action set.

## Raw finger sensing

```js
const hand = getFingerTracking("left");
if (hand.valid) {
  print(JSON.stringify({ring: hand.sensors.ring.value,
    pinky: hand.sensors.pinky.value,
    contact: hand.touches.handgrip1.value}));
}
```

The hand argument must be `"left"` or `"right"`; other values throw. No additional
native action, input focus, or stream subscription is required.

- `source: "vrserver"`, `hand`, `connected`, `available`, `valid`, `fresh`,
  `ageMs`, `staleAfterMs: 250`, `sensors`, `touches`, `skeleton: null`.
- `connected` describes the WebSocket, not a guaranteed physical-controller
  connection. `available` means at least one capacitance field was observed on
  this connection. `valid` requires availability and a recent hand-stream update.
- Each entry under `sensors` and `touches` is
  `{available, value, updateAgeMs}`. Missing, unsupported, nonfinite, disconnected,
  or stream-stale values are `null`, preserving real numeric zero and boolean false.
- `ageMs` measures receipt of the last accepted hand component update.
  `updateAgeMs` measures receipt of that individual field; neither is a hardware
  capture timestamp. An unchanged field can have an old `updateAgeMs` while
  remaining valid because vrserver sends deltas and other fields prove liveness.
- Controller connection/removal/device-index changes renew raw subscriptions.
  Missing initial state for a connected controller retries after five seconds;
  an idle stream with previously received state does not. See [lifecycle](lifecycle.md).
- Disconnect/reconnect clears cached fields and timestamps. Unsupported JSON
  values invalidate the affected field rather than becoming synthetic zeroes.

Sensor names: `ring`, `pinky`, `grip`, `trigger`, `bumper`, `thumbrest`,
`thumbstick`, `a`, `b`, `x`, `y`, `dpad`, `system`, and `menu`. Paths are
`/input/capsense/<name>`; left/right controllers expose different subsets.
Touch names include `handgrip1`, `handgrip2`, `thumbrest`, the ordinary buttons,
trigger/grip/bumper/stick, and dpad directions. Values come from corresponding
`/input/<name>/touch` paths.

These are raw capacitance/contact channels, not calibrated finger angles or
five anatomical curl values. `skeleton` stays null; no native bones are invented.
Existing `controller.value`/`controller.button` events still carry raw updates.
The `getControllerInput(hand).capacitive` convenience object includes `ring`,
`pinky`, `thumbrest`, and `dpad`, retaining its legacy zero-for-missing convention.
Use `getFingerTracking` for availability/freshness-sensitive code.
