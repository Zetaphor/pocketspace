// Load three assets and animate their object transforms independently.
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
  // Scale is the model's normalized maximum extent in meters.
  setObjectScale(lanternObject, .3 + Math.sin(angle) * .03);
}
