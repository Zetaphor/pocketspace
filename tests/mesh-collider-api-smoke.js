function check(value) { if (!value) throw Error('mesh collider API assertion'); }
function near(a, b) { check(Math.abs(a-b) < 1e-5); }
const asset = loadModel('assets/physics/RingRamp.glb');
const platform = createObject(asset, 0, 0, 0, 1);
let state = setObjectPhysics(platform, {type: 'static', shape: 'mesh'});
check(state.shape === 'mesh' && state.collider.triangleCount === 48);
check(state.collider.vertexCount === null && state.radius === null);
const half = state.collider.halfExtents.x;
const second = createObject(asset, 2, 0, 0, .5);
near(setObjectPhysics(second, {type: 'static', shape: 'mesh'}).collider.halfExtents.x, half / 2);
setObjectScale(platform, .8);
near(getObjectPhysics(platform).collider.halfExtents.x, half * .8);
state = setObjectPhysics(platform, {offset: {x: .1, y: .2, z: .3}});
near(state.collider.offset.z, .3);
for (const options of [{type: 'dynamic'}, {type: 'kinematic'}, {radius: .1}, {halfExtents: {x: .1, y: .1, z: .1}}]) {
  let failed = false;
  try { setObjectPhysics(platform, options); } catch (_) { failed = true; }
  check(failed);
  state = getObjectPhysics(platform);
  check(state.shape === 'mesh' && state.type === 'static');
}
state = setObjectPhysics(platform, {shape: 'box', halfExtents: {x: .1, y: .1, z: .1}});
check(state.collider.triangleCount === null);
state = setObjectPhysics(platform, {shape: 'mesh', offset: null});
check(state.halfExtents === null && state.offset === null);
const sphere = createObject(loadModel('assets/physics/YellowSphere.glb'), 0, .3, 0, .1);
setObjectPhysics(sphere, {shape: 'sphere'});
