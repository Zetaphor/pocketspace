setPhysicsCollisionEvents(true);
const platformAsset = loadModel('assets/physics/Platform.glb');
const platform = createObject(platformAsset, 0, -1, 0, 1.2);
setObjectPhysics(platform, {type: 'static'});
const names = new Map([[platform, 'Platform']]);
const boxes = ['BlueBox', 'YellowBox', 'GreenBox'].map((name, index) => {
  const asset = loadModel(`assets/physics/${name}.glb`);
  const x = (index - 1) * .22;
  const id = createObject(asset, x, .15 + index * .15, 0, .16);
  setObjectPhysics(id, {mass: .25, restitution: .15});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
  names.set(id, name.replace('Box', ''));
  return {id, x};
});
setLaserVisible('left', true);
setLaserVisible('right', true);
const panel = createPanel({widthPixels: 1024, heightPixels: 160, widthMeters: .9});
panelSetTransform(panel, 'world', 0, .65, .25);
const contacts = new Set();
let lastContact = 'Drop or stack a box to see collision events.';
function drawContacts() {
  panel.canvas.clear(12, 18, 29, 255);
  panel.canvas.text(24, 20, `Touching pairs: ${contacts.size}`, 240, 245, 255, 255, 36);
  panel.canvas.text(24, 85, lastContact, 180, 215, 255, 255, 30);
  panel.present();
}
drawContacts();
onInputEvent(event => {
  if (event.type === 'physics.collisionStart' || event.type === 'physics.collisionEnd') {
    const pair = `${event.objectId}:${event.otherObjectId}`;
    if (event.type === 'physics.collisionStart') {
      contacts.add(pair);
      lastContact = `${names.get(event.objectId)} / ${names.get(event.otherObjectId)}: ${event.relativeSpeed.toFixed(1)} m/s`;
    } else contacts.delete(pair);
    drawContacts();
    return;
  }
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
