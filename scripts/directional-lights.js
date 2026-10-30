// Rotate a grabbable sky-light handle to aim scene-wide lighting at a PBR car.
// Bumper changes color; A/X toggles the key light; B/Y captures then restores.
const asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768,
  {pbr: true, optimizeVertexCache: true});
setModelLighting(asset, {intensity: 0, ambient: .02,
  environment: 'assets/environments/studio.psenv', environmentIntensity: .2});
let tool = createSceneContainer();
const car = createObject(asset, 0, 0, 0, .35, {container: tool});
const handle = createObject(loadModel('assets/physics/ThinWallBlue.glb'),
  .28, .14, -.35, .1, {container: tool});
for (const object of [car, handle]) {
  enableObjectInteraction(object, {grabButton: 'both'});
  setObjectInteraction(object, {laserYaw: true, depthAdjust: true,
    handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
// Direction describes ray travel. Position has no effect on a directional light.
let light = createDirectionalLight({objectId: handle, direction: [0, -.4, 1],
  intensity: 3, color: [1, .85, .65], container: tool});
createDirectionalLight({direction: [-1, .2, .5], color: [.3, .5, 1],
  intensity: .25, container: tool});
let cool = false, snapshot = null;
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') {
    cool = !cool;
    setDirectionalLight(light, {color: cool ? [.55, .75, 1] : [1, .85, .65]});
  } else if (['/input/a/click', '/input/x/click'].includes(event.control)) {
    setDirectionalLight(light, {enabled: !getDirectionalLight(light).enabled});
  } else if (['/input/b/click', '/input/y/click'].includes(event.control)) {
    if (!snapshot) { snapshot = captureSceneContainer(tool); print('Captured lighting; B/Y restores it.'); }
    else {
      const restored = restoreSceneContainer(snapshot);
      disposeSceneContainer(tool);
      tool = restored.containerId;
      light = restored.lightMap[snapshot.lights[0].id];
      cool = getDirectionalLight(light).color[0] < .8;
    }
  }
});
setLaserVisible('left', true);
setLaserVisible('right', true);
