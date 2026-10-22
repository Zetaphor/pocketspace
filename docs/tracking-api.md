# Eye and finger tracking API

The first implementation uses documented OpenVR gaze/foveation calls and raw
controller capacitance from the existing local vrserver stream. It does not
implement bare-hand optical tracking, native hand skeletons, eye openness, or
the experimental shared-memory eye backend. See [research](tracking-research.md)
for the available sources and unresolved skeletal behavior.

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
| `sampleAgeMs` | Always `null`; the OpenVR action does not provide a capture timestamp |
| `suppressed`, `sceneAppPid` | Whether another scene process prevented action polling, and its PID |
| `error` | OpenVR input error number when a native query/update was attempted, otherwise `null` |
| `status` | `notRequested`, `notReady`, `sceneAppActive`, `inactive`, `invalid`, `error`, `valid`, `stale`, `anchorUnavailable`, or `invalidGeometry` |

The gaze action is placed in a separate set at the SDK's minimum global overlay
priority. If `GetCurrentSceneProcessId()` reports any nonzero PID, the engine
does not activate that set or query the gaze action, clears the current gaze
snapshot, and returns `status: "sceneAppActive"` with null geometry. There is no
opt-out of this policy yet. It preserves the existing controller-action set.
This is a conservative policy based on the coexistence concern found during
research; actual game startup/focus behavior still needs a headset test.

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
its game coexistence behavior has not yet been verified.

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
The older `getControllerInput(hand).capacitive` convenience object gains `ring`,
`pinky`, `thumbrest`, and `dpad`, retaining its legacy zero-for-missing convention.
Use the new getter for availability/freshness-sensitive code.

## Verification and headset exercise

```sh
c++ -std=c++20 -O2 -pthread -I src tests/tracking-regression.cpp src/frame_input.cpp -o /tmp/tracking-regression
/tmp/tracking-regression
./run-overlay-engine.sh scripts/tracking-smoke.js
./run-overlay-engine.sh scripts/tracking-test.js
```

The native regression checks scene transforms, invalid rays, freshness, the
scene-PID policy, real loopback WebSocket snapshots/deltas, unavailable field
handling, false/zero values, queue draining, and disconnect invalidation. It
also passes with libstdc++ assertions enabled. The picking regression remains
unchanged and passes.

The diagnostic panel is noninteractive: keep the head still and look at each
gold circle. A green dot shows where the combined gaze ray intersects the
fixed panel plane. Lift/curl ring and pinky fingers individually and compare
both controllers' raw values; confirm direction and independence rather than
assuming a capacitance threshold is a curl angle. Wearer verification remains
required for alignment, sensor interpretation, and main-app coexistence.

Initial Deckard integration on top of `8a09717` built successfully and ran
`scripts/tracking-test.js` for 20 seconds: live gaze, foveation, and both hands'
capacitance reached JavaScript; the diagnostic assertions passed, `gl_error=0`,
and shutdown completed at 1644 frames. The text-heavy panel run reported 83 FPS;
this is not a 90 FPS tracking-performance claim. Log:
`/tmp/tracking-api-smoke.log`. Final parser/reload verification is recorded in
the session notes below.

Final parser verification used the headless `scripts/tracking-smoke.js`, copied
to `/tmp/sf-tracking-reload.js` and touched after five seconds. The bounded
16-second run loaded twice, passed the schema/normalization/null-value checks
before and after reload, held 90.1 FPS with `gl_error=0`, and stopped cleanly at
1414 frames. Both eye APIs were valid in all logged frames; both hand streams
became valid after initial delivery and remained valid in the logged samples.
Log: `/tmp/tracking-api-reload.log`. This isolates API use from diagnostic text
drawing overhead and does not establish behavior alongside a primary scene app.

The existing `scripts/two-hand-grab-test.js` also ran for 15 seconds with the
final native sources. Its log records distinct simultaneous held objects,
independent release events, 90 FPS, `gl_error=0`, and clean shutdown at 1313
frames (`/tmp/tracking-api-two-hand-regression.log`). No wearer observations
were collected for this regression run.

## Wearer results and gaze accuracy follow-up

The wearer confirmed the finger readings worked after removing a Spigen
controller cover; they did not work properly with that cover fitted. This is
an observation for the tested cover/controller combination, not a general
compatibility conclusion for every cover.

In `scripts/tracking-test.js`, gaze was closest to the right gold circle and
substantially farther off at the center and left circles. Source validity and
tracked flags therefore do not establish gaze accuracy. The runtime log is
`/tmp/tracking-wearer-d2662c2.log`. No calibration correction has been applied.

Code inspection found the scene conversion and panel intersection consistent
with the fixed panel transform. This does not independently establish that
the runtime's calibration/projection is correct. To measure the error, run
`scripts/gaze-accuracy-test.js`: nine fixed targets, one visible at a time,
with no live gaze dot. Keep the head still and tap either trigger while looking
at each target. Sampling starts after a 350 ms settling interval and lasts one
second. At least 30 valid/tracked samples are required; otherwise retry the
same point. Either bumper restarts. The final three targets complete the grid.

`GAZE_ACCURACY_POINT` logs each target's median observed pixel position,
signed pixel error, median angular error, and 95th-percentile spread around the
median. `GAZE_ACCURACY_COMPLETE` logs the full set. These are explicit wearer
target samples, not an automatically learned correction. The measurements are
specific to this panel and headset fit; no head-pose stability claim is made.
Any later general-purpose correction should account for head-relative gaze,
headset repositioning, and held-out validation targets.

`node tests/gaze-accuracy-test.cjs` exercises the actual scene script with exact
synthetic target rays, a known 100-pixel bias, invalid samples/retry, nine-point
completion, and restart. It passes. Runtime/wearer measurements are next.
