// Carry a colored point light around a PBR car by grabbing its small marker.
// Bumper changes color; A/X toggles the light; B/Y saves/restores the lit tool in memory.
const carAsset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768, {pbr: true, optimizeVertexCache: true});
setModelLighting(carAsset, {intensity: 0, ambient: .04});
const markerAsset = loadModel('assets/physics/YellowSphere.glb');
let tool = createSceneContainer();
const car = createObject(carAsset, 0, 0, 0, .35, {container: tool});
enableObjectInteraction(car, {grabButton: 'both'});
let marker = createObject(markerAsset, -.25, .15, -.15, .035, {container: tool});
enableObjectInteraction(marker, {grabButton: 'both'});
// The light follows the marker's live pose; the marker itself uses unlit rendering.
let light = createPointLight({objectId: marker, color: [1, .25, .08], intensity: .3, range: 1.5, container: tool});
const colors = [[1, .25, .08], [.08, .4, 1], [.2, 1, .2], [1, 1, 1]];
let selected = 0, snapshot = null;
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') {
    selected = (selected + 1) % colors.length;
    setPointLight(light, {color: colors[selected]});
  } else if (['/input/a/click', '/input/x/click'].includes(event.control)) {
    setPointLight(light, {enabled: !getPointLight(light).enabled});
  } else if (['/input/b/click', '/input/y/click'].includes(event.control)) {
    if (!snapshot) { snapshot = captureSceneContainer(tool); print('Captured lit tool; B/Y restores it.'); }
    else {
      // Stage before disposing; the returned IDs also remap the light's attachment.
      const restored = restoreSceneContainer(snapshot);
      disposeSceneContainer(tool);
      tool = restored.containerId;
      light = restored.lightIds[0];
      marker = getPointLight(light).objectId;
    }
  }
});
setLaserVisible('left', true);
setLaserVisible('right', true);
