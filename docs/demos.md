# Demos

The demos are complete scenes to keep running beside your other VR apps. Each
combines several engine features; the [example scripts](examples.md) show one
feature at a time.

`tools/install-user-service.sh` copies the demos into the script catalog as
`demos/`, so the dashboard's Library lists them as `demos/clock.js` and so on;
rerun it after pulling changes. See [SteamVR dashboard](dashboard.md). The
installed demos load their models and fonts from the checkout, where the engine
service runs. To run one directly from the repository root instead:

```sh
./run-pocketspace.sh --scripts-dir demos --launch clock.js
```

Saves follow the script's catalog path, so a demo run from `demos/` this way
keeps its saves apart from the installed `demos/clock.js`.

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

## Performance monitor

[monitor.js](../demos/monitor.js) is a compact panel with the engine's frame
rate, CPU use, resident RAM, driver-reported GPU memory and its GPU allocation
estimate, from [`getEngineStats`](javascript-api.md#engine-statistics). Each has
its current value and a line of the last 60 samples. Statistics are sampled about
once a second, and the panel redraws only when a new sample arrives.

| Control | Action |
| --- | --- |
| Trigger or grip on the blue handle | Move the monitor. It stands on the handle and turns to face you. |

FPS is drawn from zero to at least 90 and CPU from zero to at least 100%. Memory
lines span their own minimum and maximum, so small changes stay visible. A value
the system does not report, such as GPU memory without DRM fdinfo, shows `n/a`
and leaves a gap in its line. The statistics cover the whole engine, not only
this demo.

**Saved:** the monitor's position.

**Shows:** engine statistics, a retained canvas redrawn once per sample, a panel
following a grabbed object, and [saved data](scene-persistence.md#saved-data).

## Sketch board

[sketch.js](../demos/sketch.js) is a 0.8-meter whiteboard in front of you with a
toolbar along its top.

| Control | Action |
| --- | --- |
| Hold the trigger on the board | Draw. Each hand draws its own stroke, so both can draw at once. |
| Trigger on a colour swatch | Pick one of six colours. |
| Trigger on a brush dot | Pick a small, medium or large brush. |
| Trigger on **Undo** | Remove the last stroke, or bring back what the last Clear removed. |
| Trigger on **Clear** | Empty the board. |

The drawing is saved after every stroke and when Undo or Clear changes it, so it
is back on the next start, and Undo still works on it. Strokes are stored as
points in the demo's [saved data](scene-persistence.md#saved-data), whose 64 KiB
limit holds several thousand points. A stroke that reaches the limit ends there,
Undo and Clear turn red, and the board accepts no new strokes until one of them
makes room. Undo can bring back what a Clear removed only until the demo stops.

**Saved:** the strokes on the board. The chosen colour and brush are not saved.

**Shows:** panel hover and pointer events with pixel coordinates, two hands on one
panel, canvas circles and rectangles on a retained canvas, partial redraws of the
toolbar, and [saved data](scene-persistence.md#saved-data).

## Sticky notes

[notes.js](../demos/notes.js) hands out coloured notes to leave around you. Each
note hangs from a small yellow pin, and the dispenser stands on a blue handle.

| Control | Action |
| --- | --- |
| Trigger on a colour on the dispenser | Take a note of that colour; it appears above the dispenser. |
| Trigger or grip on a note's pin | Move the note. Notes turn to face you. |
| Trigger on a note | Cycle its label: To do, Done, Idea, Call, then the note's own number. |
| Drop a note's pin on the red bin | Remove the note. |
| Trigger or grip on the blue handle or the bin | Move the dispenser or the bin. |

Up to 32 notes can be out at once, the most one scene save holds; the dispenser
then asks you to bin some. Notes are numbered in the order they are taken, and
numbers are not reused.

**Saved:** the notes, with their positions, colours and labels, after every
change; the dispenser and bin positions; and the next note number. The notes are
a [scene save](scene-persistence.md) of their own container, restored with fresh
IDs; their labels, colours and numbers are [saved data](scene-persistence.md#saved-data)
that the demo maps onto the restored notes.

**Shows:** [scene containers](scene-containers.md), spawning and removing objects
and panels, grabbing, panels following objects, scene saves with ID remapping,
shared render groups for the pins, and saved data.
