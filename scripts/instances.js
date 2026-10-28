// Repeated static models share a surface and use ordered GPU instancing.
const asset = loadModel('assets/Avocado.glb', 768, {gpuInstancing: true});
for (let row = 0; row < 4; row++) for (let column = 0; column < 4; column++) {
  const id = createObject(asset, (column - 1.5) * .16, (row - 1.5) * .16, 0, .12, {renderGroup: 1});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
setLaserVisible('left', true);
setLaserVisible('right', true);
function update() { pollInputEvents(); }
