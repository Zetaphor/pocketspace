// Fixed world anchor: +x right, +y up, +z farther into the scene.
const avocadoModel = loadModel("assets/Avocado.glb");
const avocado = createObject(avocadoModel, 0, 0, 0, 0.55);
// The source GLB has a half-turn node transform; retain the verified facing.
setObjectYaw(avocado, Math.PI);
function update(dt) {}
