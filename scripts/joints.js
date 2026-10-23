const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.43, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
const bodies = [];
function body(path, x, y, z, scale, type = 'dynamic', rotation = [0, 0, 0]) {
  const id = createObject(loadModel(path), x, y, z, scale);
  setObjectRotation(id, ...rotation);
  setObjectPhysics(id, {type, mass: .25});
  if (type === 'dynamic') {
    enableObjectInteraction(id, {grabButton: 'both'});
    setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
    bodies.push({id, x, y, z, rotation});
  }
  return id;
}
const pivot = body('assets/physics/Platform.glb', -.5, 0, 0, .05, 'static');
const door = body('assets/physics/ThinWallBlue.glb', -.35, 0, 0, .3, 'dynamic', [Math.PI / 2, 0, 0]);
const anchor = body('assets/physics/GreenBox.glb', .3, .25, 0, .06, 'static');
const ball = body('assets/physics/YellowSphere.glb', .3, -.05, 0, .1);
setObjectPhysics(ball, {shape: 'sphere'});
const box = body('assets/samples/Box/Box.glb', -.07, .12, .25, .1);
const avocado = body('assets/Avocado.glb', .07, .12, .25, .12);
setObjectPhysics(avocado, {shape: 'convex'});
let joints = [];
function connect() {
  joints = [
    createPhysicsJoint(pivot, door, {type: 'hinge', anchor: {x: -.5, y: 0, z: 0}, axis: {x: 0, y: 1, z: 0}, minAngle: -1.2, maxAngle: 1.2}),
    createPhysicsJoint(anchor, ball, {type: 'distance', minDistance: .3, maxDistance: .3}),
    createPhysicsJoint(box, avocado, {type: 'fixed'})
  ];
}
connect();
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down || event.control !== '/input/bumper/click') return;
  if (bodies.some(({id}) => getObjectPhysics(id).held)) return;
  joints.forEach(removePhysicsJoint);
  for (const {id, x, y, z, rotation} of bodies) {
    setObjectPosition(id, x, y, z);
    setObjectRotation(id, ...rotation);
    setObjectVelocity(id, 0, 0, 0);
    setObjectAngularVelocity(id, 0, 0, 0);
  }
  connect();
});
