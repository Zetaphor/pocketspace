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
      visible = !visible;
      setObjectVisible(object, visible);
    }
  }
}
