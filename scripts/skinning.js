const asset = loadModel('assets/samples/CesiumMan/CesiumMan.glb');
for (const [x, speed, time] of [[-.35, 1, 0], [.35, .6, .5]]) {
  const object = createObject(asset, x, 0, 0, .6);
  playAnimation(object, 0, {loop: true, speed, time});
  enableObjectInteraction(object, {grabButton: 'both'});
  setObjectInteraction(object, {
    handRotationAxes: {pitch: true, yaw: true, roll: true}
  });
}
setLaserVisible('left', true);
setLaserVisible('right', true);
