const asset = loadModel('assets/Avocado.glb');
const object = createObject(asset, 0, 0, 0, .55);
setObjectYaw(object, Math.PI);
