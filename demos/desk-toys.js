// Desk toys.
// Controls: grab an end ball of the Newton's cradle with the trigger or grip, pull
// it back and let go. Grab the green spinner and flick it, or press A or X to
// switch its motor on or off. Either bumper resets both toys when you are not
// holding them.
// Shows: hinge joints with limits, a hinge velocity motor, collision events,
// setting velocities, grabbing jointed bodies, the tracked floor height, and
// lasers shown only while aimed.
const floor = getFloorHeight() ?? -1.4;
const TOP = floor + .75, DESK = {x: 0, z: -.45};
const desk = createObject(loadModel('assets/demos/Desk.glb'), DESK.x, TOP - .015, DESK.z, .9);
setObjectPhysics(desk, {type: 'static'});
setObjectInteraction(desk, {laserPassThrough: true});

// Newton's cradle: the frame stands on the desk; each ball swings on a hinge
// along z through its pivot between the rails (see assets/demos/README.md).
const CRADLE = {x: DESK.x - .18, z: DESK.z};
const frame = createObject(loadModel('assets/demos/CradleFrame.glb'), CRADLE.x, TOP + .112, CRADLE.z, .32);
setObjectPhysics(frame, {type: 'static', halfExtents: {x: .16, y: .01, z: .09}, offset: {x: 0, y: -.102, z: 0}});
setObjectInteraction(frame, {laserPassThrough: true});
const ballModel = loadModel('assets/demos/CradleBall.glb');
const cradle = [-.08, -.04, 0, .04, .08].map(x => {
  const pivot = {x: CRADLE.x + x, y: TOP + .22, z: CRADLE.z};
  const rest = {x: pivot.x, y: pivot.y - .0698, z: pivot.z};
  const id = createObject(ballModel, rest.x, rest.y, rest.z, .1417);
  setObjectPhysics(id, {shape: 'sphere', radius: .02, offset: {x: 0, y: -.0502, z: 0}, mass: .1,
                        friction: 0, linearDamping: .01, angularDamping: .01});
  enableObjectInteraction(id, {grabButton: 'both'});
  const joint = createPhysicsJoint(frame, id, {type: 'hinge', anchor: pivot, axis: {x: 0, y: 0, z: 1},
                                               minAngle: -1.3, maxAngle: 1.3});
  return {id, rest, joint};
});

// A rigid-body solver shares one impact across the whole touching row, so the
// demo hands it on as a real cradle does: when swinging balls strike the row,
// the row stops and as many balls swing out of the far end at the same speed.
const held = id => getObjectPhysics(id).held;
const swing = new Array(cradle.length).fill(0);
let frames = 0, handedOffAt = -100;
setPhysicsCollisionEvents(true);
function handOff() {
  // One hand-off per impact: contacts settle within a few frames.
  if (frames - handedOffAt < 12 || cradle.some(ball => held(ball.id))) return;
  // Strikers are the balls that were swinging just before the impact.
  const moving = swing.map(v => Math.abs(v) > .05);
  const count = moving.filter(Boolean).length;
  if (!count || count === cradle.length) return;
  const speed = swing.filter((_, i) => moving[i]).reduce((a, b) => a + b, 0) / count;
  const fromLeft = speed > 0;
  handedOffAt = frames;
  cradle.forEach((ball, i) => {
    const out = fromLeft ? i >= cradle.length - count : i < count;
    setObjectVelocity(ball.id, out ? speed : 0, 0, 0);
    swing[i] = out ? speed : 0;
  });
}
function update() {
  frames++;
  cradle.forEach((ball, i) => { swing[i] = getObjectPhysics(ball.id).velocity.x; });
}

// Spinner: a green plate on a hinge above a small yellow post.
const SPIN = {x: DESK.x + .2, y: TOP + .046, z: DESK.z};
const post = createObject(loadModel('assets/physics/YellowBox.glb'), SPIN.x, TOP + .02, SPIN.z, .04);
setObjectPhysics(post, {type: 'static'});
setObjectInteraction(post, {laserPassThrough: true});
const spinner = createObject(loadModel('assets/physics/ThinWallGreen.glb'), SPIN.x, SPIN.y, SPIN.z, .16);
setObjectPhysics(spinner, {mass: .05, angularDamping: .08});
enableObjectInteraction(spinner, {grabButton: 'both'});
const spin = createPhysicsJoint(post, spinner, {type: 'hinge', anchor: SPIN, axis: {x: 0, y: 1, z: 0}});
let motor = false;
function drive() {
  setPhysicsJointMotor(spin, {mode: motor ? 'velocity' : 'off', targetVelocity: 10, maxTorque: .005});
}

const label = createPanel({widthPixels: 640, heightPixels: 64, widthMeters: .4});
panelLoadFont(label, 'assets/fonts/montserrat/Montserrat-SemiBold.ttf');
// A nameplate along the desk's front edge.
panelSetTransform(label, 'world', DESK.x, TOP + .025, DESK.z - .26);
function drawLabel() {
  const c = label.canvas, text = `A/X: spinner motor ${motor ? 'on' : 'off'}    Bumper: reset`;
  c.clear(30, 30, 36, 220);
  c.text((640 - (c.measureText(text, 30) ?? 0)) / 2, 14, text, 230, 235, 245, 255, 30);
  c.present();
}

function reset() {
  if (cradle.some(ball => held(ball.id)) || held(spinner)) return false;
  for (const {id, rest} of cradle) {
    setObjectPosition(id, rest.x, rest.y, rest.z);
    setObjectRotation(id, 0, 0, 0);
    setObjectVelocity(id, 0, 0, 0);
    setObjectAngularVelocity(id, 0, 0, 0);
  }
  setObjectPosition(spinner, SPIN.x, SPIN.y, SPIN.z);
  setObjectRotation(spinner, 0, 0, 0);
  setObjectAngularVelocity(spinner, 0, 0, 0);
  motor = false;
  drive();
  drawLabel();
  return true;
}

// Show a hand's laser only while it points at a toy or holds one.
const toys = new Set([spinner, ...cradle.map(ball => ball.id)]);
const aiming = {left: new Set(), right: new Set()};
function aim(hand, what, on) {
  if (!aiming[hand]) return;
  const before = aiming[hand].size > 0;
  if (on) aiming[hand].add(what); else aiming[hand].delete(what);
  if (before !== aiming[hand].size > 0) setLaserVisible(hand, aiming[hand].size > 0);
}
onInputEvent(event => {
  const {type, hand} = event;
  if (type === 'controller.button') {
    if (!event.down) return;
    if (event.control === '/input/bumper/click') reset();
    else if (event.control === '/input/a/click' || event.control === '/input/x/click') {
      motor = !motor;
      drive();
      drawLabel();
    }
    return;
  }
  if (type === 'physics.collisionStart') {
    const a = cradle.findIndex(ball => ball.id === event.objectId);
    const b = cradle.findIndex(ball => ball.id === event.otherObjectId);
    if (a >= 0 && b >= 0) handOff();
    return;
  }
  if (!toys.has(event.objectId)) return;
  if (type === 'overlay.meshHoverStart') aim(hand, `toy${event.objectId}`, true);
  else if (type === 'overlay.meshHoverEnd') aim(hand, `toy${event.objectId}`, false);
  else if (type === 'overlay.grabStart') aim(hand, 'grab', true);
  else if (type === 'overlay.grabEnd') aim(hand, 'grab', false);
});
drive();
drawLabel();
