# Scene lifecycle

The engine starts with no user scripts unless catalog `--launch` entries or
[autostart settings](#startup-and-recovery) provide them. It always runs the
[scratch scene](control-protocol.md#scratch), instance `0`, which has no file.
Scripts are started, stopped and reloaded while the engine runs from
[`pocketspace-cli`](cli.md), the [SteamVR dashboard](dashboard.md) or scratch's
`scripts` object, all through the [control protocol](control-protocol.md). Each running script has its own QuickJS context and scene. The engine
periodically checks each running script's modification time and builds a new
context and candidate scene before replacing that script's scene. A failed
reload preserves its active scene, hover and grab state, event callback, and queues.
A candidate cannot drain the active event queue or remove active objects.
`getMeshHover` reports no hit during candidate initialization.

A successful replacement detaches that script's old native surfaces before releasing
textures, retires old objects/panels, clears grab/hover state and event
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

[Bus subscriptions](javascript-api.md#messages-between-scripts) follow the scene:
a successful reload replaces them, a failed reload keeps them, and stopping a script
removes them along with any messages not yet delivered to it. Messages a candidate
publishes while loading are sent only after it commits.

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
With `engineAutostart`, the [user service](control-protocol.md#running-the-engine-as-a-service)
starts it again when SteamVR starts.
Forced termination such as SIGKILL cannot run scoped cleanup.

## Startup and recovery

Scripts come from one catalog directory, `$HOME/.local/share/pocketspace/scripts` by
default or the one given with `--scripts-dir`. Every readable `.js` file below it,
including in subdirectories, is a catalog entry named by its path relative to the
catalog, such as `tools/notes.js`. A link to another catalog file is the same entry
as that file. Links that lead outside the catalog, and symbolic links to
directories, are not followed. Only one instance of a file runs at a time.

Settings live in `$HOME/.config/pocketspace/settings.json`, or in the file given with
`--settings`. A missing file means the defaults:

| Key | Default | Meaning |
| --- | --- | --- |
| `version` | Required | Must be `1`. |
| `autostart` | `[]` | Catalog names launched in order at engine start, at most 256, without duplicates. |
| `autoRestart` | `false` | Restart scripts stopped by a callback error. |
| `engineAutostart` | `false` | Start the engine with SteamVR, through [`pocketspace-autostart.service`](control-protocol.md#running-the-engine-as-a-service). |

Unknown keys, other types and malformed names make the whole file invalid. The
engine checks the file about once a second while running. A valid edit to
`autoRestart` applies immediately; `autostart` edits take effect at the next engine
start. An invalid edit is reported, and the engine keeps the last valid settings.
Tools that change the file write a complete temporary file and rename it over the
original, so readers never see a partial file.

At startup, `--launch` entries start first and stop the engine if one cannot load.
Autostart entries then start independently: a missing, failing or duplicate entry
is reported and the others still run.

An uncaught exception in `update`, an input callback or a bus callback stops only
that script. The engine shows a SteamVR notification naming the script and error,
and publishes `engine.scriptStopped`. By default the script stays stopped. With
`autoRestart`, the engine reloads it after 1 second, then after 2, 4, 8 and 16
seconds if it keeps failing; after five attempts it stays stopped. A script that
runs for a minute without failing gets five attempts again. A restart that fails to
load counts as an attempt. Stopping a script by hand cancels a pending restart for
the rest of the session, and turning `autoRestart` off cancels all pending restarts.
The scratch scene is never restarted this way; an error in it recreates it empty.
