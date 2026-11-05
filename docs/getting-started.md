# Getting started

Run an example, then edit its JavaScript to make it your own. The engine targets
Steam Frame on Linux and runs alongside your existing SteamVR application.

## Build the engine

Requires CMake 3.20 or newer, a C++20 compiler, OpenVR headers and runtime, QuickJS-ng's static
library, FreeType, EGL, GLESv2, GBM, libdrm, and libedit development files. The default
library paths target Steam Frame's ARM64 Linux environment. Override `SDK_INCLUDE`,
`QJS_INCLUDE`, `QJS_LIB`, and `STEAMVR_BIN` through CMake for another installation.

```sh
cmake -S . -B build
cmake --build build -j1
```

Build with one job on Steam Frame: parallel compiles alongside SteamVR have
destabilized the headset. A workstation build can use more jobs.

The first CMake configuration downloads pinned Jolt Physics and meshoptimizer
source archives. See [physics builds](physics-api.md#building) for offline
Jolt configuration; use `-DFETCHCONTENT_SOURCE_DIR_MESHOPTIMIZER=/path/to/meshoptimizer`
for an offline meshoptimizer 1.0 checkout at commit
`73583c335e541c139821d0de2bf5f12960a04941`.

When `ccache` (4.8 or newer) is on `PATH`, CMake uses it automatically. A clean
rebuild of an existing build directory then reuses earlier compiler output; a new
directory reuses Pocketspace's own objects but still compiles Jolt. Pass
`-DCMAKE_CXX_COMPILER_LAUNCHER=` (empty) to build without it.

### ccache on SteamOS

SteamOS's system partition is read-only and `pacman` needs root even to download,
but ccache can live in your home directory. The commands below fetch the `ccache`
package and the `hiredis` library it needs from the system's configured package
mirror, check both against the checksums in pacman's own database, and install
them under `~/.local`, which SteamOS updates leave alone. The mirror URLs that
`pacman -Sp` prints are private to the device; do not share them.

```sh
prefix=$HOME/.local
rm -rf ~/.cache/ccache-pkg && mkdir -p ~/.cache/ccache-pkg && cd ~/.cache/ccache-pkg
for url in $(pacman -Sp ccache); do curl -fLO "$url"; done
db="$(pacman-conf DBPath)/sync/extra.db"
for pkg in ccache-*.pkg.tar.zst hiredis-*.pkg.tar.zst; do
  want=$(tar -xOf "$db" "${pkg%-*.pkg.tar.zst}/desc" | awk '/%SHA256SUM%/{getline; print}')
  [ -n "$want" ] && [ "$want" = "$(sha256sum "$pkg" | cut -d' ' -f1)" ] && echo "ok $pkg" || echo "MISMATCH $pkg"
done
```

Continue only if both lines say `ok`:

```sh
mkdir -p "$prefix/bin" "$prefix/lib"
tar -xOf ccache-*.pkg.tar.zst usr/bin/ccache > "$prefix/bin/ccache" && chmod 755 "$prefix/bin/ccache"
tar -C "$prefix/lib" --strip-components=2 -xf hiredis-*.pkg.tar.zst --wildcards 'usr/lib/libhiredis.so*'
patchelf --set-rpath '$ORIGIN/../lib' "$prefix/bin/ccache"
"$prefix/bin/ccache" --version
```

`~/.local/bin` must be on `PATH` (it is in the default SteamOS shell). Configure
build directories again afterwards so CMake finds ccache. `pacman -Sp` lists only
packages the system lacks, so it fetches `hiredis` until the system ships it. To
upgrade after a SteamOS update, repeat both steps. No ccache configuration file is
needed; CMake passes its settings on the command line.

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

To start scripts every time the engine starts, list them in
`$HOME/.config/pocketspace/settings.json` (or the file given with `--settings`):

```json
{
  "version": 1,
  "autostart": ["clock.js", "tools/notes.js"],
  "autoRestart": false,
  "engineAutostart": false
}
```

See [startup and recovery](lifecycle.md#startup-and-recovery) for how autostart,
automatic restart and invalid settings behave. To run the engine as a systemd user
service, optionally starting with SteamVR, see
[running the engine as a service](control-protocol.md#running-the-engine-as-a-service).

Inspect a model without opening VR:

```sh
./run-pocketspace.sh --inspect assets/samples/Box/Box.glb
```

Manage a running engine from a terminal with `build/pocketspace-cli`: start and
stop scripts, watch for errors, or try JavaScript live in the scratch scene with
`build/pocketspace-cli repl`. See [command line and REPL](cli.md).

## Existing installations

The executable is `build/pocketspace`, the launcher is `run-pocketspace.sh`, and
the command-line client is `build/pocketspace-cli`.
Named scene saves use a
[separate namespace per script](scene-persistence.md#storage-namespaces).

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

This project is licensed under the [GNU Affero General Public License v3.0 only](../LICENSE).
Third-party code and sample assets retain their respective licenses.
MikkTSpace by Morten S. Mikkelsen supplies tangent generation; its license is
retained in [the upstream source](../third_party/mikktspace.h).
Sample sources and licenses are listed in [asset attribution](../assets/samples/ATTRIBUTION.md).
The glTF loader uses [cgltf](https://github.com/jkuhlmann/cgltf), vendored at
commit `85cd62382dfea638278962690cf515023f33ed00`; see
[its license](../third_party/cgltf-LICENSE).
Optional GPU triangle reordering uses [meshoptimizer](https://github.com/zeux/meshoptimizer)
1.0; see [its license](../third_party/meshoptimizer-LICENSE).
