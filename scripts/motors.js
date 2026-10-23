const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.43, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
function body(path, x, y, scale, type) {
  const id = createObject(loadModel(path), x, y, 0, scale);
  setObjectPhysics(id, {type, mass: .25});
  return id;
}
const frame = body('assets/physics/Platform.glb', -.5, 0, .05, 'static');
const door = body('assets/physics/ThinWallBlue.glb', -.35, 0, .3, 'dynamic');
setObjectRotation(door, Math.PI / 2, 0, 0);
const base = body('assets/physics/YellowBox.glb', .3, -.2, .05, 'static');
const rotor = body('assets/physics/ThinWallGreen.glb', .3, 0, .25, 'dynamic');
setObjectRotation(rotor, Math.PI / 2, 0, 0);
for (const id of [door, rotor]) {
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
const hinge = createPhysicsJoint(frame, door, {type: 'hinge', anchor: {x: -.5, y: 0, z: 0}, minAngle: -1.2, maxAngle: 1.2});
const spinner = createPhysicsJoint(base, rotor, {type: 'hinge', anchor: {x: .3, y: 0, z: 0}});
let powered = true, open = false;
function drive() {
  setPhysicsJointMotor(hinge, {mode: powered ? 'position' : 'off', targetAngle: open ? .9 : 0, maxTorque: .15, frequency: 3, damping: 1});
  setPhysicsJointMotor(spinner, {mode: powered ? 'velocity' : 'off', targetVelocity: .8, maxTorque: .05});
}
drive();
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') open = !open;
  else if (event.control === '/input/a/click' || event.control === '/input/x/click') powered = !powered;
  else return;
  drive();
});
