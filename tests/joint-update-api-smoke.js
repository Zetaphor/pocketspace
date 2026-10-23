function check(value) { if (!value) throw Error('joint update API assertion'); }
function rejects(fn) { let failed = false; try { fn(); } catch (_) { failed = true; } check(failed); }
const asset = loadModel('assets/samples/Box/Box.glb');
const a = createObject(asset, 0, .5, 0, .05), b = createObject(asset, 0, .2, 0, .08);
setObjectPhysics(a, {type: 'static'});setObjectPhysics(b);
const joint = createPhysicsJoint(a, b, {type: 'distance', minDistance: .3, maxDistance: .3, spring: {frequency: 2, damping: .3}});
let state = setPhysicsJoint(joint, {minDistance: .5, maxDistance: .5, spring: {damping: 1}, breakForce: 100, collideConnected: true});
check(state.id === joint && state.spring.frequency === 2 && state.spring.damping === 1 && state.breakForce === 100 && state.collideConnected);
state = setPhysicsJoint(joint, {spring: null, breakForce: null});check(state.spring === null && state.breakForce === null);
for (const options of [null, {type: 'hinge'}, {anchor: null}, {axis: {x: 1, y: 0, z: 0}},
  {minDistance: .6, collideConnected: false}, {maxDistance: null}, {minAngle: -.2},
  {spring: {frequency: 0}}, {breakTorque: 1}]) {
  rejects(() => setPhysicsJoint(joint, options));
  state = getPhysicsJoint(joint);check(state.spring === null && state.minDistance === .5 && state.collideConnected);
}
rejects(() => setPhysicsJoint(999, {}));
function verifyLength() { check(Math.abs(getPhysicsJoint(joint).distance - .5) < .01); }
function configureBreak() { setPhysicsJoint(joint, {breakForce: 1}); }
function verifyUpdatedBreak() { check(getPhysicsJoint(joint) === null); }
