function check(condition) { if (!condition) throw Error('object transform assertion'); }
function rejects(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
function near(a, b) { return Math.abs(a - b) < 1e-5; }
const asset = loadModel('assets/samples/Box/Box.glb');
// Grabbed without a physics body.
const held = createObject(asset, .1, .2, .3, .25);
enableObjectInteraction(held, {grabButton: 'trigger'});
// Moved only by physics.
const falling = createObject(asset, 1, 1, 0, .2);
setObjectPhysics(falling);

let t = getObjectTransform(held);
check(near(t.x, .1) && near(t.y, .2) && near(t.z, .3) && t.pitch === 0 && t.yaw === 0 && t.roll === 0);
check(near(t.scale, .25) && t.visible === true);
check(Object.keys(t).join() === 'x,y,z,pitch,yaw,roll,scale,visible');
setObjectPosition(held, -.4, .5, .6);
setObjectRotation(held, .1, .2, .3);
setObjectScale(held, .5);
setObjectVisible(held, false);
t = getObjectTransform(held);
check(near(t.x, -.4) && near(t.y, .5) && near(t.z, .6) && near(t.pitch, .1) && near(t.yaw, .2) && near(t.roll, .3));
check(near(t.scale, .5) && t.visible === false);
setObjectYaw(held, -1);
check(near(getObjectTransform(held).yaw, -1));
// A returned object is a copy.
t.x = 9;
check(near(getObjectTransform(held).x, -.4));
setObjectVisible(held, true);
setObjectRotation(held, 0, 0, 0);
setObjectPosition(held, 0, 0, 0);

rejects(() => getObjectTransform());
rejects(() => getObjectTransform(9999));
rejects(() => getObjectTransform(0));
const removed = createObject(asset, 0, 0, 0, .1);
check(getObjectTransform(removed).visible);
removeObject(removed);
rejects(() => getObjectTransform(removed));

function verifyFall() {
  const physics = getObjectPhysics(falling), pose = getObjectTransform(falling);
  check(pose.y < .9 && near(pose.x, physics.position.x) && near(pose.y, physics.position.y) && near(pose.z, physics.position.z));
}
