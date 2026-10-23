const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.63, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
const weights = [
  {x: -.22, anchor: 'assets/physics/GreenBox.glb', weight: 'assets/physics/YellowSphere.glb', shape: 'sphere', damping: .15},
  {x: .22, anchor: 'assets/samples/Box/Box.glb', weight: 'assets/physics/BlueBox.glb', shape: 'box', damping: 1}
].map(({x, anchor, weight, shape, damping}) => {
  const top = createObject(loadModel(anchor), x, .3, 0, .06);
  setObjectPhysics(top, {type: 'static'});
  const id = createObject(loadModel(weight), x, -.15, 0, .12);
  setObjectPhysics(id, {shape, mass: .25});
  createPhysicsJoint(top, id, {type: 'distance', minDistance: .3, maxDistance: .3, spring: {frequency: 2, damping}});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
  return {id, x};
});
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down || event.control !== '/input/bumper/click') return;
  if (weights.some(({id}) => getObjectPhysics(id).held)) return;
  for (const {id, x} of weights) {
    setObjectPosition(id, x, -.15, 0);
    setObjectRotation(id, 0, 0, 0);
    setObjectVelocity(id, 0, 0, 0);
    setObjectAngularVelocity(id, 0, 0, 0);
  }
});
