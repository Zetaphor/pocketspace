// Move a box with the right stick and toggle it with right bumper.
// Also read controller pose and request a haptic pulse when toggling.
const asset = loadModel('assets/samples/Box/Box.glb');
const object = createObject(asset, 0, 0, 0, .2);
let x = 0, y = 0, visible = true;
function update(dt) {
  const input = getControllerInput('right');
  if (input.valid) {
    x += input.analog.thumbstick.x * dt * .3;
    y += input.analog.thumbstick.y * dt * .3;
    setObjectPosition(object, x, y, 0);
  }
  for (const event of pollInputEvents()) {
    if (event.hand === 'right' && event.type === 'controller.button' &&
        event.control === '/input/bumper/click' && event.down) {
      const pose = getController('right');
      print(pose.valid ? `Controller at ${pose.x}, ${pose.y}, ${pose.z}` : 'Controller pose unavailable');
      // Haptics use optional action bindings, so false can be normal when they are unavailable.
      pulse('right', .04, .35);
      visible = !visible;
      setObjectVisible(object, visible);
    }
  }
}
