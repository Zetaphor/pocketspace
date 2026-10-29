const owner=createSceneContainer();
const names=['Fox','RiggedFigure','RiggedSimple','InterpolationTest','AnimatedMorphCube','Box'];
const objects=names.map((name,i)=>{
  const asset=loadModel(`assets/samples/${name}/${name}.glb`,768);
  const id=createObject(asset,(i-2.5)*.2,0,0,.2,{container:owner,renderGroup:1});
  setObjectInteraction(id,{grabbable:true,grabButton:'both'});
  const clips=getAnimations(asset);
  if(clips.length){playAnimation(id,0,{time:.1});playAnimation(id,clips.length>1?1:0,{time:.2,blendDuration:.5});}
  return id;
});
function copyMixed(){globalThis.copy=restoreSceneContainer(captureSceneContainer(owner));}
function discardMixed(){disposeSceneContainer(copy.containerId);}
function interruptMixed(){
  for(const id of objects.slice(0,5)){
    playAnimation(id,0,{time:.3,blendDuration:.5});pauseAnimation(id);
  }
}
