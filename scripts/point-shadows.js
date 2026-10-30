// Move an omnidirectional lamp and objects to inspect live shadows on a lit platform.
// A/X toggles shadows, bumper cycles map resolution, B/Y captures/restores the tool.
let tool=createSceneContainer();
const floor=loadModel('assets/physics/ThinPlatform.glb',768,{pbr:true});
const cube=loadModel('assets/physics/YellowBox.glb',768,{pbr:true});
for(const asset of [floor,cube])setModelLighting(asset,{intensity:0,ambient:.04});
createObject(floor,0,-.35,.1,.65,{container:tool});
const box=createObject(cube,0,-.17,.05,.2,{container:tool});
const handle=createObject(loadModel('assets/physics/BlueBox.glb'),-.35,.5,-.45,.075,{container:tool});
for(const object of [box,handle]){
  enableObjectInteraction(object,{grabButton:'both'});
  setObjectInteraction(object,{handRotationAxes:{pitch:true,yaw:true,roll:true}});
}
// Offset the emitter outside its handle; all six directions cast shadows.
let light=createPointLight({container:tool,objectId:handle,position:[.03,-.08,.05],
  range:3,intensity:.6,shadow:{enabled:true,resolution:256,maxTriangles:500000}});
let snapshot=null;
onInputEvent(event=>{
  if(event.type!=='controller.button'||!event.down)return;
  if(['/input/a/click','/input/x/click'].includes(event.control)){
    setPointLight(light,{shadow:{enabled:!getPointLight(light).shadow.enabled}});
  }else if(event.control==='/input/bumper/click'){
    const size=getPointLight(light).shadow.resolution;
    setPointLight(light,{shadow:{resolution:size===256?512:256}});
  }else if(['/input/b/click','/input/y/click'].includes(event.control)){
    if(!snapshot){snapshot=captureSceneContainer(tool);return;}
    // Release the single shadow slot while retaining the current scene. On a
    // failed restore, put its shadow setting back before reporting the error.
    const previous=getPointLight(light).shadow;
    setPointLight(light,{shadow:{enabled:false}});
    let restored;
    try{restored=restoreSceneContainer(snapshot);}
    catch(error){setPointLight(light,{shadow:previous});throw error;}
    disposeSceneContainer(tool);tool=restored.containerId;light=restored.lightIds[0];
  }
});
const hud=createPanel({widthPixels:900,heightPixels:240,widthMeters:.9,interactive:false});
panelSetTransform(hud,'world',0,.75,.15);
let nextDraw=0,lastStatus='';
function update(){
  if(Date.now()<nextDraw)return;nextDraw=Date.now()+500;
  const stats=getEngineStats(),s=stats.shadow,c=hud.canvas;
  if(s.status!==lastStatus){print(`Shadow status: ${s.status}, GPU: ${s.gpuMs}, map: ${s.resolution}`);lastStatus=s.status;}
  c.clear(15,22,30,255);
  c.text(20,20,`Shadows: ${s.status}   Map: ${s.resolution}   FPS: ${stats.fps===null?'--':stats.fps.toFixed(0)}`,235,245,255,255,28);
  c.text(20,82,`Shadow GPU: ${s.gpuMs===null?'--':s.gpuMs.toFixed(3)+' ms'}   Triangles: ${s.triangles}`,235,245,255,255,27);
  c.text(20,148,'A/X: shadows   Bumper: resolution   B/Y: save/restore',235,245,255,255,25);
  hud.present();
}
setLaserVisible('left',true);setLaserVisible('right',true);
