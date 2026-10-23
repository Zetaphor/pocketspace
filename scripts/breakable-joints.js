const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.53, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
const links = [
  {x: -.22, top: 'assets/physics/GreenBox.glb', path: 'assets/physics/BlueBox.glb', shape: 'box', type: 'fixed', breakForce: 40},
  {x: .22, top: 'assets/samples/Box/Box.glb', path: 'assets/physics/YellowSphere.glb', shape: 'sphere', type: 'distance', breakForce: 10}
].map(({x, top, path, shape, type, breakForce}) => {
  const anchor = createObject(loadModel(top), x, .3, 0, .06);
  setObjectPhysics(anchor, {type: 'static'});
  const id = createObject(loadModel(path), x, 0, 0, .12);
  setObjectPhysics(id, {shape, mass: .25});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
  return {id, anchor, x, options: type === 'distance' ? {type, breakForce, minDistance: .3, maxDistance: .3} : {type, breakForce, anchor: {x, y: 0, z: 0}}};
});
function connect() { for (const link of links) link.joint = createPhysicsJoint(link.anchor, link.id, link.options); }
connect();
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type === 'physics.jointBreak') print(`Joint ${event.jointId} broke at ${event.force.toFixed(1)} N`);
  if (event.type !== 'controller.button' || !event.down || event.control !== '/input/bumper/click') return;
  if (links.some(({id}) => getObjectPhysics(id).held)) return;
  for (const link of links) {
    removePhysicsJoint(link.joint);
    setObjectPosition(link.id, link.x, 0, 0);
    setObjectRotation(link.id, 0, 0, 0);
    setObjectVelocity(link.id, 0, 0, 0);
    setObjectAngularVelocity(link.id, 0, 0, 0);
  }
  connect();
});
