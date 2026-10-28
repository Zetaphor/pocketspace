# Animation scene persistence validation

Validated on Steam Frame, 2026-10-27. The user accepted the interactive headset
check for saved positions, animation poses, playback states and restored grabbing.
New captures use v3; v1 static and v2 physics documents remain readable.

## Native checks

```sh
cmake --build build --target scene-persistence-regression animation-regression scene-container-regression lifecycle-script-regression examples-regression -j2
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/scene-persistence-regression
```

The real QuickJS fixture roundtrips playing, paused, finished and stopped states,
selected clips, speed, time and looping for skeletal and rigid-node animations.
It checks playback alongside a kinematic body, v1/v2 compatibility, repeated
restore/disposal and a fresh runtime loading a disk save. Restored instances have
separate CPU poses with identical expanded vertex streams, bounds and radius;
stopped instances have no animation pose allocation.

Invalid indices, clip metadata, times, speeds, booleans and finished states
reject restore. Unsupported morph clips and non-null animation in older formats
also reject. Model cache, object/container counters and existing objects survive
failures, including an overlay reservation failure after poses were evaluated.
Twenty repeated animated restore/dispose cycles preserve the original containers.
The harness also retains the complete earlier model/physics persistence coverage.

Existing animation integration (including 5,400 picking comparisons), container
and script lifecycle regressions passed. All 27 published examples and three
rendering test scenes passed. Documentation lint passed 14 documents, 69 globals
and 27 examples. Native assertion failures exit without aborting.

## Live rendering and playback

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/scene-persistence.py --animation --output /tmp/sf-animation-persistence-live
```

The bounded 19-second fixture restored and disposed three animated objects three
times while retaining a root box and HUD. It verified exact saved playback state
immediately after loading, that playing skin animation advances afterward, and
that paused and finished rigid-node animations remain fixed. Active phases
contained four objects; closed phases returned to one. Panel count stayed one.
Engine GPU allocation estimates repeated at 39,721,012 bytes active and
5,873,060 bytes closed. Grouped allocation returned to zero at shutdown, with
a 41,943,040-byte peak against the 536,870,912-byte budget. Sampled GL/native error
checks passed.

The physics live fixture also passed on the v3 engine: five saved physics objects
and two joints repeatedly returned to the root floor, with engine GPU allocation
estimates of 58,884,996 bytes active and 6,456,196 bytes closed. Grouped allocation
returned to zero at shutdown. Both fixtures used temporary storage and stopped
their engines. These are correctness/lifecycle checks, not throughput benchmarks
or interactive visual acceptance.

[Phase summary](animation-persistence/live-frame.json) and
[native/shutdown output](animation-persistence/validation-frame.txt).

For headset acceptance, run `scripts/animation-save.js`: bumper-save, use B/Y to
change playing/paused states and move the models, then A/X-restore. Check the
saved positions/poses and playback states return, then grab restored models.
Repeat a save/restore with different states.

## Headset acceptance

The user confirmed `scripts/animation-save.js` worked after the requested
save/restore, playback toggle and grabbing checks. The demo then stopped cleanly
after 6,473 frames. The final sampled GL error was zero and sampled FPS was 89.9.
Grouped buffer allocation returned to zero at shutdown, with a 312,475,648-byte
peak against the 536,870,912-byte budget. See the
[headset shutdown output](animation-persistence/headset-frame.txt).
