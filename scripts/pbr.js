// Lit material textures and directional lighting on a grabbable model.
const car = loadModel('assets/samples/ToyCar/ToyCar.glb', 768, {pbr: true});
setModelLighting(car, {direction: [-.4, .8, .5], intensity: 3, ambient: 0, environment: 'assets/environments/studio.sfenv'});
const object = createObject(car, 0, 0, 0, .35);
enableObjectInteraction(object, {grabButton: 'both'});
setObjectInteraction(object, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
setLaserVisible('left', true);
setLaserVisible('right', true);
function update() { pollInputEvents(); }
