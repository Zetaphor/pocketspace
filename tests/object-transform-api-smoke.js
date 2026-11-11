function check(condition) { if (!condition) throw Error('object transform assertion'); }
function rejects(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
function near(a, b) { return Math.abs(a - b) < 1e-5; }
function scaled(id, x, y, z) {
  const s = getObjectTransform(id).scale;
  return Object.keys(s).join() === 'x,y,z' && near(s.x, x) && near(s.y, y) && near(s.z, z);
}
const asset = loadModel('assets/samples/Box/Box.glb');
// Grabbed without a physics body.
const held = createObject(asset, .1, .2, .3, .25);
enableObjectInteraction(held, {grabButton: 'trigger'});
// Moved only by physics.
const falling = createObject(asset, 1, 1, 0, .2);
setObjectPhysics(falling);

let t = getObjectTransform(held);
check(near(t.x, .1) && near(t.y, .2) && near(t.z, .3) && t.pitch === 0 && t.yaw === 0 && t.roll === 0);
check(scaled(held, .25, .25, .25) && t.visible === true);
check(Object.keys(t).join() === 'x,y,z,pitch,yaw,roll,scale,visible');
setObjectPosition(held, -.4, .5, .6);
setObjectRotation(held, .1, .2, .3);
setObjectScale(held, .5);
setObjectVisible(held, false);
t = getObjectTransform(held);
check(near(t.x, -.4) && near(t.y, .5) && near(t.z, .6) && near(t.pitch, .1) && near(t.yaw, .2) && near(t.roll, .3));
check(scaled(held, .5, .5, .5) && t.visible === false);
setObjectYaw(held, -1);
check(near(getObjectTransform(held).yaw, -1));
// A returned object is a copy.
t.x = 9;
check(near(getObjectTransform(held).x, -.4));
setObjectVisible(held, true);
// Per-axis scale: omitted components repeat x; objects accept a number or {x, y, z}.
setObjectScale(held, .1, .2, .3);
check(scaled(held, .1, .2, .3));
setObjectScale(held, .4, .6);
check(scaled(held, .4, .6, .4));
const stretched = createObject(asset, 0, 0, 0, {x: .3, y: 1.5, z: .05});
check(scaled(stretched, .3, 1.5, .05));
getObjectTransform(stretched).scale.x = 2;
check(scaled(stretched, .3, 1.5, .05));
removeObject(stretched);
const defaultSize = createObject(asset, 0, 0, 0);
check(scaled(defaultSize, .55, .55, .55));
removeObject(defaultSize);
for (const bad of [0, -.1, 2.01, NaN, Infinity]) {
  rejects(() => setObjectScale(held, bad));
  rejects(() => setObjectScale(held, .2, bad));
  rejects(() => setObjectScale(held, .2, .2, bad));
  rejects(() => createObject(asset, 0, 0, 0, {x: .2, y: bad, z: .2}));
}
rejects(() => setObjectScale(held, '.2'));
rejects(() => createObject(asset, 0, 0, 0, {x: .2, y: .2}));
rejects(() => createObject(asset, 0, 0, 0, {x: '.2', y: .2, z: .2}));
check(scaled(held, .4, .6, .4));
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
