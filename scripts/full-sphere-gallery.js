// Three fixed objects test the front, side, and rear directions around the initial headset position.
const avocado = loadModel("assets/Avocado.glb");
const car = loadModel("assets/samples/ToyCar/ToyCar.glb");
const lantern = loadModel("assets/samples/Lantern/Lantern.glb");
const front = createObject(avocado, 0, 0, 0, 0.28);
const right = createObject(lantern, 1.1, 0, -1, 0.28);
const rear = createObject(car, 0, 0, -2.2, 0.28);
enableObjectSurface(front);
enableObjectSurface(right);
enableObjectSurface(rear);
