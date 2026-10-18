// Visual controller beam diagnostic. The beams do not consume pointer input.
const avocado = loadModel("assets/Avocado.glb");
createObject(avocado, 0, 0, 0, .25);
setLaserVisible("left", true);
setLaserVisible("right", true);
