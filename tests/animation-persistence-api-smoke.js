// Real-binding fixture: independent skin/rigid poses and all playback states.
const skin=loadModel('assets/samples/CesiumMan/CesiumMan.glb',512);
const rigid=loadModel('assets/samples/InterpolationTest/InterpolationTest.glb',512);
const owner=createSceneContainer();
function check(v,message='animation persistence assertion'){if(!v)throw Error(message);}
function fails(fn){let caught=false;try{fn();}catch(_){caught=true;}check(caught,'expected rejection');}
function object(asset,x){const id=createObject(asset,x,0,0,.15,{container:owner,renderGroup:1});enableObjectInteraction(id,{grabButton:'both'});return id;}
const playing=object(skin,-.3),paused=object(rigid,-.1),finished=object(rigid,.1),stopped=object(skin,.3);
const index=getAnimations(rigid).findIndex(c=>c.name==='Linear Rotation');
const duration=getAnimations(rigid)[index].duration;
playAnimation(playing,0,{time:.2,speed:.75,loop:true});
playAnimation(paused,index,{time:duration*.4,speed:1.25,loop:false});pauseAnimation(paused);
playAnimation(finished,index,{time:duration,loop:false});
playAnimation(stopped,0);stopAnimation(stopped);
setObjectPhysics(playing,{type:'kinematic'});
// The native harness advances finished playback once before capturing.
globalThis.animationOwner=owner;globalThis.animationFinished=finished;
function saveAndCheck(){
  const snapshot=captureSceneContainer(owner);check(snapshot.version===11&&snapshot.objects[3].animation===null);
  saveSceneContainer(owner,'animations');const copy=loadSceneContainer('animations');
  for(const source of [playing,paused,finished,stopped])check(JSON.stringify(getAnimationState(source))===JSON.stringify(getAnimationState(copy.objectMap[source])),'playback roundtrip');
  check(getObjectPhysics(copy.objectMap[playing]).type==='kinematic');
  globalThis.animationSnapshot=snapshot;globalThis.animationCopy=copy;
  function bad(edit){const s=JSON.parse(JSON.stringify(snapshot));edit(s);fails(()=>restoreSceneContainer(s));}
  bad(s=>s.version=12);bad(s=>s.version=2);bad(s=>delete s.objects[0].animation);
  bad(s=>s.objects[0].animation.clip=9999);bad(s=>s.objects[0].animation.clip=1.5);
  bad(s=>s.objects[0].animation.name='changed');bad(s=>s.objects[0].animation.duration+=1);
  bad(s=>s.objects[0].animation.time=-1);bad(s=>s.objects[0].animation.time=Infinity);
  bad(s=>s.objects[0].animation.speed=0);bad(s=>s.objects[0].animation.speed=101);
  bad(s=>s.objects[0].animation.loop=1);bad(s=>s.objects[0].animation.playing=null);
  bad(s=>s.objects[0].animation.finished=true);
  bad(s=>s.models[0].path='assets/samples/MorphPrimitivesTest/MorphPrimitivesTest.glb');
  // v2 physics saves have no animation field and still load normally.
  {const old=JSON.parse(JSON.stringify(snapshot));old.version=2;for(const o of old.objects)delete o.animation;const c=restoreSceneContainer(old);check(c.objectIds.every(id=>getAnimationState(id).status==='stopped'));disposeSceneContainer(c.containerId);}
  globalThis.animationBad=bad;
}
function update(){}
