// Deterministic fast movers: stable IDs repeatedly cross old tile boundaries.
// Override motionCount (up to 240), motionModels, and motionAdaptive in a runner.
// motionGroups=2 selects two fixed moving islands for cluster comparisons.
const count = globalThis.motionCount || 128;
const explicitGroups = globalThis.motionGroups || 0;
if (globalThis.motionAdaptive !== false)
  setRenderGroupAutoSplit(1, {maxSurfaces: 8, targetEyePixels: 1024, merge: true, rebalance: true});
const assets = (globalThis.motionModels ?
  (globalThis.motionSimpleModels ? ['assets/Avocado.glb', 'assets/Avocado.glb'] : ['assets/samples/ToyCar/ToyCar.glb', globalThis.motionCarsOnly ? 'assets/samples/ToyCar/ToyCar.glb' : 'assets/Avocado.glb']) :
  ['assets/physics/BlueBox.glb', 'assets/physics/YellowBox.glb']).map(path => loadModel(path, 768, {pbr: !!globalThis.motionPbr, lod: globalThis.motionLOD !== false, optimizeVertexCache: !!globalThis.motionCache, gpuInstancing: !!globalThis.motionInstancing}));
// Fixed mixed lights isolate shading cost while identical models keep moving.
if (globalThis.motionPbr) {
  for (const asset of assets) setModelLighting(asset, {intensity: .3, ambient: .04});
  for (let n = 0; n < (globalThis.motionLights || 0); n++) {
    const options = {position: [(n % 4 - 1.5) * .4, n < 4 ? .3 : -.3, -.6],
      color: [1, .8, .6], intensity: .15, range: 2};
    if (n % 2) createSpotLight({...options, direction: [0, 0, 1], innerConeAngle: .3, outerConeAngle: .6});
    else createPointLight(options);
  }
}
const bodies = [];
for (let n = 0; n < count; n++) {
  const angle = n * 2 * Math.PI / count;
  const id = createObject(assets[n % 2], .7 * Math.cos(angle), .35 * Math.sin(angle), 0, .045, {renderGroup: explicitGroups ? 1 + Math.floor(n * explicitGroups / count) : 1});
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
    const x = explicitGroups ? (Math.floor(n * explicitGroups / count) ? .35 : -.35) + .45 * Math.sin(t * 7 + phase) : spread * Math.sin(t * 3 + phase);
    setObjectPosition(id, x, .4 * Math.sin(t * 2.3 + phase * 7), .2 * Math.cos(t * 2 + phase * 3));
    setObjectRotation(id, t * .7, t * 1.2 + n * .2, 0);
  });
  if (t >= nextReport) {
    nextReport = t + 1;
    print('MOTION_SAMPLE ' + JSON.stringify({...getEngineStats(), seconds: t, expectedObjects: count}));
  }
}
