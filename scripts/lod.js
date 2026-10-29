// Show automatic geometry LOD on sixteen grabbable cars sharing one surface.
// Move closer or grab a car to restore full detail; textures keep their resolution.
const asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768);
for (let row = 0; row < 4; row++) for (let column = 0; column < 4; column++) {
  const id = createObject(asset, (column - 1.5) * .16, (row - 1.5) * .16, 0, .12, {renderGroup: 1});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
}
setLaserVisible('left', true);
setLaserVisible('right', true);
function update() { pollInputEvents(); }
