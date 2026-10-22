function check(condition) { if (!condition) throw Error('physics API assertion'); }
function rejects(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
const asset = loadModel('assets/samples/Box/Box.glb');
const box = createObject(asset, 0, .5, 0, .2);
check(getObjectPhysics(box) === null);
rejects(() => setObjectVelocity(box, 1, 0, 0));
setPhysicsGravity(0, 0, 0);
check(setObjectPhysics(box, {mass: 2}).type === 'dynamic');
setObjectInteraction(box, {grabbable: true});
check(getObjectPhysics(box).type === 'dynamic');
setObjectVelocity(box, 0, 0, 1);
applyObjectImpulse(box, 2, 0, 0);
check(getObjectPhysics(box).velocity.x === 1);
setObjectAngularVelocity(box, 0, 1, 0);
setObjectPosition(box, 0, .5, 0);
setObjectYaw(box, .2);
setObjectScale(box, .3);
check(Math.abs(getObjectPhysics(box).rotation.y - .2) < 1e-5);
check(getObjectPhysics(box).velocity.z === 1);
for (const options of [{mass: 0}, {friction: -1}, {restitution: 2}, {gravityFactor: NaN}, {type: 'bad'}, {shape: 'mesh'}]) rejects(() => setObjectPhysics(box, options));
check(getObjectPhysics(box).mass === 2);
rejects(() => setPhysicsGravity(Infinity, 0, 0));
setObjectPhysics(box, {type: 'static'});
rejects(() => applyObjectImpulse(box, 1, 0, 0));
check(getObjectPhysics(box).mass === 2);
setObjectPhysics(box, {type: 'dynamic'});
setObjectVelocity(box, 0, 0, 1);
function verifyAdvance() { check(getObjectPhysics(box).position.z > .49); }
