// Save a moving physics tool with bodies, hinge motor, spring and velocities.
// Bumper saves; A/X restores and resumes the saved motion.
const floor=createObject(loadModel('assets/physics/ThinPlatform.glb'),0,-.63,0,1.2);
setObjectPhysics(floor,{type:'static',halfExtents:{x:.6,y:.03,z:.6}});
let tool=createSceneContainer();
function body(path,x,y,scale,type,shape='box'){
  const id=createObject(loadModel(path),x,y,0,scale,{container:tool,renderGroup:1});
  setObjectPhysics(id,{type,shape,mass:.25});return id;
}
const pivot=body('assets/samples/Box/Box.glb',-.5,0,.05,'static');
const door=body('assets/physics/ThinWallGreen.glb',-.35,0,.3,'dynamic');
setObjectRotation(door,Math.PI/2,0,0);
const hinge=createPhysicsJoint(pivot,door,{type:'hinge',anchor:{x:-.5,y:0,z:0},minAngle:-1,maxAngle:1});
setPhysicsJointMotor(hinge,{mode:'position',targetAngle:.65,maxTorque:1,frequency:1,damping:1});
const top=body('assets/physics/GreenBox.glb',.2,.3,.06,'static');
const ball=body('assets/physics/YellowSphere.glb',.2,0,.12,'dynamic','sphere');
createPhysicsJoint(top,ball,{type:'distance',minDistance:.3,maxDistance:.3,spring:{frequency:2,damping:.3}});
const box=body('assets/physics/BlueBox.glb',.45,.1,.12,'dynamic');
for(const id of [door,ball,box]){
  enableObjectInteraction(id,{grabButton:'both'});
  setObjectInteraction(id,{handRotationAxes:{pitch:true,yaw:true,roll:true}});
}
const hud=createPanel({widthPixels:768,heightPixels:96,widthMeters:1});
panelSetTransform(hud,'world',0,.55,0);
function status(text){hud.canvas.clear(20,24,32,255);hud.canvas.text(12,24,text,255,255,255,255,26);hud.present();}
onInputEvent(event=>{
  if(event.type!=='controller.button'||!event.down)return;
  try{
    if(event.control==='/input/bumper/click'){saveSceneContainer(tool,'physics-tool');status('Saved. A/X restores position and motion.');}
    if(['/input/a/click','/input/x/click'].includes(event.control)){
      // Stage the restored tool before disposing the old one, preserving it if loading fails.
      const restored=loadSceneContainer('physics-tool');
      disposeSceneContainer(tool);tool=restored.containerId;status('Restored: hinge, spring and velocities resume.');
    }
  }catch(error){status(String(error));}
});
status('Move or throw objects. Bumper saves; A/X restores.');
setLaserVisible('left',true);setLaserVisible('right',true);
function update(){pollInputEvents();}
