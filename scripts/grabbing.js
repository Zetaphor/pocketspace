// Grab a box with either controller using grip or trigger.
// Nearby grabs follow hand rotation; distant grabs use the stick for yaw/depth.
const asset = loadModel('assets/samples/Box/Box.glb');
const object = createObject(asset, 0, 0, 0, .3);
// enableObjectInteraction is the short form; these options add distant and near-hand controls.
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
