// Open/close a tool containing grabbable boxes and a clickable panel with one lifetime.
// Bumper toggles it. Shared assets and the root update loop survive tool disposal.
const asset = loadModel('assets/samples/Box/Box.glb');
let tool = 0;
function toggleTool() {
  if (tool) { disposeSceneContainer(tool); tool = 0; return; }
  tool = createSceneContainer();
  for (let n = 0; n < 4; n++) {
    const id = createObject(asset, (n - 1.5) * .16, 0, 0, .12, {container: tool, renderGroup: 1});
    enableObjectInteraction(id, {grabButton: 'both'});
  }
  const panel = createPanel({widthPixels: 512, heightPixels: 96, widthMeters: .6, interactive: true, container: tool});
  panelSetTransform(panel, 'world', 0, .2, 0);
  let clicks = 0;
  function paint() {
    panel.canvas.clear(20, 24, 32, 255);
    panel.canvas.text(12, 16, `Tool clicks: ${clicks}`, 255, 255, 255, 255, 24);
    panel.present();
  }
  paint();
  // This callback is disposed with its panel, so it cannot use a removed handle.
  onInputEvent(event => {
    if (event.type === 'panel.pointerDown' && event.objectId === panel.id) { clicks++; paint(); }
  }, {container: tool});
}
setLaserVisible('left', true);
setLaserVisible('right', true);
toggleTool();
function update() {
  // Polling has an independent queue and survives the tool-owned callback's removal.
  for (const event of pollInputEvents()) {
    if (event.type === 'controller.button' && event.down && event.control === '/input/bumper/click') toggleTool();
  }
}
