function check(ok,message){if(!ok)throw Error(message);}
function rejected(call){let failed=false;try{call();}catch(e){failed=true;}check(failed,'expected invalid animation request to throw');}
const asset=loadModel('assets/samples/InterpolationTest/InterpolationTest.glb',512);
const clips=getAnimations(asset);check(clips.length===9&&clips.every(c=>c.supported&&c.duration===2),'clip metadata');
const a=createObject(asset,-.3,0,0,.25),b=createObject(asset,.3,0,0,.25);
check(getAnimationState(a).status==='stopped','initial state');
playAnimation(a,'Linear Translation',{loop:false,speed:2,time:.5});
check(getAnimationState(a).playing&&getAnimationState(a).time===.5,'play state');
pauseAnimation(a);check(!getAnimationState(a).playing,'pause');
seekAnimation(a,1);check(getAnimationState(a).time===1,'seek');
resumeAnimation(a);check(getAnimationState(a).playing,'resume');
playAnimation(b,'CubicSpline Rotation',{time:.25});pauseAnimation(b);
check(getAnimationState(a).time===1&&getAnimationState(b).time===.25,'independent clocks');
for(const clip of clips){playAnimation(b,clip.index,{time:1});pauseAnimation(b);}
for(const clip of ['unknown',-1,.5,999,null,{}])rejected(()=>playAnimation(a,clip));
for(const options of [{speed:0},{speed:-1},{speed:101},{speed:Infinity},{time:-1},{time:3},{time:NaN},{loop:'yes'},null])rejected(()=>playAnimation(a,0,options));
check(getAnimationState(a).time===1&&getAnimationState(a).speed===2,'invalid calls changed playback');
for(const time of [-1,3,NaN,Infinity,'1'])rejected(()=>seekAnimation(a,time));
stopAnimation(a);check(getAnimationState(a).status==='stopped','stop');
rejected(()=>seekAnimation(a,0));rejected(()=>resumeAnimation(a));
const box=loadModel('assets/samples/Box/Box.glb',512),staticObject=createObject(box,0,0,0,.1);
check(getAnimations(box).length===0,'static asset metadata');rejected(()=>playAnimation(staticObject));
const skin=loadModel('assets/samples/CesiumMan/CesiumMan.glb',512),skinObject=createObject(skin,0,0,0,.1);
check(getAnimations(skin).length>0&&getAnimations(skin).every(c=>c.supported),'skin clips must support playback');playAnimation(skinObject,0);pauseAnimation(skinObject);seekAnimation(skinObject,.5);check(getAnimationState(skinObject).time===.5,'skin seek');stopAnimation(skinObject);
const morph=loadModel('assets/samples/MorphPrimitivesTest/MorphPrimitivesTest.glb',512),morphObject=createObject(morph,0,0,0,.1);
check(getAnimations(morph).every(c=>!c.supported),'morph clips must not claim playback support');rejected(()=>playAnimation(morphObject));
removeObject(staticObject);removeObject(skinObject);removeObject(morphObject);
playAnimation(a,'Linear Scale',{loop:false,speed:2});
let finished=false,removed=false,stoppedAt=0;
function update(){
 if(removed)return;
 const state=getAnimationState(a);
 if(state.finished&&!finished){check(state.time===2&&!state.playing,'nonloop endpoint');finished=true;stoppedAt=Date.now();print('ANIMATION_ENDPOINT_PASS');}
 if(finished&&!removed&&Date.now()-stoppedAt>100){removed=true;removeObject(a);removeObject(b);print('ANIMATION_REMOVE_PASS');update=function(){};}
}
print('ANIMATION_API_PASS metadata, controls, invalid inputs, independent objects, all nine clips, skin playback');
