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
enableObjectInteraction(object); // dedicated hit region follows this object
const pointer = getPointer(object); // { valid, down, x, y, hand }
```

Coordinates are meters relative to the fixed anchor: +x right, +y up, +z
farther into the scene. Each GLB is centered and normalized to unit maximum
dimension; scale is its maximum rendered dimension in meters. A scene supports
up to 32 loaded assets and 256 objects.

`loadModel(path, eyePixels)` sets the per-eye render resolution for every
interactive surface using that asset. The default is 768 pixels; accepted
values are multiples of 64 from 256 to 2048. Loading the same path again at a
different resolution is an error. A shared surface for noninteractive objects
uses the highest requested resolution among those models. Three 2048-pixel
interactive surfaces caused headset lag and ghosting at about 65 engine fps.
At 768 pixels the three-object demo held about 90 fps and looked clear enough
in the headset; 512 pixels was too blurry. Resolution is a quality and GPU
memory tradeoff, so raise it only for models that need it.

`getController("left"|"right")` returns a tracked controller position relative
to the same anchor as objects. Invalid tracking returns `valid: false`.
`enableObjectInteraction(object)` gives that object its own compact surface
and circular laser hit region. Up to four objects can be interactive at once.
`getPointer(object)` reports its primary-button state and the left or right
controller that pressed it. The one-object form `getPointer()` remains valid
when only one object is interactive. The grab demos preserve each object's
position relative to the selecting controller in all six pose dimensions while
held; its surface follows it. The gaps between surfaces leave Frametop
reachable. Ordinary rendering scripts use a shared 0.7 m surface with laser
input disabled. The three-object demo is headset verified with both hands.
Precise mesh hit testing and independent simultaneous grabs remain future
work. Direct SteamVR Input actions were inactive for this overlay during the
first headset test.

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
