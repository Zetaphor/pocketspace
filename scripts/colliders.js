const platformAsset = loadModel('assets/physics/ThinPlatform.glb');
const platform = createObject(platformAsset, 0, -.43, 0, 1.2);
setObjectPhysics(platform, {type: 'static', halfExtents: {x: .6, y: .03, z: .6}});
for (const [x, path] of [[-.6, 'assets/physics/ThinWallBlue.glb'], [.6, 'assets/physics/ThinWallGreen.glb']]) {
  const wall = createObject(loadModel(path), x, -.1, 0, .6);
  setObjectRotation(wall, 0, 0, Math.PI / 2);
  setObjectPhysics(wall, {type: 'static', halfExtents: {x: .3, y: .015, z: .3}});
}
const bodies = [
  {path: 'assets/samples/Box/Box.glb', x: -.18, scale: .16, collider: {halfExtents: {x: .08, y: .08, z: .08}}},
  {path: 'assets/physics/YellowSphere.glb', x: .18, scale: .18, collider: {shape: 'sphere', radius: .09}}
].map(({path, x, scale, collider}) => {
  const id = createObject(loadModel(path), x, .25, 0, scale);
  setObjectPhysics(id, {mass: .25, ...collider});
  enableObjectInteraction(id, {grabButton: 'both'});
  setObjectInteraction(id, {handRotationAxes: {pitch: true, yaw: true, roll: true}});
  return {id, x};
});
setLaserVisible('left', true);
setLaserVisible('right', true);
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down || event.control !== '/input/bumper/click') return;
  for (const {id, x} of bodies) {
    if (getObjectPhysics(id).held) continue;
    setObjectPosition(id, x, .25, 0);
    setObjectRotation(id, 0, 0, 0);
    setObjectVelocity(id, 0, 0, 0);
    setObjectAngularVelocity(id, 0, 0, 0);
  }
});
