// Pet fox.
// Controls: look at a spot on the floor in front of you for a moment and the fox
// trots there. Without eye tracking it wanders on its own. Grab it with the
// trigger or grip to pick it up; let go and it drops back to the floor.
// Shows: skeletal animation with blended clip transitions, gaze tracking,
// grabbing, scripted movement and facing, and the tracked floor height.
const floor = getFloorHeight() ?? -1.4;
// The fox wanders a circle on the floor in front of you.
const AREA = {x: 0, z: .9, radius: 1.3};
// The Fox model is 0.51 times as tall as it is long; its origin is its center.
const SCALE = .6, FEET = SCALE * .51 / 2;
const WALK_SPEED = .45, RUN_SPEED = 1.5, RUN_DISTANCE = 1, TURN_RATE = 4;
const fox = createObject(loadModel('assets/samples/Fox/Fox.glb'), AREA.x, floor + FEET, AREA.z, SCALE);
enableObjectInteraction(fox, {grabButton: 'both'});
setObjectInteraction(fox, {depthAdjust: true});

let mode = 'idle', clip = '', target = null, restUntil = Date.now() + 2000, fall = 0;
let yaw = 0, gazeSpot = null, last = Date.now();
function play(name, speed = 1) {
  if (clip !== name) playAnimation(fox, name, {loop: true, blendDuration: .4, speed});
  clip = name;
}
play('Survey');

const inArea = (x, z) => Math.hypot(x - AREA.x, z - AREA.z) <= AREA.radius;
function wanderTarget() {
  const angle = Math.random() * 2 * Math.PI, distance = Math.sqrt(Math.random()) * AREA.radius;
  return {x: AREA.x + Math.cos(angle) * distance, z: AREA.z + Math.sin(angle) * distance};
}
// The gaze ray meets the floor; a spot held for 0.4 s within 0.25 m becomes the target.
function gazeTarget(now) {
  const gaze = getCalibratedEyeTracking();
  if (!gaze.valid || gaze.direction.y > -.05) { gazeSpot = null; return null; }
  const t = (floor - gaze.origin.y) / gaze.direction.y;
  const x = gaze.origin.x + gaze.direction.x * t, z = gaze.origin.z + gaze.direction.z * t;
  if (!inArea(x, z)) { gazeSpot = null; return null; }
  if (!gazeSpot || Math.hypot(x - gazeSpot.x, z - gazeSpot.z) > .25) gazeSpot = {x, z, since: now};
  return now - gazeSpot.since >= 400 ? {x: gazeSpot.x, z: gazeSpot.z} : null;
}

// Model +Z faces you at yaw zero, so a heading (dx, dz) has yaw atan2(dx, -dz).
const headingYaw = (dx, dz) => Math.atan2(dx, -dz);
const wrap = angle => Math.atan2(Math.sin(angle), Math.cos(angle));
function step(seconds, now) {
  const at = getObjectTransform(fox);
  if (mode === 'held') return;
  if (mode === 'falling') {
    fall += 9.81 * seconds;
    const y = Math.max(floor + FEET, at.y - fall * seconds);
    setObjectPosition(fox, at.x, y, at.z);
    if (y === floor + FEET) { mode = 'idle'; fall = 0; restUntil = now + 1500; play('Survey'); }
    return;
  }
  const seen = gazeTarget(now);
  if (seen && (!target || Math.hypot(seen.x - target.x, seen.z - target.z) > .25)) { target = seen; mode = 'moving'; }
  if (mode === 'idle') {
    if (now < restUntil) return;
    target = wanderTarget();
    mode = 'moving';
  }
  const dx = target.x - at.x, dz = target.z - at.z, distance = Math.hypot(dx, dz);
  if (distance < .05) {
    mode = 'idle';
    target = null;
    restUntil = now + 3000 + Math.random() * 3000;
    play('Survey');
    return;
  }
  // Run when the target is far, walk when it is near; turn toward it as it goes.
  const running = distance > RUN_DISTANCE, speed = running ? RUN_SPEED : WALK_SPEED;
  play(running ? 'Run' : 'Walk', running ? 1 : 1.3);
  const turn = wrap(headingYaw(dx, dz) - yaw), limit = TURN_RATE * seconds;
  yaw = wrap(yaw + Math.max(-limit, Math.min(limit, turn)));
  // Slow down while facing away, so it turns before it sets off.
  const move = Math.min(distance, speed * seconds * Math.max(.2, Math.cos(turn)));
  setObjectPosition(fox, at.x + Math.sin(yaw) * move, floor + FEET, at.z - Math.cos(yaw) * move);
  setObjectRotation(fox, 0, yaw, 0);
}
function update() {
  const now = Date.now(), seconds = Math.min(.1, (now - last) / 1000);
  last = now;
  step(seconds, now);
}

// Show a hand's laser only while it points at the fox or holds it.
const aiming = {left: new Set(), right: new Set()};
function aim(hand, what, on) {
  if (!aiming[hand]) return;
  const before = aiming[hand].size > 0;
  if (on) aiming[hand].add(what); else aiming[hand].delete(what);
  if (before !== aiming[hand].size > 0) setLaserVisible(hand, aiming[hand].size > 0);
}
onInputEvent(event => {
  const {type, hand} = event;
  if (event.objectId !== fox || !type.startsWith('overlay.')) return;
  if (type === 'overlay.meshHoverStart') aim(hand, 'fox', true);
  else if (type === 'overlay.meshHoverEnd') aim(hand, 'fox', false);
  else if (type === 'overlay.grabStart') {
    aim(hand, 'grab', true);
    mode = 'held';
    target = null;
    play('Survey');
  } else if (type === 'overlay.grabEnd') {
    aim(hand, 'grab', false);
    // Put down level, inside the area, and let it drop to the floor.
    const at = getObjectTransform(fox), d = Math.hypot(at.x - AREA.x, at.z - AREA.z);
    const k = d > AREA.radius ? AREA.radius / d : 1;
    yaw = at.yaw;
    setObjectPosition(fox, AREA.x + (at.x - AREA.x) * k, Math.max(at.y, floor + FEET), AREA.z + (at.z - AREA.z) * k);
    setObjectRotation(fox, 0, yaw, 0);
    mode = 'falling';
    fall = 0;
  }
});
