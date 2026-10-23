function check(value) { if (!value) throw Error('joint API assertion'); }
function rejects(fn) { let failed = false; try { fn(); } catch (_) { failed = true; } check(failed); }
const asset = loadModel('assets/samples/Box/Box.glb');
const a = createObject(asset, 0, .5, 0, .1);
const b = createObject(asset, .2, .5, 0, .1);
setObjectPhysics(a, {type: 'static'}); setObjectPhysics(b);
const weld = createPhysicsJoint(a, b);
check(getPhysicsJoint(weld).type === 'fixed' && getObjectPhysics(b).jointed);
rejects(() => setObjectScale(b, .2)); rejects(() => setObjectPhysics(a));
for (const options of [null, {type: 'bad'}, {type: 1}, {type: 'hinge', axis: {x: 0, y: 0, z: 0}},
  {type: 'hinge', minAngle: .1}, {type: 'hinge', minAngle: 0, maxAngle: 0},
  {type: 'distance', minDistance: 1, maxDistance: .1}, {collideConnected: 1},
  {anchor: {x: 0}}, {type: 'fixed', minAngle: 0}, {type: 'fixed', axis: {x: 1, y: 0, z: 0}},
  {type: 'hinge', anchorA: {x: 0, y: 0, z: 0}}, {type: 'distance', maxDistance: Infinity}])
  rejects(() => createPhysicsJoint(a, b, options));
rejects(() => createPhysicsJoint(a, a)); rejects(() => createPhysicsJoint(a, 999));
removePhysicsJoint(weld); removePhysicsJoint(weld);check(getPhysicsJoint(weld) === null);
const door = createPhysicsJoint(a, b, {type: 'hinge', anchor: {x: 0, y: .5, z: 0}, axis: {x: 0, y: 2, z: 0}, minAngle: -.5, maxAngle: .5});
let state = getPhysicsJoint(door); check(state.axis.y === 1 && state.minAngle === -.5 && state.maxDistance === null);
removeObjectPhysics(a);check(getPhysicsJoint(door) === null && !getObjectPhysics(b).jointed);
setObjectPhysics(a, {type: 'static'});
const tether = createPhysicsJoint(a, b, {type: 'distance', maxDistance: .3});
state = getPhysicsJoint(tether);check(state.minDistance === 0 && state.maxDistance > .29 && state.angle === null);
function verifyJoint() { check(getPhysicsJoint(tether).distance < .31); }
