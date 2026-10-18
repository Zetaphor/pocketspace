# Steam Frame AR-in-VR overlay engine

`src/main.cpp` is the sole active implementation. It combines the headset-verified
world-anchored stereo OpenVR renderer with embedded QuickJS and loads a GLB model at
runtime. The first model is `assets/Avocado.glb`, tracked in this repository. An
object API, multiple model instances, and controller actions are the next milestones.

The host remains a `VRApplication_Overlay`. It uses one standing-space absolute
overlay, side-by-side eye images, and three imported GPU DMA-BUFs. It runs until
Ctrl-C or SIGTERM. The first model is normalized to a one-meter maximum dimension
and centered; the physical overlay is 0.7 meters wide.

Build on Deckard:

```sh
cmake -S . -B build
cmake --build build -j2
```

Run:

```sh
./run-overlay-engine.sh
```

Pass a JS path as the first argument and a GLB path as the second. The default
`scripts/avocado.js` is polled for changes about three times per second.
`update(dt)` runs at the host's 90 Hz target. Scripts can call `print(message)`,
`setModelYaw(radians)`, and `setModelVisible(boolean)`. On a script load error the
last working script remains active. The existing object remains visible if an
update callback fails.

The current loader supports the first mesh's first indexed triangle primitive,
`POSITION`, `TEXCOORD_0`, and an embedded base-color image. Broader glTF materials,
multiple primitives, animation, and external images are future loader work.

Headset acceptance: the avocado is upright, stereo converged, continuously visible,
and stable relative to Frametop while rotating and leaning. Test with a primary VR
application open as well. Logs and build success cannot substitute for this check.

`third_party/cgltf.h` is from [cgltf](https://github.com/jkuhlmann/cgltf) at commit
`85cd62382dfea638278962690cf515023f33ed00`; see `third_party/cgltf-LICENSE`.
