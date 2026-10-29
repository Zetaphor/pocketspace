// Automatic PBR LOD. Approach for full detail; grabbing forces it too.
// Run with the normal engine; pbr-lod-demo additionally logs transitions.
const names = ['ToyCar', 'NormalTangentTest'];
for (let i = 0; i < names.length; i++) {
  const name = names[i];
  const asset = loadModel(`assets/samples/${name}/${name}.glb`, 768, {pbr: true});
  setModelLighting(asset, {environment: 'assets/environments/studio.psenv', ambient: 0});
  const object = createObject(asset, (i - .5) * .42, 0, 0, .3, {renderGroup: 1});
  enableObjectInteraction(object, {grabButton: 'both'});
  setObjectInteraction(object, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
setLaserVisible('left', true);
setLaserVisible('right', true);
function update() { pollInputEvents(); }
