// Three model assets with independent object transforms.
const box = loadModel("assets/samples/Box/Box.glb");
const car = loadModel("assets/samples/ToyCar/ToyCar.glb");
const lantern = loadModel("assets/samples/Lantern/Lantern.glb");
const boxObject = createObject(box, -0.23, 0, 0, 0.2);
const carObject = createObject(car, 0, 0, 0, 0.26);
const lanternObject = createObject(lantern, 0.23, 0, 0, 0.3);
let angle = 0;
function update(dt) {
  angle += dt * 0.35;
  setObjectYaw(boxObject, angle);
  setObjectYaw(carObject, angle * 0.5);
}
