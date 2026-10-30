// Interactive acceptance of automatic range and cone filtering with four active lights.
// Grip/trigger moves targets or lights; A/X changes range, B/Y cone width, bumper resets.
const targets = [];
for (const [index, name] of ['ToyCar', 'NormalTangentTest'].entries()) {
  const asset = loadModel(`assets/samples/${name}/${name}.glb`, 768, {pbr: true, optimizeVertexCache: true});
  setModelLighting(asset, {intensity: 0, ambient: .035});
  targets.push(createObject(asset, index ? .24 : -.24, 0, 0, .28));
}
const markers = [], lights = [];
const descriptions = [
  ['YellowSphere', [1, .12, .05], false],
  ['BlueBox', [.1, .35, 1], true],
  ['YellowBox', [1, 1, 1], true],
  ['GreenBox', [.1, 1, .2], false]
];
for (const [index, [name, color, spot]] of descriptions.entries()) {
  const marker = createObject(loadModel(`assets/physics/${name}.glb`), (index - 1.5) * .24, .12, -.4, .055);
  const options = {objectId: marker, color, intensity: .16, range: .65};
  // Both spot handles emit toward local +Z, initially toward the models.
  const light = spot
    ? createSpotLight({...options, direction: [0, 0, 1], innerConeAngle: .2, outerConeAngle: .35})
    : createPointLight(options);
  markers.push(marker); lights.push({id: light, spot});
}
for (const object of [...targets, ...markers]) {
  enableObjectInteraction(object, {grabButton: 'both'});
  setObjectInteraction(object, {laserYaw: true, depthAdjust: true,
    handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
// Keep all four lights enabled and nonzero so both automatic filters remain eligible.
let longRange = false, wide = false;
const hud = createPanel({widthPixels: 1024, heightPixels: 160, widthMeters: 1, interactive: false});
panelSetTransform(hud, 'world', 0, .4, 0);
function paint() {
  hud.canvas.clear(15, 20, 28, 245);
  hud.canvas.text(16, 16, '4 active lights: move models through light edges', 240, 245, 255, 255, 30);
  hud.canvas.text(16, 60, `A/X: range ${longRange ? '1.2 m' : '.65 m'} | B/Y: ${wide ? 'wide' : 'narrow'} cones`, 200, 225, 255, 255, 26);
  hud.canvas.text(16, 104, 'Blue/yellow boxes aim spots. Bumper resets positions.', 200, 225, 255, 255, 26);
  hud.present();
}
function configure() {
  for (const light of lights) {
    if (light.spot) setSpotLight(light.id, {range: longRange ? 1.2 : .65,
      innerConeAngle: wide ? .55 : .2, outerConeAngle: wide ? .8 : .35});
    else setPointLight(light.id, {range: longRange ? 1.2 : .65});
  }
  paint();
}
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') {
    // No physics bodies are present; reset returns the manually arranged fixtures.
    targets.forEach((id, i) => {setObjectPosition(id, i ? .24 : -.24, 0, 0);setObjectRotation(id, 0, 0, 0);});
    markers.forEach((id, i) => {setObjectPosition(id, (i - 1.5) * .24, .12, -.4);setObjectRotation(id, 0, 0, 0);});
  } else if (['/input/a/click', '/input/x/click'].includes(event.control)) longRange = !longRange;
  else if (['/input/b/click', '/input/y/click'].includes(event.control)) wide = !wide;
  else return;
  configure();
});
configure();
setLaserVisible('left', true);
setLaserVisible('right', true);
