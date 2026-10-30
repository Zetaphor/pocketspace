// Interactive shader-variant validation: mixed materials, light counts and HDR state.
// Grip/trigger grabs; A/X cycles 8/4/0 lights, B/Y toggles HDR, bumper resets poses.
const back = createObject(loadModel('assets/physics/ThinWallBlue.glb'), 0, 0, .25, 1.2, {renderGroup: 1});
setObjectRotation(back, Math.PI / 2, 0, 0);
setObjectInteraction(back, {laserPassThrough: true, grabbable: false});
const assets = [], targets = [], poses = [];
for (const [name, x, y, group] of [['ToyCar', -.25, 0, 2], ['AlphaBlendModeTest', .25, 0, 3], ['NormalTangentTest', 0, .3, 4]]) {
  const asset = loadModel(`assets/samples/${name}/${name}.glb`, 1024, {pbr: true, optimizeVertexCache: true});
  assets.push(asset);
  const object = createObject(asset, x, y, 0, .3, {renderGroup: group});
  targets.push(object); poses.push([object, x, y, 0]);
}
const lights = [], handles = [];
for (const [index, name] of ['BlueBox', 'YellowBox', 'GreenBox', 'YellowSphere'].entries()) {
  const x = (index - 1.5) * .18;
  const handle = createObject(loadModel(`assets/physics/${name}.glb`), x, -.2, -.4, .045);
  handles.push(handle); poses.push([handle, x, -.2, -.4]);
  // Local +Z points away from the viewer and toward the models initially.
  lights.push({id: createPointLight({objectId: handle, intensity: .035, range: 1.2}), spot: false});
  lights.push({id: createSpotLight({objectId: handle, direction: [0, 0, 1], intensity: .13,
    range: 1.2, innerConeAngle: .25, outerConeAngle: .55}), spot: true});
}
for (const object of [...targets, ...handles]) {
  enableObjectInteraction(object, {grabButton: 'both'});
  setObjectInteraction(object, {laserYaw: true, depthAdjust: true,
    handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
let lightState = 0, hdr = true;
const counts = [8, 4, 0];
const hud = createPanel({widthPixels: 1024, heightPixels: 130, widthMeters: 1, interactive: false});
panelSetTransform(hud, 'world', 0, .58, 0);
function configure() {
  for (const asset of assets) setModelLighting(asset, {intensity: .2, ambient: .04,
    environment: 'assets/environments/studio.psenv', environmentIntensity: hdr ? 1 : 0});
  lights.forEach((light, i) => {
    const options = {enabled: i < counts[lightState]};
    if (light.spot) setSpotLight(light.id, options); else setPointLight(light.id, options);
  });
  hud.canvas.clear(15, 20, 28, 245);
  hud.canvas.text(16, 16, `A/X: ${counts[lightState]} lights | B/Y: HDR ${hdr ? 'on' : 'off'}`, 235, 245, 255, 255, 30);
  hud.canvas.text(16, 68, 'Grab models or light handles. Bumper resets poses.', 210, 230, 250, 255, 26);
  hud.present();
}
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (['/input/a/click', '/input/x/click'].includes(event.control)) lightState = (lightState + 1) % counts.length;
  else if (['/input/b/click', '/input/y/click'].includes(event.control)) hdr = !hdr;
  else if (event.control === '/input/bumper/click') {
    // Release held objects before resetting so their grab poses do not override this.
    poses.forEach(([id, x, y, z]) => {setObjectPosition(id, x, y, z); setObjectRotation(id, 0, 0, 0);});
  } else return;
  configure();
});
configure();
setLaserVisible('left', true);
setLaserVisible('right', true);
