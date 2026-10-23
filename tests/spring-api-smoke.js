function check(value) { if (!value) throw Error('spring API assertion'); }
function rejects(fn) { let failed = false; try { fn(); } catch (_) { failed = true; } check(failed); }
const asset = loadModel('assets/samples/Box/Box.glb');
const a = createObject(asset, 0, .5, 0, .05), b = createObject(asset, 0, -.1, 0, .08);
setObjectPhysics(a, {type: 'static'});setObjectPhysics(b);
const options = {type: 'distance', minDistance: .3, maxDistance: .3};
const hard = createPhysicsJoint(a, b, {...options, spring: null});check(getPhysicsJoint(hard).spring === null);removePhysicsJoint(hard);
const defaults = createPhysicsJoint(a, b, {...options, spring: {}});
check(getPhysicsJoint(defaults).spring.frequency === 2 && getPhysicsJoint(defaults).spring.damping === .5);removePhysicsJoint(defaults);
for (const spring of [1, {frequency: 0}, {frequency: 31}, {frequency: NaN}, {damping: -1}, {damping: 11}, {damping: 'soft'}])
  rejects(() => createPhysicsJoint(a, b, {...options, spring}));
for (const type of ['fixed', 'hinge']) rejects(() => createPhysicsJoint(a, b, {type, spring: {frequency: 2}}));
const spring = createPhysicsJoint(a, b, {...options, spring: {frequency: 2, damping: .15}});
check(getPhysicsJoint(spring).spring.frequency === 2);
function verifySpring() {
  const state = getPhysicsJoint(spring);
  check(state.distance > .34 && state.distance < .38);
  check(Math.abs(getObjectPhysics(b).position.y - (.2 - 9.81 / (16 * Math.PI * Math.PI))) < .015);
}
