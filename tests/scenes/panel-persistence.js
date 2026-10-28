const root=createObject(loadModel('assets/samples/Box/Box.glb',512),-.5,0,0,.08);
const hud=createPanel({widthPixels:64,heightPixels:64,widthMeters:.1});hud.canvas.clear();hud.present();
let owner=createSceneContainer(),phase=-1,next=0,handles=[];
const id=createObject(loadModel('assets/samples/CesiumMan/CesiumMan.glb',512),0,0,0,.2,{container:owner,renderGroup:1});
playAnimation(id,0,{speed:.75});setObjectPhysics(id,{type:'kinematic'});
for(let n=0;n<2;n++){
  const p=createPanel({widthPixels:384,heightPixels:128,widthMeters:.4,interactive:n===0,container:owner});
  p.canvas.clear(20,70,130,255);p.canvas.text(12,12,'Saved panel',255,255,255,255,24);p.present();
  panelSetTransform(p,n===0?'world':'headLocked',.2*n,.25,.6*n);if(n===1)panelSetVisible(p,false);handles.push(p);
}
saveSceneContainer(owner,'live');const original=captureSceneContainer(owner);
onInputEvent(()=>{});const started=Date.now();
function check(v){if(!v)throw Error('live panel persistence assertion');}
function update(){
  const seconds=(Date.now()-started)/1000,current=Math.min(5,Math.floor(seconds/3));
  if(current!==phase){
    phase=current;
    if(phase%2===0){
      const old=owner,c=loadSceneContainer('live');owner=c.containerId;handles=c.panels;check(handles.length===2);disposeSceneContainer(old);
      const snapshot=captureSceneContainer(owner);
      for(let n=0;n<2;n++)for(const key of Object.keys(original.panels[n]))if(key!=='id')check(JSON.stringify(snapshot.panels[n][key])===JSON.stringify(original.panels[n][key]));
      check(getAnimationState(c.objectIds[0]).playing&&getObjectPhysics(c.objectIds[0]).type==='kinematic');
      handles[0].canvas.rect(0,0,20,20,255,0,0,255,true);handles[0].present();panelSetTransform(handles[0],'world',.2,0,0);
    }else disposeSceneContainer(owner);
  }
  if(seconds>=next){next=seconds+.5;print('PERSISTENCE_SAMPLE '+JSON.stringify({...getEngineStats(),phase,age:seconds-phase*3,owner:getSceneContainerState(owner)}));}
  pollInputEvents();
}
