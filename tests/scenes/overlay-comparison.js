// Test-only scene. A generated prefix can set globalThis.overlayComparison.
const config = globalThis.overlayComparison || {};
const counts = [16, 48, 96, 160, 240];
let renderer = config.renderer || 'single';
let geometry = config.geometry || 'boxes';
let targetCount = config.count || 16;
const dynamic = config.dynamic !== false;
const layout = config.layout || 'compact';
const spacing = layout === 'compact' ? .16 : .2;
const forwardOffset = layout === 'compact' ? .8 : 0;
const floorScale = layout === 'compact' ? 1.5 : 1.8;
const floorAsset = loadModel('assets/physics/ThinPlatform.glb');
const boxAssets = ['BlueBox', 'YellowBox', 'GreenBox'].map(name => loadModel(`assets/physics/${name}.glb`));
const modelAssets = [loadModel('assets/samples/ToyCar/ToyCar.glb'), loadModel('assets/Avocado.glb')];
let floor, objects = [], status = '', variantStarted = 0, lastSample = 0, nextDraw = 0, nextPrune = 0;
function resetScene() {
  while (objects.length) removeObject(objects.pop());
  if (floor !== undefined) { removeObject(floor); floor = undefined; }
  status = '';
  floor = createObject(floorAsset, 0, -.55, forwardOffset + 3.5 * spacing, floorScale, {renderGroup: renderer === 'single' ? 1 : 5});
  setObjectPhysics(floor, {type: 'static', halfExtents: {x: floorScale / 2, y: .03, z: floorScale / 2}});
  const assets = geometry === 'boxes' ? boxAssets : modelAssets;
  for (let n = 0; n < targetCount; n++) {
    let id;
    try {
      const column = n % 8, row = Math.floor(n / 8) % 8;
      const group = renderer === 'single' ? 1 : 1 + Math.floor(column / 4) + 2 * Math.floor(row / 4);
      id = createObject(assets[n % assets.length], (column - 3.5) * spacing, -.2 + Math.floor(n / 64) * spacing, forwardOffset + row * spacing, .14, {renderGroup: group});
      setObjectPhysics(id, {type: dynamic ? 'dynamic' : 'kinematic', shape: geometry === 'models' ? 'convex' : 'box', mass: .2, restitution: .1});
      if (config.interactive !== false) {
        enableObjectInteraction(id, {grabButton: 'both'});
        setObjectInteraction(id, {handPitch: true, handYaw: true, handRoll: true});
      }
      objects.push(id);
    } catch (error) {
      if (id !== undefined) removeObject(id);
      status = String(error.message || error); break;
    }
  }
  variantStarted = Date.now(); nextDraw = nextPrune = 0;
  print(`OVERLAY_VARIANT ${renderer} ${geometry} target=${targetCount} bodies=${objects.length} dynamic=${dynamic} layout=${layout}`);
}
function cycleCount() { targetCount = counts[(counts.indexOf(targetCount) + 1) % counts.length]; resetScene(); }
function toggleGeometry() { geometry = geometry === 'boxes' ? 'models' : 'boxes'; resetScene(); }
function toggleRenderer() { renderer = renderer === 'single' ? 'grouped' : 'single'; resetScene(); }
const panel = createPanel({widthPixels: 1024, heightPixels: 440, widthMeters: .95});
panelSetTransform(panel, 'world', 0, .6, .2);
function update() {
  const now = Date.now();
  if (now >= nextPrune) {
    nextPrune = now + 100;
    for (let i = objects.length - 1; i >= 0; i--) {
      const p = getObjectPhysics(objects[i]).position;
      if (p.y < -1.2 || p.x * p.x + p.y * p.y + p.z * p.z > 9) {
        removeObject(objects[i]); objects.splice(i, 1); nextDraw = 0;
      }
    }
  }
  const s = getEngineStats();
  if (s.sampleId !== lastSample) {
    lastSample = s.sampleId;
    print('OVERLAY_SAMPLE ' + JSON.stringify({renderer, geometry, targetCount, bodies: objects.length, dynamic, layout, variantSeconds: (now - variantStarted) / 1000, ...s}));
  }
  if (now < nextDraw) return;
  nextDraw = now + 500;
  const c = panel.canvas;
  c.clear(12, 18, 29, 255);
  const lines = [
    `${renderer.toUpperCase()} | ${geometry.toUpperCase()} | ${objects.length}/${targetCount}`,
    `FPS: ${s.fps === null ? '--' : s.fps.toFixed(1)}   CPU: ${s.cpuPercent === null ? '--' : s.cpuPercent.toFixed(1) + '%'}`,
    `RAM: ${(s.ramBytes / 1048576).toFixed(1)} MiB   GPU estimate: ${(s.gpuAllocatedEstimateBytes / 1048576).toFixed(1)} MiB`,
    'Bumper: count 16 / 48 / 96 / 160 / 240',
    'A/X: boxes / models   B/Y: single / grouped',
    status || 'Grip/trigger: grab. Each switch resets the scene.'
  ];
  lines.forEach((line, i) => c.text(24, 16 + i * 68, line, 230, 240, 255, 255, 30));
  panel.present();
}
onInputEvent(event => {
  if (config.interactive === false || event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') cycleCount();
  else if (event.control === '/input/a/click' || event.control === '/input/x/click') toggleGeometry();
  else if (event.control === '/input/b/click' || event.control === '/input/y/click') toggleRenderer();
});
setLaserVisible('left', config.lasers !== false);
setLaserVisible('right', config.lasers !== false);
resetScene();
