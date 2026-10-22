const assets = [loadModel('assets/Avocado.glb', 768), loadModel('assets/samples/Box/Box.glb', 768)];
const objects = [createObject(assets[0], -.3, 0, 0, .25), createObject(assets[1], .3, 0, 0, .25)];
for (const objectId of objects) setObjectInteraction(objectId, {
  grabbable: true, directGrab: true, laserYaw: true, depthAdjust: true,
  handRotationAxes: {pitch: true, yaw: true, roll: true}
});
setLaserVisible('left', true);
setLaserVisible('right', true);
const held = {left: 0, right: 0};
const meshHover = {left: 0, right: 0};
onInputEvent(event => {
  if (event.type === 'controller.button' && event.control === '/input/trigger/click')
    print(`TRIGGER hand=${event.hand} down=${event.down} mesh=${meshHover[event.hand]} held=${held[event.hand]}`);
  if (event.type === 'overlay.meshHoverStart') meshHover[event.hand] = event.objectId;
  if (event.type === 'overlay.meshHoverEnd' && meshHover[event.hand] === event.objectId) meshHover[event.hand] = 0;
  if (event.type === 'overlay.grabStart') held[event.hand] = event.objectId;
  if (event.type === 'overlay.grabEnd' && held[event.hand] === event.objectId) held[event.hand] = 0;
  if (['overlay.meshHoverStart', 'overlay.meshHoverEnd', 'overlay.surfaceHoverStart',
       'overlay.surfaceHoverEnd', 'overlay.grabStart', 'overlay.grabEnd'].includes(event.type)) {
    print(`${event.type} hand=${event.hand} object=${event.objectId} mesh=${meshHover[event.hand]} held=${held[event.hand]}`);
  }
  if (event.type === 'overlay.grabStart' && held.left && held.right && held.left !== held.right)
    print(`SIMULTANEOUS_GRABS left=${held.left} right=${held.right}`);
});
function update() { pollInputEvents(); }
