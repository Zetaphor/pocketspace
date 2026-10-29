// Give an Avocado and car model-derived convex colliders for grabbing and throwing.
// Bumper resets unheld bodies; the floor uses an explicit box collider.
const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.43, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
const bodies = [
  {path: 'assets/Avocado.glb', x: -.2, scale: .2},
  {path: 'assets/samples/ToyCar/ToyCar.glb', x: .2, scale: .3}
].map(({path, x, scale}) => {
  const id = createObject(loadModel(path), x, .2, 0, scale);
  // A convex hull fills model cavities; use a static mesh for holes/openings.
  setObjectPhysics(id, {shape: 'convex', mass: .25});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
  return {id, x};
});
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down || event.control !== '/input/bumper/click') return;
  for (const {id, x} of bodies) {
    if (getObjectPhysics(id).held) continue;
    setObjectPosition(id, x, .2, 0);
    setObjectRotation(id, 0, 0, 0);
    setObjectVelocity(id, 0, 0, 0);
    setObjectAngularVelocity(id, 0, 0, 0);
  }
});
