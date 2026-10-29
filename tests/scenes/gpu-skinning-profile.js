// Matched 32-actor skinning comparison; CPU/GPU mode is selected by the experiment executable.
const MODEL = 'CesiumMan';
const SECONDS = 20;
const stages = [[32,true],[32,false]];
const asset = loadModel(`assets/samples/${MODEL}/${MODEL}.glb`, 768);
const clip = getAnimations(asset)[0];
const actors = [];
const panel = createPanel({widthPixels:1024,heightPixels:300,widthMeters:1,interactive:false});
panelSetTransform(panel,'world',0,.65,0);
let stage = -1, started = 0, nextDraw = 0, finished = false, lastSample = -1;
function clearActors(){while(actors.length)removeObject(actors.pop());}
function selectStage(index){
  clearActors();stage=index;started=Date.now();nextDraw=0;
  if(stage===stages.length){finished=true;print('ANIMATION_PROFILE_DONE');return;}
  const [count,playing]=stages[stage];
  for(let i=0;i<count;i++){
    const id=createObject(asset,((i%8)-3.5)*.15,(Math.floor(i/8)-1.5)*.23,0,.2,{renderGroup:1});
    actors.push(id);playAnimation(id,0,{time:clip.duration*i/count});
    if(!playing)pauseAnimation(id);
  }
  print('ANIMATION_STAGE '+JSON.stringify({stage,count,playing,model:MODEL,eyePixels:768,renderGroup:1,seconds:SECONDS}));
}
function update(){
  const now=Date.now();
  if(!finished&&now-started>=SECONDS*1000)selectStage(stage+1);
  if(now<nextDraw)return;nextDraw=now+500;
  const s=getEngineStats(),c=panel.canvas;
  c.clear(12,18,29,255);
  const label=finished?'Complete: profiling stopped':`${MODEL}: ${actors.length} ${stages[stage][1]?'animated':'paused'} actors`;
  c.text(20,18,label,230,240,255,255,30);
  c.text(20,85,`FPS: ${s.fps===null?'--':s.fps.toFixed(1)}  CPU: ${s.cpuPercent===null?'--':s.cpuPercent.toFixed(1)+'%'}  RAM: ${s.ramBytes===null?'--':(s.ramBytes/1048576).toFixed(1)+' MiB'}`,230,240,255,255,27);
  c.text(20,150,finished?'Results are in the engine log.':`Stage ${stage+1}/${stages.length}; next in ${Math.max(0,SECONDS-(now-started)/1000).toFixed(0)} sec`,190,220,255,255,26);
  c.text(20,215,'Keep looking at the models. B/Y aborts.',190,220,255,255,26);panel.present();
  if(s.sampleId!==lastSample){lastSample=s.sampleId;print('ANIMATION_HUD '+JSON.stringify({stage,finished,...s}));}
}
onInputEvent(event=>{
  if(event.type==='controller.button'&&event.down&&(event.control==='/input/b/click'||event.control==='/input/y/click')&&!finished){finished=true;clearActors();nextDraw=0;print('ANIMATION_PROFILE_ABORT');}
});
selectStage(0);
