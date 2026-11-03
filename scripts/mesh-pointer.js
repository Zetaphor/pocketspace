// Custom model interaction on the engine laser: hover reports a 3D point on the mesh.
// Aim at the box and pull the trigger to print the hit point and request haptics.
const box = createObject(loadModel('assets/samples/Box/Box.glb'), 0, 0, 0, .3);
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type === 'overlay.meshHoverStart') print(`${event.hand} hover at ${event.distance.toFixed(3)} m`);
  if (event.type !== 'controller.button' || event.control !== '/input/trigger/click' || !event.down) return;
  // The box is not grabbable, so the script decides what a press on it does.
  const hover = getMeshHover(event.hand);
  if (!hover.hit || hover.objectId !== box) return;
  const {x, y, z} = hover.point;
  print(`Pressed box at ${x.toFixed(3)}, ${y.toFixed(3)}, ${z.toFixed(3)}`);
  print(`Haptic accepted: ${pulse(event.hand, .04, .35)}`);
});
