// Place models in front of, beside and behind the initial viewer.
// Turn around to see the car; these objects are not interactive.
const avocado = loadModel('assets/Avocado.glb');
const lantern = loadModel('assets/samples/Lantern/Lantern.glb');
const car = loadModel('assets/samples/ToyCar/ToyCar.glb');
const front = createObject(avocado, 0, 0, 0, .28);
setObjectYaw(front, Math.PI);
// Positions use the fixed initial anchor; negative Z is behind that anchor.
createObject(lantern, 1.1, 0, -1, .28);
createObject(car, 0, 0, -2.2, .28);
