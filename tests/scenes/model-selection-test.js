// Bumper advances cases; trigger grabs. Release both hands before advancing.
const assets = {
  frame: loadModel('assets/tests/HollowFrame.glb', 768),
  box: loadModel('assets/samples/Box/Box.glb', 768),
  lantern: loadModel('assets/samples/Lantern/Lantern.glb', 768)
};
const cases = [
  {name: 'Gap: aim through blue frame at rear box', specs: [['frame',0,0,0,.5,0], ['box',0,0,.35,.16,0]]},
  {name: 'Empty gap: center must have no mesh hover', specs: [['frame',0,0,0,.5,0]]},
  {name: 'Rotated thin frame: test edges and opening', specs: [['frame',0,0,0,.5,.9]]},
  {name: 'Overlap: front box wins where silhouettes overlap', specs: [['box',-.06,0,0,.22,.5], ['box',.06,0,.3,.3,-.4]]},
  {name: 'Lantern: test openings, handle, and silhouette', specs: [['lantern',0,0,0,.4,.5]]},
  {name: 'Two hands: select distinct rotated objects', specs: [['frame',-.3,0,0,.35,.5], ['lantern',.3,0,0,.35,-.5]]}
];
let active = -1;
let objects = [];
const labels = {};
const held = {left: 0, right: 0};
const hover = {left: 0, right: 0};
const panel = createPanel({widthPixels: 1024, heightPixels: 256, widthMeters: 1.1, interactive: false});
panelSetTransform(panel, 'world', 0, .48, 0);
panelSetVisible(panel, true);
function advance() {
  if (held.left || held.right) { print('SELECTION release both triggers before advancing'); return; }
  for (const id of objects) removeObject(id);
  objects = [];
  hover.left = hover.right = 0;
  active = (active + 1) % cases.length;
  const test = cases[active];
  for (const [asset,x,y,z,scale,yaw] of test.specs) {
    const id = createObject(assets[asset],x,y,z,scale);
    labels[id] = asset + (z > 0 ? '-rear' : '-front');
    objects.push(id);
    setObjectYaw(id,yaw);
    setObjectInteraction(id,{grabbable: true, directGrab: true, laserYaw: true, depthAdjust: true,
      handRotationAxes: {pitch: true, yaw: true, roll: true}});
  }
  panel.canvas.clear(12,18,29,255);
  panel.canvas.text(20,20,`Selection ${active+1}/${cases.length}`,255,255,255,255,32);
  panel.canvas.text(20,80,test.name,255,255,255,255,24);
  panel.canvas.text(20,140,'Bumper: next/reset cycle. Release triggers first.',180,210,255,255,24);
  panel.canvas.present();
  print(`SELECTION_CASE index=${active+1} name=${test.name} objects=${objects.map(id=>`${id}:${labels[id]}`).join(',')}`);
}
setLaserVisible('left',true);
setLaserVisible('right',true);
onInputEvent(event => {
  if (event.type === 'overlay.meshHoverStart') hover[event.hand] = event.objectId;
  if (event.type === 'overlay.meshHoverEnd' && hover[event.hand] === event.objectId) hover[event.hand] = 0;
  if (event.type === 'overlay.grabStart') held[event.hand] = event.objectId;
  if (event.type === 'overlay.grabEnd' && held[event.hand] === event.objectId) held[event.hand] = 0;
  if (['overlay.meshHoverStart','overlay.meshHoverEnd','overlay.grabStart','overlay.grabEnd'].includes(event.type))
    print(`SELECTION_EVENT case=${active+1} type=${event.type} hand=${event.hand} object=${event.objectId} label=${labels[event.objectId]} hover=${hover[event.hand]} heldLeft=${held.left} heldRight=${held.right}`);
  if (event.type === 'controller.button' && event.control === '/input/bumper/click' && event.down) advance();
});
advance();
function update() { pollInputEvents(); }
