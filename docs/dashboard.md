# SteamVR dashboard

`pocketspace-dashboard` adds a **Pocketspace** tab to the SteamVR dashboard. From the
tab you can start and stop the engine and manage scripts without a terminal. It is a separate
process from the engine. It talks to the engine over the
[control socket](control-protocol.md), so the engine and its scripts keep running
when the dashboard stops or restarts.

The build produces `build/pocketspace-dashboard`.
`tools/install-user-service.sh` installs and enables `pocketspace-dashboard.service`
with the [engine service](control-protocol.md#running-the-engine-as-a-service). The
dashboard then starts whenever SteamVR starts:

```sh
tools/install-user-service.sh
systemctl --user restart pocketspace-dashboard.service   # start it in a running SteamVR session
journalctl --user -u pocketspace-dashboard.service       # its log
```

At start the dashboard registers
`~/.local/share/pocketspace/pocketspace-dashboard.vrmanifest` with SteamVR as
`com.pocketspace.dashboard`, then turns SteamVR's auto-launch for it off, so the
systemd unit is the only thing that starts it. SteamVR then lists it as an
application.

The dashboard attaches only to a SteamVR session that is already running. It never
starts SteamVR: the unit uses `Requisite=` rather than `Requires=` or `BindsTo=`, and the
process itself connects as a background application. Without a running session it
exits with code 3. If another dashboard already owns the tab, it exits with code 4.
The service does not restart it after either exit. When SteamVR stops, the
dashboard stops too, and it starts again the next time SteamVR starts.

## The tab

The header shows whether the engine is running, followed by its process totals:
FPS, CPU, RAM, driver GPU memory (`n/a` when the driver does not report it), and the
engine's estimated GPU allocations with the shared part. These come from the
[resources](control-protocol.md#resources) sample and are never divided among scripts.

While the engine is offline, the tab offers **Start Engine**. This runs
`systemctl --user start pocketspace.service`, just as
[`pocketspace-cli start-engine`](cli.md) does. The tab shows an error if
`systemctl` fails, or if the engine has not answered within 30 seconds.

While the engine runs, **Stop Engine** in the header runs
`systemctl --user stop pocketspace.service`, which stops every script. The first
press only arms it: it turns red and reads **Press again to stop** for three
seconds. If the engine still answers 10 seconds after the second press, the tab
reports that it did not stop. This happens when the engine was started outside the
service, for example with `run-pocketspace.sh`.

With the engine running, the tab has three areas:

| Area | Shows and does |
| --- | --- |
| Scripts | Scratch (instance `0`) first, with **Reset**, then each running or failed script, newest last. For each: name, instance ID, generation, uptime or restart state, JavaScript heap, callback time per second and its worst frame, estimated GPU allocations, and object, panel, asset and body counts. If a reload failed and the old scene was kept, the row says so. A failed script shows its last error. Running scripts have **Reload** and **Stop**. A failed script waiting to restart has **Stop**, which cancels the restart. A failed script that is not restarting has **Start**. Stopped scripts are not listed; start them again from the catalog. |
| Catalog | Every script the engine can start. **Launch** starts one; it reads **Running** while that script runs. **Auto** adds the script to, or removes it from, the [autostart list](lifecycle.md#startup-and-recovery). |
| Settings | **Restart failed scripts** (`autoRestart`) and **Start engine with SteamVR** (`engineAutostart`). If the settings file is invalid, its error appears here. |

Stop and reload requests carry the generation the tab shows. If the script has
reloaded since then, the request fails. The tab reports the failure at the bottom
and refreshes the list, so the next click targets the current scene. A button
shows `...` until its reply arrives.

When a script stops because of a callback error, an alert line appears under the
header. Repeated failures of the same script, such as those of a restart loop,
update that one line and count them. **Dismiss** clears the alerts. The SteamVR
notification for the failure comes from the engine itself, so it appears even
when the tab is closed or the dashboard is not running.

## Refresh and input

The dashboard connects to the engine only while its tab is visible. It follows
the engine's watch events, so changes appear as they happen and resource values
update once a second. It reads the catalog and settings again every 10 seconds, to
pick up files changed on disk. The tab is redrawn only when something on it
changes, at most ten times a second. It is drawn on the CPU and uploaded to a GLES
texture, which SteamVR copies when it is submitted; the dashboard opens a
windowless EGL context with the same `DISPLAY` and `EGL_PLATFORM` defaults as
`run-pocketspace.sh`. While the tab is hidden, the dashboard
closes its connection and checks SteamVR five times a second. When the tab is
shown again, it reloads everything. If the engine stops or restarts while the
tab is open, the tab shows the engine as offline and reconnects every second.

The tab gets pointer input from SteamVR's dashboard laser, and only while the tab
is open. The dashboard never captures controller input outside it.

## Limitations

- Text is ASCII, drawn with the engine's default monospace font (`SF_FONT`
  overrides it). Long names and errors are cut to fit.
- Alerts appear only while the tab is open, because the dashboard is not connected
  to the engine while the tab is hidden.
- The tab has no JavaScript evaluation or REPL; use [`pocketspace-cli`](cli.md).
