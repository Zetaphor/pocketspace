// Mixed ordinary/GPU program routing, two groups and live save/restore.
const names=['Fox','CesiumMan','RiggedFigure','RiggedSimple','InterpolationTest','AnimatedMorphCube','Box','ToyCar','Avocado'];
const assets=names.map(name=>loadModel(name==='Avocado'?'assets/Avocado.glb':`assets/samples/${name}/${name}.glb`,768));
let owner=createSceneContainer(),actors=[],playing=true,finished=false,nextDraw=0;
for(let i=0;i<18;i++){
  const asset=assets[i%assets.length],clips=getAnimations(asset);
  const id=createObject(asset,(i%6-2.5)*.24,(1-Math.floor(i/6))*.24,0,.18,{container:owner,renderGroup:1+i%2});
  setObjectInteraction(id,{grabbable:true,grabButton:'both',directGrab:true,
    handRotationAxes:{pitch:true,yaw:true,roll:true}});
  const selected=names[i%assets.length]==='InterpolationTest'?clips.findIndex(clip=>clip.name==='Linear Scale'):0;
  if(clips.length)playAnimation(id,selected,{time:clips[selected].duration*(i%3)/3});
  actors.push({id,animated:clips.length>0});
}
setLaserVisible('left',true);setLaserVisible('right',true);
const panel=createPanel({widthPixels:1024,heightPixels:300,widthMeters:1,interactive:false});
panelSetTransform(panel,'world',0,.7,0);
const started=Date.now();
function finish(){
  if(finished)return;finished=true;disposeSceneContainer(owner);actors=[];
  setLaserVisible('left',false);setLaserVisible('right',false);print('ORDINARY_PRODUCTION_DONE');
}
onInputEvent(event=>{
  if(event.type!=='controller.button'||!event.down||finished)return;
  if(event.control==='/input/b/click'||event.control==='/input/y/click')finish();
  else if(event.control==='/input/a/click'||event.control==='/input/x/click'){
    playing=!playing;for(const actor of actors)if(actor.animated){if(playing)resumeAnimation(actor.id);else pauseAnimation(actor.id);}
  }else if(event.control==='/input/bumper/click'){
    const restored=restoreSceneContainer(captureSceneContainer(owner));
    disposeSceneContainer(owner);owner=restored.containerId;actors=actors.map(actor=>({...actor,id:restored.objectMap[actor.id]}));
    print('ORDINARY_PRODUCTION_RESTORED');
  }
});
function update(){
  const now=Date.now();if(now-started>300000)finish();
  if(now<nextDraw)return;nextDraw=now+500;
  const s=getEngineStats(),c=panel.canvas;c.clear(12,18,29,255);
  c.text(20,18,finished?'Mixed backend check complete':`Mixed ordinary + GPU skin: ${playing?'playing':'paused'}`,230,240,255,255,30);
  c.text(20,85,`FPS: ${s.fps===null?'--':s.fps.toFixed(1)}  CPU: ${s.cpuPercent===null?'--':s.cpuPercent.toFixed(1)+'%'}`,230,240,255,255,27);
  c.text(20,150,'Grip/trigger: grab. A/X: pause/resume.',190,220,255,255,26);
  c.text(20,215,'Bumper: save/restore. B/Y: finish (five-minute limit).',190,220,255,255,26);panel.present();
}
