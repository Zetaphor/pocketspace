// Two independently animated instances of one shared rigged asset.
const asset=loadModel('assets/samples/CesiumMan/CesiumMan.glb',768),clips=getAnimations(asset);
const objects=[createObject(asset,-.35,0,0,.6),createObject(asset,.35,0,0,.6)];
const speed=[1,.6],offset=[0,.5],started=Date.now();
function start(i){playAnimation(objects[i],0,{loop:true,speed:speed[i],time:Math.min(offset[i],clips[0].duration)});}
for(let i=0;i<2;i++){
 enableObjectInteraction(objects[i],{grabButton:'both'});
 setObjectInteraction(objects[i],{handRotationAxes:{pitch:true,yaw:true,roll:true}});start(i);
}
setLaserVisible('left',true);setLaserVisible('right',true);
const panel=createPanel({widthPixels:1200,heightPixels:260,widthMeters:1.3,interactive:false});
panelSetTransform(panel,'world',0,.55,0);panelSetVisible(panel,true);
let lastDraw=0,lastLog=0;
onInputEvent(e=>{
 if(e.type!=='controller.button'||!e.down||Date.now()-started<1000)return;
 const i=e.hand==='left'?0:1,state=getAnimationState(objects[i]);
 if(e.control==='/input/a/click'||e.control==='/input/x/click'){
  if(state.status==='stopped')start(i);else if(state.playing)pauseAnimation(objects[i]);else resumeAnimation(objects[i]);
 }else if(e.control==='/input/b/click'||e.control==='/input/y/click')start(i);
 else if(e.control==='/input/bumper/click'){if(state.status==='stopped')start(i);else stopAnimation(objects[i]);}
});
function update(){
 pollInputEvents();const now=Date.now(),states=objects.map(id=>getAnimationState(id));
 if(now-lastLog>=1700){lastLog=now;print('SKINNING_STATE '+JSON.stringify(states));}
 if(now-lastDraw<200)return;lastDraw=now;
 const c=panel.canvas;c.clear(12,18,29,255);c.text(24,16,'Skeletal animation - CesiumMan',240,245,255,255,28);
 c.text(24,65,`Left: ${states[0].status} ${states[0].time.toFixed(2)}s | Right: ${states[1].status} ${states[1].time.toFixed(2)}s`,180,220,255,255,24);
 c.text(24,110,'Each hand controls its matching character: A/X pauses; B/Y restarts.',220,230,240,255,23);
 c.text(24,155,'Bumper: stop / play. Stopping restores the default rig pose.',220,230,240,255,23);
 c.text(24,200,'Grip or trigger grabs either character. Check arms, legs, and selection.',180,200,220,255,22);c.present();
}
print('SKINNING_TEST_READY two instances, 19 joints, independent speed and time');
