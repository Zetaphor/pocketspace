// One asset, nine clips. Playback remains independent of grabbable world pose.
const asset=loadModel('assets/samples/InterpolationTest/InterpolationTest.glb',768);
const object=createObject(asset,0,0,0,.65),clips=getAnimations(asset);
enableObjectInteraction(object,{grabButton:'both'});
setObjectInteraction(object,{handRotationAxes:{pitch:true,yaw:true,roll:true}});
setLaserVisible('left',true);setLaserVisible('right',true);
let selected=clips.findIndex(c=>c.name==='Linear Rotation'),lastDraw=0,lastLog=0;
const started=Date.now();
function start(){playAnimation(object,selected,{loop:true});print('ANIMATION_CLIP '+clips[selected].name);}
start();
const panel=createPanel({widthPixels:1100,heightPixels:240,widthMeters:1.2,interactive:false});
panelSetTransform(panel,'world',0,.55,0);panelSetVisible(panel,true);
onInputEvent(e=>{
 if(e.type!=='controller.button'||!e.down||Date.now()-started<1000)return;
 if(e.control==='/input/bumper/click'){selected=(selected+1)%clips.length;start();}
 else if(e.control==='/input/a/click'||e.control==='/input/x/click'){
  const state=getAnimationState(object);if(state.playing)pauseAnimation(object);else resumeAnimation(object);
 }else if(e.control==='/input/b/click'||e.control==='/input/y/click')start();
});
function update(){
 pollInputEvents();const now=Date.now(),state=getAnimationState(object);
 if(now-lastLog>=1700){lastLog=now;print('ANIMATION_STATE '+JSON.stringify(state));}
 if(now-lastDraw<200)return;lastDraw=now;
 const c=panel.canvas;c.clear(12,18,29,255);
 c.text(24,16,'glTF animation - InterpolationTest',240,245,255,255,28);
 c.text(24,65,`${state.name}: ${state.time.toFixed(2)} / ${state.duration.toFixed(2)} sec (${state.status})`,180,220,255,255,24);
 c.text(24,110,'Bumper: next clip | A / X: pause or resume | B / Y: restart',220,230,240,255,23);
 c.text(24,155,'Grip or trigger grabs the animated model; each clip moves one test part.',220,230,240,255,21);
 c.text(24,195,'STEP jumps; LINEAR is continuous; CUBICSPLINE uses smooth tangents.',180,200,220,255,21);c.present();
}
print('ANIMATION_TEST_READY nine clips, bumper cycles, A/X pauses, B/Y restarts');
