// Basketball toss.
// Controls: grab a ball from the shelf on your right with the trigger or grip and
// throw it at the hoop. A basket counts when a ball drops down through the rim.
// A or X returns every ball to the shelf.
// Shows: dynamic bodies and throwing, a static mesh collider for the rim,
// restitution and friction, collision events for rim, backboard and floor hits,
// haptics, the tracked floor height, and a saved best streak.
const FONT = 'assets/fonts/montserrat/Montserrat-SemiBold.ttf';
const floor = getFloorHeight() ?? -1.4;
const saved = loadScriptData('basketball') ?? {};

// The hoop's parts share one authored frame with its floor at y 0 and its front
// toward +Z, which faces you at yaw zero (see assets/demos/README.md).
const HOOP_X = 0, HOOP_Z = 2;
function part(name, scale, [x, y, z]) {
  const id = createObject(loadModel(`assets/demos/${name}.glb`), HOOP_X + x, floor + y, HOOP_Z - z, scale, {renderGroup: 1});
  setObjectInteraction(id, {laserPassThrough: true});
  return id;
}
const post = part('HoopPost', 1.76, [0, .88, .025]);
const board = part('HoopBackboard', .6, [0, 1.76, .261]);
const rim = part('HoopRim', .378, [0, 1.6, .461]);
part('HoopNet', .319, [0, 1.47, .486]);
setObjectPhysics(post, {type: 'static', shape: 'mesh'});
setObjectPhysics(board, {type: 'static', restitution: .5});
setObjectPhysics(rim, {type: 'static', shape: 'mesh', restitution: .5});
const RING = {x: HOOP_X, y: floor + 1.6, z: HOOP_Z - .486}, RING_RADIUS = .15;

// An invisible floor where SteamVR tracks yours, and a shelf holding the balls.
const ground = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, floor - .05, 1, 1);
setObjectVisible(ground, false);
setObjectPhysics(ground, {type: 'static', halfExtents: {x: 6, y: .05, z: 6}, friction: .6, restitution: .6});
const SHELF = {x: .45, y: floor + .85, z: -.55};
const shelf = createObject(loadModel('assets/demos/Desk.glb'), SHELF.x, SHELF.y, SHELF.z, .6);
setObjectPhysics(shelf, {type: 'static'});
setObjectInteraction(shelf, {laserPassThrough: true});

const BALL_RADIUS = .0903, ballModel = loadModel('assets/demos/Basketball.glb');
const slot = i => ({x: SHELF.x + (i - 1) * .2, y: SHELF.y + .01 + BALL_RADIUS, z: SHELF.z});
const balls = [0, 1, 2].map(i => {
  const {x, y, z} = slot(i);
  const id = createObject(ballModel, x, y, z, BALL_RADIUS * 2);
  setObjectPhysics(id, {shape: 'sphere', mass: .6, restitution: .75, friction: .6, angularDamping: .2});
  enableObjectInteraction(id, {grabButton: 'both'});
  return {id, slot: i, hand: null, shot: null, lastY: y};
});
setPhysicsCollisionEvents(true);

let baskets = 0, shots = 0, streak = 0, best = saved.best ?? 0, status = 'Throw a ball at the hoop';
const scoreboard = createPanel({widthPixels: 800, heightPixels: 240, widthMeters: .8});
panelLoadFont(scoreboard, FONT);
panelSetTransform(scoreboard, 'world', HOOP_X, floor + 2.12, HOOP_Z - .27);
function drawScoreboard() {
  const c = scoreboard.canvas;
  c.clear(24, 28, 40, 255);
  const cells = [['Baskets', `${baskets} / ${shots}`], ['Streak', `${streak}`], ['Best streak', `${best}`]];
  cells.forEach(([label, value], i) => {
    const center = 140 + i * 260;
    c.text(center - (c.measureText(label, 28) ?? 0) / 2, 20, label, 170, 185, 210, 255, 28);
    c.text(center - (c.measureText(value, 72) ?? 0) / 2, 60, value, 255, 190, 80, 255, 72);
  });
  c.text(400 - (c.measureText(status, 36) ?? 0) / 2, 175, status, 235, 240, 250, 255, 36);
  c.present();
}

function returnBall(ball) {
  const {x, y, z} = slot(ball.slot);
  setObjectPosition(ball.id, x, y, z);
  setObjectRotation(ball.id, 0, 0, 0);
  setObjectVelocity(ball.id, 0, 0, 0);
  setObjectAngularVelocity(ball.id, 0, 0, 0);
  ball.shot = null;
  ball.lastY = y;
}
function miss(ball, text) {
  ball.shot = null;
  streak = 0;
  status = text;
  drawScoreboard();
}
function score(ball) {
  const {touched} = ball.shot;
  baskets++;
  streak++;
  status = touched.has(board) ? 'Bank shot!' : touched.has(rim) ? 'Basket!' : 'Swish!';
  if (streak > best) {
    best = streak;
    saveScriptData('basketball', {best});
  }
  if (ball.hand) pulse(ball.hand, .2, .8);
  ball.shot = null;
  drawScoreboard();
}

function update() {
  for (const ball of balls) {
    if (getObjectPhysics(ball.id).held) continue;
    const at = getObjectTransform(ball.id);
    // A basket is a downward crossing of the rim's plane inside the ring.
    if (ball.shot && ball.lastY > RING.y && at.y <= RING.y &&
        Math.hypot(at.x - RING.x, at.z - RING.z) < RING_RADIUS) score(ball);
    ball.lastY = at.y;
    // Balls that leave the court come back to the shelf.
    if (at.y < floor - 1 || Math.hypot(at.x, at.z - 1) > 6) returnBall(ball);
  }
}

// Show a hand's laser only while it points at a ball or holds one.
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
    if (!event.down || !['/input/a/click', '/input/x/click'].includes(event.control)) return;
    for (const ball of balls) if (!getObjectPhysics(ball.id).held) returnBall(ball);
    status = 'Balls back on the shelf';
    drawScoreboard();
    return;
  }
  if (type.startsWith('physics.collision')) {
    if (type !== 'physics.collisionStart') return;
    const ball = balls.find(ball => ball.id === event.objectId || ball.id === event.otherObjectId);
    if (!ball?.shot) return;
    const other = ball.id === event.objectId ? event.otherObjectId : event.objectId;
    if (other === rim || other === board) ball.shot.touched.add(other);
    else if (other === ground) miss(ball, ball.shot.touched.size ? 'So close' : 'Missed');
    return;
  }
  const ball = balls.find(ball => ball.id === event.objectId);
  if (!ball) return;
  if (type === 'overlay.meshHoverStart') aim(hand, `ball${ball.id}`, true);
  else if (type === 'overlay.meshHoverEnd') aim(hand, `ball${ball.id}`, false);
  else if (type === 'overlay.grabStart') {
    aim(hand, 'grab', true);
    if (ball.shot) miss(ball, 'Missed');
    ball.hand = hand;
  } else if (type === 'overlay.grabEnd') {
    aim(hand, 'grab', false);
    // A release with some speed is a shot; a ball set down gently is not.
    const {x, y, z} = getObjectPhysics(ball.id).velocity;
    if (Math.hypot(x, y, z) > 1) {
      shots++;
      ball.shot = {touched: new Set()};
      drawScoreboard();
    }
  }
});
drawScoreboard();
