const avocado = loadModel('assets/Avocado.glb');
const lantern = loadModel('assets/samples/Lantern/Lantern.glb');
const car = loadModel('assets/samples/ToyCar/ToyCar.glb');
const front = createObject(avocado, 0, 0, 0, .28);
setObjectYaw(front, Math.PI);
createObject(lantern, 1.1, 0, -1, .28);
createObject(car, 0, 0, -2.2, .28);
