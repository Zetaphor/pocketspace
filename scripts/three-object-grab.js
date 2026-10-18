// Grab one of three independently world-anchored models with either controller.
const avocado = loadModel("assets/Avocado.glb");
const car = loadModel("assets/samples/ToyCar/ToyCar.glb");
const lantern = loadModel("assets/samples/Lantern/Lantern.glb");
const items = [
  { name: "avocado", id: createObject(avocado, -0.36, 0, 0, 0.22), position: { x: -0.36, y: 0, z: 0 } },
  { name: "car", id: createObject(car, 0, 0, 0, 0.26), position: { x: 0, y: 0, z: 0 } },
  { name: "lantern", id: createObject(lantern, 0.36, 0, 0, 0.28), position: { x: 0.36, y: 0, z: 0 } }
];
setObjectYaw(items[0].id, Math.PI);
for (const item of items) enableObjectInteraction(item.id);
let held = null;
let grip = null;
let activeHand = null;
function update(dt) {
  if (!held) {
    for (const item of items) {
      const pointer = getPointer(item.id);
      if (!pointer.down || !pointer.hand) continue;
      const hand = getController(pointer.hand);
      if (!hand.valid) continue;
      const p = item.position, r = hand.rotation;
      const dx = p.x - hand.x, dy = p.y - hand.y, dz = p.z - hand.z;
      grip = [r[0]*dx+r[3]*dy+r[6]*dz,
              r[1]*dx+r[4]*dy+r[7]*dz,
              r[2]*dx+r[5]*dy+r[8]*dz];
      held = item;
      activeHand = pointer.hand;
      print(item.name + " grabbed");
      break;
    }
  }
  if (!held) return;
  if (!getPointer(held.id).down) {
    print(held.name + " anchored");
    held = null;
    activeHand = null;
    return;
  }
  const hand = getController(activeHand);
  if (!hand.valid) return;
  const r = hand.rotation, p = held.position;
  p.x = hand.x + r[0]*grip[0] + r[1]*grip[1] + r[2]*grip[2];
  p.y = hand.y + r[3]*grip[0] + r[4]*grip[1] + r[5]*grip[2];
  p.z = hand.z + r[6]*grip[0] + r[7]*grip[1] + r[8]*grip[2];
  setObjectPosition(held.id, p.x, p.y, p.z);
}
