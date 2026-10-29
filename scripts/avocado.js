// The smallest scene: load one GLB, place it, and turn it toward the viewer.
const asset = loadModel('assets/Avocado.glb');
const object = createObject(asset, 0, 0, 0, .55);
// Models are centered/normalized on import; scale is the largest default-pose extent in meters.
setObjectYaw(object, Math.PI);
