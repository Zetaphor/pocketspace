const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.63, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
function body(path, x, y, scale, type) {
  const id = createObject(loadModel(path), x, y, 0, scale);
  setObjectPhysics(id, {type, mass: .25});
  return id;
}
const pivot = body('assets/physics/GreenBox.glb', -.5, 0, .05, 'static');
const door = body('assets/physics/ThinWallBlue.glb', -.35, 0, .3, 'dynamic');
setObjectRotation(door, Math.PI / 2, 0, 0);
const top = body('assets/samples/Box/Box.glb', .25, .3, .06, 'static');
const ball = body('assets/physics/YellowSphere.glb', .25, 0, .12, 'dynamic');
setObjectPhysics(ball, {shape: 'sphere'});
const hinge = createPhysicsJoint(pivot, door, {type: 'hinge', anchor: {x: -.5, y: 0, z: 0}, minAngle: -.4, maxAngle: .4});
const spring = createPhysicsJoint(top, ball, {type: 'distance', minDistance: .25, maxDistance: .25, spring: {frequency: 2, damping: .3}});
for (const id of [door, ball]) {
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
let extended = false, damped = false;
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') {
    extended = !extended;
    const length = extended ? .5 : .25, angle = extended ? 1.2 : .4;
    setPhysicsJoint(spring, {minDistance: length, maxDistance: length});
    setPhysicsJoint(hinge, {minAngle: -angle, maxAngle: angle});
  } else if (event.control === '/input/a/click' || event.control === '/input/x/click') {
    damped = !damped;
    setPhysicsJoint(spring, {spring: {frequency: damped ? 3 : 2, damping: damped ? 1 : .3}});
  }
});
