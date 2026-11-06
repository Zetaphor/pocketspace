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

The tab's dashboard icon is `assets/branding/shortcut_icon_512x512.png`.

### Steam library entry

`tools/install-steam-shortcut.py` adds **Pocketspace** to the Steam library as a VR
shortcut, with the capsule, header, hero, logo and icon from `assets/branding`:

```sh
python3 tools/install-steam-shortcut.py            # add, or update the art and launch settings
python3 tools/install-steam-shortcut.py --remove   # remove it
```

It works through Steam's own client API while Steam runs, so the entry appears
at once without restarting Steam; it needs Steam's CEF debugger on
`127.0.0.1:8080`, which SteamOS enables. It adds the shortcut to the signed-in
account. Pressing Play runs `pocketspace-dashboard --show`: if the dashboard is
already running, that opens its tab and exits; otherwise it starts the dashboard
and opens the tab. The dashboard registers no SteamVR application manifest, so
this shortcut is its only library entry.

The dashboard attaches only to a SteamVR session that is already running. It never
starts SteamVR: the unit uses `Requisite=` rather than `Requires=` or `BindsTo=`, and the
process itself connects as a background application. Without a running session it
exits with code 3. If another dashboard already owns the tab, it exits with code 4.
The service does not restart it after either exit. When SteamVR stops, the
dashboard stops too, and it starts again the next time SteamVR starts.

## The tab

A rail on the left of the tab opens four pages: **Space**, **Library**, **Alerts**
and **Settings**. The engine's power control sits at the foot of the rail.

While the engine is offline, only Space is available. It offers **Start engine**, as
does the power control. This runs `systemctl --user start pocketspace.service`, just
as [`pocketspace-cli start-engine`](cli.md) does. The tab shows an error if
`systemctl` fails, or if the engine has not answered within 30 seconds. When the
engine stops, the tab returns to Space.

While the engine runs, the power control stops it with
`systemctl --user stop pocketspace.service`, which stops every script. The first
press only arms it: it turns red and reads **Press again** for three seconds. If
the engine still answers 10 seconds after the second press, the tab reports that it
did not stop. This happens when the engine was started outside the service, for
example with `run-pocketspace.sh`.

| Page | Shows and does |
| --- | --- |
| Space | The engine's uptime, FPS, CPU and RAM. A card for each running or failed script, three at a time, with its name, uptime, generation, JavaScript heap, callback time per second, object count and estimated GPU allocations. If a reload failed and the old scene was kept, the card says so. A failed script shows its last error. Running scripts have **Reload** and **Stop**. A failed script waiting to restart has **Stop**, which cancels the restart. A failed script that is not restarting has **Start again**. Below the cards, **Quick launch** starts up to eight scripts that are not running, autostart scripts first. |
| Library | Every script the engine can start, nine to a page. **Launch** starts one; it reads **Running** while that script runs. The **Autostart** switch adds the script to, or removes it from, the [autostart list](lifecycle.md#startup-and-recovery). **Reset** deletes the script's [saves](scene-persistence.md#storage-namespaces), with [`data.reset`](control-protocol.md#resetting-saved-data); a running script reloads from its defaults. The first press only arms it: it turns red and reads **Press again** for three seconds. The tab then reports how many files were removed. |
| Alerts | Scripts that stopped on an error, with the error. **Dismiss all** clears them. |
| Settings | **Restart failed scripts** (`autoRestart`) and **Start the engine with SteamVR** (`engineAutostart`). **Reset** clears the scratch scene (instance `0`) that the CLI REPL runs in. If the settings file is invalid, its error appears here. |

Stopped scripts are not shown on Space; start them again from Quick launch or the
Library. Process totals come from the [resources](control-protocol.md#resources)
sample and are never divided among scripts.

Stop and reload requests carry the generation the tab shows. If the script has
reloaded since then, the request fails. The tab reports the failure at the bottom
and refreshes the list, so the next click targets the current scene. A button
shows `...` until its reply arrives.

When a script stops because of a callback error, it is added to Alerts, and the
rail shows the number of alerts. Repeated failures of the same script, such as
those of a restart loop, update its one alert and count them. The SteamVR
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

- Text is drawn with the Montserrat fonts in `assets/fonts/montserrat`, read from
  the checkout the dashboard runs from. Long names and errors are cut to fit.
- Alerts appear only while the tab is open, because the dashboard is not connected
  to the engine while the tab is hidden.
- The tab has no JavaScript evaluation or REPL; use [`pocketspace-cli`](cli.md).
