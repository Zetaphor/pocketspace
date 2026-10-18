// Point the controller laser at the overlay, hold the trigger to move the car,
// and release to anchor it at the pointer position.
enablePointerInput();
const car = loadModel("assets/samples/ToyCar/ToyCar.glb");
const object = createObject(car, 0, 0, 0, 0.26);
let held = false;
function update(dt) {
  const pointer = getPointer();
  if (pointer.valid && pointer.down) {
    setObjectPosition(object, pointer.x, pointer.y, 0);
    if (!held) print("pointer: moving car");
    held = true;
  } else if (held) {
    print("pointer: car anchored");
    held = false;
  }
}
