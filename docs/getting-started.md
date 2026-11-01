# Getting started

Run an example, then edit its JavaScript to make it your own. The engine targets
Steam Frame on Linux and runs alongside your existing SteamVR application.

## Build the engine

Requires CMake 3.20 or newer, a C++20 compiler, OpenVR headers and runtime, QuickJS-ng's static
library, FreeType, EGL, GLESv2, GBM, and libdrm development files. The default
library paths target Steam Frame's ARM64 Linux environment. Override `SDK_INCLUDE`,
`QJS_INCLUDE`, `QJS_LIB`, and `STEAMVR_BIN` through CMake for another installation.

```sh
cmake -S . -B build
cmake --build build -j2
```

The first CMake configuration downloads pinned Jolt Physics and meshoptimizer
source archives. See [physics builds](physics-api.md#building) for offline
Jolt configuration; use `-DFETCHCONTENT_SOURCE_DIR_MESHOPTIMIZER=/path/to/meshoptimizer`
for an offline meshoptimizer 1.0 checkout at commit
`73583c335e541c139821d0de2bf5f12960a04941`.

## Run an example

Start SteamVR, then run the grabbing example from the repository's example
catalog:

```sh
./run-pocketspace.sh --scripts-dir scripts --launch grabbing.js
```

The box can be grabbed with either controller using grip or trigger. The wrapper
sets the runtime library path and X11/EGL environment, then runs from the
repository root. Running `./run-pocketspace.sh` starts the engine with no user
scripts. Stop with Ctrl-C or SIGTERM. Script edits reload automatically; a
failed reload preserves the existing scene.

The default catalog is `$HOME/.local/share/pocketspace/scripts`. Use `--launch`
more than once to start several scripts from that directory. Names may include
subdirectories; paths that leave the catalog are rejected.

```sh
./run-pocketspace.sh --launch clock.js --launch tools/notes.js
```

Inspect a model without opening VR:

```sh
./run-pocketspace.sh --inspect assets/samples/Box/Box.glb
```

## Existing installations

The executable is `build/pocketspace` and the launcher is `run-pocketspace.sh`.
The previous `run-overlay-engine.sh` launcher, `sf-overlay-engine` build target
and `build/sf-overlay-engine` executable alias remain supported. Reconfigure and
rebuild an existing checkout to generate the new executable and alias.

Existing named scene saves retain their storage directory and format identifier;
see [scene persistence](scene-persistence.md). SteamVR's application identity is
also retained so existing controller bindings keep working.

## Create your first scene

Save this as `$HOME/.local/share/pocketspace/scripts/my-scene.js`:

```js
const model = loadModel('assets/Avocado.glb');
const object = createObject(model, 0, 0, 0, .4);
enableObjectInteraction(object, {grabButton: 'both'});
setLaserVisible('left', true);
setLaserVisible('right', true);
function update() { pollInputEvents(); }
```

Run it with `./run-pocketspace.sh --launch my-scene.js`. Use either controller's
grip or trigger to grab the model. Edit the file while it runs to reload the scene.
You can start with any of the [example scripts](examples.md) instead.

## Where to go next

- [Examples](examples.md): working scenes for each capability.
- [JavaScript API](javascript-api.md): models, placement, interaction and input.
- [Panels and UI](panels.md), [physics](physics-api.md) and
  [animation](animation-api.md): add behavior to your scene.
- [Lighting](lighting-api.md) and [tracking](tracking-api.md): add material
  lighting, environment reflections and tracking-aware behavior.
- [Architecture](architecture.md): engine internals and rendering policies.

## Third-party licenses

This project is licensed under the [MIT License](../LICENSE).
Third-party code and sample assets retain their respective licenses.
MikkTSpace by Morten S. Mikkelsen supplies tangent generation; its license is
retained in [the upstream source](../third_party/mikktspace.h).
Sample sources and licenses are listed in [asset attribution](../assets/samples/ATTRIBUTION.md).
The glTF loader uses [cgltf](https://github.com/jkuhlmann/cgltf), vendored at
commit `85cd62382dfea638278962690cf515023f33ed00`; see
[its license](../third_party/cgltf-LICENSE).
Optional GPU triangle reordering uses [meshoptimizer](https://github.com/zeux/meshoptimizer)
1.0; see [its license](../third_party/meshoptimizer-LICENSE).
