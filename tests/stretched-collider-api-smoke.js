function check(value) { if (!value) throw Error('stretched collider API assertion'); }
function near(a, b, tolerance = 1e-4) { check(Math.abs(a - b) < tolerance); }
function rejects(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
function halves(state, x, y, z) { const h = state.collider.halfExtents; near(h.x, x); near(h.y, y); near(h.z, z); }
const asset = loadModel('assets/samples/Box/Box.glb');
const size = {x: .2, y: .4, z: .1};
// Automatic box, convex and mesh sizes follow each axis of the object's scale.
const box = createObject(asset, 0, 1, 0, size);
halves(setObjectPhysics(box), .1, .2, .05);
halves(setObjectPhysics(box, {shape: 'convex'}), .1, .2, .05);
setObjectScale(box, .4, .2, .3);
halves(getObjectPhysics(box), .2, .1, .15);
const floor = createObject(asset, 2, 0, 0, size);
halves(setObjectPhysics(floor, {type: 'static', shape: 'mesh'}), .1, .2, .05);
// A sphere sized from the model can't stretch into an ellipsoid; an explicit radius is in meters.
const ball = createObject(asset, 4, 1, 0, size);
rejects(() => setObjectPhysics(ball, {shape: 'sphere'}));
check(getObjectPhysics(ball) === null);
near(setObjectPhysics(ball, {shape: 'sphere', radius: .15}).collider.radius, .15);
setObjectScale(ball, .3);
near(setObjectPhysics(ball, {radius: null}).collider.radius, .15);
// Stretching an automatic sphere fails and keeps the previous scale and body.
rejects(() => setObjectScale(ball, .3, .5, .3));
const scale = getObjectTransform(ball).scale;
near(scale.x, .3); near(scale.y, .3); near(scale.z, .3);
near(getObjectPhysics(ball).collider.radius, .15);
