// Manual GPU skinning acceptance: rays, both-hand grabs, pause and movement.
const asset=loadModel('assets/samples/CesiumMan/CesiumMan.glb',768);
const clip=getAnimations(asset)[0],actors=[];
for(let i=0;i<32;i++){
  const id=createObject(asset,((i%8)-3.5)*.15,(Math.floor(i/8)-1.5)*.23,0,.2,{renderGroup:1});
  setObjectInteraction(id,{grabbable:true,grabButton:'both',directGrab:true,
    handRotationAxes:{pitch:true,yaw:true,roll:true}});
  playAnimation(id,0,{time:clip.duration*i/32});actors.push(id);
}
setLaserVisible('left',true);setLaserVisible('right',true);
const panel=createPanel({widthPixels:1024,heightPixels:300,widthMeters:1,interactive:false});
panelSetTransform(panel,'world',0,.65,0);
let playing=true,finished=false,nextDraw=0;
const started=Date.now();
function finish(){
  if(finished)return;finished=true;while(actors.length)removeObject(actors.pop());
  setLaserVisible('left',false);setLaserVisible('right',false);
  print('GPU_SKIN_INTERACTION_DONE');
}
onInputEvent(event=>{
  if(event.type!=='controller.button'||!event.down||finished)return;
  if(event.control==='/input/b/click'||event.control==='/input/y/click')finish();
  else if(event.control==='/input/a/click'||event.control==='/input/x/click'){
    playing=!playing;for(const id of actors){if(playing)resumeAnimation(id);else pauseAnimation(id);}
    print('GPU_SKIN_INTERACTION_PLAYING '+playing);
  }
});
function update(){
  const now=Date.now();if(now-started>300000)finish();
  if(now<nextDraw)return;nextDraw=now+500;
  const s=getEngineStats(),c=panel.canvas;c.clear(12,18,29,255);
  c.text(20,18,finished?'Interaction check complete':`32 actors: ${playing?'animated':'paused'}`,230,240,255,255,30);
  c.text(20,85,`FPS: ${s.fps===null?'--':s.fps.toFixed(1)}  CPU: ${s.cpuPercent===null?'--':s.cpuPercent.toFixed(1)+'%'}`,230,240,255,255,27);
  c.text(20,150,'Grip/trigger: grab with either hand; move and rotate.',190,220,255,255,26);
  c.text(20,215,'A/X: pause/resume. B/Y: finish. Five-minute limit.',190,220,255,255,26);panel.present();
}
