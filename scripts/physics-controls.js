// Apply impulses, toggle gravity and detach/recreate a body's simulation while a kinematic platform moves.
// Bumper launches the unheld ball; A/X toggles gravity; B/Y toggles its physics body.
const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.55, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
const ball = createObject(loadModel('assets/physics/YellowSphere.glb'), 0, 0, 0, .12);
setObjectPhysics(ball, {shape: 'sphere', mass: .25});
enableObjectInteraction(ball, {grabButton: 'both'});
const platform = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.2, 0, .3);
setObjectPhysics(platform, {type: 'kinematic'});
let gravity = true, time = 0;
function update(dt) {
  time += dt;
  // Position setters give kinematic bodies a simulation target; the solver moves them.
  setObjectPosition(platform, Math.sin(time) * .2, -.2, 0);
}
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  const body = getObjectPhysics(ball); // null means a live rendered object has no physics body.
  if (event.control === '/input/bumper/click') {
    // Velocity/impulse calls require a dynamic body that is not held by a controller.
    if (body && !body.held) applyObjectImpulse(ball, 0, .6, 0);
  } else if (['/input/a/click', '/input/x/click'].includes(event.control)) {
    gravity = !gravity;
    setPhysicsGravity(0, gravity ? -9.81 : 0, 0);
  } else if (['/input/b/click', '/input/y/click'].includes(event.control)) {
    if (body && body.held) return;
    // Removing physics preserves the rendered transform; reattaching starts a fresh body.
    if (body) removeObjectPhysics(ball);
    else setObjectPhysics(ball, {shape: 'sphere', mass: .25});
  }
});
setLaserVisible('left', true);
setLaserVisible('right', true);
