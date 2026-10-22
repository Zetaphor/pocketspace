// Four instances share one asset and its picking index; resolution stays 768.
const asset = loadModel('assets/samples/ToyCar/ToyCar.glb', 768);
for (const [x,y,yaw] of [[-.32,.22,.5],[.32,.22,-.5],[-.32,-.22,.9],[.32,-.22,-.9]]) {
  const object = createObject(asset,x,y,0,.28);
  setObjectYaw(object,yaw);
  setObjectInteraction(object,{grabbable: true});
}
setLaserVisible('left',true);
setLaserVisible('right',true);
function update() { pollInputEvents(); }
