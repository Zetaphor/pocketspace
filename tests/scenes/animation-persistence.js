// Bounded live fixture: moving, paused and finished poses survive disk restore.
const root=createObject(loadModel('assets/samples/Box/Box.glb',512),-.5,0,0,.08);
const hud=createPanel({widthPixels:64,heightPixels:64,widthMeters:.1});hud.canvas.clear();hud.present();
const skin=loadModel('assets/samples/CesiumMan/CesiumMan.glb',512);
const rigid=loadModel('assets/samples/InterpolationTest/InterpolationTest.glb',512);
let owner=createSceneContainer(),phase=-1,next=0,original=null,started=0,ids=[],advanced=false;
for(let n=0;n<3;n++)ids.push(createObject(n===0?skin:rigid,(n-1)*.25,0,0,.2,{container:owner,renderGroup:1}));
const clip=getAnimations(rigid).findIndex(c=>c.name==='Linear Rotation'),duration=getAnimations(rigid)[clip].duration;
playAnimation(ids[0],0,{time:.2,speed:.75});
playAnimation(ids[1],clip,{time:duration*.4,speed:1.25,loop:false});pauseAnimation(ids[1]);
playAnimation(ids[2],clip,{time:duration,loop:false});
onInputEvent(()=>{});
function check(v){if(!v)throw Error('live animation persistence assertion');}
function update(){
  if(!original){
    if(!getAnimationState(ids[2]).finished){pollInputEvents();return;}
    saveSceneContainer(owner,'live');original=captureSceneContainer(owner);started=Date.now();
  }
  const seconds=(Date.now()-started)/1000,current=Math.min(5,Math.floor(seconds/3));
  if(current!==phase){
    phase=current;advanced=false;
    if(phase%2===0){
      const old=owner,restored=loadSceneContainer('live');owner=restored.containerId;ids=restored.objectIds;check(ids.length===3);disposeSceneContainer(old);
      const snapshot=captureSceneContainer(owner);for(let n=0;n<3;n++)check(JSON.stringify(snapshot.objects[n].animation)===JSON.stringify(original.objects[n].animation));
    }else{disposeSceneContainer(owner);check(getSceneContainerState(owner).disposed);}
  }
  if(phase%2===0){
    check(getAnimationState(ids[1]).status==='paused'&&getAnimationState(ids[1]).time===original.objects[1].animation.time);
    check(getAnimationState(ids[2]).status==='finished');
    if(seconds-phase*3>.75&&!advanced){check(Math.abs(getAnimationState(ids[0]).time-original.objects[0].animation.time)>.01);advanced=true;}
  }
  if(seconds>=next){next=seconds+.5;print('PERSISTENCE_SAMPLE '+JSON.stringify({...getEngineStats(),phase,age:seconds-phase*3,owner:getSceneContainerState(owner)}));}
  pollInputEvents();
}
