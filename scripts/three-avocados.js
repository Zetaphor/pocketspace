// Three independently controlled instances of the same runtime-loaded GLB.
const left = createObject(-0.22, 0, 0, 0.25);
const middle = createObject(0, 0, 0, 0.25);
const right = createObject(0.22, 0, 0, 0.25);
let angle = 0;
function update(dt) {
  angle += dt * 0.5;
  setObjectYaw(middle, angle);
  setObjectYaw(left, -angle * 0.5);
  setObjectPosition(right, 0.22, Math.sin(angle) * 0.03, 0);
}
