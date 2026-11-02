// Import another script's save. Run scene-save.js from the same catalog and save
// its boxes first; bumper then copies them here with fresh, independent IDs.
let imported = 0;
const hud = createPanel({widthPixels: 640, heightPixels: 64, widthMeters: .8});
panelSetTransform(hud, 'world', 0, .4, 0);
function status(text) {
  hud.canvas.clear(20, 24, 32, 255);
  hud.canvas.text(12, 16, text, 255, 255, 255, 255, 24); hud.present();
}
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down || event.control !== '/input/bumper/click') return;
  try {
    // Import must succeed before an earlier copy is disposed.
    const restored = importSceneContainer('scene-save.js', 'boxes');
    if (imported) disposeSceneContainer(imported);
    imported = restored.containerId; status(`Imported ${restored.objectIds.length} boxes.`);
  } catch (error) { status(String(error)); }
});
status('Bumper imports the save from scene-save.js.');
setLaserVisible('left', true); setLaserVisible('right', true);
function update() { pollInputEvents(); }
