// Grab and rotate Khronos's Compare Sheen model to inspect its fabric materials.
// Bumper switches between direct light and the baked environment; A/X toggles both.
const model = loadModel('assets/samples/CompareSheen/CompareSheen.glb', 768, {pbr: true});
let environment = false, enabled = true;
function lighting() {
  setModelLighting(model, {
    direction: [-.6, .3, .7], intensity: enabled && !environment ? 3 : 0, ambient: 0,
    environment: environment ? 'assets/environments/studio.psenv' : null,
    environmentIntensity: enabled ? 1 : 0
  });
}
lighting();
const object = createObject(model, 0, 0, 0, .6);
enableObjectInteraction(object, {grabButton: 'both'});
setObjectInteraction(object, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') environment = !environment;
  else if (['/input/a/click', '/input/x/click'].includes(event.control)) enabled = !enabled;
  else return;
  lighting();
});
setLaserVisible('left', true);
setLaserVisible('right', true);
