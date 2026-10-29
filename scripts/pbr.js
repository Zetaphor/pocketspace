// Light a grabbable car with PBR materials and a baked .psenv environment.
// Uses opt-in cache optimization and automatic LOD; rotate the car to see reflections.
// Cache reordering is opt-in because coplanar overlapping triangles can depend on draw order.
const car = loadModel('assets/samples/ToyCar/ToyCar.glb', 768, {pbr: true, optimizeVertexCache: true});
// Lighting is shared by every object loaded from this asset; the environment does not draw a skybox.
setModelLighting(car, {direction: [-.4, .8, .5], intensity: 3, ambient: 0, environment: 'assets/environments/studio.psenv'});
const object = createObject(car, 0, 0, 0, .35);
enableObjectInteraction(object, {grabButton: 'both'});
setObjectInteraction(object, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
setLaserVisible('left', true);
setLaserVisible('right', true);
function update() { pollInputEvents(); }
