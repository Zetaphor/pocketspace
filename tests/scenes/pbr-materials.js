// Non-interactive material gallery. No pointer capture, HUD input or lasers.
// The million-triangle sphere grids stay in the offscreen regression.
const names = ['NormalTangentTest', 'NormalTangentMirrorTest', 'NegativeScaleTest',
  'TextureEncodingTest', 'UnlitTest', 'EmissiveStrengthTest'];
for (let i = 0; i < names.length; i++) {
  const name = names[i];
  const asset = loadModel(`assets/samples/${name}/${name}.glb`, 768, {pbr: true});
  setModelLighting(asset, {direction: [-.4, .8, .5], intensity: 3, ambient: 0, environment: 'assets/environments/studio.psenv'});
  const object = createObject(asset, (i % 3 - 1) * .4, (.5 - Math.floor(i / 3)) * .38, 0, .32);
  setObjectInteraction(object, {laserPassThrough: true, grabbable: false});
}
function update() {}
