// Save and restore an interactive panel, including pixels and layout.
// Bumper saves, B/Y changes it, A/X restores; clicking the panel repaints it.
let tool=createSceneContainer();
let panel=createPanel({widthPixels:640,heightPixels:256,widthMeters:.8,interactive:true,container:tool});
panelSetTransform(panel,'world',0,0,0);
function paint(label,r,g,b){
  panel.canvas.clear(r,g,b,255);panel.canvas.text(24,32,label,255,255,255,255,32);
  panel.canvas.text(24,110,'Click this panel to test input',255,255,255,255,24);panel.present();
}
paint('Ready to save',20,70,130);
const hud=createPanel({widthPixels:768,heightPixels:64,widthMeters:1});
panelSetTransform(hud,'world',0,.3,0);
hud.canvas.clear(20,24,32,255);hud.canvas.text(12,16,'Bumper: save. B/Y: change. A/X: restore.',255,255,255,255,24);hud.present();
// Match the current panel handle: restoration gives objects and panels fresh IDs.
onInputEvent(event=>{
  if(event.type==='panel.pointerDown'&&event.objectId===panel.id){paint('Panel clicked',120,60,20);return;}
  if(event.type!=='controller.button'||!event.down)return;
  try{
    if(event.control==='/input/bumper/click')saveSceneContainer(tool,'panel-tool');
    if(['/input/b/click','/input/y/click'].includes(event.control)){paint('Changed after saving',100,20,90);panelSetTransform(panel,'world',.2,-.1,0);}
    if(['/input/a/click','/input/x/click'].includes(event.control)){
      // Restore first; a missing/bad save must leave the current tool intact.
      const restored=loadSceneContainer('panel-tool');disposeSceneContainer(tool);tool=restored.containerId;panel=restored.panels[0];
    }
  }catch(error){print(String(error));}
});
setLaserVisible('left',true);setLaserVisible('right',true);
function update(){pollInputEvents();}
