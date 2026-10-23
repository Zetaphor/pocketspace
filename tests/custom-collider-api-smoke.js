function check(value) { if (!value) throw Error('custom collider API assertion'); }
function near(a, b) { check(Math.abs(a - b) < 1e-5); }
function rejects(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
const asset = loadModel('assets/samples/Box/Box.glb');
const box = createObject(asset, 0, .5, 0, .2);
let state = setObjectPhysics(box);
check(state.halfExtents === null && state.radius === null && state.offset === null);
near(state.collider.halfExtents.y, .1);
state = setObjectPhysics(box, {halfExtents: {x: .2, y: .03, z: .4}, offset: {x: 0, y: -.1, z: .2}});
near(state.halfExtents.y, .03); near(state.collider.offset.z, .2);
setObjectScale(box, .8);
state = getObjectPhysics(box); near(state.collider.halfExtents.y, .03); near(state.collider.offset.z, .2);
state = setObjectPhysics(box, {friction: .7}); near(state.halfExtents.z, .4);
for (const options of [
  {halfExtents: 1}, {halfExtents: {x: .1, y: .1}}, {halfExtents: {x: 0, y: .1, z: .1}},
  {halfExtents: {x: .0001, y: .1, z: .1}}, {halfExtents: {x: 101, y: .1, z: .1}},
  {offset: {x: 0, y: NaN, z: 0}}, {offset: {x: 0, y: 0, z: 101}},
  {radius: .1}, {radius: 'small'}, {shape: 'sphere', halfExtents: {x: .1, y: .1, z: .1}}
]) rejects(() => setObjectPhysics(box, options));
state = getObjectPhysics(box); check(state.shape === 'box'); near(state.halfExtents.z, .4);
state = setObjectPhysics(box, {shape: 'sphere', radius: .12});
check(state.halfExtents === null && state.collider.halfExtents === null);
near(state.radius, .12); near(state.collider.radius, .12); near(state.offset.z, .2);
setObjectScale(box, .4); near(getObjectPhysics(box).collider.radius, .12);
rejects(() => setObjectPhysics(box, {radius: 0}));
rejects(() => setObjectPhysics(box, {radius: 101}));
state = setObjectPhysics(box, {radius: null, offset: null});
check(state.radius === null && state.offset === null); near(state.collider.radius, .2);
state = setObjectPhysics(box, {shape: 'box', halfExtents: {x: .2, y: .03, z: .4}});
check(state.radius === null && state.collider.radius === null);
state = setObjectPhysics(box, {halfExtents: null}); near(state.collider.halfExtents.y, .2);
// A failing property getter cannot partially replace a body.
rejects(() => setObjectPhysics(box, {halfExtents: {get x() {throw Error('getter');}, y: .1, z: .1}}));
near(getObjectPhysics(box).collider.halfExtents.y, .2);
// The flattened model remains thin after import; its node transform is baked.
const slab = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.43, 0, 1.2);
near(setObjectPhysics(slab, {type: 'static'}).collider.halfExtents.y, .03);
