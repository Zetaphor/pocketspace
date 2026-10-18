# Steam Frame AR-in-VR overlay engine

The single active implementation is `src/main.cpp`. It combines the headset-verified
world-anchored stereo OpenVR renderer with the embedded QuickJS host. It is an
integration baseline: the avocado mesh and texture are still compiled/extracted
assets, and the JavaScript API currently controls yaw and visibility. Runtime GLB
loading, arbitrary objects, and controller actions are the next milestones.

The host stays a `VRApplication_Overlay` and uses one standing-space absolute overlay,
side-by-side eye images, and three imported GPU DMA-BUFs. It runs until Ctrl-C or SIGTERM.

Build on Deckard:

```sh
cmake -S . -B build
cmake --build build -j2
```

Run:

```sh
./run-overlay-engine.sh
```

Pass a different JS path as the first argument. The default `scripts/avocado.js` is
polled for changes about three times per second. `update(dt)` runs at the host's
90 Hz target. A script can call `print(message)`, `setModelYaw(radians)`, and
`setModelVisible(boolean)`. On a script load error the last working script remains
active. The existing object remains visible if an update callback fails.

Headset acceptance: avocado upright, stereo converged, continuously visible, and
stable relative to Frametop while rotating and leaning. Test with a primary VR
application open as well. Logs and build success cannot substitute for this check.
