// Interactive boxes/models with two islands in one logical render group.
// Global overrides are only for the bounded native regression runner.
if (globalThis.splitEnabled !== false) setRenderGroupAutoSplit(1, {maxSurfaces: globalThis.splitLimit || 4, targetEyePixels: 1024, merge: globalThis.splitMerge !== false, rebalance: globalThis.splitRebalance !== false});
const floor = createObject(loadModel('assets/physics/ThinPlatform.glb'), 0, -.5, .2, 1.5, {renderGroup: 2});
setObjectPhysics(floor, {type: 'static', halfExtents: {x: .75, y: .03, z: .75}});
const assets = ['assets/physics/BlueBox.glb', 'assets/physics/YellowBox.glb', 'assets/samples/ToyCar/ToyCar.glb', 'assets/Avocado.glb'].map(path => loadModel(path));
const bodies = [];
let models = !!globalThis.splitModels, wide = true, nextDraw = 0;
const started = Date.now();
function bodyPosition(n, mixed = false) {
  if (!wide) {
    const slot = [0, 1, 2, 3, 5, 4, 7, 6, 8, 9, 10, 11, 13, 12, 15, 14][n];
    return {x: (slot % 4 - 1.5) * .09, y: -.25 + Math.floor(slot / 4) * .09, z: .1};
  }
  const slot = mixed ? Math.floor(n / 2) : n % 8;
  return {x: (mixed ? (n % 2 ? -1 : 1) : (n < 8 ? -1 : 1)) * .55 + (slot % 2) * .09, y: -.25 + Math.floor(slot / 2) * .09, z: .1};
}
function moveBodies(mixed = false) {
  bodies.forEach((id, n) => { const p = bodyPosition(n, mixed); setObjectPosition(id, p.x, p.y, p.z); });
  nextDraw = 0;
}
function rebuild() {
  while (bodies.length) removeObject(bodies.pop());
  for (let n = 0; n < 16; n++) {
    const p = bodyPosition(n);
    const id = createObject(assets[(models ? 2 : 0) + n % 2], p.x, p.y, p.z, .08, {renderGroup: 1});
    setObjectPhysics(id, {type: globalThis.splitKinematic ? 'kinematic' : 'dynamic', mass: .2});
    if (!globalThis.splitKinematic) {
      enableObjectInteraction(id, {grabButton: 'both'});
      setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
    }
    bodies.push(id);
  }
}
const panel = createPanel({widthPixels: 1024, heightPixels: 300, widthMeters: .9});
panelSetTransform(panel, 'world', 0, .55, .2);
function update() {
  if (Date.now() < nextDraw) return;
  nextDraw = Date.now() + 500;
  const s = getEngineStats(), c = panel.canvas;
  c.clear(12, 18, 29, 255);
  [
    `Adaptive group: ${models ? 'models' : 'boxes'} / ${wide ? 'wide' : 'compact'} / ${bodies.length} bodies`,
    `FPS: ${s.fps === null ? '--' : s.fps.toFixed(1)}   GPU estimate: ${(s.gpuAllocatedEstimateBytes / 1048576).toFixed(1)} MiB`,
    'Bumper: move wide/compact   A/X: boxes/models   B/Y: clear',
    'Grip/trigger: grab. Move your head to check occlusion.'
  ].forEach((line, i) => c.text(24, 12 + i * 65, line, 230, 240, 255, 255, 28));
  panel.present();
  if (globalThis.splitKinematic) print('SPLIT_SAMPLE ' + JSON.stringify({...s, seconds: (Date.now() - started) / 1000, stage: globalThis.regroupStage, stageAge: globalThis.regroupPhaseStarted ? (Date.now() - globalThis.regroupPhaseStarted) / 1000 : 0, bodyIds: bodies}));
}
onInputEvent(event => {
  if (globalThis.splitKinematic || event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') { wide = !wide; moveBodies(); }
  else if (event.control === '/input/a/click' || event.control === '/input/x/click') { models = !models; rebuild(); }
  else if (event.control === '/input/b/click' || event.control === '/input/y/click') { while (bodies.length) removeObject(bodies.pop()); }
});
setLaserVisible('left', !globalThis.splitKinematic);
setLaserVisible('right', !globalThis.splitKinematic);
rebuild();
