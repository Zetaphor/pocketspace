// Bumper closes/reopens a nested model tool; A/X releases its unused model asset.
// B/Y captures an open tool, or restores the saved subtree after closing it.
let asset = 0, tool = 0, snapshot = null;
function openTool() {
  // An unloaded path can be loaded again with a fresh ID and different options.
  if (!asset) asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768,
    {pbr: true, optimizeVertexCache: true});
  tool = createSceneContainer();
  const child = createSceneContainer({parent: tool});
  const car = createObject(asset, 0, 0, 0, .35, {container: child});
  enableObjectInteraction(car, {grabButton: 'both'});
}
openTool();
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') {
    if (tool) { disposeSceneContainer(tool); tool = 0; }
    else openTool();
  } else if (['/input/a/click', '/input/x/click'].includes(event.control)) {
    if (tool) { print('Close the tool with bumper before unloading its model.'); return; }
    if (asset) { unloadModel(asset); asset = 0; print('Released unused model.'); }
  } else if (['/input/b/click', '/input/y/click'].includes(event.control)) {
    if (tool) { snapshot = captureSceneContainer(tool); print('Captured subtree. Close it, then B/Y restores.'); }
    else if (snapshot) {
      // Restore reloads the saved path even if its old asset ID was released.
      tool = restoreSceneContainer(snapshot).containerId;
      asset = loadModel('assets/samples/ToyCar/ToyCar.glb');
    }
  }
});
setLaserVisible('left', true);
setLaserVisible('right', true);
