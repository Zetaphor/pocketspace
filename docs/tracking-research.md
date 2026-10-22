# Steam Frame eye and finger tracking investigation

Investigated October 22, 2026, on Deckard. Engine baseline `a423205`; this
research adds diagnostic files only and does not yet add engine JavaScript
tracking functions. Remaining rendering optimizations are deferred in
[remaining work](remaining-work.md).

## What is available

| Signal | Documented API | Actual Deckard result | Practical API direction |
| --- | --- | --- | --- |
| Combined gaze origin and fixation point | OpenVR `IVRInput::GetEyeTrackingDataRelativeToNow` / `GetEyeTrackingDataForNextFrame`, with an `eyetracking` action | 450/450 samples active and valid; tracked=true in the logged samples; no scene process | Suitable for a combined gaze ray; transform standing coordinates to our scene anchor |
| Per-eye foveation centers | Installed `IVRSystem::GetEyeTrackedFoveationCenter`, plus projection variant | 450/450 samples valid, including a second run without polling an eye action | Expose explicitly as NDC points, not per-eye 3D poses or independently verified tracking confidence |
| Hand skeleton and curls/splay | OpenVR Skeletal Input API | Correct bindings, 31 bones per hand, but action inactive for all 450 samples; summaries and dynamic bones returned NoData | Investigate activation/focus/runtime behavior; do not advertise live skeleton support yet |
| Controller finger capacitance/contact | Local vrserver WebSocket component subscriptions | Both controllers streamed ring/pinky and input-surface capacitance, plus handgrip touch flags globally | Expose raw sensor values with availability/freshness; these are not calibrated anatomical curls |
| Gaze/skeleton through ordinary WebSocket updates | Not documented | Discovery advertises EyeTracking and Skeleton components, but the 8-second update subscription delivered neither | Presence in discovery does not prove the stream can transport those types |
| Per-eye directions, fixation, filter uncertainty | Undocumented `/dev/shm/eye-server.mmap` | Read-only probe found live counter/timestamp and unit vectors with the SteamOS 0.4 layout (+5-byte shift) | Optional experimental backend for overlay coexistence; validate layout and sample age before use |
| Eye openness | SteamVR release notes describe OSC when streaming from Frame; requires SteamOS 0.4.3+ | OSC was not enabled or probed; current public eye struct has no openness field | Separate future capability; do not infer it from gaze validity or promise it for standalone use |

## Primary documentation

- [Valve: Steam Frame Input](https://partner.steamgames.com/doc/steamhardware/steamframe/input)
  documents capacitive controller fingers, OpenVR Skeletal Input, controller types
  `frame_controller` / `frame_hmd`, OpenXR gaze, and the two OpenVR eye-tracking calls.
- [Valve: custom engines](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
  recommends OpenXR, supports OpenVR, and lists foveated-rendering extensions.
- [Valve: SteamVR Skeletal Input](https://github.com/ValveSoftware/openvr/wiki/SteamVR-Skeletal-Input)
  explains skeleton actions and pose/summary data. Some prose/signatures are old;
  use the installed SDK header for current structures and function signatures.
- [Valve: public OpenVR header](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h)
  defines `VREyeTrackingData_t` (`bActive`, `bValid`, `bTracked`, gaze origin, gaze
  target) and skeletal interfaces. No eye-openness/pupil-diameter fields occur in
  the inspected public eye structure.
- [Khronos: gaze sample time](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrEyeGazeSampleTimeEXT.html)
  covers gaze timing. `XR_EXT_eye_gaze_interaction` supplies a gaze pose, not
  arbitrary facial expressions or controller-less hand tracking.
- [Valve: SteamVR news](https://steamcommunity.com/app/250820/allnews/)
  October 1, 2026, version 2.18.2: eye openness via OSC when streaming from
  Frame with SteamOS 0.4.3+, with OpenVR SDK support described as upcoming.
- [Frametop source: ft-gaze](https://github.com/Frametop/frametop/blob/main/gaze/ft-gaze.cpp)
  is primary implementation evidence for its own reverse-engineered mmap reader
  and focus observations, not Valve documentation. Installed checkout: `24a86dc`.

The OpenXR controller extension is `XR_VALVE_frame_controller_interaction`,
profile `/interaction_profiles/valve/frame_controller_valve`. Eye gaze uses
`XR_EXT_eye_gaze_interaction`. Neither statement establishes support for
`XR_EXT_hand_tracking` bare-hand optical tracking on this device. A switch to
OpenXR is not needed to use the confirmed OpenVR eye API in the current engine.

## Documented OpenVR probe

Added `scripts/tracking-probe.cpp` and isolated manifests under
`cfg/tracking-probe/`; normal engine actions/bindings are unchanged. Deckard's
installed SDK exposes `IVRInput_011` and `IVRSystem_026`.

```sh
cd ~/Code/sf-overlay-mvp
c++ -std=c++20 -O2 -I "$HOME/.sdk/include" scripts/tracking-probe.cpp \
  -L /opt/steamvr/bin/linuxarm64 -lopenvr_api -o /tmp/sf-tracking-probe
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 /tmp/sf-tracking-probe
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 /tmp/sf-tracking-probe --gaze-action
```

Each run lasts 450 iterations, roughly five seconds, and creates no rendered
surfaces. Default mode activates only the skeleton set and reads foveation
centers directly. `--gaze-action` activates/reads the separate gaze set only
while `GetCurrentSceneProcessId()==0`; the check is repeated every iteration.
The probe is an OpenVR overlay client; active sets use the reserved minimum
global overlay priority. No primary scene app was running during the measured
runs, so game coexistence has NOT been verified.

Logs on Deckard:

- `/tmp/sf-tracking-api-probe.log`: combined gaze and per-eye NDC valid in all
  450 samples; gaze active and tracked in the logged samples, valid in all 450.
- `/tmp/sf-tracking-api-skeleton-probe.log`: same foveation success without eye
  action polling; skeleton inactive for all samples, both hands.

`GetActionBindingInfo` confirmed `/user/hand/left` → `/input/skeleton/left` and
right equivalents in `skeleton` mode. `GetBoneCount` returned 31. Both device
and animation summary calls returned `VRInputError_NoData` (13); dynamic bone
calls also returned 13 even when attempted without gating on action activity.
`GetSkeletalTrackingLevel` returned `VRInputError_IPCError` (7). This is an
unresolved runtime/activation issue, not evidence that Frame lacks finger
sensors. No controller simulation mode or user binding was changed to force it.

The installed OpenVR API includes `WithController` and
`WithoutController` skeletal motion ranges. These motion ranges
control reconstructed hand pose; they do not mean camera-based bare hands.

## Undocumented vrserver results

Read-only HTTP discovery:

```text
GET http://127.0.0.1:27062/input/getstate.json
Referer: http://127.0.0.1:27062/dashboard/controllerbinding.html
```

WebSocket at the same local port, with a unique mailbox:

```text
mailbox_open <unique-name>
mailbox_send input_server {"type":"request_input_state_updates","device_path":"/user/hand/left","returnAddress":"<unique-name>"}
```

Subscribe to right hand and `/user/head` similarly. The eight-second probe used
Frametop's existing `input/vrws.py` transport and saved its summary to
`/tmp/sf-tracking-ws-probe.json`. Discovery was saved to
`/tmp/sf-tracking-getstate.json` (may include user/device identity; keep local).
It received 758 right-hand, 750 left-hand, and one head component message.

Confirmed scalar paths on both hands:

```text
/input/capsense/ring
/input/capsense/pinky
/input/capsense/grip
/input/capsense/trigger
/input/capsense/bumper
/input/capsense/thumbrest
/input/capsense/thumbstick
/input/capsense/system
/input/capsense/menu
```

Right additionally exposes `/input/capsense/a`, `/b`, `/x`, `/y`; left exposes
`/input/capsense/dpad`. Both expose `/input/handgrip1/touch`,
`/input/handgrip2/touch`, `/input/thumbrest/touch`, and the ordinary button touch
paths. Scalar values were numbers (JSON float or integer), not bone arrays.
Ring/pinky signals were observed, but no wearer sequence correlating individual
finger movement with sensor values was collected; do not relabel grip/bumper
as specific anatomical joints without that validation.

Discovery lists `/input/skeleton/left` or `/right` as `InputValueType_Skeleton`,
and `/user/head` has `/eyetracking` as `InputValueType_EyeTracking`. Neither was
included in the observed `update_component_states` payloads. Head updates only
contained system/volume buttons and proximity. No separate skeletal/gaze
subscription protocol has been established.

The engine already retains raw scalar paths in `getControllerInput(hand).raw`
and emits `controller.value` events for them. Its convenience `capacitive`
projection currently omits ring, pinky, thumbrest, and dpad because it reuses a
button-name list. Exposing these raw signals does not require replacing the
transport; freshness/disconnect handling should be improved with the new API.

## Extra undocumented gaze backend

Read-only `/dev/shm/eye-server.mmap` exists (324,127 bytes in the probe).
Installed Frametop supports two layouts, shift 0 or +5 from its original
packed field offsets. The observed +5 layout has:

| Field | Offset / interpretation |
| --- | --- |
| Sample counter | `0x38`, uint32 |
| Timestamp | `0x15c`, double, CLOCK_MONOTONIC_RAW seconds |
| Set-1 directions | `0x164`, `0x170`, three floats each, head space |
| Set-1 fixation | `0x194`, three floats |
| Set-2 directions | `0x1a0`, `0x1ac`, three floats each |

Five read-only snapshots over 0.6 seconds showed counters progressing from
478043 to 478097 (about 90 Hz), sample ages around 22–29 ms, and earlier
readings showed both direction lengths near 1. The unshifted layout yielded
implausible timestamps/directions. This supports the current layout, not a
stable ABI guarantee.

Frametop labels additional floats as probable openness/confidence and filter
variance. Do not expose those with stronger semantics than the evidence.
Its reader validates timestamp progression and unit directions, uses stable
counter/timestamp reads, and accounts for gaze sample age using pose history.
Do not write this mapping: another region carries calibration commands.

Installed Frametop also documents an observed issue where reading the OpenVR
gaze action during a game repeatedly restarted the eye tracker and interrupted
game focus. That observation is not reverified here, but it is material to our
overlay coexistence requirement. Its game-time path reads shared memory instead.

## Proposed engine API and implementation order

These functions are a proposal, NOT currently bound in QuickJS:

1. `getEyeTracking()`: combined gaze with `source`, `available`, `active`,
   `valid`, `tracked`, coordinate `space`, `origin`, `target`, normalized
   `direction`, and timing/freshness where the source actually provides it.
   Convert standing-space points with the same anchor as objects/controllers;
   directions omit translation. Preserve the source's flags. The OpenVR action
   has no sample timestamp field: do not invent a device capture time from the
   local poll time. Include a documented policy for scene-app coexistence.
2. `getEyeFoveationCenters()`: separate per-eye NDC centers, projection convention,
   and validity. These are useful for gaze/debugging but not independent eye poses.
3. `getFingerTracking(hand)`: source `vrserver`, raw named sensor values and touch
   states, per-field availability and sample age. Extend the existing capacitive
   projection. Unknown fields stay absent/null, not synthesized zero curl.
4. `getHandSkeleton(hand)`: separate availability/activity, errors, tracking level,
   bone names/parents, coordinate convention, motion range, curls/splay, and
   transforms once the native action path produces data. A 31-bone declaration
   alone does not make this valid. Never silently substitute raw capacitance
   estimates while claiming native skeletal poses.

First implementation should use the confirmed OpenVR combined gaze and raw
controller capacitance, then validate both with a panel showing live values and
explicit target/finger exercises. Keep the experimental mmap backend optional
until its layout, lifecycle, stale-data behavior, and game coexistence tests pass.
Full skeleton activation needs its own follow-up probe; bare-hand optical
tracking and local OSC openness support remain unestablished.
