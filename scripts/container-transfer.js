// Transfer a grabbable box and its panel between two independent lifetime containers.
// B/Y transfers; A/X disposes the left container; bumper disposes the right. Reload to reset.
const owners = [createSceneContainer(), createSceneContainer()];
let side = 0;
const box = createObject(loadModel('assets/samples/Box/Box.glb'), 0, 0, 0, .2, {container: owners[side]});
enableObjectInteraction(box, {grabButton: 'both'});
const label = createPanel({widthPixels: 512, heightPixels: 64, widthMeters: .65, container: owners[side]});
panelSetTransform(label, 'world', 0, .2, 0);
function describe() {
  const states = owners.map(getSceneContainerState);
  print(JSON.stringify(states));
  if (states[side].disposed) return; // Disposal invalidates the old object and panel handles.
  label.canvas.clear(20, 24, 32, 255);
  label.canvas.text(12, 16, `Owned by ${side === 0 ? 'left' : 'right'} container`, 255, 255, 255, 255, 24);
  label.present();
}
describe();
// Root callback lifetime lets it keep reporting state after either container closes.
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (['/input/b/click', '/input/y/click'].includes(event.control)) {
    const destination = 1 - side;
    if (getSceneContainerState(owners[side]).disposed || getSceneContainerState(owners[destination]).disposed) return;
    // Ownership changes cleanup scope, not placement, physics or render grouping.
    setObjectContainer(box, owners[destination]);
    setPanelContainer(label, owners[destination]);
    side = destination;
  } else if (['/input/a/click', '/input/x/click'].includes(event.control)) disposeSceneContainer(owners[0]);
  else if (event.control === '/input/bumper/click') disposeSceneContainer(owners[1]);
  else return;
  describe();
});
setLaserVisible('left', true);
setLaserVisible('right', true);
