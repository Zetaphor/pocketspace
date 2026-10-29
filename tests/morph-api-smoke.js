function check(ok,message){if(!ok)throw Error(message);}
const asset=loadModel('assets/samples/AnimatedMorphCube/AnimatedMorphCube.glb');
const clips=getAnimations(asset);
check(clips.length>0&&clips.every(c=>c.supported),'morph clip metadata');
const owner=createSceneContainer();
const a=createObject(asset,-.3,0,0,.3,{container:owner}),b=createObject(asset,.3,0,0,.3,{container:owner});
playAnimation(a,0,{time:clips[0].duration/2});pauseAnimation(a);
playAnimation(b,0,{loop:false});seekAnimation(b,clips[0].duration);
check(getAnimationState(a).time===clips[0].duration/2,'independent morph clock');
const restored=restoreSceneContainer(captureSceneContainer(owner));
check(JSON.stringify(getAnimationState(a))===JSON.stringify(getAnimationState(restored.objectMap[a])),'morph playback roundtrip');
globalThis.morphSource=b;globalThis.morphCopy=restored.objectMap[b];
stopAnimation(a);check(getAnimationState(a).status==='stopped','restore default morph pose');
removeObject(a);
print('MORPH_API_PASS');
