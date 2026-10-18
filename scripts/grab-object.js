// Point at the car, hold either trigger, move your controller in 6DoF,
// and release to leave the car at its new world position.
const car = loadModel("assets/samples/ToyCar/ToyCar.glb");
const object = createObject(car, 0, 0, 0, 0.26);
enableObjectInteraction(object);
let held = false;
let grip;
let activeHand = null;
const objectPosition = { x: 0, y: 0, z: 0 };
function update(dt) {
  const pointer = getPointer();
  if (!held && pointer.down && pointer.hand) activeHand = pointer.hand;
  const hand = activeHand ? getController(activeHand) : { valid: false };
  if (pointer.down && hand.valid) {
    if (!held) {
      const dx = objectPosition.x - hand.x;
      const dy = objectPosition.y - hand.y;
      const dz = objectPosition.z - hand.z;
      const r = hand.rotation;
      grip = [r[0]*dx+r[3]*dy+r[6]*dz,
              r[1]*dx+r[4]*dy+r[7]*dz,
              r[2]*dx+r[5]*dy+r[8]*dz];
      held = true;
      print("car grabbed");
    }
    const r = hand.rotation;
    objectPosition.x = hand.x + r[0]*grip[0] + r[1]*grip[1] + r[2]*grip[2];
    objectPosition.y = hand.y + r[3]*grip[0] + r[4]*grip[1] + r[5]*grip[2];
    objectPosition.z = hand.z + r[6]*grip[0] + r[7]*grip[1] + r[8]*grip[2];
    setObjectPosition(object, objectPosition.x, objectPosition.y, objectPosition.z);
  } else if (held) {
    held = false;
    activeHand = null;
    print("car anchored");
  }
}
