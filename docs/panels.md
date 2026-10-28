# Panels

`createPanel(options)` returns `{id, canvas, present}`. Options default to
`{widthPixels: 1024, heightPixels: 512, widthMeters: 1.2, interactive: false}`.
Optional `container` assigns the panel to an active [scene container](scene-containers.md);
omission or 0 keeps root-scene ownership. `setPanelContainer` transfers it later.
Pixel dimensions must be 1–4096; width must be greater than zero and at most
10 meters. Physical height follows the pixel aspect ratio.

The canvas retains its pixels until changed. Draw only when content changes,
then call `canvas.present()` or `panel.present()` to request a texture upload.
Coordinates are pixels with origin at the top left. Color channels are clamped
to 0–255.

| Canvas method | Arguments and behavior |
| --- | --- |
| `clear(r=0, g=0, b=0, a=255)` | Fill the canvas. |
| `text(x, y, text, r=255, g=255, b=255, a=255, pixels=32)` | Draw text; return font/draw success. |
| `rect(x, y, width, height, r, g, b, a, fill=false)` | Outline or filled rectangle. |
| `line(x1, y1, x2, y2, r, g, b, a)` | Draw a line. |
| `circle(x, y, radius, r, g, b, a, fill=false)` | Outline or filled circle. |
| `image(x, y, path)` | Decode/draw an image at native size; return success. |
| `present()` | Schedule upload of the retained canvas. |

`panelLoadFont(panel, path, pixels)` stores the chosen font path and size;
font loading occurs when text is drawn. Canvas text defaults to 32 pixels
unless supplied explicitly. The legacy `panelText` default uses the stored
font size. Font files must exist on the runtime host.

## Placement

`panelSetTransform(panel, mode, x, y, z)` supports:

| Mode | Position and orientation |
| --- | --- |
| `'world'` | Fixed relative to the scene anchor. |
| `'viewerFacing'` | Fixed position relative to the anchor, facing the HMD. |
| `'headLocked'` | HMD-relative position; positive Z places it forward. |
| `'arbitrary'` | Anchor-relative position and the explicit rotation below. |

`panelSetRotation(panel, m00, m01, m02, m10, m11, m12, m20, m21, m22)`
sets the row-major 3×3 rotation used by arbitrary placement.
`panelSetVisible(panel, visible)` changes visibility. `removePanel(panel)`
removes it; its native surface detaches before backing resources are freed.
These globals accept either the returned panel object or its numeric ID.

Unchanged panels retain their compositor submission. Uploaded content is submitted
once per update, placement is submitted when its computed transform changes,
and visibility is submitted on transitions. Head-locked and viewer-facing panels
continue following the headset. Content changed while hidden appears when the
panel is shown again. Failed submissions are retried automatically.

## Pointer events and compatibility

Interactive panels receive native SteamVR pointer events through
`onInputEvent` or `pollInputEvents`. Match `objectId === panel.id` and a
`panel.*` type. Hover events are `panel.hoverStart`, `panel.hover`, and
`panel.hoverEnd`. Buttons use `panel.pointerDown`/`panel.pointerUp`,
`control: 'left' | 'right' | 'other'`, and `down`. `x` and `y` are panel pixels.
The [panel example](../scripts/panels.js) redraws a button only after a click.

Compatibility globals remain available:
`panelClear(panel, r, g, b, a)`,
`panelText(panel, x, y, text, r, g, b, a, pixels)`, and
`panelPresent(panel)`. `panelCanvasClear` and `panelCanvasText` are aliases of
`panelClear` and `panelText`.

## Saving and restoring panels

Scene snapshots preserve owned panels, including their lossless retained pixels,
size, placement, rotation, visibility, input configuration and font settings.
`loadSceneContainer` / `restoreSceneContainer` return `panels` with normal canvas
handles, plus `panelIds` and `panelMap` for reconnecting script callbacks. See the
[complete v4 persistence contract](scene-persistence.md#panels-and-ui-pixels-v4)
for pixel encodings, file/decoded-memory limits and legacy save compatibility.
The [panel save example](../scripts/panel-save.js) demonstrates editing and
clicking a restored panel.
