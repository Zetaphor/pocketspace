// Move the boxes; bumper saves, A/X restores the saved arrangement.
const model = loadModel('assets/samples/Box/Box.glb');
let tool = createSceneContainer();
for (let n = 0; n < 4; n++) {
  const id = createObject(model, (n - 1.5) * .16, 0, 0, .12, {container: tool, renderGroup: 1});
  enableObjectInteraction(id, {grabButton: 'both'});
}
const hud = createPanel({widthPixels: 640, heightPixels: 64, widthMeters: .8});
panelSetTransform(hud, 'world', 0, .25, 0);
function status(text) {
  hud.canvas.clear(20, 24, 32, 255);
  hud.canvas.text(12, 16, text, 255, 255, 255, 255, 24); hud.present();
}
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  try {
    if (event.control === '/input/bumper/click') { saveSceneContainer(tool, 'boxes'); status('Saved. A/X restores the boxes.'); }
    if (['/input/a/click', '/input/x/click'].includes(event.control)) {
      const restored = loadSceneContainer('boxes');
      disposeSceneContainer(tool); tool = restored.containerId; status('Restored. Bumper saves again.');
    }
  } catch (error) { status(String(error)); }
});
status('Bumper saves; A/X restores after moving.');
setLaserVisible('left', true); setLaserVisible('right', true);
function update() { pollInputEvents(); }
