# Scene lifecycle

The engine periodically checks the script's modification time. It builds a new
QuickJS context and candidate scene before replacing the current one. A failed
load preserves the active scene, pointer state, event callback, and queues.
A candidate cannot drain the active event queue or remove active objects.
`getPointer` has no active pointer snapshot during candidate initialization.

A successful replacement detaches the old native surfaces before releasing
textures, retires old objects/panels, clears pointer/grab/hover state and event
queues, and installs the new callback. Allowed grab buttons must be released
before they can acquire an object in the new scene. External side effects such
as logging or saving calibration files are not rolled back with a failed scene.

Removing an object cancels its grab/hover state and removes its physics body
and attached joints. Body removal restores the surviving connected bodies
through the normal joint cleanup path. Explicit removal does not emit a joint
break event. Each scene owns an independent physics world, discarded on
successful reload. Removing a panel defers resource release until its native
overlay has detached. Scene assets live until scene
replacement or shutdown, even if their last object is removed.

Controller disconnection invalidates raw hand/pointer state and cancels held
objects. Temporary pose loss preserves a hold while an allowed button remains
down. Loss of the raw vrserver connection clears cached values and queued input
while preserving pending physics collision and joint-break events;
reconnect attempts use a one-second interval with a bounded 200 ms handshake.

Ctrl-C, SIGTERM, and native shutdown unwind overlay, GPU/EGL, input, and OpenVR
resources. A SteamVR quit event is acknowledged and exits the process. Relaunch
the engine after SteamVR restarts; there is no in-process session recreation.
Forced termination such as SIGKILL cannot run scoped cleanup.
