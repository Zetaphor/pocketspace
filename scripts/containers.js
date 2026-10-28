// Bumper opens/closes one tool; its model asset and the callback stay shared.
const asset = loadModel('assets/samples/Box/Box.glb');
let tool = 0;
function toggleTool() {
  if (tool) { disposeSceneContainer(tool); tool = 0; return; }
  tool = createSceneContainer();
  for (let n = 0; n < 4; n++) {
    const id = createObject(asset, (n - 1.5) * .16, 0, 0, .12, {container: tool, renderGroup: 1});
    enableObjectInteraction(id, {grabButton: 'both'});
  }
  const panel = createPanel({widthPixels: 384, heightPixels: 64, widthMeters: .6, container: tool});
  panelSetTransform(panel, 'world', 0, .2, 0);
  panel.canvas.clear(20, 24, 32, 255);
  panel.canvas.text(12, 16, 'Bumper closes this tool', 255, 255, 255, 255, 24);
  panel.present();
}
onInputEvent(event => {
  if (event.type === 'controller.button' && event.down && event.control === '/input/bumper/click') toggleTool();
});
setLaserVisible('left', true); setLaserVisible('right', true);
toggleTool();
function update() { pollInputEvents(); }
