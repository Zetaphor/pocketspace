# Scene persistence validation

Validated on Steam Frame with the production renderer, 2026-10-27.
No interactive headset acceptance has been recorded for this feature yet.

## Native checks

```sh
cmake --build build --target scene-persistence-regression scene-container-regression lifecycle-script-regression examples-regression -j2
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/scene-persistence-regression
```

The real QuickJS loader passed JSON and disk roundtrips, including a fresh
runtime and empty asset cache; complete transforms, visibility, interaction and
model loading options; ID/group remapping; owned-panel/physics/animated-pose
rejection; malformed versions, fields, IDs, scales and oversized data; cache
conflicts; missing assets after successful staging; injected second-overlay
reservation failure with first-overlay release; forty restore/dispose cycles;
bounded regular-file reads, symlink refusal, failed replacement cleanup and
continued loading of the original save. Assertion failures exit without aborting.

Existing container and script lifecycle regressions passed. The examples harness
passed all 25 published examples and its three rendering test scenes; its save
files use temporary storage. Documentation lint passed 14 documents and 69 globals.

## Live production rendering

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/scene-persistence.py --output /tmp/sf-persistence-live
```

The 19-second bounded run loaded/restored/disposed four grouped grabbable boxes
three times while retaining a root object, root HUD and root callback. Each
restore verified saved transform/interaction/model settings before modifying
positions. Every active phase contained five objects; each closed phase returned
to one. Panel count stayed one. The measured engine GPU allocation estimate
repeated at 4,218,092 bytes active and 2,120,940 bytes closed. These are engine
allocation estimates, not total device VRAM.

The harness passed sampled GL/native error checks and verified grouped buffer
accounting returned to zero at shutdown, with peak allocation below its budget.
It used a temporary state directory and stopped the engine afterward. This is
resource/lifecycle evidence, not a frame-rate benchmark or visual acceptance.

[Phase summary](scene-persistence/live-frame.json) and
[native/shutdown output](scene-persistence/validation-frame.txt).
Interactive follow-up: move boxes, bumper-save, move again, A/X-restore; verify
the saved arrangement returns and grip/trigger grabs still work after restoring.
