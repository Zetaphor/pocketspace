// Repeated live disk restores; root object, HUD and input callback must survive.
const model=loadModel('assets/samples/Box/Box.glb',512);
const root=createObject(model,-.4,0,0,.08,{renderGroup:1});
const hud=createPanel({widthPixels:64,heightPixels:64,widthMeters:.1});hud.canvas.clear();hud.present();
let owner=createSceneContainer(),phase=-1,next=0;
for(let n=0;n<4;n++){
  const id=createObject(model,(n-1.5)*.14,0,0,.1,{container:owner,renderGroup:2});
  enableObjectInteraction(id,{grabButton:'both'});
}
saveSceneContainer(owner,'live');const original=captureSceneContainer(owner);
onInputEvent(()=>{});const started=Date.now();
function check(v){if(!v)throw Error('live persistence assertion');}
function update(){
  const seconds=(Date.now()-started)/1000,current=Math.min(5,Math.floor(seconds/3));
  if(current!==phase){
    phase=current;
    if(phase%2===0){
      const old=owner,restored=loadSceneContainer('live');owner=restored.containerId;
      check(owner!==old&&restored.objectIds.length===4);
      disposeSceneContainer(old);const snapshot=captureSceneContainer(owner);
      check(JSON.stringify(snapshot.models)===JSON.stringify(original.models));
      for(let n=0;n<4;n++)for(const key of Object.keys(original.objects[n]))if(!['id','renderGroup'].includes(key))check(snapshot.objects[n][key]===original.objects[n][key]);
      for(const id of restored.objectIds)setObjectPosition(id,.1,phase*.01,0);
    }else{disposeSceneContainer(owner);check(getSceneContainerState(owner).disposed);}
  }
  if(seconds>=next){next=seconds+.5;print('PERSISTENCE_SAMPLE '+JSON.stringify({...getEngineStats(),phase,age:seconds-phase*3,owner:getSceneContainerState(owner)}));}
  pollInputEvents();
}
