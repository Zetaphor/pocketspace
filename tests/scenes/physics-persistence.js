// Bounded live simulation fixture: save once, restore and dispose three times.
const floor=createObject(loadModel('assets/physics/ThinPlatform.glb',512),0,-.63,0,1.2);
setObjectPhysics(floor,{type:'static',halfExtents:{x:.6,y:.03,z:.6}});
const hud=createPanel({widthPixels:64,heightPixels:64,widthMeters:.1});hud.canvas.clear();hud.present();
let owner=createSceneContainer(),phase=-1,next=0,joints=[],freeBox=0;
function body(path,x,y,type,shape='box'){
  const id=createObject(loadModel(path,512),x,y,0,.1,{container:owner,renderGroup:1});
  setObjectPhysics(id,{type,shape,mass:.25});enableObjectInteraction(id,{grabButton:'both'});return id;
}
const pivot=body('assets/samples/Box/Box.glb',-.4,.1,'static');
const door=body('assets/physics/GreenBox.glb',-.2,.1,'dynamic');
const hinge=createPhysicsJoint(pivot,door,{type:'hinge',axis:{x:0,y:0,z:1},anchor:{x:-.4,y:.1,z:0},minAngle:-1,maxAngle:1});
setPhysicsJointMotor(hinge,{mode:'position',targetAngle:.5,maxTorque:1});
const top=body('assets/physics/GreenBox.glb',.2,.3,'static');
const ball=body('assets/physics/YellowSphere.glb',.2,0,'dynamic','sphere');
createPhysicsJoint(top,ball,{type:'distance',minDistance:.3,maxDistance:.3,spring:{frequency:2,damping:.3}});
const box=body('assets/physics/BlueBox.glb',.4,.1,'dynamic');setObjectVelocity(box,-.1,.5,.1);
saveSceneContainer(owner,'live');const original=captureSceneContainer(owner);
onInputEvent(()=>{});const started=Date.now();
function check(v){if(!v)throw Error('live physics persistence assertion');}
function update(){
  const seconds=(Date.now()-started)/1000,current=Math.min(5,Math.floor(seconds/3));
  if(current!==phase){
    phase=current;
    if(phase%2===0){
      const old=owner,restored=loadSceneContainer('live');owner=restored.containerId;joints=restored.jointIds;freeBox=restored.objectMap[box];
      check(owner!==old&&restored.objectIds.length===5&&joints.length===2);
      disposeSceneContainer(old);const snapshot=captureSceneContainer(owner);
      for(let n=0;n<5;n++){
        check(JSON.stringify(snapshot.objects[n].physics)===JSON.stringify(original.objects[n].physics));
        for(const key of ['x','y','z','pitch','yaw','roll'])check(Math.abs(snapshot.objects[n][key]-original.objects[n][key])<1e-5);
      }
      check(getPhysicsJoint(restored.jointMap[hinge]).motor.mode==='position');
    }else{disposeSceneContainer(owner);check(joints.every(id=>getPhysicsJoint(id)===null));}
  }
  if(phase%2===0&&seconds-phase*3>.75)check(Math.abs(getObjectPhysics(freeBox).position.y-original.objects[4].y)>.01);
  if(seconds>=next){next=seconds+.5;print('PERSISTENCE_SAMPLE '+JSON.stringify({...getEngineStats(),phase,age:seconds-phase*3,owner:getSceneContainerState(owner),jointCount:joints.filter(id=>getPhysicsJoint(id)!==null).length}));}
  pollInputEvents();
}
