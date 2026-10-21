const asset = loadModel('assets/Avocado.glb', 768);
const id = createObject(asset, 0, 0, 0, .3);
setObjectInteraction(id, {grabbable:true, directGrab:true, laserYaw:true, depthAdjust:true,
  handRotationAxes:{pitch:true, yaw:true, roll:true}});
let first = true;
function report(event) {
  if (first) print(JSON.stringify({eventApi:true, event}));
}
onInputEvent(report);
function update(dt) {
  if (first) {
    print(JSON.stringify({objectId:id, input:getControllerInput('left')}));
    first = false;
  }
  for (const event of pollInputEvents()) print(JSON.stringify({queued:event}));
}
