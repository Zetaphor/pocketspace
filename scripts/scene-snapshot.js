// Capture a tool in memory and restore it without reading or writing a save file.
// Move the box; bumper captures, A/X restores. IDs are remapped on every restore.
const model = loadModel('assets/samples/Box/Box.glb');
let tool = createSceneContainer();
let box = createObject(model, 0, 0, 0, .25, {container: tool});
enableObjectInteraction(box, {grabButton: 'both'});
let snapshot = captureSceneContainer(tool);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') snapshot = captureSceneContainer(tool);
  else if (['/input/a/click', '/input/x/click'].includes(event.control)) {
    // Restore stages a new container; dispose the old one only after success.
    const restored = restoreSceneContainer(snapshot);
    disposeSceneContainer(tool);
    tool = restored.containerId;
    box = restored.objectIds[0]; // The old object ID was removed with its container.
    print(`Restored box ${box} in container ${tool}`);
  }
});
setLaserVisible('left', true);
setLaserVisible('right', true);
