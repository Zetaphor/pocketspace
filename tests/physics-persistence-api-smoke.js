// Real-binding fixture; no VR session required.
const asset=loadModel('assets/samples/Box/Box.glb',512);
const owner=createSceneContainer();setPhysicsGravity(0,0,0);
function check(v,message='physics persistence assertion'){if(!v)throw Error(message);}
function fails(fn){let caught=false;try{fn();}catch(_){caught=true;}check(caught,'expected rejection');}
function body(x,type,shape,options={}){
  const id=createObject(asset,x,0,0,.1,{container:owner,renderGroup:1});
  setObjectPhysics(id,{type,shape,...options});enableObjectInteraction(id,{grabButton:'both'});return id;
}
const a=body(-.5,'static','mesh'),b=body(-.25,'dynamic','sphere',{radius:.06,offset:{x:.01,y:.02,z:.03},mass:.3,friction:.6,restitution:.2,continuous:false,gravityFactor:.4,linearDamping:.12,angularDamping:.23});
const c=body(.1,'static','box',{halfExtents:{x:.04,y:.05,z:.06}}),d=body(.3,'dynamic','convex');
const e=body(.6,'kinematic','box'),f=body(.8,'dynamic','box');
const hinge=createPhysicsJoint(a,b,{type:'hinge',anchor:{x:-.5,y:0,z:0},axis:{x:0,y:0,z:1},minAngle:-1,maxAngle:1,breakForce:1000,breakTorque:1000});
setPhysicsJointMotor(hinge,{mode:'position',targetAngle:.4,maxTorque:3,frequency:1.5,damping:.8});
const fixed=createPhysicsJoint(c,d,{type:'fixed',collideConnected:true,breakForce:1000,breakTorque:500});
const spring=createPhysicsJoint(e,f,{type:'distance',minDistance:.1,maxDistance:.3,spring:{frequency:2.5,damping:.6},breakForce:1200});
setPhysicsJoint(spring,{maxDistance:.35,spring:{frequency:3,damping:.7}});
setObjectRotation(b,0,0,.65);setObjectPosition(b,-.5+.25*Math.cos(.65),.25*Math.sin(.65),0);
setObjectVelocity(b,.12,.23,.34);setObjectAngularVelocity(b,.3,.2,.1);
const sourceAngle=getPhysicsJoint(hinge).angle;check(Math.abs(sourceAngle)>.5,'hinge must be away from zero');
const snap=captureSceneContainer(owner);check(snap.version===10&&snap.joints.length===3&&snap.objects.every(o=>o.physics));
saveSceneContainer(owner,'physics');
const copy=loadSceneContainer('physics');check(copy.jointIds.length===3&&copy.objectIds.length===6);
const restored=captureSceneContainer(copy.containerId);
for(let n=0;n<snap.objects.length;n++){
  const source=snap.objects[n],target=restored.objects[n];
  check(JSON.stringify(source.physics)===JSON.stringify(target.physics),'body settings and velocities');
  for(const key of ['x','y','z','pitch','yaw','roll'])check(Math.abs(source[key]-target[key])<1e-5,'body pose');
}
for(const source of snap.joints){
  const target=restored.joints.find(j=>j.id===copy.jointMap[source.id]);
  check(target&&target.objectA===copy.objectMap[source.objectA]&&target.objectB===copy.objectMap[source.objectB],'joint ID mapping');
  for(const key of ['settings','motor','referenceA','referenceB'])check(JSON.stringify(source[key])===JSON.stringify(target[key]),'joint frames/settings');
}
check(Math.abs(getPhysicsJoint(copy.jointMap[hinge]).angle-sourceAngle)<1e-5,'preserve hinge zero reference');
check(getPhysicsJoint(copy.jointMap[hinge]).motor.mode==='position');disposeSceneContainer(copy.containerId);
function bad(edit){const snapshot=JSON.parse(JSON.stringify(snap));edit(snapshot);fails(()=>restoreSceneContainer(snapshot));}
bad(s=>s.objects[3].physics.settings.mass=0);bad(s=>s.objects[1].physics.settings.radius=-1);
bad(s=>s.objects[0].physics.settings.type='dynamic');bad(s=>s.objects[0].physics.settings.continuous=1);
bad(s=>s.objects[1].physics.velocity.x=null);bad(s=>delete s.objects[1].physics);
bad(s=>s.joints[0].objectA=999);bad(s=>s.joints[0].objectB=s.joints[0].objectA);
bad(s=>s.objects[0].physics=null);bad(s=>s.joints[1].id=s.joints[0].id);
bad(s=>s.joints[1].settings.breakForce=-1);bad(s=>s.joints[2].settings.spring.frequency=0);
bad(s=>s.joints[0].motor.maxTorque=-1);bad(s=>s.joints[0].referenceA.pitch=null);
bad(s=>s.joints=Array(513).fill(s.joints[0]));bad(s=>s.version=1);
const outside=createObject(asset,2,0,0,.1);setObjectPhysics(outside,{type:'static'});
const cross=createPhysicsJoint(outside,f,{type:'distance'});fails(()=>captureSceneContainer(owner));removePhysicsJoint(cross);removeObject(outside);
for(const mode of ['velocity','off']){
  setPhysicsJointMotor(hinge,{mode,targetVelocity:.3});const c=restoreSceneContainer(captureSceneContainer(owner));
  check(getPhysicsJoint(c.jointMap[hinge]).motor.mode===mode);disposeSceneContainer(c.containerId);
}
setPhysicsJointMotor(hinge,{mode:'position'});
{const c=restoreSceneContainer(snap);removePhysicsJoint(c.jointMap[fixed]);const trimmed=captureSceneContainer(c.containerId);check(trimmed.joints.length===2);const d=restoreSceneContainer(trimmed);check(d.jointIds.length===2);disposeSceneContainer(c.containerId);disposeSceneContainer(d.containerId);}
// Held bodies restore as ordinary dynamic bodies without a controller owner.
// Native harness exercises hold before capturing this fixture's owner.
globalThis.physicsOwner=owner;globalThis.physicsSnapshot=snap;globalThis.physicsBad=bad;
for(let i=0;i<10;i++){const c=restoreSceneContainer(snap);disposeSceneContainer(c.containerId);}
check(getSceneContainerState(owner).objectIds.length===6&&getPhysicsJoint(hinge)!==null);
function update(){}
