// A parent tool owns two child modules; bumper closes/reopens the whole hierarchy.
// A/X moves the yellow module between the tool and root; B/Y captures/restores the tool.
const box = loadModel('assets/samples/Box/Box.glb');
const yellow = loadModel('assets/physics/YellowBox.glb');
let tool = 0, module = 0, detached = false, snapshot = null, snapshotModule = 0;
function openTool() {
  tool = createSceneContainer();
  const left = createSceneContainer({parent: tool});
  module = createSceneContainer({parent: tool});
  for (const [asset, x, owner] of [[box, -.16, left], [yellow, .16, module]]) {
    const object = createObject(asset, x, 0, 0, .15, {container: owner, renderGroup: 1});
    enableObjectInteraction(object, {grabButton: 'both'});
  }
  detached = false;
}
openTool();
// Keep the callback at root lifetime so it can reopen a disposed tool.
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') {
    if (tool) {
      disposeSceneContainer(tool);
      tool = 0; // A detached yellow module survives its former parent.
    } else {
      if (detached) disposeSceneContainer(module); // Retire it before building a new tool.
      openTool();
    }
  } else if (tool && ['/input/a/click', '/input/x/click'].includes(event.control)) {
    detached = !detached;
    setSceneContainerParent(module, detached ? 0 : tool);
  } else if (tool && ['/input/b/click', '/input/y/click'].includes(event.control)) {
    if (!snapshot) { snapshot = captureSceneContainer(tool); snapshotModule = module; }
    else {
      const restored = restoreSceneContainer(snapshot);
      disposeSceneContainer(tool);
      if (detached) disposeSceneContainer(module);
      tool = restored.containerId;
      // The source module can be absent when captured while detached.
      module = restored.containerMap[snapshotModule];
      if (!module) module = createSceneContainer({parent: tool});
      detached = false;
    }
  }
});
setLaserVisible('left', true);
setLaserVisible('right', true);
