// Three independent world-anchored models with laser and near-hand manipulation.
const assets=[
  loadModel('assets/Avocado.glb',768),
  loadModel('assets/samples/ToyCar/ToyCar.glb',768),
  loadModel('assets/samples/Lantern/Lantern.glb',768)
];
const poses=[[-.36,0,0,.22],[0,0,0,.26],[.36,0,0,.28]];
for(let i=0;i<3;i++){
  const p=poses[i],id=createObject(assets[i],p[0],p[1],p[2],p[3]);
  if(i===0)setObjectYaw(id,Math.PI);
  setObjectInteraction(id,{
    grabbable:true,directGrab:true,
    laserYaw:true,depthAdjust:true,
    handRotationAxes:{pitch:true,yaw:true,roll:true}
  });
}
setLaserVisible('left',true);
setLaserVisible('right',true);
