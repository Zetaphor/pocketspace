# Physics scene persistence validation

Validated on Steam Frame, 2026-10-27. Interactive headset acceptance is pending.
The production executable supports v2 physics snapshots and v1 static saves.

## Native API and rollback

```sh
cmake --build build --target scene-persistence-regression -j2
LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 ./build/scene-persistence-regression
```

The real QuickJS fixture covers all four collider shapes and all three body
motion types, complete body settings and dynamic velocities, all three joint
types, all hinge motor modes, updated distance springs/limits, collision filters,
break thresholds, and fresh object/joint IDs. A hinge saved away from its zero
angle retains that angle on restore. Original construction poses, settings and
motor parameters roundtrip; removed joints stay absent. Held bodies restore
unheld. Cross-container joints reject capture instead of losing a connection.

Invalid body, joint, motor and spring settings reject restoration. A failure
after new mesh/convex shapes and earlier joints were created rolls back bodies,
joints, shape caches, overlays, model cache additions and public ID counters.
Existing resources survive. Repeated restore/dispose, a fresh runtime loading
physics from disk, and v1 static compatibility passed. Native solver stepping
also passed after a fresh physics load.

The existing container, script lifecycle, examples, physics API, native physics,
joint and collision regressions passed. Examples include all 26 published scripts
plus the three rendering test scenes. Documentation lint passed 14 documents,
69 globals and 26 examples. Regression assertion failures exit without aborting.

## Live renderer and simulation

```sh
DISPLAY=:0 EGL_PLATFORM=x11 LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 \
  python3 tests/scene-persistence.py --physics --output /tmp/sf-physics-persistence-live
```

The 19-second fixture restored and disposed five physics objects and two joints
three times, retaining a root floor and HUD. It verified saved body transforms,
settings and velocities immediately after each restore, continued body motion
after simulation resumed, motor mode, live joint counts and removal. Active
phases held six objects and two joints; closed phases returned to the single
root object and no saved joints. Panel count remained one. Engine GPU allocation
estimates repeatedly returned from 58,884,996 bytes active to 6,456,196 bytes
closed. Grouped allocation returned to zero at shutdown, with a 60,817,408-byte
peak against the 536,870,912-byte budget. Sampled GL/native error checks passed.

The existing static live fixture also passed on the new engine. Its active and
closed GPU estimates remained 4,218,092 and 2,120,940 bytes respectively, and
shutdown released all grouped allocation. Both tests used temporary save
storage and stopped their engines. These are lifecycle/correctness checks,
not throughput benchmarks or visual acceptance.

[Phase summary](physics-persistence/live-frame.json) and
[native/shutdown output](physics-persistence/validation-frame.txt).

For headset acceptance, run `scripts/physics-save.js`: move the green hinged
panel and yellow spring ball, throw the blue box, bumper-save, change the
arrangement, then A/X-restore. Check attachments, motor behavior, resumed motion,
and grabbing restored objects. Repeat a save/restore cycle.
