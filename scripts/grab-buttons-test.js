// Both hands: trigger-only, grip-only, and either-button grabs.
const asset=loadModel('assets/samples/Box/Box.glb',768);
const modes=['trigger','grip','both'];
for(let i=0;i<modes.length;i++){
  const id=createObject(asset,(i-1)*.4,0,0,.18);
  enableObjectInteraction(id,{grabButton:modes[i]});
  setObjectInteraction(id,{directGrab:true,handRotationAxes:{pitch:true,yaw:true,roll:true}});
}
setLaserVisible('left',true);setLaserVisible('right',true);
const panel=createPanel({widthPixels:1200,heightPixels:240,widthMeters:1.2,interactive:false});
panelSetTransform(panel,'world',0,.35,0);panelSetVisible(panel,true);
const c=panel.canvas;c.clear(12,18,29,255);
c.text(60,16,'TRIGGER ONLY',240,245,255,255,28);
c.text(460,16,'GRIP ONLY',240,245,255,255,28);
c.text(860,16,'EITHER BUTTON',240,245,255,255,28);
c.text(24,75,'Aim at a box; hold its allowed button. Release to drop. Try both hands.',220,230,240,255,24);
c.text(24,118,'Both: switch buttons while holding either. Disallowed buttons must not grab.',220,230,240,255,23);
c.text(24,160,'Nearby boxes follow hand rotation. Selection still requires a current mesh hit.',180,200,220,255,23);c.present();
onInputEvent(e=>{if(e.type==='overlay.grabStart'||e.type==='overlay.grabEnd')print('GRAB_BUTTON_EVENT '+JSON.stringify(e));});
function update(){pollInputEvents();}
print('GRAB_BUTTON_TEST_READY left=trigger center=grip right=both');
