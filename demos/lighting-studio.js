// Lighting studio.
// Controls: grab the yellow lamp with the trigger or grip and move it around the
// turntable; it carries a shadow-casting point light. Grab the blue sun paddle and
// turn it to aim the sunlight. Press the panel's buttons with the trigger to
// switch the studio environment, the lamp's shadows and the turntable on or off.
// Shows: PBR materials (metal, plastic, glass, fabric sheen), baked environment
// lighting, point and directional lights attached to objects, point-light
// shadows, scripted turntable motion, panel buttons, and lasers shown only while
// aimed.
const FONT = 'assets/fonts/montserrat/Montserrat-SemiBold.ttf';
const ENVIRONMENT = 'assets/environments/studio.psenv';
const TABLE = {x: 0, y: -.5, z: -.1}, TOP = TABLE.y + .03;
const turntable = load('assets/demos/Turntable.glb');
// Each piece: model, scale, half its height as a fraction of scale, and its place
// on the turntable as a distance from the center and an angle.
const pieces = [
  {path: 'assets/samples/ToyCar/ToyCar.glb', scale: .22, half: .195, r: .15, angle: 0, yaw: Math.PI / 2},
  {path: 'assets/samples/Lantern/Lantern.glb', scale: .3, half: .5, r: .16, angle: Math.PI / 2, yaw: 0},
  {path: 'assets/demos/MaterialSpheres.glb', scale: .2, half: .136, r: .16, angle: Math.PI, yaw: Math.PI},
  {path: 'assets/samples/CompareSheen/CompareSheen.glb', scale: .16, half: .238, r: .16, angle: -Math.PI / 2, yaw: -Math.PI / 2},
];
function load(path) {
  return loadModel(path, 768, {pbr: true, optimizeVertexCache: true});
}
const assets = [turntable, ...pieces.map(piece => piece.asset = load(piece.path))];
const table = createObject(turntable, TABLE.x, TABLE.y + .018, TABLE.z, .6);
for (const piece of pieces) piece.id = createObject(piece.asset, 0, 0, 0, piece.scale);

// The lamp's light hangs just below the bulb so the bulb does not shadow it.
const lamp = createObject(loadModel('assets/physics/YellowSphere.glb'), -.3, TOP + .45, TABLE.z - .15, .05);
const lampLight = createPointLight({objectId: lamp, position: [0, -.04, 0], color: [1, .85, .6],
  intensity: .5, range: 2.5, shadow: {enabled: true, resolution: 256, maxTriangles: 1000000}});
// The sun's rays leave the paddle's underside; tilt the paddle to aim them.
const sun = createObject(loadModel('assets/physics/ThinWallBlue.glb'), .4, TOP + .55, TABLE.z - .1, .1);
setObjectRotation(sun, 0, 0, -.6);
createDirectionalLight({objectId: sun, direction: [0, -1, 0], color: [.75, .85, 1], intensity: 1.2});
for (const id of [lamp, sun]) {
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {depthAdjust: true, handRotationAxes: {pitch: true, yaw: true, roll: true}});
}

let environment = true, shadows = true, turning = true, angle = 0, last = Date.now();
function lighting() {
  for (const asset of assets) {
    setModelLighting(asset, {intensity: 0, ambient: .02, environment: ENVIRONMENT,
                             environmentIntensity: environment ? 1 : 0});
  }
  setPointLight(lampLight, {shadow: {enabled: shadows}});
}
// Forward at yaw zero points toward you, so a heading of `a` is (sin a, -cos a).
function place() {
  setObjectYaw(table, angle);
  for (const piece of pieces) {
    const a = angle + piece.angle;
    setObjectPosition(piece.id, TABLE.x + Math.sin(a) * piece.r, TOP + piece.half * piece.scale,
                      TABLE.z - Math.cos(a) * piece.r);
    setObjectYaw(piece.id, a + piece.yaw);
  }
}
function update() {
  const now = Date.now();
  if (turning) {
    angle = (angle + .25 * Math.min(.1, (now - last) / 1000)) % (2 * Math.PI);
    place();
  }
  last = now;
}

const panel = createPanel({widthPixels: 400, heightPixels: 300, widthMeters: .22, interactive: true});
panelLoadFont(panel, FONT);
panelSetTransform(panel, 'world', -.42, TOP + .12, TABLE.z - .3);
const buttons = [
  {label: 'Environment', on: () => environment, press: () => { environment = !environment; lighting(); }},
  {label: 'Shadows', on: () => shadows, press: () => { shadows = !shadows; lighting(); }},
  {label: 'Turntable', on: () => turning, press: () => { turning = !turning; }},
].map((button, i) => ({...button, x: 20, y: 20 + i * 90, w: 360, h: 80}));
const hovered = {left: -1, right: -1};
function drawPanel() {
  const c = panel.canvas;
  c.clear(26, 30, 40, 255);
  buttons.forEach((b, i) => {
    const on = b.on(), lit = hovered.left === i || hovered.right === i ? 30 : 0;
    c.rect(b.x, b.y, b.w, b.h, (on ? 50 : 60) + lit, (on ? 120 : 64) + lit, (on ? 80 : 76) + lit, 255, true);
    const text = `${b.label}: ${on ? 'on' : 'off'}`;
    c.text(b.x + (b.w - (c.measureText(text, 34) ?? 0)) / 2, b.y + 22, text, 240, 245, 250, 255, 34);
  });
  c.present();
}
const buttonAt = (x, y) => buttons.findIndex(b => x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h);

// Show a hand's laser only while it points at the panel, the lamp or the sun.
const aiming = {left: new Set(), right: new Set()};
function aim(hand, what, on) {
  if (!aiming[hand]) return;
  const before = aiming[hand].size > 0;
  if (on) aiming[hand].add(what); else aiming[hand].delete(what);
  if (before !== aiming[hand].size > 0) setLaserVisible(hand, aiming[hand].size > 0);
}
onInputEvent(event => {
  const {type, hand} = event;
  if (type.startsWith('panel.')) {
    if (event.objectId !== panel.id || !aiming[hand]) return;
    if (type === 'panel.hoverStart') aim(hand, 'panel', true);
    else if (type === 'panel.hoverEnd') aim(hand, 'panel', false);
    const index = type === 'panel.hoverEnd' ? -1 : buttonAt(event.x, event.y);
    const pressed = type === 'panel.pointerDown' && event.control === 'left' && index >= 0;
    if (pressed) buttons[index].press();
    // Redraw only when a press or the hovered button changes what is shown.
    if (pressed || hovered[hand] !== index) {
      hovered[hand] = index;
      drawPanel();
    }
    return;
  }
  if (event.objectId !== lamp && event.objectId !== sun) return;
  if (type === 'overlay.meshHoverStart') aim(hand, `object${event.objectId}`, true);
  else if (type === 'overlay.meshHoverEnd') aim(hand, `object${event.objectId}`, false);
  else if (type === 'overlay.grabStart') aim(hand, 'grab', true);
  else if (type === 'overlay.grabEnd') aim(hand, 'grab', false);
});
lighting();
place();
drawPanel();
