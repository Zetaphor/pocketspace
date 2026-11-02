# Scene lifecycle

The engine starts with no user scripts unless catalog `--launch` entries are
provided. Each running script has its own QuickJS context and scene. The engine
periodically checks each running script's modification time and builds a new
context and candidate scene before replacing that script's scene. A failed
reload preserves its active scene, pointer state, event callback, and queues.
A candidate cannot drain the active event queue or remove active objects.
`getPointer` has no active pointer snapshot during candidate initialization.

A successful replacement detaches that script's old native surfaces before releasing
textures, retires old objects/panels, clears pointer/grab/hover state and event
queues, and installs the new callback. Allowed grab buttons must be released
before they can acquire an object in the new scene. External side effects such
as logging or saving calibration files are not rolled back with a failed scene.

Removing an object cancels its grab/hover state and removes its physics body
and attached joints. Body removal restores the surviving connected bodies
through the normal joint cleanup path. Explicit removal does not emit a joint
break event. Each scene owns an independent physics world, discarded on
successful reload. Stopping one script releases its scene resources while other
scripts continue running. Removing a panel defers resource release until its native
overlay has detached. Scene assets live until scene
replacement or shutdown, even if their last object is removed, unless explicitly
released with `unloadModel` after all users are gone.

[Scene containers](scene-containers.md) provide explicit cleanup scopes inside
a scene, with optional parent/child ownership. Recursive disposal reuses individual object/panel cleanup, including attached
physics and optionally owned input callbacks, while retaining shared assets.
Callback ownership is isolated with the candidate scene during reload.

Controller disconnection invalidates raw hand/pointer state and cancels held
objects. Temporary pose loss preserves a hold while an allowed button remains
down. Loss of the raw vrserver connection clears cached values and queued input
while preserving pending physics collision and joint-break events;
reconnect attempts use a one-second interval with a bounded 200 ms handshake.
Changes to a hand's connected OpenVR device index renew the raw subscriptions,
including connection, removal and replacement. This uses the same input/grab
cleanup as connection loss, while preserving physics events and the scene.
A connected controller that sends no initial raw state within five seconds
also triggers a retry. Once initial state arrives, an idle delta stream alone
is not a reason to reconnect. Temporary pose loss without device disconnection
keeps the current subscription.

Ctrl-C, SIGTERM, and native shutdown unwind overlay, GPU/EGL, input, and OpenVR
resources. A SteamVR quit event is acknowledged and exits the process. Relaunch
the engine after SteamVR restarts; there is no in-process session recreation.
Forced termination such as SIGKILL cannot run scoped cleanup.
