function check(value) { if (!value) throw Error('convex API assertion'); }
function near(a, b) { check(Math.abs(a - b) < 1e-5); }
for (const path of ['assets/Avocado.glb', 'assets/samples/ToyCar/ToyCar.glb']) {
  const asset = loadModel(path);
  const id = createObject(asset, 0, 1, 0, .2);
  let state = setObjectPhysics(id, {shape: 'convex'});
  check(state.shape === 'convex' && state.collider.vertexCount >= 4 && state.collider.vertexCount <= 256);
  check(state.halfExtents === null && state.radius === null);
  check(Number.isFinite(state.collider.centerOfMass.x));
  const half = state.collider.halfExtents.x;
  const second = createObject(asset, 1, 1, 0, .4);
  near(setObjectPhysics(second, {shape: 'convex', type: 'static'}).collider.halfExtents.x, half * 2);
  setObjectScale(id, .4);
  near(getObjectPhysics(id).collider.halfExtents.x, half * 2);
  state = setObjectPhysics(id, {offset: {x: 0, y: -.1, z: .2}, type: 'kinematic'});
  near(state.collider.offset.z, .2);
  for (const options of [{halfExtents: {x: .1, y: .1, z: .1}}, {radius: .1}]) {
    let failed = false;
    try { setObjectPhysics(id, options); } catch (_) { failed = true; }
    check(failed && getObjectPhysics(id).shape === 'convex');
  }
  setObjectPhysics(id, {shape: 'sphere', radius: .1});
  state = setObjectPhysics(id, {shape: 'convex'});
  check(state.radius === null && state.shape === 'convex');
}
