// Headset check for the engine laser's model hits. The debug panel is display-only.
// Sweep each beam across the models; pull the trigger on one to record a press point.
// A (right) and X (left) toggle that hand's beam: hover and grabs must work either way.
const box = createObject(loadModel('assets/physics/GreenBox.glb'), -.34, -.08, 0, .16);
enableObjectInteraction(box, {grabButton: 'both'});
setObjectInteraction(box, {laserYaw: true, depthAdjust: true});
// Not grabbable: the script handles presses itself.
const avocado = createObject(loadModel('assets/Avocado.glb'), 0, -.08, 0, .22);
// The laser passes through this wall to the avocado behind it.
const wall = createObject(loadModel('assets/physics/ThinWallBlue.glb'), 0, -.08, -.16, .22);
setObjectRotation(wall, Math.PI / 2, 0, 0);
setObjectInteraction(wall, {laserPassThrough: true});
const fox = createObject(loadModel('assets/samples/Fox/Fox.glb'), .34, -.2, 0, .2);
playAnimation(fox, 0, {loop: true});
const names = {[box]: 'box (grabbable)', [avocado]: 'avocado', [wall]: 'wall (pass-through)', [fox]: 'fox (animated)'};

const panel = createPanel({widthPixels: 1024, heightPixels: 400, widthMeters: .8, interactive: false});
panelSetTransform(panel, 'world', 0, .3, .05);
setLaserVisible('left', true);
setLaserVisible('right', true);

const hands = ['left', 'right'];
const counts = {left: {}, right: {}};
const press = {left: 'none', right: 'none'};
let grabbed = {left: 'none', right: 'none'};
const fixed = v => v.toFixed(3);
const point = p => `${fixed(p.x)}, ${fixed(p.y)}, ${fixed(p.z)}`;
const label = id => names[id] || `object ${id}`;

onInputEvent(event => {
  if (!hands.includes(event.hand)) return;
  const hand = event.hand;
  if (event.type.startsWith('overlay.meshHover')) {
    counts[hand][event.type] = (counts[hand][event.type] || 0) + 1;
    if (event.type !== 'overlay.meshHover') print(`${event.type} ${hand} ${label(event.objectId)} d=${fixed(event.distance)} p=${point(event.point)}`);
  }
  if (event.type === 'overlay.grabStart') {
    grabbed[hand] = `${label(event.objectId)} at ${point(event.point)}`;
    print(`grabStart ${hand} ${grabbed[hand]}`);
  }
  if (event.type === 'overlay.grabEnd') grabbed[hand] = 'none';
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/trigger/click') {
    const hover = getMeshHover(hand);
    press[hand] = hover.hit ? `${label(hover.objectId)} at ${point(hover.point)}` : 'nothing';
    print(`press ${hand} ${press[hand]}`);
  }
  if ((hand === 'right' && event.control === '/input/a/click') || (hand === 'left' && event.control === '/input/x/click')) {
    setLaserVisible(hand, !getLaserVisible(hand));
    print(`laser ${hand} visible=${getLaserVisible(hand)}`);
  }
});

let elapsed = 0;
function update(dt) {
  elapsed += dt;
  if (elapsed < .1) return;
  elapsed = 0;
  const c = panel.canvas;
  c.clear(8, 17, 31, 240);
  c.text(20, 12, 'Engine laser: model hits', 235, 245, 255, 255, 28);
  hands.forEach((hand, i) => {
    const y = 60 + i * 165;
    const hover = getMeshHover(hand);
    const n = counts[hand];
    c.line(20, y - 6, 1004, y - 6, 50, 130, 185, 255);
    c.text(20, y, `${hand.toUpperCase()}  beam ${getLaserVisible(hand) ? 'on' : 'off'}  ${hover.hit ? 'HIT' : 'no hit'}`,
           hover.hit ? 120 : 255, hover.hit ? 235 : 190, hover.hit ? 150 : 120, 255, 24);
    c.text(20, y + 32, `target: ${hover.hit ? label(hover.objectId) : '-'}   distance: ${hover.hit ? fixed(hover.distance) + ' m' : '-'}`, 200, 230, 255, 255, 20);
    c.text(20, y + 58, `point: ${hover.hit ? point(hover.point) : '-'}`, 200, 230, 255, 255, 20);
    c.text(20, y + 84, `events start ${n['overlay.meshHoverStart'] || 0}  hover ${n['overlay.meshHover'] || 0}  end ${n['overlay.meshHoverEnd'] || 0}`, 170, 235, 190, 255, 18);
    c.text(20, y + 108, `last press: ${press[hand]}   held: ${grabbed[hand]}`, 255, 215, 130, 255, 18);
  });
  c.present();
}
