// Wearer follow-up: disconnect while held, remove while held, and panel churn.
const asset=loadModel('assets/samples/Box/Box.glb',768);
let object=0,panel=null,held=false,removeAt=0,respawnAt=0,lastDraw=0;
let message='Grab the box with grip or trigger. Bumper toggles removal-on-hold.';
let removeOnHold=false;
function spawn(){object=createObject(asset,0,0,0,.2);enableObjectInteraction(object,{grabButton:'both'});setObjectInteraction(object,{handRotationAxes:{pitch:true,yaw:true,roll:true}});}
function makePanel(){panel=createPanel({widthPixels:1100,heightPixels:240,widthMeters:1.2,interactive:true});panelSetTransform(panel,'world',0,.35,0);}
spawn();makePanel();setLaserVisible('left',true);setLaserVisible('right',true);
onInputEvent(e=>{
 if(e.type==='overlay.grabStart'){held=true;if(removeOnHold)removeAt=Date.now()+2000;print('LIFECYCLE_GRAB_START '+e.hand);}
 if(e.type==='overlay.grabEnd'){held=false;removeAt=0;print('LIFECYCLE_GRAB_END '+e.hand);}
 if(e.type==='controller.button'&&e.down&&e.control==='/input/bumper/click'){
  removeOnHold=!removeOnHold;removeAt=removeOnHold&&held?Date.now()+2000:0;
  message=removeOnHold?'Removal enabled: hold box two seconds; release before grabbing its replacement.':'Removal disabled: hold box and power that controller off, then on.';
  removePanel(panel);makePanel();print('LIFECYCLE_PANEL_REPLACED');
 }
});
function update(){
 pollInputEvents();const now=Date.now();
 if(object&&removeAt&&now>=removeAt){const removed=object;removeObject(object);object=0;removeAt=0;respawnAt=now+1500;print('LIFECYCLE_OBJECT_REMOVED '+removed);}
 if(!object&&respawnAt&&now>=respawnAt){spawn();respawnAt=0;print('LIFECYCLE_OBJECT_REPLACED '+object);}
 if(now-lastDraw<200)return;lastDraw=now;
 const c=panel.canvas;c.clear(12,18,29,255);c.text(24,16,'Lifecycle checks - grip or trigger',240,245,255,255,28);
 c.text(24,65,`Held: ${held} | remove-on-hold: ${removeOnHold}`,180,220,255,255,24);
 c.text(24,110,message,220,230,240,255,20);
 c.text(24,155,'Disconnect: power off holding controller, then reconnect. Box should release.',220,230,240,255,20);
 c.text(24,195,'Bumper also removes/recreates this interactive panel.',180,200,220,255,20);c.present();
}
print('LIFECYCLE_TEST_READY removal disabled; bumper enables two-second held removal');
