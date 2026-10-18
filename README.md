# Steam Frame AR-in-VR overlay engine

This is the sole active implementation. A SteamVR overlay remains beside the
primary VR experience. Standing-space world anchors hold side-by-side
stereo images, each rendered through three GPU DMA-BUFs. QuickJS scripts place and
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
./run-overlay-engine.sh scripts/grab-object.js
./run-overlay-engine.sh scripts/three-object-grab.js
./run-overlay-engine.sh scripts/full-sphere-gallery.js
./run-overlay-engine.sh scripts/dual-laser-test.js
./run-overlay-engine.sh scripts/native-grab-demo.js
```

Stop with Ctrl-C or SIGTERM. The script is checked for changes about three
times per second. A successful reload replaces the previous scene; a failed
reload leaves it visible. `update(dt)` runs at the 90 Hz host target.

The script API is:

```js
const asset = loadModel("assets/samples/Box/Box.glb", 768); // eye pixels, optional
const object = createObject(asset, x, y, z, scaleMeters);
setObjectPosition(object, x, y, z);
setObjectYaw(object, radians);
setObjectScale(object, scaleMeters);
setObjectVisible(object, true);
removeObject(object);
print("message");
const right = getController("right"); // { valid, x, y, z, rotation[9] }
enableObjectInteraction(object); // grabbable by the SteamVR laser
setObjectInteraction(object, {laserPassThrough: true, grabbable: false});
setObjectInteraction(object, {
  grabbable: true, directGrab: true, laserYaw: true, depthAdjust: true,
  handRotationAxes: {pitch: true, yaw: true, roll: true}
});
setLaserVisible("left", true); // optional visual beam, also supports "right"
const pointer = getPointer(object); // { valid, down, x, y, hand }
```

Coordinates are meters relative to the fixed anchor: +x right, +y up, +z
farther into the scene. Each GLB is centered and normalized to unit maximum
dimension; scale is its maximum rendered dimension in meters. A scene supports
up to 32 loaded assets and 256 objects.

`loadModel(path, eyePixels)` sets the per-eye render resolution for each
object surface using that asset. The default is 768 pixels; accepted
values are multiples of 64 from 256 to 2048. Loading the same path again at a
different resolution is an error. Three 2048-pixel
interactive surfaces caused headset lag and ghosting at about 65 engine fps.
At 768 pixels the three-object demo held about 90 fps and looked clear enough
in the headset; 512 pixels was too blurry. Resolution is a quality and GPU
memory tradeoff, so raise it only for models that need it.

`getController("left"|"right")` returns a tracked controller position relative
to the same anchor as objects. Invalid tracking returns `valid: false`.
Every visible object gets a compact surface that follows it throughout the
full sphere. Surfaces turn toward the headset while their objects stay at
fixed world positions. By default, the SteamVR laser passes through them.
`setObjectInteraction(object, {laserPassThrough, grabbable})` changes that
behavior. A grabbable object always blocks the laser. An object can also block
the laser without being grabbable. `enableObjectInteraction(object)` is the
short form for a grabbable object. `enableObjectSurface(object)` remains a
compatibility no-op. The previous four-surface limit is gone; GPU memory and
SteamVR overlay capacity remain practical limits for large scenes.
`setLaserVisible(hand, visible)` draws a noninteractive beam from either
controller using the Frame controller's aim transform. Both can be shown at
once. These beams are visual only; SteamVR still owns pointer events.
`getPointer(object)` reports its primary-button state and the left or right
controller that pressed it. The one-object form `getPointer()` remains valid
when only one object is interactive. The grab demos preserve each object's
position relative to the selecting controller in all six pose dimensions while
held; its surface follows it. The gaps between surfaces leave Frametop
reachable. Ordinary rendering scripts use a shared 0.7 m surface with laser
input disabled. The three-object demo is headset verified with both hands.
The full-sphere gallery places objects in front, to
the side, and behind the starting viewpoint; all three remained visible, and
an object dragged behind the user remained visible and grabbable in headset
testing.
The native grab demo uses the active SteamVR laser's trigger to pick up an
object. Moving the controller moves the object with it. While holding an
object at a distance, thumbstick left/right rotates its yaw and thumbstick
up/down moves it farther/closer; up means farther. If the object is close to
the controller when selected, `directGrab` holds it at the hand, and enabled
`handRotationAxes` follow the hand's pitch, yaw, and roll. These controls were
verified in the headset. SteamVR's grip click changes the active laser and
releases a native pointer grab, so it cannot currently act as a modifier.
`laserPitchWithStickClick` is experimental: stick clicks reach the engine,
but the user did not see pitch rotation. Leave it off in normal demos.
The optional `getInput(hand)` action bindings cover the Steam Frame controls,
but action data was inactive in the overlay input probes with the native
laser. The engine polls them only when a script requests them. Precise mesh
hit testing and simultaneous independent grabs are still unverified.
See `docs/interaction-status.md` for the detailed verified state and next steps.

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
