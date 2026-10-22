const asset = loadModel('assets/samples/Box/Box.glb');
const object = createObject(asset, 0, 0, 0, .3);
setObjectInteraction(object, {
  grabbable: true,
  grabButton: 'both',
  directGrab: true,
  laserYaw: true,
  depthAdjust: true,
  handRotationAxes: {pitch: true, yaw: true, roll: true}
});
setLaserVisible('left', true);
setLaserVisible('right', true);
