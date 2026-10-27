// Compact body groups can subdivide as their members spread apart.
for (let group = 1; group <= 4; group++) setRenderGroupAutoSplit(group, {maxSurfaces: 2, targetEyePixels: 2048, merge: true, rebalance: true});
const LIMIT = 240;
const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.55, 0, 1.2, {renderGroup: 5});
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
const assets = ['BlueBox', 'YellowBox', 'GreenBox'].map(name => loadModel(`assets/physics/${name}.glb`));
const objects = [];
let sequence = 0, status = '', nextDraw = 0, nextPrune = 0;
function add(count) {
  status = '';
  while (count-- > 0 && objects.length < LIMIT) {
    let id;
    try {
      const n = sequence++;
      id = createObject(assets[n % assets.length], ((n % 8) - 3.5) * .12, -.2 + Math.floor(objects.length / 64) * .12, (Math.floor(n / 8) % 8 - 3.5) * .12, .1, {renderGroup: 1 + Math.floor((n % 8) / 4) + 2 * Math.floor((Math.floor(n / 8) % 8) / 4)});
      setObjectPhysics(id, {mass: .2, restitution: .1});
      enableObjectInteraction(id, {grabButton: 'both'});
      objects.push(id);
    } catch (error) {
      if (id !== undefined) removeObject(id);
      status = String(error.message || error);
      break;
    }
  }
  nextDraw = 0;
}
function clear() { while (objects.length) removeObject(objects.pop()); nextDraw = 0; }
const panel = createPanel({widthPixels: 1024, heightPixels: 360, widthMeters: .9});
panelSetTransform(panel, 'world', 0, .55, .25);
function update() {
  if (Date.now() >= nextPrune) {
    nextPrune = Date.now() + 100;
    for (let i = objects.length - 1; i >= 0; i--) {
      const p = getObjectPhysics(objects[i]).position;
      if (p.y < -1.2 || p.x * p.x + p.y * p.y + p.z * p.z > 9) {
        removeObject(objects[i]); objects.splice(i, 1); nextDraw = 0;
      }
    }
  }
  if (Date.now() < nextDraw) return;
  nextDraw = Date.now() + 500;
  const s = getEngineStats(), c = panel.canvas;
  c.clear(12, 18, 29, 255);
  const lines = [
    `Grouped bodies: ${objects.length}/${LIMIT}   FPS: ${s.fps === null ? '--' : s.fps.toFixed(1)}`,
    `CPU: ${s.cpuPercent === null ? '--' : s.cpuPercent.toFixed(1) + '%'}   RAM: ${(s.ramBytes / 1048576).toFixed(1)} MiB`,
    `GPU estimate: ${(s.gpuAllocatedEstimateBytes / 1048576).toFixed(1)} MiB   Workers: ${s.physicsWorkers}`,
    'Bumper: +10   A/X: +40   B/Y: clear',
    status || 'Grip/trigger: grab individual bodies.'
  ];
  lines.forEach((line, i) => c.text(24, 16 + i * 65, line, 230, 240, 255, 255, 30));
  panel.present();
}
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') add(10);
  else if (event.control === '/input/a/click' || event.control === '/input/x/click') add(40);
  else if (event.control === '/input/b/click' || event.control === '/input/y/click') clear();
});
setLaserVisible('left', true);
setLaserVisible('right', true);
add(16);
