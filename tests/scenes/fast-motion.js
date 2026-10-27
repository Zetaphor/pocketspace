// Deterministic fast movers: stable IDs repeatedly cross old tile boundaries.
// Override motionCount (up to 240), motionModels, and motionAdaptive in a runner.
const count = globalThis.motionCount || 128;
if (globalThis.motionAdaptive !== false)
  setRenderGroupAutoSplit(1, {maxSurfaces: 8, targetEyePixels: 1024, merge: true, rebalance: true});
const assets = (globalThis.motionModels ?
  (globalThis.motionSimpleModels ? ['assets/Avocado.glb', 'assets/Avocado.glb'] : ['assets/samples/ToyCar/ToyCar.glb', 'assets/Avocado.glb']) :
  ['assets/physics/BlueBox.glb', 'assets/physics/YellowBox.glb']).map(path => loadModel(path, 768, {optimizeVertexCache: !!globalThis.motionCache}));
const bodies = [];
for (let n = 0; n < count; n++) {
  const angle = n * 2 * Math.PI / count;
  const id = createObject(assets[n % 2], .7 * Math.cos(angle), .35 * Math.sin(angle), 0, .045, {renderGroup: 1});
  setObjectPhysics(id, {type: 'kinematic'});
  bodies.push(id);
}
setLaserVisible('left', false);setLaserVisible('right', false);
const started = Date.now();let nextReport = 0;
function update() {
  const t = (Date.now() - started) / 1000;
  bodies.forEach((id, n) => {
    const phase = n * 2 * Math.PI / count;
    // Independent trajectories up to roughly 4 m/s, with repeated expansion.
    const spread = .25 + .75 * (.5 + .5 * Math.sin(t * 1.7));
    setObjectPosition(id, spread * Math.sin(t * 3 + phase), .4 * Math.sin(t * 2.3 + phase * 7), .2 * Math.cos(t * 2 + phase * 3));
    setObjectRotation(id, t * .7, t * 1.2 + n * .2, 0);
  });
  if (t >= nextReport) {
    nextReport = t + 1;
    print('MOTION_SAMPLE ' + JSON.stringify({...getEngineStats(), seconds: t, expectedObjects: count}));
  }
}
