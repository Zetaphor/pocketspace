function check(value) { if (!value) throw Error('motor API assertion'); }
function rejects(fn) { let failed = false; try { fn(); } catch (_) { failed = true; } check(failed); }
const asset = loadModel('assets/samples/Box/Box.glb');
const a = createObject(asset, 0, 0, 0, .1), b = createObject(asset, 0, 0, 0, .1);
setObjectPhysics(a, {type: 'static'}); setObjectPhysics(b);
const hinge = createPhysicsJoint(a, b, {type: 'hinge', minAngle: -.8, maxAngle: .8});
check(getPhysicsJoint(hinge).motor.mode === 'off');
let state = setPhysicsJointMotor(hinge, {mode: 'position', targetAngle: .6, maxTorque: .2});
check(state.motor.mode === 'position' && state.motor.frequency === 2);
state = setPhysicsJointMotor(hinge, {damping: .9});check(state.motor.mode === 'position' && state.motor.targetAngle > .59);
for (const options of [null, {mode: 'bad'}, {mode: 1}, {targetAngle: 2}, {targetAngle: NaN},
  {targetVelocity: Infinity}, {targetVelocity: 31}, {maxTorque: -1}, {maxTorque: 'strong'},
  {frequency: 0}, {frequency: 31}, {damping: -1}, {damping: 11}]) {
  rejects(() => setPhysicsJointMotor(hinge, options));
  check(getPhysicsJoint(hinge).motor.mode === 'position');
}
rejects(() => setPhysicsJointMotor(999, {}));
const fixed = createPhysicsJoint(a, b);
check(getPhysicsJoint(fixed).motor === null);
rejects(() => setPhysicsJointMotor(fixed, {mode: 'velocity'}));removePhysicsJoint(fixed);
function verifyMotor() { check(Math.abs(getPhysicsJoint(hinge).angle - .6) < .02); }
function reverseMotor() { setPhysicsJointMotor(hinge, {mode: 'velocity', targetVelocity: -1}); }
function verifyReverse() { check(getPhysicsJoint(hinge).angle < -.75); }
