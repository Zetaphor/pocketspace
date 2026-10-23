function check(value) { if (!value) throw Error('breakable joint API assertion'); }
function rejects(fn) { let failed = false; try { fn(); } catch (_) { failed = true; } check(failed); }
const asset = loadModel('assets/samples/Box/Box.glb');
const a = createObject(asset, 0, .5, 0, .05), b = createObject(asset, 0, .2, 0, .08);
setObjectPhysics(a, {type: 'static'});setObjectPhysics(b);
for (const options of [{breakForce: 0}, {breakForce: NaN}, {breakForce: 'weak'}, {breakTorque: -1}, {breakTorque: 1e10}, {type: 'distance', breakTorque: 1}])
  rejects(() => createPhysicsJoint(a, b, options));
const joint = createPhysicsJoint(a, b, {type: 'distance', minDistance: .3, maxDistance: .3, breakForce: 20});
check(getPhysicsJoint(joint).breakForce === 20 && getPhysicsJoint(joint).breakTorque === null);
const received = [];
onInputEvent(event => { if (event.type === 'physics.jointBreak') received.push(event); });
function pull() { applyObjectImpulse(b, 0, -1, 0); }
function verifyBreak() {
  check(getPhysicsJoint(joint) === null && !getObjectPhysics(b).jointed);
  setPhysicsCollisionEvents(false); // Collision opt-in must not clear break notifications.
  const polled = pollInputEvents().filter(event => event.type === 'physics.jointBreak');
  check(received.length === 1 && polled.length === 1);
  check(JSON.stringify(received[0]) === JSON.stringify(polled[0]));
  const event = polled[0];
  check(event.jointId === joint && event.objectId === a && event.otherObjectId === b && event.force > 20 && event.torque === 0);
  check(event.point === undefined);
  removePhysicsJoint(joint);
}
