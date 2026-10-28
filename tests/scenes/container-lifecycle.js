// Bounded native fixture: a root survivor shares the tool's render group.
const asset=loadModel('assets/samples/Box/Box.glb');
const root=createObject(asset,-.35,0,0,.1,{renderGroup:1});setObjectPhysics(root,{type:'static'});
const hud=createPanel({widthPixels:64,heightPixels:64,widthMeters:.1});hud.canvas.clear();hud.present();
setPhysicsGravity(0,0,0);setLaserVisible('left',false);setLaserVisible('right',false);
let owner=0,owned=[],joint=0,phase=-1,next=0;const started=Date.now();
function check(value){if(!value)throw Error('container lifecycle assertion');}
function update(){
  const seconds=(Date.now()-started)/1000,current=Math.min(5,Math.floor(seconds/5));
  if(current!==phase){
    phase=current;
    if(phase%2===0){
      owner=createSceneContainer();owned=[];
      for(let n=0;n<8;n++){
        const id=createObject(asset,(n%4-1.5)*.13,(Math.floor(n/4)-.5)*.15,0,.08,{container:owner,renderGroup:1});
        setObjectPhysics(id);owned.push(id);
      }
      joint=createPhysicsJoint(root,owned[0]);
      const panel=createPanel({widthPixels:64,heightPixels:64,widthMeters:.1,container:owner});panel.canvas.clear(0,120,200,255);panel.present();
      onInputEvent(()=>{},{container:owner});
    }else{
      disposeSceneContainer(owner);disposeSceneContainer(owner);
      check(getSceneContainerState(owner).disposed);check(getPhysicsJoint(joint)===null);check(!getObjectPhysics(root).jointed);
      for(const id of owned){let removed=false;try{getObjectPhysics(id);}catch(_){removed=true;}check(removed);}
    }
  }
  if(seconds>=next){next=seconds+1;print('CONTAINER_SAMPLE '+JSON.stringify({...getEngineStats(),phase,seconds,age:seconds-phase*5,container:getSceneContainerState(owner)}));}
  pollInputEvents();
}
