// Compare world-fixed, head-locked, explicitly rotated and object-following panels.
// A/X toggles visibility; B/Y removes or recreates the head-locked panel.
// Grab the box with the trigger; its label follows it and turns to face you.
function make(label, mode, x, y, z, options) {
  const panel = createPanel({widthPixels: 384, heightPixels: 96, widthMeters: .45});
  panelSetTransform(panel, mode, x, y, z, options);
  panel.canvas.clear(20, 70, 40, 255);
  panel.canvas.text(12, 24, label, 255, 255, 255, 255, 24);
  panel.present();
  return panel;
}
const world = make('World fixed', 'world', 0, .3, 0);
const tilted = make('Arbitrary rotation', 'arbitrary', -.45, 0, 0);
// Arbitrary placement uses a row-major 3x3 rotation, here a 20-degree yaw.
const angle = Math.PI / 9, c = Math.cos(angle), s = Math.sin(angle);
panelSetRotation(tilted, c, 0, s, 0, 1, 0, -s, 0, c);
const box = createObject(loadModel('assets/samples/Box/Box.glb'), .45, -.1, 0, .15);
enableObjectInteraction(box);
setLaserVisible('right', true);
// The label sits 15 cm above the box, in the box's own frame.
const label = make('Follows the box', 'object', 0, .15, 0, {objectId: box, viewerFacing: true});
let head = make('Head locked', 'headLocked', .3, -.2, .8), visible = true;
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (['/input/a/click', '/input/x/click'].includes(event.control)) {
    visible = !visible;
    for (const panel of [world, tilted, label, head]) if (panel) panelSetVisible(panel, visible);
  } else if (['/input/b/click', '/input/y/click'].includes(event.control)) {
    if (head) { removePanel(head); head = null; }
    else {
      head = make('Head locked', 'headLocked', .3, -.2, .8);
      panelSetVisible(head, visible);
    }
  }
});
