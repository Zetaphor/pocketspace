// Paired with multi-script-left.js: local IDs and top-level names must not collide.
const asset = loadModel('assets/samples/Box/Box.glb');
const object = createObject(asset, 0.25, 0, 0, 0.2);
const panel = createPanel({widthPixels: 64, heightPixels: 64, widthMeters: 0.1, interactive: false});
if (object !== 1 || panel.id !== 1) throw Error('right fixture expected local IDs 1');
onInputEvent(() => {});
