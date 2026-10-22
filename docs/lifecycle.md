# Scene and runtime lifecycle

Lifecycle hardening covers successful/failed scene reloads, object/panel removal,
controller and raw-input disconnects, SIGTERM/Ctrl-C, and SteamVR quit requests.
It preserves existing grab-button and picking behavior. The automated coverage
below is separate from physical headset/controller acceptance.

## Behavior

- A successful reload detaches old native overlays before freeing scene textures,
  clears pointers, grabs, hover IDs, panel cursors, and both input event queues,
  and replaces the callback/context. Held buttons must be released before the
  new scene accepts another grab. Calibration remains loaded across reloads.
- A failed reload keeps the active scene and callback. Candidate scripts cannot
  drain the active input queue, erase its pointer state by removing a candidate
  object with a matching ID, or read an old scene's native pointer through an
  ID collision. Failed candidates release their own resources. This is scene
  rollback, not rollback of external effects such as logging or calibration saves.
- Removing, hiding, or disabling a held object ends its grab once and clears the
  state even though its object no longer exists. Release is required before
  acquiring a replacement. Removed object hover/pointer bindings are cleared.
  Object removal retains its shared model asset until the scene is unloaded.
- Panel removal defers texture destruction until its native overlay is detached
  on the next synchronization pass. Hidden panels lose hover and are hidden even
  if the headset currently lacks a valid render pose.
- `getController(hand)` now exposes `connected` independently of `valid`.
  Temporary pose loss with the controller still connected preserves a hold while
  its selected button remains down. An actual disconnected device clears that
  hand's raw input/native pointer state and ends the hold. Reconnection requires
  release before grabbing again.
- Raw WebSocket disconnection clears cached button/sensor state, both queued-event
  copies, and hover bindings. Reconnection is attempted on a one-second wall-clock
  schedule rather than a rendered-frame counter. The connection/upgrade attempt
  has a 200 ms total timeout; a failed attempt can still cause a frame-time spike
  of that duration. HTTP rejection and WebSocket close frames are handled, and
  a first state frame sharing the HTTP upgrade packet is retained.
- SteamVR `VREvent_Quit` is acknowledged and exits through cleanup. SIGTERM and
  Ctrl-C use the same cleanup path. This implementation exits when SteamVR quits;
  it does not recreate the OpenVR session in process. Restart SteamVR, wait for
  valid headset tracking, then relaunch the desired engine command.
- Scope guards clean up overlays/imported buffers, shaders, model/panel resources,
  raw input, OpenVR, GBM/file descriptors, and EGL on normal shutdown and native
  initialization failures. Texture users are detached before texture deletion.
  Process termination by SIGKILL is outside the engine's cleanup control.

End events for removed/disabled/disconnected held objects reach the active scene
on its next normal event dispatch. A scene replaced during reload receives no
terminal callback: its callback/context is being destroyed. The replacement
scene receives no old queued events. IDs can restart in a replacement scene,
so scripts should not treat an ID as globally unique across reloads.

## Automated checks

From the repository root on a host with a C++20 compiler:

```sh
python3 tests/grab-buttons-regression.py
c++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -I src tests/lifecycle-input-regression.cpp src/frame_input.cpp -o /tmp/lifecycle-input-regression
timeout 10 /tmp/lifecycle-input-regression
c++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -I src tests/tracking-regression.cpp src/frame_input.cpp -o /tmp/tracking-regression
timeout 10 /tmp/tracking-regression
```

The actual grab state machine is tested for held-object removal/hiding, duplicate
end-event prevention, temporary tracking loss versus disconnect, and release
before reacquisition/reload. The real input monitor uses a loopback server to
exercise queued-state reset, per-hand invalidation, upgrade packets containing
state frames, connection loss/reconnect, close frames, rejected/stalled upgrades,
and repeated failed-connection descriptor cleanup. Scope-exit order and early
return are also checked.

On Deckard, test the actual QuickJS loader without starting a VR session:

```sh
cmake -S . -B build
cmake --build build --target lifecycle-script-regression -j2
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/lifecycle-script-regression
```

This explicit target is excluded from normal builds and retains assertions in
Release. It checks candidate isolation, pointer/queue preservation on failure,
restoration and invocation of the old callback, the commit cleanup hook, deferred
panel ownership, and 30 repeated successful/failed context replacements.

For the live SteamVR integration run, stop other engine instances first:

```sh
c++ -std=c++20 -O2 -I /home/steamos/.sdk/include scripts/lifecycle-probe.cpp -L /opt/steamvr/bin/linuxarm64 -lopenvr_api -o /tmp/sf-lifecycle-probe
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 python3 tests/lifecycle-runtime.py
```

The driver starts/stops its own engine, creates only temporary scene files, runs
10 failed/successful reload pairs, removes/replaces objects and panels, and checks
native overlay keys through a read-only background probe. It verifies descriptor
counts and SIGTERM exit, including absent laser handles afterward. It does not
restart SteamVR or power-cycle controllers. Log: `/tmp/lifecycle-runtime.log`.

Initial results passed all listed checks. The live test retained 104 open file
descriptors across every sample; RSS grew from 62,056 to 63,252 kB over the run.
That short measurement is not proof against all long-running memory leaks.
Native callback/rollback log: `/tmp/lifecycle-script-regression.log`.
Physical controller disconnection, held-object visual removal, and a real
SteamVR quit/restart were not accepted as tested. The wearer scene was launched
(`/tmp/lifecycle-wearer.log`, about 89.8–90 FPS, `gl_error=0`) and then stopped
at the user's request to move on: powering off while maintaining a hold was
not a practical test for them. No physical-disconnect or restart pass is claimed.
Those manual checks are deferred rather than blocking the completed automated
hardening work. The instructions below are optional future reproduction steps.

## Headset checks

```sh
./run-overlay-engine.sh scripts/lifecycle-test.js
```

The default box allows grip or trigger. Removal starts disabled.

1. Grab it, power off the holding controller, then power it on. The box should
   release; it should not resume following until the buttons are released and
   another grab starts. Repeat with the other hand.
2. Tap bumper to enable removal-on-hold and replace the interactive panel. Grab
   the box for two seconds: it disappears, ends the grab, and respawns after
   1.5 seconds. Release before grabbing the replacement. Repeat with both hands.
3. Repeatedly tap bumper while pointing at the panel. The old panel should leave
   no ghost, and the replacement should retain working native pointer input.
4. Hold a box while an operator touches the scene file to trigger reload. The
   replacement scene must wait for release; a failed edited script must retain
   the old scene. Use a temporary copy when deliberately introducing errors.
5. In a dedicated runtime session, quit SteamVR while holding a box. Expect
   `[lifecycle] SteamVR quit; exiting` and process exit. Restart SteamVR and
   relaunch after tracking is ready; verify there are no stale grabs or overlays.

Do not restart another active primary VR experience as part of this test. Record
engine revision, logs, each observed result, and any untested step. The read-only
probe reports `scenePid` to help identify an active primary scene process.
