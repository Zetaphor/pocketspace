let announced = false;
function update(dt) {
  const left = getControllerInput('left');
  const right = getControllerInput('right');
  if (!announced) {
    print(JSON.stringify({
      leftKeys: Object.keys(left),
      rightKeys: Object.keys(right),
      leftButtons: Object.keys(left.buttons),
      leftAnalog: left.analog,
      leftCapacitive: left.capacitive
    }));
    onInputEvent(event => print(JSON.stringify({callbackEvent: event})));
    announced = true;
  }
  for (const event of pollInputEvents()) print(JSON.stringify({queuedEvent: event}));
}
