// Cache-ordered full detail with automatic LOD and independent alpha surfaces.
const background = loadModel('assets/physics/ThinWallBlue.glb', 768);
const back = createObject(background, 0, 0, .25, 1.2, {renderGroup: 1});
setObjectRotation(back, Math.PI / 2, 0, 0);
setObjectInteraction(back, {laserPassThrough: true, grabbable: false});
for (const [name, x, group] of [['ToyCar', -.24, 2], ['AlphaBlendModeTest', .24, 3], ['NormalTangentTest', 0, 4]]) {
  const asset = loadModel(`assets/samples/${name}/${name}.glb`, 768, {pbr: true, optimizeVertexCache: true});
  setModelLighting(asset, {environment: 'assets/environments/studio.psenv', ambient: 0});
  const object = createObject(asset, x, name === 'NormalTangentTest' ? .3 : 0, 0, .32, {renderGroup: group});
  enableObjectInteraction(object, {grabButton: 'both'});
  setObjectInteraction(object, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
setLaserVisible('left', true);
setLaserVisible('right', true);
function update() { pollInputEvents(); }
