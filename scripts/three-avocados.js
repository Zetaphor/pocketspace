const avocadoModel = loadModel("assets/Avocado.glb");
const left = createObject(avocadoModel, -0.22, 0, 0, 0.25);
const middle = createObject(avocadoModel, 0, 0, 0, 0.25);
const right = createObject(avocadoModel, 0.22, 0, 0, 0.25);
let angle = 0;
function update(dt) {
  angle += dt * 0.5;
  setObjectYaw(middle, Math.PI + angle);
  setObjectYaw(left, Math.PI - angle * 0.5);
  setObjectYaw(right, Math.PI);
  setObjectPosition(right, 0.22, Math.sin(angle) * 0.03, 0);
}
