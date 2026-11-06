# Demos

The demos are complete scenes to keep running beside your other VR apps. Each
combines several engine features; the [example scripts](examples.md) show one
feature at a time.

Run a demo from the repository root:

```sh
./run-pocketspace.sh --scripts-dir demos --launch clock.js
```

Demos show a controller's laser only while it points at something you can press
or grab. What a demo saves stays in its own save namespace; clear it with the
dashboard's **Reset** or `pocketspace-cli data reset <name>`.

## Desk clock and timer

[clock.js](../demos/clock.js) shows the time and date above a countdown timer.

| Control | Action |
| --- | --- |
| Trigger on **1**, **5**, **15** or **25** | Set the timer to that many minutes and stop it. |
| Trigger on **Start** / **Pause** | Run or pause the countdown. A timer at zero starts again from its duration. |
| Trigger on **Reset** | Stop the timer and return it to its duration. |
| Trigger on **24h** / **12h** | Switch the clock between 24-hour and 12-hour time. |
| Trigger or grip on the blue handle | Move the clock. It stands on the handle and turns to face you. |

At zero the panel flashes and both controllers buzz once a second for ten
seconds. A press on the panel during the alarm silences it without pressing the
button underneath.

**Saved:** the clock's position, the last duration and the clock format. A
running countdown is not saved.

**Shows:** retained-canvas drawing that redraws only when the display changes,
panel buttons with hover, haptics, a panel following a grabbed object, and
[saved data](scene-persistence.md#saved-data).
