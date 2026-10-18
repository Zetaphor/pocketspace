# Steam Frame AR-in-VR overlay engine

This is the sole active implementation. A SteamVR overlay remains beside the
primary VR experience. One standing-space world anchor holds a side-by-side
stereo image, rendered through three GPU DMA-BUFs. QuickJS scripts place and
animate objects. The headset-verified avocado and three-object demos remain
available; the curated Khronos samples exercise distinct glTF features.

Build on Deckard:

```sh
cmake -S . -B build
cmake --build build -j2
```

Run the default avocado or a sample gallery:

```sh
./run-overlay-engine.sh
./run-overlay-engine.sh scripts/sample-gallery.js
./run-overlay-engine.sh scripts/feature-gallery.js
./run-overlay-engine.sh scripts/pointer-place.js
```

Stop with Ctrl-C or SIGTERM. The script is checked for changes about three
times per second. A successful reload replaces the previous scene; a failed
reload leaves it visible. `update(dt)` runs at the 90 Hz host target.

The script API is:

```js
const asset = loadModel("assets/samples/Box/Box.glb");
const object = createObject(asset, x, y, z, scaleMeters);
setObjectPosition(object, x, y, z);
setObjectYaw(object, radians);
setObjectScale(object, scaleMeters);
setObjectVisible(object, true);
removeObject(object);
print("message");
const right = getController("right"); // { valid, x, y, z }
enablePointerInput(); // opt this script into overlay laser events
const pointer = getPointer(); // { valid, down, x, y }
```

Coordinates are meters relative to the fixed anchor: +x right, +y up, +z
farther into the scene. Each GLB is centered and normalized to unit maximum
dimension; scale is its maximum rendered dimension in meters. A scene supports
up to 32 loaded assets and 256 objects.

`getController("left"|"right")` returns a tracked controller position relative
to the same anchor as objects. Invalid tracking returns `valid: false`.
`enablePointerInput()` opts a script into SteamVR overlay pointer events;
`getPointer()` reports pointer position and primary-button state. The pointer
placement demo moves a car while the overlay laser trigger is held and anchors
it on release. Pointer scripts use a 1.8 m wide surface at 0.8 m; ordinary
rendering scripts retain the 0.7 m surface at 1 m and leave laser input
disabled. A single finite surface can still clip an object at its boundary,
especially when the viewer leans. Tiled or adaptive surfaces are future work
for room-wide placement. Direct SteamVR Input actions were inactive for this
overlay during the first headset test; a reliable action and coexistence policy
remain future input work.

The loader handles GLB triangle primitives across all mesh nodes, node
transforms, embedded base-color textures, material factors, alpha modes,
texture UV transforms, and a basic sheen tint. The sheen tint is an
approximation; full PBR lighting, skinning, morph animation, and glTF animation
playback are future milestones. The current feature gallery shows the default
pose for animated assets. `build/sf-overlay-engine --inspect path.glb` checks
an asset without opening VR.

The sample models, upstream references, and licenses are listed in
`assets/samples/ATTRIBUTION.md`. `third_party/cgltf.h` is from
[cgltf](https://github.com/jkuhlmann/cgltf) commit
`85cd62382dfea638278962690cf515023f33ed00`; see
`third_party/cgltf-LICENSE`.
