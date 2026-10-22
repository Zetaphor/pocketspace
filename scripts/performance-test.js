// Fixed Toy Car workload at the established 768-pixel eye resolution.
const asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768);
const object = createObject(asset, 0, 0, 0, .4);
setObjectYaw(object, .5);
setObjectInteraction(object, {grabbable: true});
setLaserVisible('left', true);
setLaserVisible('right', true);
function update() { pollInputEvents(); }
