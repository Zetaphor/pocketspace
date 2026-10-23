const platformAsset = loadModel('assets/physics/Platform.glb');
const platform = createObject(platformAsset, 0, -1, 0, 1.2);
setObjectPhysics(platform, {type: 'static'});
const boxes = ['BlueBox', 'YellowBox', 'GreenBox'].map((name, index) => {
  const asset = loadModel(`assets/physics/${name}.glb`);
  const x = (index - 1) * .22;
  const id = createObject(asset, x, .15 + index * .15, 0, .16);
  setObjectPhysics(id, {mass: .25, restitution: .15});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
  return {id, x};
});
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down ||
      event.control !== '/input/bumper/click') return;
  for (const {id, x} of boxes) {
    if (getObjectPhysics(id).held) continue;
    setObjectPosition(id, x, .3, 0);
    setObjectRotation(id, 0, 0, 0);
    setObjectVelocity(id, 0, 0, 0);
    setObjectAngularVelocity(id, 0, 0, 0);
  }
});
