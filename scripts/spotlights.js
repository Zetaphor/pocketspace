// Carry a flashlight around a PBR car; the beam follows the handle's position and rotation.
// Bumper widens/narrows the cone; A/X toggles it; B/Y captures then restores the tool.
const asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768, {pbr: true, optimizeVertexCache: true});
setModelLighting(asset, {intensity: 0, ambient: .04});
let tool = createSceneContainer();
const car = createObject(asset, 0, 0, 0, .35, {container: tool});
enableObjectInteraction(car, {grabButton: 'both'});
const handle = createObject(loadModel('assets/physics/ThinWallBlue.glb'), 0, .12, -.45, .1, {container: tool});
enableObjectInteraction(handle, {grabButton: 'both'});
// Close grabs follow hand rotation; distant grabs can use stick yaw/depth.
for (const object of [car, handle]) setObjectInteraction(object, {
  laserYaw: true, depthAdjust: true, handRotationAxes: {pitch: true, yaw: true, roll: true}
});
// The beam emits toward local +Z, away from the viewer initially. Angles are half-angles in radians.
let light = createSpotLight({objectId: handle, position: [0, 0, .06], direction: [0, 0, 1],
  intensity: .3, range: 2, innerConeAngle: .25, outerConeAngle: .45, container: tool});
let wide = false, snapshot = null;
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') {
    wide = !wide;
    setSpotLight(light, {innerConeAngle: wide ? .5 : .25, outerConeAngle: wide ? .8 : .45});
  } else if (['/input/a/click', '/input/x/click'].includes(event.control)) {
    setSpotLight(light, {enabled: !getSpotLight(light).enabled});
  } else if (['/input/b/click', '/input/y/click'].includes(event.control)) {
    if (!snapshot) { snapshot = captureSceneContainer(tool); print('Captured flashlight; B/Y restores it.'); }
    else {
      const restored = restoreSceneContainer(snapshot);
      disposeSceneContainer(tool);
      tool = restored.containerId;
      light = restored.lightIds[0]; // Attachment was remapped to the restored handle automatically.
      wide = getSpotLight(light).outerConeAngle > .6;
    }
  }
});
setLaserVisible('left', true);
setLaserVisible('right', true);
