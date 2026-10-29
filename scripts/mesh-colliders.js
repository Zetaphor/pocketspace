// Roll or throw bodies through a static model-derived platform with an opening and ramp.
// Grip/trigger grabs; bumper resets unheld bodies.
const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.53, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
const platform = createObject(loadModel('assets/physics/RingRamp.glb'), 0, -.2, 0, 1);
// Triangle-mesh colliders preserve openings but are restricted to static bodies.
setObjectPhysics(platform, {type: 'static', shape: 'mesh'});
const bodies = [
  {path: 'assets/physics/YellowSphere.glb', x: 0, z: -.32, scale: .1, shape: 'sphere'},
  {path: 'assets/samples/Box/Box.glb', x: -.3, z: 0, scale: .12, shape: 'box'}
].map(({path, x, z, scale, shape}) => {
  const id = createObject(loadModel(path), x, .2, z, scale);
  setObjectPhysics(id, {shape, mass: .25, friction: .3});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
  return {id, x, z};
});
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down || event.control !== '/input/bumper/click') return;
  for (const {id, x, z} of bodies) {
    if (getObjectPhysics(id).held) continue;
    setObjectPosition(id, x, .2, z);
    setObjectRotation(id, 0, 0, 0);
    setObjectVelocity(id, 0, 0, 0);
    setObjectAngularVelocity(id, 0, 0, 0);
  }
});
