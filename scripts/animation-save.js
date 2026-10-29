// Save and restore independent animation poses and playback.
// Bumper saves, A/X restores, and B/Y pauses or resumes both models.
const skin=loadModel('assets/samples/CesiumMan/CesiumMan.glb');
const rigid=loadModel('assets/samples/InterpolationTest/InterpolationTest.glb');
let tool=createSceneContainer();
let objects=[createObject(skin,-.25,0,0,.35,{container:tool,renderGroup:1}),createObject(rigid,.25,0,0,.35,{container:tool,renderGroup:1})];
playAnimation(objects[0],0,{speed:.75});
playAnimation(objects[1],'Linear Rotation',{speed:1.25});pauseAnimation(objects[1]);
for(const id of objects)enableObjectInteraction(id,{grabButton:'both'});
const hud=createPanel({widthPixels:768,heightPixels:96,widthMeters:1});
panelSetTransform(hud,'world',0,.4,0);
function status(text){hud.canvas.clear(20,24,32,255);hud.canvas.text(12,24,text,255,255,255,255,24);hud.present();}
onInputEvent(event=>{
  if(event.type!=='controller.button'||!event.down)return;
  try{
    if(event.control==='/input/bumper/click'){saveSceneContainer(tool,'animation-tool');status('Saved playback. A/X restores it.');}
    if(['/input/a/click','/input/x/click'].includes(event.control)){
      const restored=loadSceneContainer('animation-tool');disposeSceneContainer(tool);tool=restored.containerId;objects=restored.objectIds;status('Restored saved poses and playback.');
    }
    if(['/input/b/click','/input/y/click'].includes(event.control)){
      for(const id of objects){if(getAnimationState(id).playing)pauseAnimation(id);else resumeAnimation(id);}
      status('Playback toggled. Bumper saves; A/X restores.');
    }
  }catch(error){status(String(error));}
});
status('Bumper: save. A/X: restore. B/Y: pause/resume.');
setLaserVisible('left',true);setLaserVisible('right',true);
function update(){pollInputEvents();}
