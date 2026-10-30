// Static full-detail cars and fixed lights for matched engine depth measurements.
// Run only with the profiling engine's canonical-view flag; no controller input.
const car = loadModel('assets/samples/ToyCar/ToyCar.glb', 512,
  {pbr: true, lod: false, optimizeVertexCache: true, gpuInstancing: true});
setModelLighting(car, {intensity: .3, ambient: .04});
for (let n = 0; n < 8; ++n) {
  const light = {position: [(n % 4 - 1.5) * .4, n < 4 ? .3 : -.3, -.6],
    color: [1, .8, .6], intensity: .15, range: 2};
  if (n % 2) createSpotLight({...light, direction: [0, 0, 1], innerConeAngle: .3, outerConeAngle: .6});
  else createPointLight(light);
}
for (let n = 0; n < 32; ++n) {
  const column = n % 8, row = Math.floor(n / 8);
  // Overlapping neighboring groups exercise external depth as well as color.
  const group = globalThis.depthColorControl ? 1 : 1 + Math.floor(column / 4) + 2 * Math.floor(row / 2);
  const object = createObject(car, (column - 3.5) * .1, (row - 1.5) * .1,
    (n % 3) * .02, .16, {renderGroup: group});
  setObjectRotation(object, .15, .3 + n * .04, 0);
  setObjectInteraction(object, {laserPassThrough: true, grabbable: false});
}
setLaserVisible('left', false);setLaserVisible('right', false);
let nextReport = 0;
function update() {
  if (Date.now() >= nextReport) {
    nextReport = Date.now() + 1000;
    print('DEPTH_CONTROL_STATS ' + JSON.stringify(getEngineStats()));
  }
}
