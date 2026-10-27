const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.55, 0, 1.2);
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
const assets = ['assets/physics/BlueBox.glb', 'assets/physics/YellowBox.glb', 'assets/physics/GreenBox.glb', 'assets/samples/Box/Box.glb'].map(path => loadModel(path));
const objects = [];
let sequence = 0, spawnStatus = '';
function add(count) {
  spawnStatus = '';
  while (count-- > 0) {
    let id;
    try {
      const n = sequence++;
      id = createObject(assets[n % assets.length], ((n % 5) - 2) * .15, .05 + Math.floor(objects.length / 25) * .16, (Math.floor(n / 5) % 5 - 2) * .15, .1);
      setObjectPhysics(id, {mass: .2, restitution: .1});
      enableObjectInteraction(id, {grabButton: 'both'});
      objects.push(id);
    } catch (error) {
      if (id !== undefined) removeObject(id);
      spawnStatus = String(error.message || error);
      print(spawnStatus);
      break;
    }
  }
  nextDraw = 0;
}
function clear() { while (objects.length) removeObject(objects.pop()); spawnStatus = ''; nextDraw = 0; }

function prune() {
  let removed = false;
  for (let i = objects.length - 1; i >= 0; i--) {
    const p = getObjectPhysics(objects[i]).position;
    if (p.y < -1.2 || p.x * p.x + p.y * p.y + p.z * p.z > 9) {
      removeObject(objects[i]);
      objects.splice(i, 1);
      removed = true;
    }
  }
  return removed;
}
const panel = createPanel({widthPixels: 1024, heightPixels: 552, widthMeters: .9});
panelSetTransform(panel, 'world', 0, .55, .25);
const mib = bytes => bytes === null ? 'warming up' : `${(bytes / 1048576).toFixed(1)} MiB`;
function draw() {
  const s = getEngineStats(), c = panel.canvas;
  c.clear(12, 18, 29, 255);
  const lines = [
    `Objects: ${objects.length}   FPS: ${s.fps === null ? '--' : s.fps.toFixed(1)}`,
    `CPU: ${s.cpuPercent === null ? '--' : s.cpuPercent.toFixed(1) + '%'} (100% = one core)`,
    `RAM (process RSS): ${mib(s.ramBytes)}`,
    s.vramBytes !== null ? `GPU driver memory: ${mib(s.vramBytes)}` : `GPU allocations (estimate): ${mib(s.gpuAllocatedEstimateBytes)}`,
    `Physics: ${s.physicsSolverMs.toFixed(2)} ms   Active: ${s.physicsActiveBodies}   Workers: ${s.physicsWorkers}`,
    'Bumper: +1    A/X: +5    B/Y: clear',
    spawnStatus || 'Grip/trigger: grab. GPU shares system RAM.'
  ];
  lines.forEach((line, i) => c.text(24, 20 + i * 72, line, 230, 240, 255, 255, 31));
  panel.present();
}
let nextDraw = 0, nextPrune = 0;
function update() {
  const now = Date.now();
  if (now >= nextPrune) { if (prune()) nextDraw = 0; nextPrune = now + 100; }
  if (now >= nextDraw) { draw(); nextDraw = now + 500; }
}
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') add(1);
  else if (event.control === '/input/a/click' || event.control === '/input/x/click') add(5);
  else if (event.control === '/input/b/click' || event.control === '/input/y/click') clear();
});
setLaserVisible('left', true);
setLaserVisible('right', true);
add(4);
draw();
