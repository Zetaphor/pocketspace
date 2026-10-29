function check(ok,message){if(!ok)throw Error(message);}
function rejected(call){let failed=false;try{call();}catch(e){failed=true;}check(failed,'expected rejection');}
const owner=createSceneContainer();
const assets=[
  loadModel('assets/samples/InterpolationTest/InterpolationTest.glb'),
  loadModel('assets/samples/CesiumMan/CesiumMan.glb'),
  loadModel('assets/samples/AnimatedMorphCube/AnimatedMorphCube.glb')
];
const objects=assets.map((asset,i)=>createObject(asset,(i-1)*.3,0,0,.25,{container:owner}));
for(const [i,id] of objects.entries()){
  playAnimation(id,0,{time:.2});
  playAnimation(id,i===0?1:0,{time:.8,blendDuration:1});
  const before=JSON.stringify(getAnimationState(id));
  for(const blendDuration of [-1,61,NaN,Infinity,'1',null])rejected(()=>playAnimation(id,0,{blendDuration}));
  check(JSON.stringify(getAnimationState(id))===before,'invalid fade changed playback');
}
function snapshotBlend(){
  const snapshot=captureSceneContainer(owner);check(snapshot.version===6,'snapshot version');
  globalThis.blendSnapshot=snapshot;
  globalThis.blendCopy=restoreSceneContainer(snapshot);
  for(const id of objects)check(JSON.stringify(getAnimationState(id))===JSON.stringify(getAnimationState(blendCopy.objectMap[id])),'transition state roundtrip');
}
function invalidBlendSnapshots(){
  function bad(edit){const s=JSON.parse(JSON.stringify(blendSnapshot));edit(s);rejected(()=>restoreSceneContainer(s));}
  bad(s=>s.version=4);bad(s=>delete s.objects[0].animation.transition);
  bad(s=>s.objects[0].animation.transition.duration=0);
  bad(s=>s.objects[0].animation.transition.duration=61);
  bad(s=>s.objects[0].animation.transition.elapsed=1);
  bad(s=>s.objects[0].animation.transition.source.clip=99999);
}
function interruptBlends(){for(const id of objects){playAnimation(id,0,{time:.1,blendDuration:.8});pauseAnimation(id);}}
function invalidFrozenSnapshots(){
  function bad(edit){const s=JSON.parse(JSON.stringify(blendSnapshot));edit(s);rejected(()=>restoreSceneContainer(s));}
  bad(s=>s.objects[0].animation.transition.frozen=[]);
  bad(s=>s.objects[0].animation.transition.frozen[0].rotation=[0,0,0,0]);
  bad(s=>s.objects[0].animation.transition.frozen[0].weights=[0]);
  bad(s=>s.objects[0].animation.transition.frozen[0].translation=[0]);
}
