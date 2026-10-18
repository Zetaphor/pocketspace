// First host integration: the JS update loop drives the verified stereo renderer.
let angle = 0;
function update(dt) {
  angle += dt * 0.25;
  setModelYaw(angle);
}
