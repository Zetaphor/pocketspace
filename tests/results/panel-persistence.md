# Panel scene persistence validation

Validated on Steam Frame, 2026-10-27. The user accepted the interactive headset
check for saved panel appearance, placement and pointer input after restoration.
New captures use v4; v1–v3 documents remain readable.

## Native API and rollback

```sh
cmake --build build --target scene-persistence-regression scene-container-regression lifecycle-script-regression panel-submission-regression examples-regression -j2
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/scene-persistence-regression
```

The real QuickJS fixture captures/restores all four placement modes, arbitrary
rotation, pixel/physical dimensions, visibility, interaction and font settings.
A 1024×512 panel includes text, an image, shapes and transparent drawing; another
uses per-pixel variation to exercise raw RGBA encoding. Native comparisons verify
independent pixel storage and exact restored bytes. RLE/raw encodings roundtrip
with the same raster content. Restored handles support canvas edits, presentation
and a real dispatched pointer callback matched against the fresh panel ID.
A fresh runtime can load panels and draw additional text without creating a
panel first. Inherited JavaScript setters do not run during handle construction.

Malformed dimensions, physical aspect ratios, rotations, modes, booleans, font
sizes, encodings, base64, pixel lengths and pixel runs reject restore. Snapshot
panel count, pixel budget and document/file size checks pass. Native overlay
failure after an earlier panel reservation releases both object and panel
reservations without changing IDs or scene membership. A later native physics
failure also releases all panel reservations and leaves the source body intact.
Repeated restoration/disposal and v3 object-only compatibility passed. Earlier
model, animation, physics and v1/v2 persistence coverage remains in the harness.

Existing container, script lifecycle and panel submission regressions passed,
including uploaded pixels, placement, cached content, visibility and error
retries. All 28 published examples and three rendering test scenes passed.
Documentation lint passed 14 documents, 69 globals and 28 examples.

## Live renderer and mixed scene

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/scene-persistence.py --panels --output /tmp/sf-panel-persistence-live
```

The bounded 19-second fixture restored/disposed a mixed tool three times. It
contains an animated kinematic model, an interactive world panel and a hidden
head-locked panel; a root box and HUD survive. Restores verify every saved panel
field and pixel encoding before editing the new canvas and moving it, plus
animation playback and the restored body type. Active phases contain two objects
and three panels; closed phases return to one object and one panel. Engine GPU
allocation estimates repeatedly return from 14,532,988 bytes active to 5,470,844
bytes closed. Grouped allocation returns to zero at shutdown and stays below its
budget. Sampled GL/native error checks pass.

The existing animation live fixture also passes with fixed paused/finished
states, advancing playback, and stable allocation baselines. Both runs use
temporary storage and stop their engines. These are correctness/resource checks,
not throughput benchmarks or interactive visual acceptance.

[Phase summary](panel-persistence/live-frame.json) and
[native/shutdown output](panel-persistence/validation-frame.txt).

For headset acceptance, run `scripts/panel-save.js`: click the panel, bumper-save,
use B/Y to change its text/colors and position, then A/X-restore. Check the saved
appearance and placement return and the restored panel still responds to clicks.
Repeat after saving different content.

## Headset acceptance

The user confirmed `scripts/panel-save.js` worked after the requested click,
save, change and restore checks. The demo then stopped cleanly after 5,249
frames. The final sampled GL error was zero and sampled FPS was 90.0. This
panel-only scene allocated no grouped model buffers; final and peak grouped
allocation were both zero. See the
[headset shutdown output](panel-persistence/headset-frame.txt).
